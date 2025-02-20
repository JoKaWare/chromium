/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2012 Intel Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @implements {Timeline.TimelineController.Client}
 * @implements {Timeline.TimelineModeViewDelegate}
 * @implements {UI.Searchable}
 * @unrestricted
 */
Timeline.TimelinePanel = class extends UI.Panel {
  constructor() {
    super('timeline');
    this.registerRequiredCSS('timeline/timelinePanel.css');
    this.element.addEventListener('contextmenu', this._contextMenu.bind(this), false);
    this._dropTarget = new UI.DropTarget(
        this.element, [UI.DropTarget.Types.Files, UI.DropTarget.Types.URIList],
        Common.UIString('Drop timeline file or URL here'), this._handleDrop.bind(this));

    /** @type {!Array<!UI.ToolbarItem>} */
    this._recordingOptionUIControls = [];
    this._state = Timeline.TimelinePanel.State.Idle;
    this._windowStartTime = 0;
    this._windowEndTime = Infinity;
    this._millisecondsToRecordAfterLoadEvent = 3000;
    this._toggleRecordAction =
        /** @type {!UI.Action }*/ (UI.actionRegistry.action('timeline.toggle-recording'));

    /** @type {!Array<!TimelineModel.TimelineModelFilter>} */
    this._filters = [];
    if (!Runtime.experiments.isEnabled('timelineShowAllEvents')) {
      this._filters.push(Timeline.TimelineUIUtils.visibleEventsFilter());
      this._filters.push(new TimelineModel.ExcludeTopLevelFilter());
    }

    /** @type {?Timeline.PerformanceModel} */
    this._performanceModel = null;
    /** @type {?Timeline.PerformanceModel} */
    this._pendingPerformanceModel = null;

    this._cpuThrottlingManager = new Components.CPUThrottlingManager();

    /** @type {!Array<!Timeline.TimelineModeView>} */
    this._currentViews = [];

    this._viewModeSetting =
        Common.settings.createSetting('timelineViewMode', Timeline.TimelinePanel.ViewMode.FlameChart);

    this._disableCaptureJSProfileSetting = Common.settings.createSetting('timelineDisableJSSampling', false);
    this._captureLayersAndPicturesSetting = Common.settings.createSetting('timelineCaptureLayersAndPictures', false);

    this._showScreenshotsSetting = Common.settings.createLocalSetting('timelineShowScreenshots', true);
    this._showScreenshotsSetting.addChangeListener(this._onModeChanged, this);
    this._showMemorySetting = Common.settings.createLocalSetting('timelineShowMemory', false);
    this._showMemorySetting.addChangeListener(this._onModeChanged, this);

    this._panelToolbar = new UI.Toolbar('', this.element);
    this._createSettingsPane();
    this._updateShowSettingsToolbarButton();

    this._timelinePane = new UI.VBox();
    this._timelinePane.show(this.element);
    var topPaneElement = this._timelinePane.element.createChild('div', 'hbox');
    topPaneElement.id = 'timeline-overview-panel';

    // Create top overview component.
    this._overviewPane = new PerfUI.TimelineOverviewPane('timeline');
    this._overviewPane.addEventListener(
        PerfUI.TimelineOverviewPane.Events.WindowChanged, this._onWindowChanged.bind(this));
    this._overviewPane.show(topPaneElement);
    /** @type {!Array<!Timeline.TimelineEventOverview>} */
    this._overviewControls = [];

    this._statusPaneContainer = this._timelinePane.element.createChild('div', 'status-pane-container fill');

    this._createFileSelector();

    SDK.targetManager.addEventListener(SDK.TargetManager.Events.PageReloadRequested, this._pageReloadRequested, this);
    SDK.targetManager.addEventListener(SDK.TargetManager.Events.Load, this._loadEventFired, this);

    this._searchableView = new UI.SearchableView(this);
    this._searchableView.setMinimumSize(0, 100);
    this._searchableView.element.classList.add('searchable-view');

    this._stackView = new UI.StackView(false);
    this._stackView.element.classList.add('timeline-view-stack');

    if (Runtime.experiments.isEnabled('timelineMultipleMainViews')) {
      const viewMode = Timeline.TimelinePanel.ViewMode;
      this._tabbedPane = new UI.TabbedPane();
      this._tabbedPane.appendTab(viewMode.FlameChart, Common.UIString('Flame Chart'), new UI.VBox());
      this._tabbedPane.appendTab(viewMode.BottomUp, Common.UIString('Bottom-Up'), new UI.VBox());
      this._tabbedPane.appendTab(viewMode.CallTree, Common.UIString('Call Tree'), new UI.VBox());
      this._tabbedPane.appendTab(viewMode.EventLog, Common.UIString('Event Log'), new UI.VBox());
      this._tabbedPane.addEventListener(UI.TabbedPane.Events.TabSelected, this._onMainViewChanged.bind(this));
      this._tabbedPane.selectTab(this._viewModeSetting.get());
      this._tabbedPane.show(this._searchableView.element);
      this._searchableView.show(this._timelinePane.element);
    } else {
      // Create top level properties splitter.
      this._detailsSplitWidget = new UI.SplitWidget(false, true, 'timelinePanelDetailsSplitViewState', 400);
      this._detailsSplitWidget.element.classList.add('timeline-details-split');
      this._detailsView = new Timeline.TimelineDetailsView(this._filters, this);
      this._detailsSplitWidget.installResizer(this._detailsView.headerElement());
      this._detailsSplitWidget.setSidebarWidget(this._detailsView);
      this._detailsSplitWidget.setMainWidget(this._searchableView);
      this._detailsSplitWidget.hideSidebar();
      this._detailsSplitWidget.show(this._timelinePane.element);
      this._stackView.show(this._searchableView.element);
    }

    this._onModeChanged();
    this._populateToolbar();
    this._showLandingPage();

    Extensions.extensionServer.addEventListener(
        Extensions.ExtensionServer.Events.TraceProviderAdded, this._appendExtensionsToToolbar, this);
    SDK.targetManager.addEventListener(SDK.TargetManager.Events.SuspendStateChanged, this._onSuspendStateChanged, this);

    /** @type {!SDK.TracingModel.Event}|undefined */
    this._selectedSearchResult;
    /** @type {!Array<!SDK.TracingModel.Event>}|undefined */
    this._searchResults;
  }

  /**
   * @return {!Timeline.TimelinePanel}
   */
  static instance() {
    return /** @type {!Timeline.TimelinePanel} */ (self.runtime.sharedInstance(Timeline.TimelinePanel));
  }

  /**
   * @override
   * @return {?UI.SearchableView}
   */
  searchableView() {
    return this._searchableView;
  }

  /**
   * @override
   */
  wasShown() {
    UI.context.setFlavor(Timeline.TimelinePanel, this);
  }

  /**
   * @override
   */
  willHide() {
    UI.context.setFlavor(Timeline.TimelinePanel, null);
  }

  /**
   * @param {!Common.Event} event
   */
  _onWindowChanged(event) {
    this._windowStartTime = event.data.startTime;
    this._windowEndTime = event.data.endTime;

    for (var i = 0; i < this._currentViews.length; ++i)
      this._currentViews[i].setWindowTimes(this._windowStartTime, this._windowEndTime);

    if (!this._selection || this._selection.type() === Timeline.TimelineSelection.Type.Range)
      this.select(null);
  }

  _onMainViewChanged() {
    this._viewModeSetting.set(this._tabbedPane.selectedTabId);
    this._onModeChanged();
  }

  /**
   * @param {!Common.Event} event
   */
  _onOverviewSelectionChanged(event) {
    var selection = /** @type {!Timeline.TimelineSelection} */ (event.data);
    this.select(selection);
  }

  /**
   * @override
   * @param {number} windowStartTime
   * @param {number} windowEndTime
   */
  requestWindowTimes(windowStartTime, windowEndTime) {
    this._overviewPane.requestWindowTimes(windowStartTime, windowEndTime);
  }

  /**
   * @param {!Timeline.TimelineModeView} modeView
   */
  _addModeView(modeView) {
    modeView.setModel(this._performanceModel);
    modeView.setWindowTimes(this._windowStartTime, this._windowEndTime);
    var splitWidget =
        this._stackView.appendView(modeView.view(), 'timelinePanelTimelineStackSplitViewState', undefined, 112);
    var resizer = modeView.resizerElement();
    if (splitWidget && resizer) {
      splitWidget.hideDefaultResizer();
      splitWidget.installResizer(resizer);
    }
    this._currentViews.push(modeView);
  }

  _removeAllModeViews() {
    this._currentViews = [];
    this._stackView.detachChildWidgets();
  }

  /**
   * @param {!Timeline.TimelinePanel.State} state
   */
  _setState(state) {
    this._state = state;
    this._updateTimelineControls();
  }

  /**
   * @param {string} name
   * @param {!Common.Setting} setting
   * @param {string} tooltip
   * @return {!UI.ToolbarItem}
   */
  _createSettingCheckbox(name, setting, tooltip) {
    const checkboxItem = new UI.ToolbarCheckbox(name, tooltip, setting);
    this._recordingOptionUIControls.push(checkboxItem);
    return checkboxItem;
  }

  _populateToolbar() {
    // Record
    this._panelToolbar.appendToolbarItem(UI.Toolbar.createActionButton(this._toggleRecordAction));
    this._panelToolbar.appendToolbarItem(UI.Toolbar.createActionButtonForId('main.reload'));
    var clearButton = new UI.ToolbarButton(Common.UIString('Clear'), 'largeicon-clear');
    clearButton.addEventListener(UI.ToolbarButton.Events.Click, () => this._clear());
    this._panelToolbar.appendToolbarItem(clearButton);
    this._panelToolbar.appendSeparator();

    // View
    this._panelToolbar.appendSeparator();
    this._showScreenshotsToolbarCheckbox = this._createSettingCheckbox(
        Common.UIString('Screenshots'), this._showScreenshotsSetting, Common.UIString('Capture screenshots'));
    this._panelToolbar.appendToolbarItem(this._showScreenshotsToolbarCheckbox);

    this._showMemoryToolbarCheckbox = this._createSettingCheckbox(
        Common.UIString('Memory'), this._showMemorySetting, Common.UIString('Show memory timeline.'));
    this._panelToolbar.appendToolbarItem(this._showMemoryToolbarCheckbox);

    // GC
    this._panelToolbar.appendToolbarItem(UI.Toolbar.createActionButtonForId('components.collect-garbage'));

    // Settings
    this._panelToolbar.appendSpacer();
    this._panelToolbar.appendText('');
    this._panelToolbar.appendSeparator();
    this._panelToolbar.appendToolbarItem(this._showSettingsPaneButton);
  }

  _createSettingsPane() {
    this._showSettingsPaneSetting = Common.settings.createSetting('timelineShowSettingsToolbar', false);
    this._showSettingsPaneButton = new UI.ToolbarSettingToggle(
        this._showSettingsPaneSetting, 'largeicon-settings-gear', Common.UIString('Capture settings'));
    SDK.multitargetNetworkManager.addEventListener(
        SDK.MultitargetNetworkManager.Events.ConditionsChanged, this._updateShowSettingsToolbarButton, this);
    this._cpuThrottlingManager.addEventListener(
        Components.CPUThrottlingManager.Events.RateChanged, this._updateShowSettingsToolbarButton, this);
    this._disableCaptureJSProfileSetting.addChangeListener(this._updateShowSettingsToolbarButton, this);
    this._captureLayersAndPicturesSetting.addChangeListener(this._updateShowSettingsToolbarButton, this);

    this._settingsPane = new UI.HBox();
    this._settingsPane.element.classList.add('timeline-settings-pane');
    this._settingsPane.show(this.element);

    var captureToolbar = new UI.Toolbar('', this._settingsPane.element);
    captureToolbar.element.classList.add('flex-auto');
    captureToolbar.makeVertical();
    captureToolbar.appendToolbarItem(this._createSettingCheckbox(
        Common.UIString('Disable JavaScript Samples'), this._disableCaptureJSProfileSetting,
        Common.UIString('Disables JavaScript sampling, reduces overhead when running against mobile devices')));
    captureToolbar.appendToolbarItem(this._createSettingCheckbox(
        Common.UIString('Enable advanced paint instrumentation (slow)'), this._captureLayersAndPicturesSetting,
        Common.UIString('Captures advanced paint instrumentation, introduces significant performance overhead')));

    var throttlingPane = new UI.VBox();
    throttlingPane.element.classList.add('flex-auto');
    throttlingPane.show(this._settingsPane.element);

    var throttlingToolbar1 = new UI.Toolbar('', throttlingPane.element);
    throttlingToolbar1.appendText(Common.UIString('Network:'));
    throttlingToolbar1.appendToolbarItem(this._createNetworkConditionsSelect());
    var throttlingToolbar2 = new UI.Toolbar('', throttlingPane.element);
    throttlingToolbar2.appendText(Common.UIString('CPU:'));
    throttlingToolbar2.appendToolbarItem(this._cpuThrottlingManager.createControl());

    this._showSettingsPaneSetting.addChangeListener(this._updateSettingsPaneVisibility.bind(this));
    this._updateSettingsPaneVisibility();
  }

  /**
    * @param {!Common.Event} event
    */
  _appendExtensionsToToolbar(event) {
    var provider = /** @type {!Extensions.ExtensionTraceProvider} */ (event.data);
    const setting = Timeline.TimelinePanel._settingForTraceProvider(provider);
    const checkbox = this._createSettingCheckbox(provider.shortDisplayName(), setting, provider.longDisplayName());
    this._panelToolbar.appendToolbarItem(checkbox);
  }

  /**
   * @param {!Extensions.ExtensionTraceProvider} traceProvider
   * @return {!Common.Setting<boolean>}
   */
  static _settingForTraceProvider(traceProvider) {
    var setting = traceProvider[Timeline.TimelinePanel._traceProviderSettingSymbol];
    if (!setting) {
      var providerId = traceProvider.persistentIdentifier();
      setting = Common.settings.createSetting(providerId, false);
      traceProvider[Timeline.TimelinePanel._traceProviderSettingSymbol] = setting;
    }
    return setting;
  }

  /**
   * @return {!UI.ToolbarComboBox}
   */
  _createNetworkConditionsSelect() {
    var toolbarItem = new UI.ToolbarComboBox(null);
    toolbarItem.setMaxWidth(140);
    NetworkConditions.NetworkConditionsSelector.decorateSelect(toolbarItem.selectElement());
    return toolbarItem;
  }

  _prepareToLoadTimeline() {
    console.assert(this._state === Timeline.TimelinePanel.State.Idle);
    this._setState(Timeline.TimelinePanel.State.Loading);
    this._pendingPerformanceModel = new Timeline.PerformanceModel();
  }

  _createFileSelector() {
    if (this._fileSelectorElement)
      this._fileSelectorElement.remove();
    this._fileSelectorElement = UI.createFileSelectorElement(this._loadFromFile.bind(this));
    this._timelinePane.element.appendChild(this._fileSelectorElement);
  }

  /**
   * @param {!Event} event
   */
  _contextMenu(event) {
    var contextMenu = new UI.ContextMenu(event);
    contextMenu.appendItemsAtLocation('timelineMenu');
    contextMenu.show();
  }

  /**
   * @return {boolean}
   */
  _saveToFile() {
    if (this._state !== Timeline.TimelinePanel.State.Idle)
      return true;
    if (!this._performanceModel)
      return true;

    var now = new Date();
    var fileName = 'Profile-' + now.toISO8601Compact() + '.json';
    var stream = new Bindings.FileOutputStream();

    /**
     * @param {boolean} accepted
     * @this {Timeline.TimelinePanel}
     */
    function callback(accepted) {
      if (!accepted)
        return;
      var saver = new Timeline.TracingTimelineSaver();
      this._backingStorage.writeToStream(stream, saver);
    }
    stream.open(fileName, callback.bind(this));
    return true;
  }

  /**
   * @return {boolean}
   */
  _selectFileToLoad() {
    this._fileSelectorElement.click();
    return true;
  }

  /**
   * @param {!File} file
   */
  _loadFromFile(file) {
    if (this._state !== Timeline.TimelinePanel.State.Idle)
      return;
    this._prepareToLoadTimeline();
    this._loader = Timeline.TimelineLoader.loadFromFile(file, this);
    this._createFileSelector();
  }

  /**
   * @param {string} url
   */
  _loadFromURL(url) {
    if (this._state !== Timeline.TimelinePanel.State.Idle)
      return;
    this._prepareToLoadTimeline();
    this._loader = Timeline.TimelineLoader.loadFromURL(url, this);
  }

  _onModeChanged() {
    const showMemory = this._showMemorySetting.get();
    const showScreenshots = this._showScreenshotsSetting.get();
    // Set up overview controls.
    this._overviewControls = [];
    this._overviewControls.push(new Timeline.TimelineEventOverviewResponsiveness());
    if (Runtime.experiments.isEnabled('inputEventsOnTimelineOverview'))
      this._overviewControls.push(new Timeline.TimelineEventOverviewInput());
    this._overviewControls.push(new Timeline.TimelineEventOverviewFrames());
    this._overviewControls.push(new Timeline.TimelineEventOverviewCPUActivity());
    this._overviewControls.push(new Timeline.TimelineEventOverviewNetwork());
    if (showScreenshots)
      this._overviewControls.push(new Timeline.TimelineFilmStripOverview());
    if (showMemory)
      this._overviewControls.push(new Timeline.TimelineEventOverviewMemory());
    for (var control of this._overviewControls)
      control.setModel(this._performanceModel);
    this._overviewPane.setOverviewControls(this._overviewControls);

    // Set up the main view.
    this._removeAllModeViews();

    var viewMode = Timeline.TimelinePanel.ViewMode.FlameChart;
    this._flameChart = null;
    if (Runtime.experiments.isEnabled('timelineMultipleMainViews')) {
      viewMode = this._tabbedPane.selectedTabId;
      this._stackView.detach();
      this._stackView.show(this._tabbedPane.visibleView.element);
    }
    if (viewMode === Timeline.TimelinePanel.ViewMode.FlameChart) {
      this._flameChart = new Timeline.TimelineFlameChartView(this, this._filters);
      this._addModeView(this._flameChart);
      if (showMemory)
        this._addModeView(new Timeline.CountersGraph(this));
    } else {
      var innerView;
      switch (viewMode) {
        case Timeline.TimelinePanel.ViewMode.CallTree:
          innerView = new Timeline.CallTreeTimelineTreeView(this._filters);
          break;
        case Timeline.TimelinePanel.ViewMode.EventLog:
          innerView = new Timeline.EventsTimelineTreeView(this._filters, this);
          break;
        default:
          innerView = new Timeline.BottomUpTimelineTreeView(this._filters);
          break;
      }
      const treeView = new Timeline.TimelineTreeModeView(this, innerView);
      this._addModeView(treeView);
    }

    this.doResize();
    this.select(null);
  }

  _updateSettingsPaneVisibility() {
    if (this._showSettingsPaneSetting.get())
      this._settingsPane.showWidget();
    else
      this._settingsPane.hideWidget();
  }

  _updateShowSettingsToolbarButton() {
    var messages = [];
    if (this._cpuThrottlingManager.rate() !== 1)
      messages.push(Common.UIString('- CPU throttling is enabled'));
    if (SDK.multitargetNetworkManager.isThrottling())
      messages.push(Common.UIString('- Network throttling is enabled'));
    if (this._captureLayersAndPicturesSetting.get())
      messages.push(Common.UIString('- Significant overhead due to paint instrumentation'));
    if (this._disableCaptureJSProfileSetting.get())
      messages.push(Common.UIString('- JavaScript sampling is disabled'));

    this._showSettingsPaneButton.setDefaultWithRedColor(messages.length);
    this._showSettingsPaneButton.setToggleWithRedColor(messages.length);

    if (messages.length) {
      var tooltipElement = createElement('div');
      messages.forEach(message => {
        tooltipElement.createChild('div').textContent = message;
      });
      this._showSettingsPaneButton.setTitle(tooltipElement);
    } else {
      this._showSettingsPaneButton.setTitle(Common.UIString('Capture settings'));
    }
  }

  /**
   * @param {boolean} enabled
   */
  _setUIControlsEnabled(enabled) {
    this._recordingOptionUIControls.forEach(control => control.setEnabled(enabled));
  }

  /**
   * @param {boolean} userInitiated
   */
  _startRecording(userInitiated) {
    console.assert(!this._statusPane, 'Status pane is already opened.');
    var mainTarget = SDK.targetManager.mainTarget();
    if (!mainTarget)
      return;
    this._setState(Timeline.TimelinePanel.State.StartPending);
    this._showRecordingStarted();

    this._autoRecordGeneration = userInitiated ? null : Symbol('Generation');
    var enabledTraceProviders = Extensions.extensionServer.traceProviders().filter(
        provider => Timeline.TimelinePanel._settingForTraceProvider(provider).get());

    const recordingOptions = {
      enableJSSampling: !this._disableCaptureJSProfileSetting.get(),
      capturePictures: this._captureLayersAndPicturesSetting.get(),
      captureFilmStrip: this._showScreenshotsSetting.get()
    };

    this._pendingPerformanceModel = new Timeline.PerformanceModel();
    this._controller = new Timeline.TimelineController(mainTarget, this._pendingPerformanceModel, this);
    this._controller.startRecording(recordingOptions, enabledTraceProviders);

    Host.userMetrics.actionTaken(
        userInitiated ? Host.UserMetrics.Action.TimelineStarted : Host.UserMetrics.Action.TimelinePageReloadStarted);
    this._setUIControlsEnabled(false);
    this._hideLandingPage();
  }

  _stopRecording() {
    if (this._statusPane) {
      this._statusPane.finish();
      this._statusPane.updateStatus(Common.UIString('Stopping timeline\u2026'));
      this._statusPane.updateProgressBar(Common.UIString('Received'), 0);
    }
    this._setState(Timeline.TimelinePanel.State.StopPending);
    this._autoRecordGeneration = null;
    this._controller.stopRecording();
    this._controller = null;
    this._setUIControlsEnabled(true);
  }

  _onSuspendStateChanged() {
    this._updateTimelineControls();
  }

  _updateTimelineControls() {
    var state = Timeline.TimelinePanel.State;
    this._toggleRecordAction.setToggled(this._state === state.Recording);
    this._toggleRecordAction.setEnabled(this._state === state.Recording || this._state === state.Idle);
    this._panelToolbar.setEnabled(this._state !== state.Loading);
    this._dropTarget.setEnabled(this._state === state.Idle);
  }

  _toggleRecording() {
    if (this._state === Timeline.TimelinePanel.State.Idle)
      this._startRecording(true);
    else if (this._state === Timeline.TimelinePanel.State.Recording)
      this._stopRecording();
  }

  _clear() {
    this._showLandingPage();
    if (this._detailsSplitWidget)
      this._detailsSplitWidget.hideSidebar();
    this._reset();
  }

  _reset() {
    PerfUI.LineLevelProfile.instance().reset();
    this._setModel(null);
    delete this._selection;
  }

  /**
   * @param {?Timeline.PerformanceModel} model
   */
  _setModel(model) {
    if (this._performanceModel)
      this._performanceModel.dispose();
    this._performanceModel = model;
    this._currentViews.forEach(view => view.setModel(this._performanceModel));
    this._overviewPane.reset();
    if (model) {
      this._overviewPane.setBounds(
          model.timelineModel().minimumRecordTime(), model.timelineModel().maximumRecordTime());
    }
    for (var control of this._overviewControls)
      control.setModel(model);
    if (model) {
      var cpuProfiles = model.timelineModel().cpuProfiles();
      cpuProfiles.forEach(profile => PerfUI.LineLevelProfile.instance().appendCPUProfile(profile));

      this._setAutoWindowTimes(model.timelineModel());
      this._setMarkers(model.timelineModel());
    } else {
      this.requestWindowTimes(0, Infinity);
    }
    this._overviewPane.scheduleUpdate();
    this.select(null);
    this._updateSearchHighlight(false, true);
  }

  /**
   * @override
   */
  recordingStarted() {
    this._reset();
    this._setState(Timeline.TimelinePanel.State.Recording);
    this._showRecordingStarted();
    this._statusPane.updateStatus(Common.UIString('Profiling\u2026'));
    this._statusPane.updateProgressBar(Common.UIString('Buffer usage'), 0);
    this._statusPane.startTimer();
    this._hideLandingPage();
  }

  /**
   * @override
   * @param {number} usage
   */
  recordingProgress(usage) {
    this._statusPane.updateProgressBar(Common.UIString('Buffer usage'), usage * 100);
  }

  _showLandingPage() {
    if (this._landingPage) {
      this._landingPage.show(this._statusPaneContainer);
      return;
    }

    /**
     * @param {string} tagName
     * @param {string} contents
     */
    function encloseWithTag(tagName, contents) {
      var e = createElement(tagName);
      e.textContent = contents;
      return e;
    }

    var learnMoreNode = UI.createExternalLink(
        'https://developers.google.com/web/tools/chrome-devtools/evaluate-performance/', Common.UIString('Learn more'));
    var recordNode =
        encloseWithTag('b', UI.shortcutRegistry.shortcutDescriptorsForAction('timeline.toggle-recording')[0].name);
    var reloadNode = encloseWithTag('b', UI.shortcutRegistry.shortcutDescriptorsForAction('main.reload')[0].name);
    var navigateNode = encloseWithTag('b', Common.UIString('WASD'));

    this._landingPage = new UI.VBox();
    this._landingPage.contentElement.classList.add('timeline-landing-page', 'fill');
    var centered = this._landingPage.contentElement.createChild('div');

    centered.createChild('p').appendChild(UI.formatLocalized(
        'To capture a new recording, click the record button or hit %s.%s' +
            'To evaluate the page load, click the reload button or hit %s to record the reload.',
        [recordNode, createElement('br'), reloadNode]));

    centered.createChild('p').appendChild(UI.formatLocalized(
        'After recording, select an area of interest in the overview by dragging. ' +
            'Then, zoom and pan the timeline with the mousewheel or %s keys. %s',
        [navigateNode, learnMoreNode]));

    var cpuProfilerHintSetting = Common.settings.createSetting('timelineShowProfilerHint', true);
    if (cpuProfilerHintSetting.get()) {
      var warning = centered.createChild('p', 'timeline-landing-warning');
      var closeButton = warning.createChild('div', 'timeline-landing-warning-close', 'dt-close-button');
      closeButton.addEventListener('click', () => {
        warning.style.visibility = 'hidden';
        cpuProfilerHintSetting.set(false);
      }, false);
      var performanceSpan = encloseWithTag('b', Common.UIString('Performance'));
      warning.createChild('div').appendChild(UI.formatLocalized(
          'The %s panel provides the combined functionality of Timeline and CPU profiler.%s' +
              'The JavaScript CPU profiler will be removed shortly. Meanwhile, it\'s available under ' +
              '%s \u2192 More Tools \u2192 JavaScript Profiler.',
          [performanceSpan, createElement('p'), UI.Icon.create('largeicon-menu')]));
    }

    this._landingPage.show(this._statusPaneContainer);
  }

  _hideLandingPage() {
    this._landingPage.detach();
  }

  /**
   * @override
   */
  loadingStarted() {
    this._hideLandingPage();

    if (this._statusPane)
      this._statusPane.hide();
    this._statusPane = new Timeline.TimelinePanel.StatusPane(false, this._cancelLoading.bind(this));
    this._statusPane.showPane(this._statusPaneContainer);
    this._statusPane.updateStatus(Common.UIString('Loading profile\u2026'));
    // FIXME: make loading from backend cancelable as well.
    if (!this._loader)
      this._statusPane.finish();
    this.loadingProgress(0);
  }

  /**
   * @override
   * @param {number=} progress
   */
  loadingProgress(progress) {
    if (typeof progress === 'number')
      this._statusPane.updateProgressBar(Common.UIString('Received'), progress * 100);
  }

  /**
   * @override
   * @param {?SDK.TracingModel} tracingModel
   * @param {?Bindings.TempFileBackingStorage} backingStorage
   */
  loadingComplete(tracingModel, backingStorage) {
    delete this._loader;
    this._setState(Timeline.TimelinePanel.State.Idle);
    var performanceModel = this._pendingPerformanceModel;
    this._pendingPerformanceModel = null;
    this._backingStorage = backingStorage;

    if (this._statusPane)
      this._statusPane.hide();
    delete this._statusPane;

    if (!tracingModel) {
      performanceModel.dispose();
      this._clear();
      return;
    }

    performanceModel.setTracingModel(tracingModel);
    this._backingStorage = backingStorage;
    this._setModel(performanceModel);

    if (this._flameChart)
      this._flameChart.resizeToPreferredHeights();
    if (this._detailsSplitWidget)
      this._detailsSplitWidget.showBoth();
  }

  _showRecordingStarted() {
    if (this._statusPane)
      return;
    this._statusPane = new Timeline.TimelinePanel.StatusPane(true, this._stopRecording.bind(this));
    this._statusPane.showPane(this._statusPaneContainer);
    this._statusPane.updateStatus(Common.UIString('Initializing profiler\u2026'));
  }

  _cancelLoading() {
    if (this._loader)
      this._loader.cancel();
  }

  /**
   * @param {!TimelineModel.TimelineModel} timelineModel
   */
  _setMarkers(timelineModel) {
    var markers = new Map();
    var recordTypes = TimelineModel.TimelineModel.RecordType;
    var zeroTime = timelineModel.minimumRecordTime();
    for (var event of timelineModel.eventDividers()) {
      if (event.name === recordTypes.TimeStamp || event.name === recordTypes.ConsoleTime)
        continue;
      markers.set(event.startTime, Timeline.TimelineUIUtils.createEventDivider(event, zeroTime));
    }
    this._overviewPane.setMarkers(markers);
  }

  /**
   * @param {!Common.Event} event
   */
  _pageReloadRequested(event) {
    if (this._state !== Timeline.TimelinePanel.State.Idle || !this.isShowing())
      return;
    this._startRecording(false);
  }

  /**
   * @param {!Common.Event} event
   */
  _loadEventFired(event) {
    if (this._state !== Timeline.TimelinePanel.State.Recording || !this._autoRecordGeneration)
      return;
    setTimeout(stopRecordingOnReload.bind(this, this._autoRecordGeneration), this._millisecondsToRecordAfterLoadEvent);

    /**
     * @this {Timeline.TimelinePanel}
     * @param {!Object} recordGeneration
     */
    function stopRecordingOnReload(recordGeneration) {
      // Check if we're still in the same recording session.
      if (this._state !== Timeline.TimelinePanel.State.Recording || this._autoRecordGeneration !== recordGeneration)
        return;
      this._stopRecording();
    }
  }

  // UI.Searchable implementation

  /**
   * @override
   */
  jumpToNextSearchResult() {
    if (!this._searchResults || !this._searchResults.length)
      return;
    var index = this._selectedSearchResult ? this._searchResults.indexOf(this._selectedSearchResult) : -1;
    this._jumpToSearchResult(index + 1);
  }

  /**
   * @override
   */
  jumpToPreviousSearchResult() {
    if (!this._searchResults || !this._searchResults.length)
      return;
    var index = this._selectedSearchResult ? this._searchResults.indexOf(this._selectedSearchResult) : 0;
    this._jumpToSearchResult(index - 1);
  }

  /**
   * @override
   * @return {boolean}
   */
  supportsCaseSensitiveSearch() {
    return false;
  }

  /**
   * @override
   * @return {boolean}
   */
  supportsRegexSearch() {
    return false;
  }

  /**
   * @param {number} index
   */
  _jumpToSearchResult(index) {
    this._selectSearchResult((index + this._searchResults.length) % this._searchResults.length);
    this._currentViews[0].highlightSearchResult(this._selectedSearchResult, this._searchRegex, true);
  }

  /**
   * @param {number} index
   */
  _selectSearchResult(index) {
    this._selectedSearchResult = this._searchResults[index];
    this._searchableView.updateCurrentMatchIndex(index);
  }

  _clearHighlight() {
    this._currentViews[0].highlightSearchResult(null);
  }

  /**
   * @param {boolean} revealRecord
   * @param {boolean} shouldJump
   * @param {boolean=} jumpBackwards
   */
  _updateSearchHighlight(revealRecord, shouldJump, jumpBackwards) {
    if (!this._searchRegex) {
      this._clearHighlight();
      return;
    }

    if (!this._searchResults)
      this._updateSearchResults(shouldJump, jumpBackwards);
    this._currentViews[0].highlightSearchResult(this._selectedSearchResult, this._searchRegex, revealRecord);
  }

  /**
   * @param {boolean} shouldJump
   * @param {boolean=} jumpBackwards
   */
  _updateSearchResults(shouldJump, jumpBackwards) {
    if (!this._searchRegex)
      return;

    // FIXME: search on all threads.
    var events = this._performanceModel ? this._performanceModel.timelineModel().mainThreadEvents() : [];
    var filters = this._filters.concat([new Timeline.TimelineFilters.RegExp(this._searchRegex)]);
    var matches = [];
    for (var index = events.lowerBound(this._windowStartTime, (time, event) => time - event.startTime);
         index < events.length; ++index) {
      var event = events[index];
      if (event.startTime > this._windowEndTime)
        break;
      if (TimelineModel.TimelineModel.isVisible(filters, event))
        matches.push(event);
    }

    var matchesCount = matches.length;
    if (matchesCount) {
      this._searchResults = matches;
      this._searchableView.updateSearchMatchesCount(matchesCount);

      var selectedIndex = matches.indexOf(this._selectedSearchResult);
      if (shouldJump && selectedIndex === -1)
        selectedIndex = jumpBackwards ? this._searchResults.length - 1 : 0;
      this._selectSearchResult(selectedIndex);
    } else {
      this._searchableView.updateSearchMatchesCount(0);
      delete this._selectedSearchResult;
    }
  }

  /**
   * @override
   */
  searchCanceled() {
    this._clearHighlight();
    delete this._searchResults;
    delete this._selectedSearchResult;
    delete this._searchRegex;
  }

  /**
   * @override
   * @param {!UI.SearchableView.SearchConfig} searchConfig
   * @param {boolean} shouldJump
   * @param {boolean=} jumpBackwards
   */
  performSearch(searchConfig, shouldJump, jumpBackwards) {
    var query = searchConfig.query;
    this._searchRegex = createPlainTextSearchRegex(query, 'i');
    delete this._searchResults;
    this._updateSearchHighlight(true, shouldJump, jumpBackwards);
  }

  /**
   * @param {!Timeline.TimelineSelection} selection
   * @return {?TimelineModel.TimelineFrame}
   */
  _frameForSelection(selection) {
    switch (selection.type()) {
      case Timeline.TimelineSelection.Type.Frame:
        return /** @type {!TimelineModel.TimelineFrame} */ (selection.object());
      case Timeline.TimelineSelection.Type.Range:
        return null;
      case Timeline.TimelineSelection.Type.TraceEvent:
        return this._performanceModel.frameModel().filteredFrames(selection._endTime, selection._endTime)[0];
      default:
        console.assert(false, 'Should never be reached');
        return null;
    }
  }

  /**
   * @param {number} offset
   */
  _jumpToFrame(offset) {
    var currentFrame = this._frameForSelection(this._selection);
    if (!currentFrame)
      return;
    var frames = this._performanceModel.frames();
    var index = frames.indexOf(currentFrame);
    console.assert(index >= 0, 'Can\'t find current frame in the frame list');
    index = Number.constrain(index + offset, 0, frames.length - 1);
    var frame = frames[index];
    this._revealTimeRange(frame.startTime, frame.endTime);
    this.select(Timeline.TimelineSelection.fromFrame(frame));
    return true;
  }

  /**
   * @override
   * @param {?Timeline.TimelineSelection} selection
   * @param {!Timeline.TimelineDetailsView.Tab=} preferredTab
   */
  select(selection, preferredTab) {
    if (!selection)
      selection = Timeline.TimelineSelection.fromRange(this._windowStartTime, this._windowEndTime);
    this._selection = selection;
    if (preferredTab && this._detailsView)
      this._detailsView.setPreferredTab(preferredTab);
    for (var view of this._currentViews)
      view.setSelection(selection);
    if (this._detailsView)
      this._detailsView.setSelection(selection);
  }

  /**
   * @override
   * @param {number} time
   */
  selectEntryAtTime(time) {
    var events = this._performanceModel ? this._performanceModel.timelineModel().mainThreadEvents() : [];
    // Find best match, then backtrack to the first visible entry.
    for (var index = events.upperBound(time, (time, event) => time - event.startTime) - 1; index >= 0; --index) {
      var event = events[index];
      var endTime = event.endTime || event.startTime;
      if (SDK.TracingModel.isTopLevelEvent(event) && endTime < time)
        break;
      if (TimelineModel.TimelineModel.isVisible(this._filters, event) && endTime >= time) {
        this.select(Timeline.TimelineSelection.fromTraceEvent(event));
        return;
      }
    }
    this.select(null);
  }

  /**
   * @override
   * @param {?SDK.TracingModel.Event} event
   */
  highlightEvent(event) {
    for (var view of this._currentViews)
      view.highlightEvent(event);
  }

  /**
   * @param {number} startTime
   * @param {number} endTime
   */
  _revealTimeRange(startTime, endTime) {
    var timeShift = 0;
    if (this._windowEndTime < endTime)
      timeShift = endTime - this._windowEndTime;
    else if (this._windowStartTime > startTime)
      timeShift = startTime - this._windowStartTime;
    if (timeShift)
      this.requestWindowTimes(this._windowStartTime + timeShift, this._windowEndTime + timeShift);
  }

  /**
   * @param {!DataTransfer} dataTransfer
   */
  _handleDrop(dataTransfer) {
    var items = dataTransfer.items;
    if (!items.length)
      return;
    var item = items[0];
    if (item.kind === 'string') {
      var url = dataTransfer.getData('text/uri-list');
      if (new Common.ParsedURL(url).isValid)
        this._loadFromURL(url);
    } else if (item.kind === 'file') {
      var entry = items[0].webkitGetAsEntry();
      if (!entry.isFile)
        return;
      entry.file(this._loadFromFile.bind(this));
    }
  }

  /**
   * @param {!TimelineModel.TimelineModel} timelineModel
   */
  _setAutoWindowTimes(timelineModel) {
    var tasks = timelineModel.mainThreadTasks();
    if (!tasks.length) {
      this.requestWindowTimes(timelineModel.minimumRecordTime(), timelineModel.maximumRecordTime());
      return;
    }
    /**
     * @param {number} startIndex
     * @param {number} stopIndex
     * @return {number}
     */
    function findLowUtilizationRegion(startIndex, stopIndex) {
      var /** @const */ threshold = 0.1;
      var cutIndex = startIndex;
      var cutTime = (tasks[cutIndex].startTime + tasks[cutIndex].endTime) / 2;
      var usedTime = 0;
      var step = Math.sign(stopIndex - startIndex);
      for (var i = startIndex; i !== stopIndex; i += step) {
        var task = tasks[i];
        var taskTime = (task.startTime + task.endTime) / 2;
        var interval = Math.abs(cutTime - taskTime);
        if (usedTime < threshold * interval) {
          cutIndex = i;
          cutTime = taskTime;
          usedTime = 0;
        }
        usedTime += task.duration;
      }
      return cutIndex;
    }
    var rightIndex = findLowUtilizationRegion(tasks.length - 1, 0);
    var leftIndex = findLowUtilizationRegion(0, rightIndex);
    var leftTime = tasks[leftIndex].startTime;
    var rightTime = tasks[rightIndex].endTime;
    var span = rightTime - leftTime;
    var totalSpan = timelineModel.maximumRecordTime() - timelineModel.minimumRecordTime();
    if (span < totalSpan * 0.1) {
      leftTime = timelineModel.minimumRecordTime();
      rightTime = timelineModel.maximumRecordTime();
    } else {
      leftTime = Math.max(leftTime - 0.05 * span, timelineModel.minimumRecordTime());
      rightTime = Math.min(rightTime + 0.05 * span, timelineModel.maximumRecordTime());
    }
    this.requestWindowTimes(leftTime, rightTime);
  }
};

