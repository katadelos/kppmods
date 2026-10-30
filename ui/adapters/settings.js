// Render extensions with stock Settings rows in both list and side-nav layouts.
module.exports = function installSettings(host) {
  const {
    React,
    loadStockModule,
    listContributions,
    patchStockMethod,
    ExtensionErrorBoundary
  } = host;
  const Settings = loadStockModule(
    'settingsRoot',
    (value) => value.default && typeof value.default.prototype.render === 'function'
  ).default;
  const NavRow = loadStockModule(
    'settingsNavigationRow',
    (value) => !!value.default
  ).default;
  const ToggleRow = loadStockModule(
    'settingsSwitchRow',
    (value) => !!value.default
  ).default;
  const Separator = loadStockModule(
    'settingsSeparator',
    (value) => !!value.default
  ).default;
  const wired = new WeakSet();
  const items = () => listContributions('settingsPages').filter((item) => !item.hide);
  const visible = (id) =>
    !listContributions('settingsPages').some(
      (item) => item.hide && item.stockId === id
    );
  function row(instance, item, options, onPress) {
    // Reuse the native widget factory, including the sidebar's selection styling.
    return React.cloneElement(
      instance.createWidget(
        'device_options',
        onPress || (() => instance.navigateToView(item.id)),
        Object.assign({}, options, {
          title: item.title,
          description: '',
          widgetId: item.id
        })
      ),
      { key: item.id }
    );
  }
  function adapt(element, instance) {
    if (!React.isValidElement(element)) return element;
    const props = element.props || {};
    if (
      Array.isArray(props.sideNavComponentIds) &&
      typeof props.createWidgetFunc === 'function'
    ) {
      const create = props.createWidgetFunc;
      return React.cloneElement(element, {
        sideNavComponentIds: items()
          .filter((item) => !item.parent)
          .map((item) => item.id)
          .concat(props.sideNavComponentIds.filter(visible)),
        createWidgetFunc: (id, callback, options) => {
          const item = items().find((entry) => entry.id === id);
          return item
            ? row(instance, item, options, callback)
            : create(id, callback, options);
        }
      });
    }
    if (props.children)
      return React.cloneElement(
        element,
        {},
        React.Children.map(props.children, (child) => adapt(child, instance))
      );
    return element;
  }
  patchStockMethod(
    'settingsPages',
    Settings.prototype,
    'render',
    (original) =>
      function () {
        if (!wired.has(this)) {
          const home = this.renderSettingsHomeView;
          const active = this.renderActiveView;
          const title = this.getChromeTitle;
          this.getChromeTitle = (id, ...args) => {
            const page = items().find((item) => item.id === id);
            return page ? page.title : title.call(this, id, ...args);
          };
          this.renderActiveView = (id, ...args) => {
            const page = items().find((item) => item.id === id);
            if (!page) return active.call(this, id, ...args);
            return React.createElement(
              ExtensionErrorBoundary,
              { owner: page.owner },
              React.createElement(page.component, {
                NavigationRow: NavRow,
                ToggleRow,
                PageContainer: React.Fragment,
                Separator,
                navigateToPage: (nextId) => this.navigateToView(nextId)
              })
            );
          };
          this.renderSettingsHomeView = (ids, ...args) =>
            items()
              .filter((item) => !item.parent)
              .map((item) => row(this, item))
              .concat(home.call(this, ids.filter(visible), ...args));
          wired.add(this);
        }
        // Native routing owns the chrome, Back stack, context and two-pane layout.
        return adapt(original.apply(this, arguments), this);
      }
  );
};
