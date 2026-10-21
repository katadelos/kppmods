/* KPP extension host. Stock Metro modules are resolved by the activation adapter. */
module.exports = function createHost(global) {
  'use strict';

  if (global.KPPMods) throw new Error('KPPMods host loaded twice');
  const extensionFactories = {},
    activeExtensions = {},
    publishedServices = {};
  let configuration, React, ReactNative;
  let replacing = false;
  const patchedMethods = new Set();
  const installedAdapters = new Set();
  const statusOwners = new Set();
  const statusSyncStates = new Map();
  const statusRetryDelays = [250, 500, 1000, 2000, 4000, 8000];
  const componentSlots = {};
  const packageRevisions = {};
  const registry = require('./registry')(reportExtensionError);
  const {
    contributions,
    kinds: contributionKinds,
    emit: emitLocalEvent,
    subscribe: subscribeToEvent,
    list: listContributions
  } = registry;
  let broker, useRegistry, ExtensionErrorBoundary;
  const requestBroker = (operation, body) => broker.request(operation, body);
  function cancelStatusSync(owner) {
    const state = statusSyncStates.get(owner);
    if (!state) return;
    if (state.timer !== null) clearTimeout(state.timer);
    state.timer = null;
    state.generation++;
  }
  function reportExtensionError(owner, error) {
    console.error('[KPPMods:' + owner + '] ' + (error.stack || error));
    if (configuration && ReactNative)
      requestBroker('events', {
        package: activeExtensions[owner] ? owner : configuration.ui_runtime,
        name: 'ui:error',
        data: {
          owner,
          message: String(error.stack || error)
        }
      }).catch(() => {});
  }
  function loadStockModule(name, check) {
    const id = configuration.modules && configuration.modules[name];
    if (typeof id !== 'number') throw new Error('No resolved stock module: ' + name);
    const exports = global.__r(id);
    if (check && !check(exports))
      throw new Error('Stock module shape mismatch: ' + name + ' (' + id + ')');
    return exports;
  }
  function patchStockMethod(key, target, property, wrapper) {
    if (patchedMethods.has(key)) return;
    const original = target[property];
    if (typeof original !== 'function') throw new Error('Invalid patch target ' + key);
    const replacement = wrapper(original);
    target[property] = replacement;
    patchedMethods.add(key);
  }
  function resolveIconSource(id) {
    const item = contributions.icons[id];
    if (!item) return id && /^(file:|data:|https?:)/.test(id) ? id : undefined;
    return item.uri;
  }
  function executeAction(owner, action, args) {
    if (typeof action !== 'string' || !action)
      return Promise.reject(new Error('Action identifier is required'));
    const id = action.indexOf(':') < 0 ? owner + ':' + action : action;
    const item = contributions.actions[id];
    if (item && item.run) return Promise.resolve().then(() => item.run(args));
    const colon = id.indexOf(':');
    return requestBroker('action', {
      package: id.slice(0, colon),
      action: id.slice(colon + 1),
      args: args || {}
    });
  }
  function createActionHandler(item) {
    return (value) => {
      executeAction(
        item.owner,
        item.action,
        value === undefined ? item.args : value
      ).catch((error) => {
        reportExtensionError(item.owner, error);
      });
    };
  }
  function wrapExtensionComponent(item) {
    // A route keeps one component identity while its owning package is replaced.
    if (!componentSlots[item.id]) {
      componentSlots[item.id] = function ExtensionSlot(props) {
        const current = useRegistry('tabs').find((entry) => entry.id === item.id);
        const route = current && current.route;
        React.useEffect(() => {
          if (!route || props.activeAppID !== route) return;
          // Native KPP does not have a chrome profile for extension routes.
          // Configure it on entry so Settings or a reader cannot leave their
          // title, controls, or hidden footer behind on an extension tab.
          ReactNative.NativeModules.KPPChromeConfigurationModule.configureChrome(
            {
              appId: route,
              source: 'org.kppmods.' + current.owner,
              topNavBar: { template: 'searchBar', buttons: [] },
              systemMenu: { clientParams: { profile: { items: [] } } },
              bottomChromeConfig: { hideBottomChrome: false }
            },
            {}
          );
        }, [route, props.activeAppID]);
        if (!current) return null;
        return React.createElement(
          ExtensionErrorBoundary,
          { owner: current.owner, key: packageRevisions[current.owner] || 0 },
          React.createElement(current.component, props)
        );
      };
    }
    return componentSlots[item.id];
  }
  function ensureAdapters() {
    Object.keys(stockAdapters).forEach((kind) => {
      if (
        installedAdapters.has(kind) ||
        (!configuration.development && !listContributions(kind).length)
      )
        return;
      try {
        stockAdapters[kind]();
        installedAdapters.add(kind);
      } catch (error) {
        reportExtensionError(kind, error);
      }
    });
  }
  function activateExtension(extensionPackage, factory) {
    const api = require('./extension-context')(extensionPackage, {
      configuration,
      React,
      ReactNative,
      extensionHost,
      broker,
      registry,
      executeAction,
      useRegistry,
      resolveIconSource,
      loadStockModule,
      publishedServices,
      reportExtensionError,
      revisionForPackage: (id) => packageRevisions[id] || 0
    });
    api.onDispose(() => cancelStatusSync(extensionPackage.id));
    try {
      const cleanup = factory(api);
      if (typeof cleanup === 'function') api.onDispose(cleanup);
      activeExtensions[extensionPackage.id] = api;
    } catch (error) {
      api.stop();
      throw error;
    }
  }
  const stockAdapters = {
    menuItems: () =>
      require('../adapters/menu')({
        loadStockModule,
        listContributions,
        patchStockMethod,
        ReactNative
      }),
    tabs: () =>
      require('../adapters/tabs')({
        loadStockModule,
        listContributions,
        patchStockMethod,
        useRegistry,
        React,
        wrapExtensionComponent,
        contributions,
        subscribeToEvent
      }),
    settingsPages: () =>
      require('../adapters/settings')({
        React,
        loadStockModule,
        listContributions,
        patchStockMethod,
        ExtensionErrorBoundary
      }),
    wifiSettings: () =>
      require('../adapters/wifi-settings')({
        React,
        ReactNative,
        loadStockModule,
        patchStockMethod,
        useRegistry,
        ExtensionErrorBoundary
      }),
    home: () =>
      require('../adapters/home')({
        loadStockModule,
        patchStockMethod,
        useRegistry,
        React,
        ReactNative,
        ExtensionErrorBoundary
      }),
    quickSettings: () =>
      require('../adapters/quick-settings')({
        loadStockModule,
        patchStockMethod,
        React,
        useRegistry,
        resolveIconSource,
        executeAction,
        reportExtensionError,
        ExtensionErrorBoundary
      }),
    sliders: () =>
      require('../adapters/sliders')({
        loadStockModule,
        React,
        createActionHandler,
        patchStockMethod,
        useRegistry,
        ExtensionErrorBoundary
      })
  };
  const extensionHost = (global.KPPMods = {
    toolkit: null,
    define: (id, factory) => {
      if (extensionFactories[id]) throw new Error('Duplicate extension ' + id);
      extensionFactories[id] = factory;
    },
    stock: loadStockModule,
    start: (options) => {
      if (configuration) throw new Error('KPPMods already started');
      configuration = options;
      React = loadStockModule(
        'react',
        (moduleExports) => moduleExports.createElement && moduleExports.Component
      );
      ReactNative = loadStockModule(
        'reactNative',
        (moduleExports) => moduleExports.NativeModules && moduleExports.View
      );
      useRegistry = registry.createHook(React);
      ExtensionErrorBoundary = require('./error-boundary.jsx')(
        React,
        ReactNative,
        reportExtensionError
      );
      broker = require('./broker-client')(ReactNative, (state) => {
        (state.events || []).forEach((event) =>
          emitLocalEvent('bridge:' + event.name, event)
        );
        emitLocalEvent('broker:state', state);
        Object.keys(state.settings || {}).forEach((id) => {
          if (activeExtensions[id])
            activeExtensions[id].refreshSettings(state.settings[id]);
        });
      });
      if (extensionHost.loadToolkit)
        extensionHost.toolkit = extensionHost.loadToolkit();
      (configuration.packages || []).forEach((extensionPackage) => {
        if (!extensionFactories[extensionPackage.id]) return;
        try {
          activateExtension(extensionPackage, extensionFactories[extensionPackage.id]);
        } catch (error) {
          reportExtensionError(extensionPackage.id, error);
        }
      });
      ensureAdapters();
      function syncStatus() {
        (configuration.packages || []).forEach((extensionPackage) => {
          const items = listContributions('status').filter(
            (item) => item.owner === extensionPackage.id
          );
          if (!items.length && !statusOwners.has(extensionPackage.id)) return;
          const owner = extensionPackage.id;
          cancelStatusSync(owner);
          const api = activeExtensions[owner];
          if (!api) return;
          statusOwners.add(owner);
          if (!statusSyncStates.has(owner))
            statusSyncStates.set(owner, { generation: 0, timer: null });
          const state = statusSyncStates.get(owner);
          const generation = state.generation;
          const revision = packageRevisions[owner] || 0;
          let retryAttempt = 0;
          const current = () =>
            state.generation === generation &&
            activeExtensions[owner] === api &&
            (packageRevisions[owner] || 0) === revision;
          function send() {
            if (!current()) return;
            state.timer = null;
            requestBroker('status', {
              package: owner,
              items: listContributions('status')
                .filter((item) => item.owner === owner)
                .map((item) => ({
                  id: item.id.slice(owner.length + 1),
                  icon: resolveIconSource(item.icon),
                  uri: item.uri
                }))
            }).catch((error) => {
              if (!current()) return;
              const unavailable = error.message === 'Statusbar update rejected: 3';
              if (unavailable && retryAttempt < statusRetryDelays.length) {
                state.timer = setTimeout(send, statusRetryDelays[retryAttempt++]);
                return;
              }
              reportExtensionError(
                owner,
                unavailable
                  ? new Error(error.message + ' after ' + retryAttempt + ' retries')
                  : error
              );
            });
          }
          send();
        });
      }
      syncStatus();
      subscribeToEvent('registry:status', syncStatus);
      if (configuration.development) {
        require('./development')({
          extensionHost,
          configuration,
          requestBroker,
          reportExtensionError
        });
      }
      emitLocalEvent('host:ready', extensionHost.inspect());
      if (ReactNative)
        requestBroker('events', {
          package: configuration.ui_runtime,
          name: 'ui:ready',
          data: extensionHost.inspect()
        }).catch((error) => {
          console.warn('[KPPMods] Cannot report ready: ' + error.message);
        });
      console.log('[KPPMods] active: ' + Object.keys(activeExtensions).join(', '));
    },
    stop: (id) => {
      if (activeExtensions[id]) {
        activeExtensions[id].stop();
        delete activeExtensions[id];
      }
    },
    replaceSource: (id, source, metadata) => {
      let factory;
      const loader = Object.create(extensionHost);
      loader.define = (candidateId, definition) => {
        if (candidateId !== id || factory)
          throw new Error('Expected one definition for ' + id);
        factory = definition;
      };
      // Parse/evaluate the registration wrapper before disposing the active mod.
      new Function('KPPMods', source)(loader);
      if (!factory) throw new Error('No extension definition in update');
      extensionHost.replace(id, factory, metadata);
    },
    replace: (id, factory, metadata = {}) => {
      const extensionPackage = configuration.packages.find((item) => item.id === id);
      if (!extensionPackage || !activeExtensions[id])
        throw new Error('Package is not active: ' + id);
      if (extensionPackage.ui_runtime)
        throw new Error('The UI host requires a process restart');
      if (typeof factory !== 'function')
        throw new Error('Expected an extension factory');
      if (replacing) throw new Error('Another replacement is in progress');
      const previous = extensionFactories[id];
      const previousMetadata = Object.assign({}, extensionPackage);
      ['root', 'options', 'apps', 'title'].forEach((key) => {
        if (metadata[key] !== undefined) extensionPackage[key] = metadata[key];
      });
      configuration.settings = configuration.settings || {};
      configuration.settings[id] = activeExtensions[id].settings.snapshot();
      replacing = true;
      registry.beginBatch();
      packageRevisions[id] = (packageRevisions[id] || 0) + 1;
      cancelStatusSync(id);
      try {
        activeExtensions[id].stop();
        delete activeExtensions[id];
        try {
          activateExtension(extensionPackage, factory);
          extensionFactories[id] = factory;
        } catch (error) {
          Object.assign(extensionPackage, previousMetadata);
          activateExtension(extensionPackage, previous);
          throw error;
        }
      } finally {
        replacing = false;
        ensureAdapters();
        registry.endBatch();
      }
    },
    inspect: () => ({
      packages: Object.keys(activeExtensions),
      contributions: contributionKinds.reduce((out, key) => {
        out[key] = listContributions(key).map((item) => item.id);
        return out;
      }, {})
    })
  });
};
