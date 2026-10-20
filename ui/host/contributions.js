// Bind contribution IDs, validation and cleanup to one extension.
module.exports = function createContributions(
  owner,
  { registry, scope, qualifyIdentifier, assetUri, revisionForPackage }
) {
  const {
    kinds: contributionKinds,
    contributions,
    list: listContributions,
    emit: emitLocalEvent
  } = registry;
  const api = {};
  contributionKinds.forEach((kind) => {
    const registrations = new Map();
    api[kind] = {
      list: () => listContributions(kind).filter((item) => item.owner === owner),
      remove: (id) => {
        const full = id.indexOf(':') < 0 ? qualifyIdentifier(id) : id;
        if (contributions[kind][full] && contributions[kind][full].owner !== owner)
          throw new Error('Cannot remove another package contribution');
        const dispose = registrations.get(full);
        if (dispose) dispose();
      },
      add: (definition) => {
        const id = qualifyIdentifier(definition.id);
        if (contributions[kind][id]) throw new Error('Duplicate ' + kind + ': ' + id);
        if (
          ((kind === 'tabs' && !definition.hide) ||
            kind === 'home' ||
            kind === 'wifiSettings' ||
            (kind === 'settingsPages' && !definition.hide)) &&
          typeof definition.component !== 'function'
        )
          throw new Error(kind + ' requires a React component');
        const item = Object.assign({}, definition, {
          id,
          owner: owner,
          revision: revisionForPackage(owner)
        });
        if (item.icon && item.icon.indexOf(':') < 0)
          item.icon = qualifyIdentifier(item.icon);
        if (kind === 'icons') item.uri = definition.uri || assetUri(definition.path);
        if (kind === 'status' && (!definition.uri || !/^app:\/\//.test(definition.uri)))
          throw new Error('Status item requires an app:// URI');
        if (
          kind === 'menuItems' &&
          (!definition.uri || !/^app:\/\//.test(definition.uri))
        )
          throw new Error('Menu item requires an app:// URI');
        contributions[kind][id] = item;
        registrations.set(
          id,
          scope.track(() => {
            registrations.delete(id);
            delete contributions[kind][id];
            emitLocalEvent('registry:' + kind);
          })
        );
        emitLocalEvent('registry:' + kind);
        return id;
      }
    };
  });
  api.tabs.hide = (stockId) =>
    api.tabs.add({
      id: 'hide-' + stockId,
      hide: true,
      stockId
    });
  api.settingsPages.hide = (stockId) =>
    api.settingsPages.add({
      id: 'hide-' + stockId,
      hide: true,
      stockId
    });
  return api;
};
