// Contribution storage and local notifications are shared by device and preview.
const kinds = [
  'tabs',
  'menuItems',
  'settingsPages',
  'wifiSettings',
  'home',
  'quickSettings',
  'sliders',
  'status',
  'icons',
  'actions'
];
module.exports = function createRegistry(reportError) {
  const contributions = kinds.reduce((items, kind) => {
    items[kind] = {};
    return items;
  }, {});
  const listeners = new Map();
  const pending = new Set();
  let batching = false;
  function emit(name, value) {
    if (batching && name.startsWith('registry:')) {
      pending.add(name);
      return;
    }
    Array.from(listeners.get(name) || []).forEach((listener) => {
      try {
        listener(value);
      } catch (error) {
        reportError(name, error);
      }
    });
  }
  function subscribe(name, listener) {
    if (!listeners.has(name)) listeners.set(name, new Set());
    const group = listeners.get(name);
    group.add(listener);
    return () => {
      if (group.delete(listener) && !group.size) listeners.delete(name);
    };
  }
  const list = (kind) =>
    Object.keys(contributions[kind])
      .map((id) => contributions[kind][id])
      .sort((a, b) => (a.order || 0) - (b.order || 0) || a.id.localeCompare(b.id));
  return {
    kinds,
    contributions,
    emit,
    subscribe,
    list,
    beginBatch() {
      batching = true;
    },
    endBatch() {
      batching = false;
      const changed = Array.from(pending);
      pending.clear();
      changed.forEach((name) => emit(name));
    },
    createHook(React) {
      return function useRegistry(kind) {
        const [, render] = React.useState(0);
        React.useEffect(() => {
          const off = subscribe('registry:' + kind, () =>
            render((revision) => revision + 1)
          );
          render((revision) => revision + 1);
          return off;
        }, [kind]);
        return list(kind);
      };
    }
  };
};
