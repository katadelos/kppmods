"""Semantic strings identifying stock Metro modules."""

SIGNATURES = {
    "deviceInfo": {
        "getDeviceInfo",
        "renderDeviceInfoRow",
        "settingsapp.device_info",
    },
    "quickSettingsStatusBar": {
        "QuickSettings_deviceNameChanged",
        "renderLeftContainer",
        "quicksettings.statusbar.container",
    },
    "quickActionsHeader": {
        "deviceNameSub",
        "avatarInitial",
        "quickactions.header.container",
    },
    "settingsNavigationRow": {
        "settingsapp.navigational_row.",
        "widgetId",
        "isShowNavigationIcon",
    },
    "settingsSwitchRow": {
        "currentState",
        "settingsapp.row.switchButton",
        "disableControlComponent",
    },
    "settingsSeparator": {
        "BlackRowSeperator",
        "FullWidthBlackRowSeperator",
        "fullWidthSeparator",
    },
    "dialogLauncher": {
        "LauncherStore",
        "[KPPDialogLauncher.show] KPPDialog is closed. Component state could not be updated.",
        "onHideDialog",
    },
    "simpleModal": {
        "SimpleModalV2_",
        "bodyScrollable",
        "nonWrappableFooter",
    },
    "wifiSettings": {
        "getWifiWidgetState",
        "renderPageItems",
        "settingsapp.wifi_and_bluetooth",
    },
    "wifiNetworks": {
        "renderNetworkButton",
        "renderConnectedOverlay",
        "wifiPage.list.wifi.",
    },
    "settingsRoot": {
        "renderSettingsHomeView",
        "renderActiveView",
        "settings.window.contentview",
        "onSettingSelected",
    },
    "react": {
        "__SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED",
        "createElement",
        "useState",
        "Component",
    },
    "reactNative": {
        "requireNativeComponent",
        "NativeModules",
        "AppRegistry",
        "YellowBox",
    },
    "views": {"KPPViews", "appManagerRoute", "KPP_HOME", "KPP_LIBRARY"},
    "viewNames": {"KPP_VIEWS", "LEGACY_APPNAME_TO_APPID", "LEGACY_APP_NAMES"},
    "footer": {"chrome.home.button", "chrome.library.button", "selectionHandler"},
    "bookOverlay": {
        "Empty string for lastItemOpen bookFilePath!",
        "getPortraitScreenWidthInDP",
        "thumbnailLocation",
        "chrome.lastBook.button",
    },
    "home": {
        "TemplateWelcomeUnregisteredCard",
        "Template_unregistered_welcome_parent_layout",
    },
    "homeStyles": {
        "WelcomeUnregisteredTemplateStyles",
        "paddingHorizontal",
        "processStyleSheet",
    },
    "widgetMap": {
        "WidgetMap",
        "file://KPPUIQuickSettings/Right-Chevron.svg",
        "rightChevron",
    },
    "widgetRow": {"WidgetMap", "quicksettings.widgetview", "renderAirplane"},
    "widgetView": {"quicksettings.widgets.button.", "renderWidgetText", "widgetStatus"},
    "settingsWindow": {
        "quicksettings.window.contentview",
        "closeQuickSettingsHandler",
        "renderQuickSettings",
    },
    "chromeContextMenu": {
        "chrome.context.menu.scrollview",
        "contextMenuConfig",
        "renderItems",
        "onNavigateFromContextMenu",
    },
    "sliderRow": {
        "renderWarmthSlider",
        "renderBrightnessSlider",
        "quicksettings.sliderpanel",
    },
    "warmthView": {"quicksettings.warmthview", "caflInfo", "onSliderChange", "memo"},
}

REQUIRED = {"react", "reactNative"}