/**
 * @enum {symbol}
 */
Timeline.TimelinePanel.State = {
  Idle: Symbol('Idle'),
  StartPending: Symbol('StartPending'),
  Recording: Symbol('Recording'),
  StopPending: Symbol('StopPending'),
  Loading: Symbol('Loading')
};

/**
 * @enum {string}
 */
Timeline.TimelinePanel.ViewMode = {
  FlameChart: 'FlameChart',
  BottomUp: 'BottomUp',
  CallTree: 'CallTree',
  EventLog: 'EventLog'
};

// Define row and header height, should be in sync with styles for timeline graphs.
Timeline.TimelinePanel.rowHeight = 18;
Timeline.TimelinePanel.headerHeight = 20;


Timeline.TimelineSelection = class {
  /**
   * @param {!Timeline.TimelineSelection.Type} type
   * @param {number} startTime
   * @param {number} endTime
   * @param {!Object=} object
   */
  constructor(type, startTime, endTime, object) {
    this._type = type;
    this._startTime = startTime;
    this._endTime = endTime;
    this._object = object || null;
  }

  /**
   * @param {!TimelineModel.TimelineFrame} frame
   * @return {!Timeline.TimelineSelection}
   */
  static fromFrame(frame) {
    return new Timeline.TimelineSelection(Timeline.TimelineSelection.Type.Frame, frame.startTime, frame.endTime, frame);
  }

  /**
   * @param {!TimelineModel.TimelineModel.NetworkRequest} request
   * @return {!Timeline.TimelineSelection}
   */
  static fromNetworkRequest(request) {
    return new Timeline.TimelineSelection(
        Timeline.TimelineSelection.Type.NetworkRequest, request.startTime, request.endTime || request.startTime,
        request);
  }

  /**
   * @param {!SDK.TracingModel.Event} event
   * @return {!Timeline.TimelineSelection}
   */
  static fromTraceEvent(event) {
    return new Timeline.TimelineSelection(
        Timeline.TimelineSelection.Type.TraceEvent, event.startTime, event.endTime || (event.startTime + 1), event);
  }

  /**
   * @param {number} startTime
   * @param {number} endTime
   * @return {!Timeline.TimelineSelection}
   */
  static fromRange(startTime, endTime) {
    return new Timeline.TimelineSelection(Timeline.TimelineSelection.Type.Range, startTime, endTime);
  }

  /**
   * @return {!Timeline.TimelineSelection.Type}
   */
  type() {
    return this._type;
  }

  /**
   * @return {?Object}
   */
  object() {
    return this._object;
  }

  /**
   * @return {number}
   */
  startTime() {
    return this._startTime;
  }

  /**
   * @return {number}
   */
  endTime() {
    return this._endTime;
  }
};

