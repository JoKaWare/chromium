/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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
 * @unrestricted
 */
Common.Settings = class {
  /**
   * @param {!Common.SettingsStorage} globalStorage
   * @param {!Common.SettingsStorage} localStorage
   */
  constructor(globalStorage, localStorage) {
    this._settingsStorage = globalStorage;
    this._localStorage = localStorage;

    this._eventSupport = new Common.Object();
    /** @type {!Map<string, !Common.Setting>} */
    this._registry = new Map();
    /** @type {!Map<string, !Common.Setting>} */
    this._moduleSettings = new Map();
    self.runtime.extensions('setting').forEach(this._registerModuleSetting.bind(this));
  }

  /**
   * @param {!Runtime.Extension} extension
   */
  _registerModuleSetting(extension) {
    var descriptor = extension.descriptor();
    var settingName = descriptor['settingName'];
    var settingType = descriptor['settingType'];
    var defaultValue = descriptor['defaultValue'];
    var isLocal = !!descriptor['local'];
    var setting = settingType === 'regex' ? this.createRegExpSetting(settingName, defaultValue, undefined, isLocal) :
                                            this.createSetting(settingName, defaultValue, isLocal);
    this._moduleSettings.set(settingName, setting);
  }

  /**
   * @param {string} settingName
   * @return {!Common.Setting}
   */
  moduleSetting(settingName) {
    var setting = this._moduleSettings.get(settingName);
    if (!setting)
      throw new Error('No setting registered: ' + settingName);
    return setting;
  }

  /**
   * @param {string} settingName
   * @return {!Common.Setting}
   */
  settingForTest(settingName) {
    var setting = this._registry.get(settingName);
    if (!setting)
      throw new Error('No setting registered: ' + settingName);
    return setting;
  }

  /**
   * @param {string} key
   * @param {*} defaultValue
   * @param {boolean=} isLocal
   * @return {!Common.Setting}
   */
  createSetting(key, defaultValue, isLocal) {
    if (!this._registry.get(key)) {
      this._registry.set(
          key, new Common.Setting(
                   this, key, defaultValue, this._eventSupport, isLocal ? this._localStorage : this._settingsStorage));
    }
    return /** @type {!Common.Setting} */ (this._registry.get(key));
  }

  /**
   * @param {string} key
   * @param {*} defaultValue
   * @return {!Common.Setting}
   */
  createLocalSetting(key, defaultValue) {
    return this.createSetting(key, defaultValue, true);
  }

  /**
   * @param {string} key
   * @param {string} defaultValue
   * @param {string=} regexFlags
   * @param {boolean=} isLocal
   * @return {!Common.RegExpSetting}
   */
  createRegExpSetting(key, defaultValue, regexFlags, isLocal) {
    if (!this._registry.get(key)) {
      this._registry.set(
          key, new Common.RegExpSetting(
                   this, key, defaultValue, this._eventSupport, isLocal ? this._localStorage : this._settingsStorage,
                   regexFlags));
    }
    return /** @type {!Common.RegExpSetting} */ (this._registry.get(key));
  }

  clearAll() {
    this._settingsStorage.removeAll();
    this._localStorage.removeAll();
    var versionSetting = Common.settings.createSetting(Common.VersionController._currentVersionName, 0);
    versionSetting.set(Common.VersionController.currentVersion);
  }
};

/**
 * @unrestricted
 */
