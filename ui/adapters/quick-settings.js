// Append registered buttons to the stock menu and use its own close handler.
module.exports = function installQuickSettings(host) {
  const {
    loadStockModule,
    patchStockMethod,
    React,
    useRegistry,
    resolveIconSource,
    executeAction,
    reportExtensionError,
    ExtensionErrorBoundary
  } = host;
  const widgetDefinitions = loadStockModule(
    'widgetMap',
    (moduleExports) => moduleExports.WidgetMap
  ).WidgetMap;
  const prototype = loadStockModule(
    'widgetRow',
    (moduleExports) =>
      moduleExports.default &&
      moduleExports.default.prototype &&
      typeof moduleExports.default.prototype.render === 'function'
  ).default.prototype;
  const Widget = loadStockModule(
    'widgetView',
    (moduleExports) => !!moduleExports.default
  ).default;
  const settingsWindowPrototype = loadStockModule(
    'settingsWindow',
    (moduleExports) =>
      moduleExports.default &&
      moduleExports.default.prototype &&
      moduleExports.default.prototype.render
  ).default.prototype;
  let closeQuickSettings;
  patchStockMethod(
    'settingsWindow',
    settingsWindowPrototype,
    'render',
    function (original) {
      return function () {
        closeQuickSettings =
          this.closeQuickSettingsHandler && this.closeQuickSettingsHandler.bind(this);
        return original.apply(this, arguments);
      };
    }
  );
  function ExtensionWidget({ item, stockWidget }) {
    const [, setRevision] = React.useState(0);
    const [pending, setPending] = React.useState(false);
    const [failed, setFailed] = React.useState(false);
    const busy = React.useRef(false);
    const mounted = React.useRef(false);
    React.useEffect(() => {
      mounted.current = true;
      const refresh = () => {
        if (mounted.current) setRevision((revision) => revision + 1);
      };
      const dispose = item.subscribe && item.subscribe(refresh);
      refresh();
      return () => {
        mounted.current = false;
        if (typeof dispose === 'function') dispose();
        delete widgetDefinitions[item.id];
      };
    }, [item]);
    const icon = {
      iconPath: resolveIconSource(item.icon),
      accessibilityLabel: item.title
    };
    if (!icon.iconPath) return null;
    widgetDefinitions[item.id] = {
      On: item.iconOn ? { ...icon, iconPath: resolveIconSource(item.iconOn) } : icon,
      Off: icon
    };
    return React.createElement(Widget, {
      widgetName: item.id,
      widgetText: item.title + (pending ? '…' : failed ? ' · Failed' : ''),
      widgetStatus: item.value && item.value() ? 'On' : 'Off',
      widgetStyle: stockWidget.widgetStyle,
      is2xScalingSupported: stockWidget.is2xScalingSupported,
      onWidgetPress: () => {
        if (busy.current || (item.disabled && item.disabled())) return;
        busy.current = true;
        setPending(true);
        setFailed(false);
        if (item.close !== false && closeQuickSettings) closeQuickSettings();
        executeAction(item.owner, item.action, item.args).then(
          () => {
            busy.current = false;
            if (mounted.current) setPending(false);
          },
          (error) => {
            busy.current = false;
            if (mounted.current) {
              setPending(false);
              setFailed(true);
            }
            reportExtensionError(item.owner, error);
          }
        );
      }
    });
  }
  function ExtensionWidgets({ stockWidget }) {
    const items = useRegistry('quickSettings');
    useRegistry('icons');
    return React.createElement(
      React.Fragment,
      {},
      items.map((item) =>
        React.createElement(
          ExtensionErrorBoundary,
          { key: item.id + ':' + item.revision, owner: item.owner },
          React.createElement(ExtensionWidget, { item, stockWidget })
        )
      )
    );
  }
  patchStockMethod(
    'quickSettings',
    prototype,
    'render',
    (original) =>
      function () {
        const tree = original.apply(this, arguments);
        // Reuse the actual stock widget layout. 5.14.1 supplies this inline;
        // later firmware computes it through getWidgetViewStyle().
        const children = React.Children.toArray(tree.props.children);
        const stockWidget = children.find(
          (child) => React.isValidElement(child) && child.props.widgetStyle !== undefined
        );
        if (!stockWidget) return tree;
        return React.cloneElement(
          tree,
          {},
          children.concat(
            React.createElement(ExtensionWidgets, {
              key: 'kppmods-widgets',
              stockWidget: stockWidget.props
            })
          )
        );
      }
  );
};