/**
 * @enum {string}
 */
Timeline.TimelineSelection.Type = {
  Frame: 'Frame',
  NetworkRequest: 'NetworkRequest',
  TraceEvent: 'TraceEvent',
  Range: 'Range'
};


/**
 * @interface
 * @extends {Common.EventTarget}
 */
Timeline.TimelineModeView = function() {};

Timeline.TimelineModeView.prototype = {
  /**
   * @return {!UI.Widget}
   */
  view() {},

  /**
   * @return {?Element}
   */
  resizerElement() {},

  /**
   * @param {?Timeline.PerformanceModel} model
   */
  setModel(model) {},

  /**
   * @param {?SDK.TracingModel.Event} event
   * @param {string=} regex
   * @param {boolean=} select
   */
  highlightSearchResult(event, regex, select) {},

  /**
   * @param {number} startTime
   * @param {number} endTime
   */
  setWindowTimes(startTime, endTime) {},

  /**
   * @param {?Timeline.TimelineSelection} selection
   */
  setSelection(selection) {},

  /**
   * @param {?SDK.TracingModel.Event} event
   */
  highlightEvent(event) {}
};

/**
 * @interface
 */
Timeline.TimelineModeViewDelegate = function() {};

Timeline.TimelineModeViewDelegate.prototype = {
  /**
   * @param {number} startTime
   * @param {number} endTime
   */
  requestWindowTimes(startTime, endTime) {},

  /**
   * @param {?Timeline.TimelineSelection} selection
   * @param {!Timeline.TimelineDetailsView.Tab=} preferredTab
   */
  select(selection, preferredTab) {},

  /**
   * @param {number} time
   */
  selectEntryAtTime(time) {},

  /**
   * @param {?SDK.TracingModel.Event} event
   */
  highlightEvent(event) {}
};