Common.SettingsStorage = class {
  /**
   * @param {!Object} object
   * @param {function(string, string)=} setCallback
   * @param {function(string)=} removeCallback
   * @param {function(string)=} removeAllCallback
   * @param {string=} storagePrefix
   */
  constructor(object, setCallback, removeCallback, removeAllCallback, storagePrefix) {
    this._object = object;
    this._setCallback = setCallback || function() {};
    this._removeCallback = removeCallback || function() {};
    this._removeAllCallback = removeAllCallback || function() {};
    this._storagePrefix = storagePrefix || '';
  }

  /**
   * @param {string} name
   * @param {string} value
   */
  set(name, value) {
    name = this._storagePrefix + name;
    this._object[name] = value;
    this._setCallback(name, value);
  }

  /**
   * @param {string} name
   * @return {boolean}
   */
  has(name) {
    name = this._storagePrefix + name;
    return name in this._object;
  }

  /**
   * @param {string} name
   * @return {string}
   */
  get(name) {
    name = this._storagePrefix + name;
    return this._object[name];
  }

  /**
   * @param {string} name
   */
  remove(name) {
    name = this._storagePrefix + name;
    delete this._object[name];
    this._removeCallback(name);
  }

  removeAll() {
    this._object = {};
    this._removeAllCallback();
  }

  _dumpSizes() {
    Common.console.log('Ten largest settings: ');

    var sizes = {__proto__: null};
    for (var key in this._object)
      sizes[key] = this._object[key].length;
    var keys = Object.keys(sizes);

    function comparator(key1, key2) {
      return sizes[key2] - sizes[key1];
    }

    keys.sort(comparator);

    for (var i = 0; i < 10 && i < keys.length; ++i)
      Common.console.log('Setting: \'' + keys[i] + '\', size: ' + sizes[keys[i]]);
  }
};

/**
 * @template V
 * @unrestricted
 */
Common.Setting = class {
  /**
   * @param {!Common.Settings} settings
   * @param {string} name
   * @param {V} defaultValue
   * @param {!Common.Object} eventSupport
   * @param {!Common.SettingsStorage} storage
   */
  constructor(settings, name, defaultValue, eventSupport, storage) {
    this._settings = settings;
    this._name = name;
    this._defaultValue = defaultValue;
    this._eventSupport = eventSupport;
    this._storage = storage;
  }

  /**
   * @param {function(!Common.Event)} listener
   * @param {!Object=} thisObject
   */
  addChangeListener(listener, thisObject) {
    this._eventSupport.addEventListener(this._name, listener, thisObject);
  }

  /**
   * @param {function(!Common.Event)} listener
   * @param {!Object=} thisObject
   */
  removeChangeListener(listener, thisObject) {
    this._eventSupport.removeEventListener(this._name, listener, thisObject);
  }

  get name() {
    return this._name;
  }

  /**
   * @return {V}
   */
  get() {
    if (typeof this._value !== 'undefined')
      return this._value;

    this._value = this._defaultValue;
    if (this._storage.has(this._name)) {
      try {
        this._value = JSON.parse(this._storage.get(this._name));
      } catch (e) {
        this._storage.remove(this._name);
      }
    }
    return this._value;
  }

  /**
   * @param {V} value
   */
  set(value) {
    this._value = value;
    try {
      var settingString = JSON.stringify(value);
      try {
        this._storage.set(this._name, settingString);
      } catch (e) {
        this._printSettingsSavingError(e.message, this._name, settingString);
      }
    } catch (e) {
      Common.console.error('Cannot stringify setting with name: ' + this._name + ', error: ' + e.message);
    }
    this._eventSupport.dispatchEventToListeners(this._name, value);
  }

  remove() {
    this._settings._registry.delete(this._name);
    this._settings._moduleSettings.delete(this._name);
    this._storage.remove(this._name);
  }

  /**
   * @param {string} message
   * @param {string} name
   * @param {string} value
   */
  _printSettingsSavingError(message, name, value) {
    var errorMessage =
        'Error saving setting with name: ' + this._name + ', value length: ' + value.length + '. Error: ' + message;
    console.error(errorMessage);
    Common.console.error(errorMessage);
    this._storage._dumpSizes();
  }
};

/**
 * @unrestricted
 */
