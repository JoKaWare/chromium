// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Timeline.FlameChartStyle = {
  textColor: '#333'
};

/**
 * @enum {symbol}
 */
Timeline.TimelineFlameChartEntryType = {
  Frame: Symbol('Frame'),
  Event: Symbol('Event'),
  InteractionRecord: Symbol('InteractionRecord'),
  ExtensionEvent: Symbol('ExtensionEvent')
};

/**
 * @implements {PerfUI.FlameChartMarker}
 * @unrestricted
 */
Timeline.TimelineFlameChartMarker = class {
  /**
   * @param {number} startTime
   * @param {number} startOffset
   * @param {!Timeline.TimelineMarkerStyle} style
   */
  constructor(startTime, startOffset, style) {
    this._startTime = startTime;
    this._startOffset = startOffset;
    this._style = style;
  }

  /**
   * @override
   * @return {number}
   */
  startTime() {
    return this._startTime;
  }

  /**
   * @override
   * @return {string}
   */
  color() {
    return this._style.color;
  }

  /**
   * @override
   * @return {string}
   */
  title() {
    var startTime = Number.millisToString(this._startOffset);
    return Common.UIString('%s at %s', this._style.title, startTime);
  }

  /**
   * @override
   * @param {!CanvasRenderingContext2D} context
   * @param {number} x
   * @param {number} height
   * @param {number} pixelsPerMillisecond
   */
  draw(context, x, height, pixelsPerMillisecond) {
    var lowPriorityVisibilityThresholdInPixelsPerMs = 4;

    if (this._style.lowPriority && pixelsPerMillisecond < lowPriorityVisibilityThresholdInPixelsPerMs)
      return;
    context.save();

    if (!this._style.lowPriority) {
      context.strokeStyle = this._style.color;
      context.lineWidth = 2;
      context.beginPath();
      context.moveTo(x, 0);
      context.lineTo(x, height);
      context.stroke();
    }

    if (this._style.tall) {
      context.strokeStyle = this._style.color;
      context.lineWidth = this._style.lineWidth;
      context.translate(this._style.lineWidth < 1 || (this._style.lineWidth & 1) ? 0.5 : 0, 0.5);
      context.beginPath();
      context.moveTo(x, height);
      context.setLineDash(this._style.dashStyle);
      context.lineTo(x, context.canvas.height);
      context.stroke();
    }
    context.restore();
  }
};

/**
 * @implements {Timeline.TimelineModeView}
 * @implements {PerfUI.FlameChartDelegate}
 * @unrestricted
 */
