// Register extension routes and merge their buttons into the stock footer.
module.exports = function installTabs(host) {
  const {
    loadStockModule,
    listContributions,
    patchStockMethod,
    useRegistry,
    React,
    wrapExtensionComponent,
    contributions,
    subscribeToEvent
  } = host;
  const { View } = loadStockModule('reactNative');
  const views = loadStockModule(
    'views',
    (moduleExports) => moduleExports.KPPViews && moduleExports.KPPViews.KPP_HOME
  ).KPPViews;
  const names = loadStockModule('viewNames', (moduleExports) =>
    Array.isArray(moduleExports.KPP_VIEWS)
  ).KPP_VIEWS;
  const footer = loadStockModule(
    'footer',
    (moduleExports) =>
      moduleExports.default && typeof moduleExports.default.type === 'function'
  ).default;
  if (
    listContributions('tabs').some(
      (item) => item.hide && item.stockId === 'chrome.lastBook.button'
    )
  ) {
    const bookOverlay = loadStockModule(
      'bookOverlay',
      (moduleExports) =>
        moduleExports.default && typeof moduleExports.default.type === 'function'
    ).default;
    // The floating book cover is rendered outside ChromeFooter on current KPP.
    // Its own wrapper must observe the same hide registry as the footer tabs.
    patchStockMethod('bookOverlay', bookOverlay, 'type', (original) => (props) => {
      const tabs = useRegistry('tabs');
      return tabs.some((item) => item.hide && item.stockId === 'chrome.lastBook.button')
        ? null
        : React.createElement(original, props);
    });
  }
  function synchronize() {
    listContributions('tabs').forEach((item) => {
      if (item.hide) return;
      const route = 'KPP_MOD_' + item.id.replace(/[^a-zA-Z0-9]/g, '_').toUpperCase();
      item.route = route;
      if (views[route] && views[route].__modOwner !== item.id)
        throw new Error('View collision: ' + route);
      views[route] = {
        name: route,
        component: wrapExtensionComponent(item),
        appManagerRoute: 'app://com.lab126.KPPMainApp/?view=' + route,
        layer: views.KPP_HOME.layer,
        __modOwner: item.id
      };
      if (names.indexOf(route) < 0) names.push(route);
    });
    Object.keys(views).forEach((name) => {
      if (views[name].__modOwner && !contributions.tabs[views[name].__modOwner]) {
        delete views[name];
        const index = names.indexOf(name);
        if (index >= 0) names.splice(index, 1);
      }
    });
  }
  synchronize();
  subscribeToEvent('registry:tabs', synchronize);
  patchStockMethod('tabs', footer, 'type', (original) => (props) => {
    const tabs = useRegistry('tabs'),
      tree = original(props);
    let children = React.Children.toArray(tree.props.children);
    const template = children.filter(
      (child) => child.props && child.props.testID === 'chrome.home.button'
    )[0];
    if (!template) return tree;
    function containsDivider(node) {
      return (
        React.isValidElement(node) &&
        (node.props.barOrientation === 'vertical' ||
          React.Children.toArray(node.props.children).some(containsDivider))
      );
    }
    const divider = children.find(containsDivider);
    const hidden = tabs.filter((tab) => tab.hide).map((tab) => tab.stockId);
    function visible(node) {
      if (!React.isValidElement(node)) return node;
      if (hidden.indexOf(node.props.testID) >= 0) return null;
      // Scribe reserves a separate empty View for the floating book cover.
      // Remove that space with the cover so all remaining tabs share the row.
      if (
        props.isScreenSize10Plus &&
        hidden.indexOf('chrome.lastBook.button') >= 0 &&
        node.type === View &&
        node.props.children === undefined &&
        !node.props.testID
      )
        return null;
      // The stock book shortcut can be wrapped in a layout element; its
      // image-navigation callback identifies the actual cover contribution.
      if (
        hidden.indexOf('chrome.lastBook.button') >= 0 &&
        typeof node.props.imageNavigationHandler === 'function'
      )
        return null;
      if (node.props.children === undefined) return node;
      return React.cloneElement(
        node,
        {},
        React.Children.map(node.props.children, visible)
      );
    }
    children = children.map(visible).filter((child) => child !== null);
    tabs
      .filter((tab) => !tab.hide && tab.footer !== false)
      .forEach((item) => {
        const node = React.cloneElement(template, {
          key: item.id,
          testID: 'kppmods.' + item.id,
          title: item.title,
          selected: props.activeAppID === item.route,
          selectionHandler: () => {
            props.navigation.onNavigateToKPPViewCallback({
              view: item.route
            });
          },
          flowTo: undefined
        });
        const index = children.findIndex(
          (child) => child.props && child.props.testID === 'chrome.more.button'
        );
        const additions = divider
          ? [React.cloneElement(divider, { key: item.id + ':divider' }), node]
          : [node];
        children.splice(index < 0 ? children.length : index, 0, ...additions);
      });
    const customSelected = tabs.some((item) => item.route === props.activeAppID);
    return React.cloneElement(
      tree,
      {},
      children.map((child) => {
        if (!child.props || !child.props.selectionHandler) return child;
        return React.cloneElement(child, {
          flowTo: undefined,
          selected: customSelected
            ? child.props.selected && /^kppmods\./.test(child.props.testID || '')
            : child.props.selected,
          style: [
            child.props.style,
            {
              flex: 1,
              width: undefined,
              marginLeft: 0,
              marginRight: 0,
              // Stock side tabs reserve space for the central book cover.
              // Equal-width tabs need symmetric content insets instead.
              paddingHorizontal: 0,
              paddingLeft: 0,
              paddingRight: 0
            }
          ]
        });
      })
    );
  });
};