/**
 * @unrestricted
 */
Timeline.TimelinePanel.StatusPane = class extends UI.VBox {
  /**
   * @param {boolean} showTimer
   * @param {function()} stopCallback
   */
  constructor(showTimer, stopCallback) {
    super(true);
    this.registerRequiredCSS('timeline/timelineStatusDialog.css');
    this.contentElement.classList.add('timeline-status-dialog');

    var statusLine = this.contentElement.createChild('div', 'status-dialog-line status');
    statusLine.createChild('div', 'label').textContent = Common.UIString('Status');
    this._status = statusLine.createChild('div', 'content');

    if (showTimer) {
      var timeLine = this.contentElement.createChild('div', 'status-dialog-line time');
      timeLine.createChild('div', 'label').textContent = Common.UIString('Time');
      this._time = timeLine.createChild('div', 'content');
    }
    var progressLine = this.contentElement.createChild('div', 'status-dialog-line progress');
    this._progressLabel = progressLine.createChild('div', 'label');
    this._progressBar = progressLine.createChild('div', 'indicator-container').createChild('div', 'indicator');

    this._stopButton = UI.createTextButton(Common.UIString('Stop'), stopCallback);
    this.contentElement.createChild('div', 'stop-button').appendChild(this._stopButton);
  }

  finish() {
    this._stopTimer();
    this._stopButton.disabled = true;
  }

  hide() {
    this.element.parentNode.classList.remove('tinted');
    this.element.remove();
  }

  /**
   * @param {!Element} parent
   */
  showPane(parent) {
    this.show(parent);
    parent.classList.add('tinted');
  }

  /**
   * @param {string} text
   */
  updateStatus(text) {
    this._status.textContent = text;
  }

  /**
   * @param {string} activity
   * @param {number} percent
   */
  updateProgressBar(activity, percent) {
    this._progressLabel.textContent = activity;
    this._progressBar.style.width = percent.toFixed(1) + '%';
    this._updateTimer();
  }

  startTimer() {
    this._startTime = Date.now();
    this._timeUpdateTimer = setInterval(this._updateTimer.bind(this, false), 1000);
    this._updateTimer();
  }

  _stopTimer() {
    if (!this._timeUpdateTimer)
      return;
    clearInterval(this._timeUpdateTimer);
    this._updateTimer(true);
    delete this._timeUpdateTimer;
  }

  /**
   * @param {boolean=} precise
   */
  _updateTimer(precise) {
    if (!this._timeUpdateTimer)
      return;
    var elapsed = (Date.now() - this._startTime) / 1000;
    this._time.textContent = Common.UIString('%s\u2009sec', elapsed.toFixed(precise ? 1 : 0));
  }
};