Common.RegExpSetting = class extends Common.Setting {
  /**
   * @param {!Common.Settings} settings
   * @param {string} name
   * @param {string} defaultValue
   * @param {!Common.Object} eventSupport
   * @param {!Common.SettingsStorage} storage
   * @param {string=} regexFlags
   */
  constructor(settings, name, defaultValue, eventSupport, storage, regexFlags) {
    super(settings, name, defaultValue ? [{pattern: defaultValue}] : [], eventSupport, storage);
    this._regexFlags = regexFlags;
  }

  /**
   * @override
   * @return {string}
   */
  get() {
    var result = [];
    var items = this.getAsArray();
    for (var i = 0; i < items.length; ++i) {
      var item = items[i];
      if (item.pattern && !item.disabled)
        result.push(item.pattern);
    }
    return result.join('|');
  }

  /**
   * @return {!Array.<{pattern: string, disabled: (boolean|undefined)}>}
   */
  getAsArray() {
    return super.get();
  }

  /**
   * @override
   * @param {string} value
   */
  set(value) {
    this.setAsArray([{pattern: value}]);
  }

  /**
   * @param {!Array.<{pattern: string, disabled: (boolean|undefined)}>} value
   */
  setAsArray(value) {
    delete this._regex;
    super.set(value);
  }

  /**
   * @return {?RegExp}
   */
  asRegExp() {
    if (typeof this._regex !== 'undefined')
      return this._regex;
    this._regex = null;
    try {
      var pattern = this.get();
      if (pattern)
        this._regex = new RegExp(pattern, this._regexFlags || '');
    } catch (e) {
    }
    return this._regex;
  }
};

/**
 * @unrestricted
 */
