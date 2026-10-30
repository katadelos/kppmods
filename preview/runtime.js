// Share extension ownership, settings and disposal with the device runtime.
import React, { useEffect, useState } from 'react';
import * as RN from './native.jsx';
import * as toolkit from 'react-kindle';
import validateSettings from '../ui/host/validate-settings.js';
import createRegistry from '../ui/host/registry.js';
import createExtensionContext from '../ui/host/extension-context.js';

const registry = createRegistry(reportError);
const { contributions, list, subscribe, emit } = registry;
export { list, subscribe, emit };
const extensions = new Map(),
  publishedServices = {};
const storageKey = 'kppmods:preview:settings';
let configuration,
  actionResult = 'success',
  events = [],
  sequence = 0;
export let entries = [];
export function record(name, data, owner = 'preview') {
  const item = {
    seq: ++sequence,
    name,
    data,
    package: owner,
    time: new Date().toLocaleTimeString()
  };
  events = [...events.slice(-199), item];
  emit('inspector', events);
  emit('bridge:' + name, item);
  return item;
}
export const useRegistry = registry.createHook(React);
export function useEvents() {
  const [value, update] = useState(events);
  useEffect(() => subscribe('inspector', update), []);
  return value;
}
export function getExtension(id) {
  if (!extensions.has(id)) throw Error('Extension has not been activated: ' + id);
  return extensions.get(id);
}
export const resolveIcon = (id) => RN.assetURL(contributions.icons[id]?.uri || id);
export function navigate(uri) {
  record('navigate', { uri });
  const view = String(uri).includes('://')
    ? new URL(uri).searchParams.get('view')
    : uri;
  const tab = list('tabs').find(
    (item) => 'KPP_MOD_' + item.id.replace(/[^a-zA-Z0-9]/g, '_').toUpperCase() === view
  );
  emit(
    'navigate',
    tab?.id ||
      (view === 'KPP_LIBRARY'
        ? 'library'
        : view === 'KPP_SETTINGS'
          ? 'settings'
          : view === 'KPP_HOME'
            ? 'home'
            : 'unknown')
  );
}
export function setActionResult(value) {
  actionResult = value;
}
export async function execute(owner, action, args) {
  if (typeof action !== 'string' || !action)
    throw Error('Action identifier is required');
  const id = action.includes(':') ? action : `${owner}:${action}`;
  const local = contributions.actions[id];
  if (local?.run) {
    record('action:js', { id, args }, owner);
    return local.run(args);
  }
  const [packageId, name] = id.split(':');
  const definition = configuration.packages.find((item) => item.id === packageId)
    ?.actions?.[name];
  if (!definition) throw Error('Undeclared action: ' + id);
  record(
    'action:dispatch',
    { id, args, argv: definition.argv, user: definition.user, simulated: true },
    packageId
  );
  if (actionResult === 'failure') throw Error('Simulated action failure: ' + id);
  if (actionResult === 'timeout') throw Error('Simulated action timeout: ' + id);
  // Interpret navigation for an interactive preview; never execute argv.
  const uri = definition.argv?.find((value) => value.startsWith('app://'));
  if (uri) navigate(uri);
  queueMicrotask(() =>
    record('action:exit', { action: name, exitCode: 0, simulated: true }, packageId)
  );
  return { dispatched: true, simulated: true };
}
export function runItem(item, args = item.args) {
  return execute(item.owner, item.action, args).catch((error) =>
    reportError(item.owner, error)
  );
}
export function reportError(owner, error) {
  console.error(error);
  record('ui:error', { owner, message: String(error.message || error) }, owner);
}
async function requestBroker(operation, body) {
  if (operation === 'state')
    return {
      settings: configuration.settings,
      events,
      seq: sequence,
      streamId: 'preview'
    };
  if (operation === 'settings') {
    const id = body.package;
    const pkg = configuration.packages.find((item) => item.id === id);
    if (!pkg || !extensions.has(id)) throw Error('Unknown active extension: ' + id);
    validateSettings(pkg.settingsSchema || {}, body.values);
    const values = { ...pkg.settings, ...configuration.settings[id], ...body.values };
    const settings = { ...configuration.settings, [id]: values };
    localStorage.setItem(
      storageKey,
      JSON.stringify({
        ...JSON.parse(localStorage.getItem(storageKey) || '{}'),
        ...settings
      })
    );
    configuration.settings = settings;
    record('settings:changed', values, id);
    extensions.get(id).refreshSettings(values);
    return values;
  }
  if (operation === 'events') {
    record(body.name, body.data, body.package);
    return { ok: true };
  }
  throw Error('Unsupported preview broker request: ' + operation);
}
export function resetSettings() {
  localStorage.removeItem(storageKey);
  location.reload();
}
export function initialize(packageEntries, firmware) {
  entries = packageEntries;
  let settings = {};
  try {
    settings = JSON.parse(localStorage.getItem(storageKey) || '{}');
  } catch {
    localStorage.removeItem(storageKey);
  }
  configuration = {
    firmware,
    settings,
    packages: entries.map(({ manifest }) => ({
      ...manifest,
      root: '/packages/' + manifest.id
    }))
  };
  const disabledKey = 'kppmods:preview:disabled';
  const disabledPackages = new Set(
    JSON.parse(sessionStorage.getItem(disabledKey) || '[]')
  );
  const availablePackages = configuration.packages;
  configuration.packages = availablePackages.filter(
    (pkg) => !disabledPackages.has(pkg.id)
  );
  configuration.settings = Object.fromEntries(
    configuration.packages.map((pkg) => [
      pkg.id,
      { ...pkg.settings, ...settings[pkg.id] }
    ])
  );
  const capabilities = new URLSearchParams(location.search).get('capabilities');
  configuration.modules = Object.fromEntries(
    (capabilities === null
      ? [
          'settingsRoot',
          'settingsNavigationRow',
          'settingsSwitchRow',
          'settingsSeparator'
        ]
      : capabilities.split(',').filter(Boolean)
    ).map((name) => [name, true])
  );
  const host = {
    configuration,
    React,
    ReactNative: RN,
    extensionHost: { toolkit },
    broker: { request: requestBroker, subscribe: () => () => {} },
    registry,
    executeAction: execute,
    useRegistry,
    resolveIconSource: resolveIcon,
    loadStockModule: (name) => {
      throw Error(
        `Stock Metro module '${name}' is device-only; use the toolkit or an explicit preview adapter`
      );
    },
    publishedServices,
    revisionForPackage: () => 0,
    reportExtensionError: reportError
  };
  for (const entry of entries) {
    if (disabledPackages.has(entry.manifest.id)) continue;
    const manifest = configuration.packages.find(
      (item) => item.id === entry.manifest.id
    );
    const api = createExtensionContext(manifest, host);
    // Browser management is an explicit simulation; never launch host commands.
    api.exec = () => Promise.reject(new Error('Shell execution requires a device'));
    api.management = {
      apps: async () => [],
      launchApp: async () => {
        throw new Error('Launching scriptlets requires a device');
      },
      list: async () =>
        availablePackages.map((pkg) => ({
          id: pkg.id,
          title: pkg.title || pkg.id,
          enabled: !disabledPackages.has(pkg.id),
          available: true,
          unavailableReason: '',
          needs: pkg.needs || []
        })),
      apply: async (disabled) => {
        if (
          !Array.isArray(disabled) ||
          disabled.some((id) => !availablePackages.some((pkg) => pkg.id === id))
        )
          throw Error('Unknown disabled extension');
        for (const pkg of availablePackages) {
          if (
            !disabled.includes(pkg.id) &&
            (pkg.needs || []).some((id) => disabled.includes(id))
          )
            throw Error('Required extension is disabled: ' + pkg.id);
        }
        sessionStorage.setItem(disabledKey, JSON.stringify(disabled));
        record('extensions:applied', { disabled, simulated: true }, manifest.id);
        location.reload();
      }
    };
    extensions.set(manifest.id, api);
    try {
      if (entry.activate) {
        const dispose = entry.activate(api);
        if (typeof dispose === 'function') api.onDispose(dispose);
      }
      record('extension:active', { version: manifest.version }, manifest.id);
    } catch (error) {
      api.stop();
      extensions.delete(manifest.id);
      reportError(manifest.id, error);
    }
  }
  RN.setDeviceHandler((name, value) => {
    if (name === 'navigate') navigate(value);
    else {
      record(name, value);
      emit(name, value);
    }
  });
  addEventListener('pagehide', () => extensions.forEach((api) => api.stop()), {
    once: true
  });
}