Timeline.TimelineFlameChartView = class extends UI.VBox {
  /**
   * @param {!Timeline.TimelineModeViewDelegate} delegate
   * @param {!Array<!TimelineModel.TimelineModelFilter>} filters
   */
  constructor(delegate, filters) {
    super();
    this.element.classList.add('timeline-flamechart');
    this._delegate = delegate;
    /** @type {?Timeline.PerformanceModel} */
    this._model = null;

    this._splitWidget = new UI.SplitWidget(false, false, 'timelineFlamechartMainView', 150);

    this._dataProvider = new Timeline.TimelineFlameChartDataProvider(filters);
    var mainViewGroupExpansionSetting = Common.settings.createSetting('timelineFlamechartMainViewGroupExpansion', {});
    this._mainView = new PerfUI.FlameChart(this._dataProvider, this, mainViewGroupExpansionSetting);
    this._mainView.alwaysShowVerticalScroll();
    this._mainView.enableRuler(false);

    var networkViewGroupExpansionSetting =
        Common.settings.createSetting('timelineFlamechartNetworkViewGroupExpansion', {});
    this._networkDataProvider = new Timeline.TimelineFlameChartNetworkDataProvider();
    this._networkView = new PerfUI.FlameChart(this._networkDataProvider, this, networkViewGroupExpansionSetting);
    this._networkView.alwaysShowVerticalScroll();
    networkViewGroupExpansionSetting.addChangeListener(this.resizeToPreferredHeights.bind(this));

    this._networkPane = new UI.VBox();
    this._networkPane.setMinimumSize(23, 23);
    this._networkView.show(this._networkPane.element);
    this._splitResizer = this._networkPane.element.createChild('div', 'timeline-flamechart-resizer');
    this._splitWidget.hideDefaultResizer(true);
    this._splitWidget.installResizer(this._splitResizer);

    this._splitWidget.setMainWidget(this._mainView);
    this._splitWidget.setSidebarWidget(this._networkPane);

    if (Runtime.experiments.isEnabled('timelineMultipleMainViews')) {
      // Create top level properties splitter.
      this._detailsSplitWidget = new UI.SplitWidget(false, true, 'timelinePanelDetailsSplitViewState');
      this._detailsSplitWidget.element.classList.add('timeline-details-split');
      this._detailsView = new Timeline.TimelineDetailsView(filters, delegate);
      this._detailsSplitWidget.installResizer(this._detailsView.headerElement());
      this._detailsSplitWidget.setMainWidget(this._splitWidget);
      this._detailsSplitWidget.setSidebarWidget(this._detailsView);
      this._detailsSplitWidget.show(this.element);
    } else {
      this._splitWidget.show(this.element);
    }

    this._onMainEntrySelected = this._onEntrySelected.bind(this, this._dataProvider);
    this._onNetworkEntrySelected = this._onEntrySelected.bind(this, this._networkDataProvider);
    this._mainView.addEventListener(PerfUI.FlameChart.Events.EntrySelected, this._onMainEntrySelected, this);
    this._networkView.addEventListener(PerfUI.FlameChart.Events.EntrySelected, this._onNetworkEntrySelected, this);
    this._nextExtensionIndex = 0;

    this._boundRefresh = this._refresh.bind(this);
  }


  /**
   * @override
   * @return {?Element}
   */
  resizerElement() {
    return null;
  }

  /**
   * @override
   * @param {number} windowStartTime
   * @param {number} windowEndTime
   */
  requestWindowTimes(windowStartTime, windowEndTime) {
    this._delegate.requestWindowTimes(windowStartTime, windowEndTime);
  }

  /**
   * @override
   * @param {number} startTime
   * @param {number} endTime
   */
  updateRangeSelection(startTime, endTime) {
    this._delegate.select(Timeline.TimelineSelection.fromRange(startTime, endTime));
  }

  /**
   * @override
   * @param {?Timeline.PerformanceModel} model
   */
  setModel(model) {
    var extensionDataAdded = Timeline.PerformanceModel.Events.ExtensionDataAdded;
    if (this._model)
      this._model.removeEventListener(extensionDataAdded, this._appendExtensionData, this);
    this._model = model;
    if (this._model)
      this._model.addEventListener(extensionDataAdded, this._appendExtensionData, this);
    this._refresh();
  }

  _refresh() {
    this._dataProvider.setModel(this._model);
    this._mainView.reset();

    this._networkDataProvider.setModel(this._model);
    this._networkView.reset();

    this._detailsView.setModel(this._model);

    this._nextExtensionIndex = 0;
    this._appendExtensionData();
    this._mainView.scheduleUpdate();

    this._networkDataProvider.reset();
    if (this._networkDataProvider.isEmpty()) {
      this._mainView.enableRuler(true);
      this._splitWidget.hideSidebar();
    } else {
      this._mainView.enableRuler(false);
      this._splitWidget.showBoth();
      this.resizeToPreferredHeights();
    }
    this._networkView.scheduleUpdate();
  }

  _appendExtensionData() {
    if (!this._model)
      return;
    var extensions = this._model.extensionInfo();
    while (this._nextExtensionIndex < extensions.length)
      this._dataProvider.appendExtensionEvents(extensions[this._nextExtensionIndex++]);
    this._mainView.scheduleUpdate();
  }

  /**
   * @override
   * @param {?SDK.TracingModel.Event} event
   */
  highlightEvent(event) {
    var entryIndex =
        event ? this._dataProvider.entryIndexForSelection(Timeline.TimelineSelection.fromTraceEvent(event)) : -1;
    if (entryIndex >= 0)
      this._mainView.highlightEntry(entryIndex);
    else
      this._mainView.hideHighlight();
  }

  /**
   * @override
   */
  willHide() {
    Bindings.blackboxManager.removeChangeListener(this._boundRefresh);
  }

  /**
   * @override
   */
  wasShown() {
    Bindings.blackboxManager.addChangeListener(this._boundRefresh);
    this._mainView.scheduleUpdate();
    this._networkView.scheduleUpdate();
  }

  /**
   * @override
   * @return {!UI.Widget}
   */
  view() {
    return this;
  }

  /**
   * @override
   * @param {number} startTime
   * @param {number} endTime
   */
  setWindowTimes(startTime, endTime) {
    this._mainView.setWindowTimes(startTime, endTime);
    this._networkView.setWindowTimes(startTime, endTime);
    this._networkDataProvider.setWindowTimes(startTime, endTime);
  }

  /**
   * @override
   * @param {?SDK.TracingModel.Event} event
   * @param {string=} regex
   * @param {boolean=} select
   */
  highlightSearchResult(event, regex, select) {
    if (!event) {
      this._delegate.select(null);
      return;
    }
    var timelineSelection = this._dataProvider.selectionForEvent(event);
    if (timelineSelection)
      this._delegate.select(timelineSelection);
  }

  /**
   * @override
   * @param {?Timeline.TimelineSelection} selection
   */
  setSelection(selection) {
    var index = this._dataProvider.entryIndexForSelection(selection);
    this._mainView.setSelectedEntry(index);
    index = this._networkDataProvider.entryIndexForSelection(selection);
    this._networkView.setSelectedEntry(index);
    if (selection && this._detailsView)
      this._detailsView.setSelection(selection);
  }

  /**
   * @param {!PerfUI.FlameChartDataProvider} dataProvider
   * @param {!Common.Event} event
   */
  _onEntrySelected(dataProvider, event) {
    var entryIndex = /** @type{number} */ (event.data);
    this._delegate.select(dataProvider.createSelection(entryIndex));
  }

  resizeToPreferredHeights() {
    this._networkPane.element.classList.toggle(
        'timeline-network-resizer-disabled', !this._networkDataProvider.isExpanded());
    this._splitWidget.setSidebarSize(
        this._networkDataProvider.preferredHeight() + this._splitResizer.clientHeight + PerfUI.FlameChart.HeaderHeight +
        2);
  }
};

/**
 * @unrestricted
 */
Timeline.TimelineFlameChartView.Selection = class {
  /**
   * @param {!Timeline.TimelineSelection} selection
   * @param {number} entryIndex
   */
  constructor(selection, entryIndex) {
    this.timelineSelection = selection;
    this.entryIndex = entryIndex;
  }
};