/**
 * @implements {Common.QueryParamHandler}
 * @unrestricted
 */
Timeline.LoadTimelineHandler = class {
  /**
   * @override
   * @param {string} value
   */
  handleQueryParam(value) {
    UI.viewManager.showView('timeline').then(() => {
      Timeline.TimelinePanel.instance()._loadFromURL(window.decodeURIComponent(value));
    });
  }
};

/**
 * @implements {UI.ActionDelegate}
 * @unrestricted
 */
Timeline.TimelinePanel.ActionDelegate = class {
  /**
   * @override
   * @param {!UI.Context} context
   * @param {string} actionId
   * @return {boolean}
   */
  handleAction(context, actionId) {
    var panel = UI.context.flavor(Timeline.TimelinePanel);
    console.assert(panel && panel instanceof Timeline.TimelinePanel);
    switch (actionId) {
      case 'timeline.toggle-recording':
        panel._toggleRecording();
        return true;
      case 'timeline.save-to-file':
        panel._saveToFile();
        return true;
      case 'timeline.load-from-file':
        panel._selectFileToLoad();
        return true;
      case 'timeline.jump-to-previous-frame':
        panel._jumpToFrame(-1);
        return true;
      case 'timeline.jump-to-next-frame':
        panel._jumpToFrame(1);
        return true;
    }
    return false;
  }
};

Timeline.TimelinePanel._traceProviderSettingSymbol = Symbol('traceProviderSetting');