Common.VersionController = class {
  updateVersion() {
    var localStorageVersion =
        window.localStorage ? window.localStorage[Common.VersionController._currentVersionName] : 0;
    var versionSetting = Common.settings.createSetting(Common.VersionController._currentVersionName, 0);
    var currentVersion = Common.VersionController.currentVersion;
    var oldVersion = versionSetting.get() || parseInt(localStorageVersion || '0', 10);
    if (oldVersion === 0) {
      // First run, no need to do anything.
      versionSetting.set(currentVersion);
      return;
    }
    var methodsToRun = this._methodsToRunToUpdateVersion(oldVersion, currentVersion);
    for (var i = 0; i < methodsToRun.length; ++i)
      this[methodsToRun[i]].call(this);
    versionSetting.set(currentVersion);
  }

  /**
   * @param {number} oldVersion
   * @param {number} currentVersion
   */
  _methodsToRunToUpdateVersion(oldVersion, currentVersion) {
    var result = [];
    for (var i = oldVersion; i < currentVersion; ++i)
      result.push('_updateVersionFrom' + i + 'To' + (i + 1));
    return result;
  }

  _updateVersionFrom0To1() {
    this._clearBreakpointsWhenTooMany(Common.settings.createLocalSetting('breakpoints', []), 500000);
  }

  _updateVersionFrom1To2() {
    Common.settings.createSetting('previouslyViewedFiles', []).set([]);
  }

  _updateVersionFrom2To3() {
    Common.settings.createSetting('fileSystemMapping', {}).set({});
    Common.settings.createSetting('fileMappingEntries', []).remove();
  }

  _updateVersionFrom3To4() {
    var advancedMode = Common.settings.createSetting('showHeaSnapshotObjectsHiddenProperties', false);
    Common.moduleSetting('showAdvancedHeapSnapshotProperties').set(advancedMode.get());
    advancedMode.remove();
  }

  _updateVersionFrom4To5() {
    var settingNames = {
      'FileSystemViewSidebarWidth': 'fileSystemViewSplitViewState',
      'elementsSidebarWidth': 'elementsPanelSplitViewState',
      'StylesPaneSplitRatio': 'stylesPaneSplitViewState',
      'heapSnapshotRetainersViewSize': 'heapSnapshotSplitViewState',
      'InspectorView.splitView': 'InspectorView.splitViewState',
      'InspectorView.screencastSplitView': 'InspectorView.screencastSplitViewState',
      'Inspector.drawerSplitView': 'Inspector.drawerSplitViewState',
      'layerDetailsSplitView': 'layerDetailsSplitViewState',
      'networkSidebarWidth': 'networkPanelSplitViewState',
      'sourcesSidebarWidth': 'sourcesPanelSplitViewState',
      'scriptsPanelNavigatorSidebarWidth': 'sourcesPanelNavigatorSplitViewState',
      'sourcesPanelSplitSidebarRatio': 'sourcesPanelDebuggerSidebarSplitViewState',
      'timeline-details': 'timelinePanelDetailsSplitViewState',
      'timeline-split': 'timelinePanelRecorsSplitViewState',
      'timeline-view': 'timelinePanelTimelineStackSplitViewState',
      'auditsSidebarWidth': 'auditsPanelSplitViewState',
      'layersSidebarWidth': 'layersPanelSplitViewState',
      'profilesSidebarWidth': 'profilesPanelSplitViewState',
      'resourcesSidebarWidth': 'resourcesPanelSplitViewState'
    };
    var empty = {};
    for (var oldName in settingNames) {
      var newName = settingNames[oldName];
      var oldNameH = oldName + 'H';

      var newValue = null;
      var oldSetting = Common.settings.createSetting(oldName, empty);
      if (oldSetting.get() !== empty) {
        newValue = newValue || {};
        newValue.vertical = {};
        newValue.vertical.size = oldSetting.get();
        oldSetting.remove();
      }
      var oldSettingH = Common.settings.createSetting(oldNameH, empty);
      if (oldSettingH.get() !== empty) {
        newValue = newValue || {};
        newValue.horizontal = {};
        newValue.horizontal.size = oldSettingH.get();
        oldSettingH.remove();
      }
      if (newValue)
        Common.settings.createSetting(newName, {}).set(newValue);
    }
  }

  _updateVersionFrom5To6() {
    var settingNames = {
      'debuggerSidebarHidden': 'sourcesPanelSplitViewState',
      'navigatorHidden': 'sourcesPanelNavigatorSplitViewState',
      'WebInspector.Drawer.showOnLoad': 'Inspector.drawerSplitViewState'
    };

    for (var oldName in settingNames) {
      var oldSetting = Common.settings.createSetting(oldName, null);
      if (oldSetting.get() === null) {
        oldSetting.remove();
        continue;
      }

      var newName = settingNames[oldName];
      var invert = oldName === 'WebInspector.Drawer.showOnLoad';
      var hidden = oldSetting.get() !== invert;
      oldSetting.remove();
      var showMode = hidden ? 'OnlyMain' : 'Both';

      var newSetting = Common.settings.createSetting(newName, {});
      var newValue = newSetting.get() || {};
      newValue.vertical = newValue.vertical || {};
      newValue.vertical.showMode = showMode;
      newValue.horizontal = newValue.horizontal || {};
      newValue.horizontal.showMode = showMode;
      newSetting.set(newValue);
    }
  }

  _updateVersionFrom6To7() {
    var settingNames = {
      'sourcesPanelNavigatorSplitViewState': 'sourcesPanelNavigatorSplitViewState',
      'elementsPanelSplitViewState': 'elementsPanelSplitViewState',
      'stylesPaneSplitViewState': 'stylesPaneSplitViewState',
      'sourcesPanelDebuggerSidebarSplitViewState': 'sourcesPanelDebuggerSidebarSplitViewState'
    };

    var empty = {};
    for (var name in settingNames) {
      var setting = Common.settings.createSetting(name, empty);
      var value = setting.get();
      if (value === empty)
        continue;
      // Zero out saved percentage sizes, and they will be restored to defaults.
      if (value.vertical && value.vertical.size && value.vertical.size < 1)
        value.vertical.size = 0;
      if (value.horizontal && value.horizontal.size && value.horizontal.size < 1)
        value.horizontal.size = 0;
      setting.set(value);
    }
  }

  _updateVersionFrom7To8() {
  }

  _updateVersionFrom8To9() {
    var settingNames = ['skipStackFramesPattern', 'workspaceFolderExcludePattern'];

    for (var i = 0; i < settingNames.length; ++i) {
      var setting = Common.settings.createSetting(settingNames[i], '');
      var value = setting.get();
      if (!value)
        return;
      if (typeof value === 'string')
        value = [value];
      for (var j = 0; j < value.length; ++j) {
        if (typeof value[j] === 'string')
          value[j] = {pattern: value[j]};
      }
      setting.set(value);
    }
  }

  _updateVersionFrom9To10() {
    // This one is localStorage specific, which is fine.
    if (!window.localStorage)
      return;
    for (var key in window.localStorage) {
      if (key.startsWith('revision-history'))
        window.localStorage.removeItem(key);
    }
  }

  _updateVersionFrom10To11() {
    var oldSettingName = 'customDevicePresets';
    var newSettingName = 'customEmulatedDeviceList';
    var oldSetting = Common.settings.createSetting(oldSettingName, undefined);
    var list = oldSetting.get();
    if (!Array.isArray(list))
      return;
    var newList = [];
    for (var i = 0; i < list.length; ++i) {
      var value = list[i];
      var device = {};
      device['title'] = value['title'];
      device['type'] = 'unknown';
      device['user-agent'] = value['userAgent'];
      device['capabilities'] = [];
      if (value['touch'])
        device['capabilities'].push('touch');
      if (value['mobile'])
        device['capabilities'].push('mobile');
      device['screen'] = {};
      device['screen']['vertical'] = {width: value['width'], height: value['height']};
      device['screen']['horizontal'] = {width: value['height'], height: value['width']};
      device['screen']['device-pixel-ratio'] = value['deviceScaleFactor'];
      device['modes'] = [];
      device['show-by-default'] = true;
      device['show'] = 'Default';
      newList.push(device);
    }
    if (newList.length)
      Common.settings.createSetting(newSettingName, []).set(newList);
    oldSetting.remove();
  }

  _updateVersionFrom11To12() {
    this._migrateSettingsFromLocalStorage();
  }

  _updateVersionFrom12To13() {
    this._migrateSettingsFromLocalStorage();
    Common.settings.createSetting('timelineOverviewMode', '').remove();
  }

  _updateVersionFrom13To14() {
    var defaultValue = {'throughput': -1, 'latency': 0};
    Common.settings.createSetting('networkConditions', defaultValue).set(defaultValue);
  }

  _updateVersionFrom14To15() {
    var setting = Common.settings.createLocalSetting('workspaceExcludedFolders', {});
    var oldValue = setting.get();
    var newValue = {};
    for (var fileSystemPath in oldValue) {
      newValue[fileSystemPath] = [];
      for (var entry of oldValue[fileSystemPath])
        newValue[fileSystemPath].push(entry.path);
    }
    setting.set(newValue);
  }

  _updateVersionFrom15To16() {
    var setting = Common.settings.createSetting('InspectorView.panelOrder', {});
    var tabOrders = setting.get();
    for (var key of Object.keys(tabOrders))
      tabOrders[key] = (tabOrders[key] + 1) * 10;
    setting.set(tabOrders);
  }

  _updateVersionFrom16To17() {
    var setting = Common.settings.createSetting('networkConditionsCustomProfiles', []);
    var oldValue = setting.get();
    var newValue = [];
    if (Array.isArray(oldValue)) {
      for (var preset of oldValue) {
        if (typeof preset.title === 'string' && typeof preset.value === 'object' &&
            typeof preset.value.throughput === 'number' && typeof preset.value.latency === 'number') {
          newValue.push({
            title: preset.title,
            value:
                {download: preset.value.throughput, upload: preset.value.throughput, latency: preset.value.latency}
          });
        }
      }
    }
    setting.set(newValue);
  }

  _updateVersionFrom17To18() {
    var setting = Common.settings.createLocalSetting('workspaceExcludedFolders', {});
    var oldValue = setting.get();
    var newValue = {};
    for (var oldKey in oldValue) {
      var newKey = oldKey.replace(/\\/g, '/');
      if (!newKey.startsWith('file://')) {
        if (newKey.startsWith('/'))
          newKey = 'file://' + newKey;
        else
          newKey = 'file:///' + newKey;
      }
      newValue[newKey] = oldValue[oldKey];
    }
    setting.set(newValue);
  }

  _updateVersionFrom18To19() {
    var defaultColumns = {status: true, type: true, initiator: true, size: true, time: true};
    var visibleColumnSettings = Common.settings.createSetting('networkLogColumnsVisibility', defaultColumns);
    var visibleColumns = visibleColumnSettings.get();
    visibleColumns.name = true;
    visibleColumns.timeline = true;

    var configs = {};
    for (var columnId in visibleColumns) {
      if (!visibleColumns.hasOwnProperty(columnId))
        continue;
      configs[columnId.toLowerCase()] = {visible: visibleColumns[columnId]};
    }
    var newSetting = Common.settings.createSetting('networkLogColumns', {});
    newSetting.set(configs);
    visibleColumnSettings.remove();
  }

  _updateVersionFrom19To20() {
    var oldSetting = Common.settings.createSetting('InspectorView.panelOrder', {});
    var newSetting = Common.settings.createSetting('panel-tabOrder', {});
    newSetting.set(oldSetting.get());
    oldSetting.remove();
  }

  _updateVersionFrom20To21() {
    var networkColumns = Common.settings.createSetting('networkLogColumns', {});
    var columns = /** @type {!Object} */ (networkColumns.get());
    delete columns['timeline'];
    delete columns['waterfall'];
    networkColumns.set(columns);
  }

  _updateVersionFrom21To22() {
    var breakpointsSetting = Common.settings.createLocalSetting('breakpoints', []);
    var breakpoints = breakpointsSetting.get();
    for (var breakpoint of breakpoints) {
      breakpoint['url'] = breakpoint['sourceFileId'];
      delete breakpoint['sourceFileId'];
    }
    breakpointsSetting.set(breakpoints);
  }

  _updateVersionFrom22To23() {
    // This update is no-op.
  }

  _migrateSettingsFromLocalStorage() {
    // This step migrates all the settings except for the ones below into the browser profile.
    var localSettings = new Set([
      'advancedSearchConfig', 'breakpoints', 'consoleHistory', 'domBreakpoints', 'eventListenerBreakpoints',
      'fileSystemMapping', 'lastSelectedSourcesSidebarPaneTab', 'previouslyViewedFiles', 'savedURLs',
      'watchExpressions', 'workspaceExcludedFolders', 'xhrBreakpoints'
    ]);
    if (!window.localStorage)
      return;

    for (var key in window.localStorage) {
      if (localSettings.has(key))
        continue;
      var value = window.localStorage[key];
      window.localStorage.removeItem(key);
      Common.settings._settingsStorage[key] = value;
    }
  }

  /**
   * @param {!Common.Setting} breakpointsSetting
   * @param {number} maxBreakpointsCount
   */
  _clearBreakpointsWhenTooMany(breakpointsSetting, maxBreakpointsCount) {
    // If there are too many breakpoints in a storage, it is likely due to a recent bug that caused
    // periodical breakpoints duplication leading to inspector slowness.
    if (breakpointsSetting.get().length > maxBreakpointsCount)
      breakpointsSetting.set([]);
  }
};

Common.VersionController._currentVersionName = 'inspectorVersion';
Common.VersionController.currentVersion = 23;

/**
 * @type {!Common.Settings}
 */
Common.settings;

/**
 * @param {string} settingName
 * @return {!Common.Setting}
 */
Common.moduleSetting = function(settingName) {
  return Common.settings.moduleSetting(settingName);
};

/**
 * @param {string} settingName
 * @return {!Common.Setting}
 */
Common.settingForTest = function(settingName) {
  return Common.settings.settingForTest(settingName);
};
