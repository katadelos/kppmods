// Append extension rows inside the stock Wi-Fi page's scrolling row container.
module.exports = function installWifiSettings(host) {
  const {
    React,
    ReactNative,
    loadStockModule,
    patchStockMethod,
    useRegistry,
    ExtensionErrorBoundary
  } = host;
  const WifiSettings = loadStockModule(
    'wifiSettings',
    (value) => value.default && typeof value.default.prototype.render === 'function'
  ).default;
  const rowProps = {
    NavigationRow: loadStockModule('settingsNavigationRow').default,
    ToggleRow: loadStockModule('settingsSwitchRow').default,
    Separator: loadStockModule('settingsSeparator').default,
    PageContainer: React.Fragment
  };
  function Rows() {
    const items = useRegistry('wifiSettings');
    return items.map((item) =>
      React.createElement(
        ExtensionErrorBoundary,
        { owner: item.owner, key: item.id + ':' + item.revision },
        React.createElement(item.component, rowProps)
      )
    );
  }
  const wired = new WeakSet();
  patchStockMethod(
    'wifiSettings',
    WifiSettings.prototype,
    'render',
    (original) =>
      function () {
        if (!wired.has(this)) {
          const renderItems = this.renderPageItems;
          this.renderPageItems = (...args) => {
            const content = renderItems.apply(this, args);
            return React.cloneElement(
              content,
              {},
              content.props.children,
              React.createElement(
                ReactNative.View,
                { key: 'extension-wifi-settings', style: content.props.separatorStyle },
                React.createElement(Rows)
              )
            );
          };
          wired.add(this);
        }
        return original.apply(this, arguments);
      }
  );
};
