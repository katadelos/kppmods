# UI extension API

For a runnable example, start with [Write a UI extension](extensions.md).
This reference describes the object passed to `activate(extension)` and returned
by `useExtension()`. Extension code runs inside KPP and is trusted in-process code.

## Entry point and lifecycle

```js
export function activate(extension) {
  extension.log('Activated');
  return () => extension.log('Disposed');
}
```

Entries activate once in dependency order. Registered contributions, subscriptions
and published services are removed on disposal. Return a cleanup function or use
`extension.onDispose(callback)` for resources you own. Register contributions
synchronously during activation.

| Property                 | Value                                                 |
| ------------------------ | ----------------------------------------------------- |
| `id`, `root`             | Package ID and installed directory                    |
| `options`                | Merged package configuration                          |
| `firmware`               | Running firmware version                              |
| `React`, `RN`, `toolkit` | Shared React, React Native and React Kindle instances |

## Imports and React hooks

| Import              | Provides                                            |
| ------------------- | --------------------------------------------------- |
| `react`             | Stock React and hooks                               |
| `react-native`      | Stock React Native components and modules           |
| `react-kindle`      | Shared toolkit components and hooks                 |
| `@kppmods/toolkit`  | Alias for the same toolkit instance                 |
| `@kppmods/react`    | Extension hooks below                               |
| `./Component`       | Local module bundled into the entry                 |
| `./assets/icon.svg` | Package asset URI; SVG, PNG, JPEG and GIF supported |

JSX uses the automatic runtime. Assets remain package files. Do not bundle a second
React runtime or use browser DOM elements.

| Hook                            | Result                                                        |
| ------------------------------- | ------------------------------------------------------------- |
| `useExtension()`                | This extension's API                                          |
| `useSetting(key, initialValue)` | `[value, setValue]`; the setter returns a persistence promise |
| `useAction(name)`               | Stable callback invoking `execute(name, args)`                |
| `useApps()`                     | Enabled packages' manifest app entries                        |
| `usePackages()`                 | Enabled packages' metadata                                    |
| `useRegistry(kind)`             | Contributions, with updates when the registry changes         |
| `useEvent(name, listener)`      | Event subscription while mounted; use a stable callback       |

Wrap toolkit components in `KindleProvider` with `firmware={extension.firmware}`.
For embedded dialogs and sheets, pass `applicationId="com.lab126.KPPMainApp"`.
See [React Kindle's source and documentation](../vendor/react-kindle) for component
props. `useKindle().supports('slider')` checks a feature inside a provider;
`inspectFeature(extension.firmware, 'slider')` returns availability and a reason
outside React.

Embedded extensions register tabs or Home content; they do not call
`registerApplication`. Standalone application lifecycle and `useChromeConfiguration`
need their own native transport, which importing the toolkit does not install.

## Register UI

Every registry has `add(definition)`, `list()` and `remove(localId)`. `add` returns
an ID qualified by the package, such as `reading-tools:tools`. `list` returns that
package's registrations; `extension.list(kind)` reads all contributors. A package
can only remove its own registrations. Lower `order` values sort first (default 0).

| Registry        | Definition                                                                                                                   |
| --------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `tabs`          | `id`, `title`, React `component`; optional `order`, `footer` (set `false` for a route without a footer button)               |
| `home`          | `id`, React `component`, optional `order`; replaces stock Home content                                                       |
| `settingsPages` | `id`, `title`, React `component`, optional `order`                                                                           |
| `menuItems`     | `id`, `title`, `app://` `uri`, optional `order`; stock three-dot menu                                                        |
| `quickSettings` | `id`, `title`, `icon`, `action`; optional `iconOn`, `args`, `close`, `order`, `value()`, `disabled()`, `subscribe(listener)` |
| `sliders`       | `id`, `title`, `min`, `max`; optional `get()`, `set(value)`, `subscribe(listener)`, `action`, `order`                        |
| `icons`         | `id` and package-relative `path`, or explicit `uri`                                                                          |
| `status`        | `id`, `icon`, `app://` `uri`; requires `org.kppmods.statusbar`                                                               |
| `actions`       | `id`, JavaScript `run(args)`                                                                                                 |

For Quick Settings, `value()` supplies the current on/off state. `iconOn` selects
the on-state icon. `disabled()` blocks interaction. Return the action's completion
promise so the control can show pending state and failures; `close: false` keeps
the menu open. A control or slider's `subscribe(listener)` returns an unsubscribe
function and is active while the control is mounted.

Custom tab routes use the qualified ID, uppercased with punctuation replaced by
underscores. `reading-tools:tools` becomes:

```text
app://com.lab126.KPPMainApp/?view=KPP_MOD_READING_TOOLS_TOOLS
```

Use `extension.navigate(uri)` to open it. Menu items can link to routes registered
with `footer: false`, including on firmware with Java Settings.

`tabs.hide('chrome.notebook.button')` hides a stock footer item.
`tabs.hide('chrome.lastBook.button')` also hides its floating book-cover overlay.
`settingsPages.hide('help')` and `settingsPages.hide('legal')` hide stock KPP Settings
entries. They do not affect Java Settings or block direct stock routes.

### Settings page components

A Settings page receives `NavigationRow`, `ToggleRow`, `PageContainer` and
`Separator` as props. These use the firmware's own Settings components:

- `NavigationRow`: `widgetId`, `title`, `description`, `onPress`, `disabled`, `isShowNavigationIcon`.
- `ToggleRow`: `widgetId`, `title`, `description`, `currentState`, `onPress`, `disabled`.
- `PageContainer` and `Separator`: group rows and draw stock dividers.

The stock renderer supplies margins, scrolling and Back navigation. Avoid adding a
second inset. Keep draft selections in React state until the user applies them.

### Assets and icons

`extension.asset('assets/icon.svg')` returns a package file URI. Paths must stay
inside the package. `extension.Icon` takes `name`, `label` and `style` and renders
a registered icon inside a `KindleProvider`.

An icon reference can use another package's qualified ID, such as
`custom-icons:apps`; declare the provider in `needs`. Native status-bar icons must
belong to the contributing package. Status items run in GTK, separately from KPP's
footer and menus.

## Persist settings

```js
const size = extension.settings.get('fontSize', 20);
await extension.settings.set('fontSize', size + 1);
const unsubscribe = extension.settings.subscribe(({ key, value }) => {
  extension.log(`${key}: ${value}`);
});
```

`settings.snapshot()` returns the current settings object. Manifest defaults are
merged with persisted values; `settingsSchema` validates writes on the device.
Writes are serialized per extension. `set` resolves with confirmed settings and
then notifies subscribers. Rejected writes leave the confirmed value unchanged.
For React components, use `useSetting` instead of subscribing manually.

## Invoke an action

```js
extension.actions.add({ id: 'refresh', run: async () => refreshData() });
await extension.execute('refresh');
```

`execute('local-action', args)` or `execute('other-package:action', args)` first
looks for a registered JavaScript action, then a manifest action. JavaScript
results follow `run`. For native actions, resolution confirms dispatch; completion
is reported through `bridge:action:exit`. Native actions use the manifest's
`argv`, user and timeout. Arguments are JSON-encoded in `KPPMOD_ACTION_ARGS` and
replace any `argv` element equal to `{args}`; they are not interpolated into shell text.

`extension.apps()` returns enabled packages' manifest app entries with package
ownership and resolved icon URIs. Scriptlet discovery is a separate operation:
`extension.management.apps()`.

## Run a command

```js
const command = extension.exec(['/usr/bin/id', '-u'], { timeout: 10 });
const result = await command;
if (result.exitCode !== 0) extension.log(result.stderr);
```

An argument array requires an absolute executable path. A string runs through
`/bin/sh -c`. Options are `user` (default `framework`, also accepts `root`), `cwd`
(default package directory) and `timeout` (1–3600 seconds, default 60).

The result contains `id`, `done`, `stdout`, `stderr`, `exitCode`, `timedOut`,
`cancelled` and `truncated`. Nonzero exits resolve normally; dispatch or transport
errors reject. Call `command.cancel()` on the promise to stop the process group.

Each output stream retains up to 64 KiB of UTF-8 text; invalid bytes are replaced.
The broker allows eight running commands and retains 32 results. Descendants are
terminated when the command exits, times out, is cancelled, its extension is
disposed, or the broker stops. Disposal rejects the pending promise and removes
its subscriptions, including when the start response is still in flight.
For persistent daemons, [package an Upstart job](packaging.md#package-owned-build-and-install-steps).

## Events and shared services

| Method                                          | Behavior                                                  |
| ----------------------------------------------- | --------------------------------------------------------- |
| `events.on(name, callback)`                     | Subscribe; returns an unsubscribe function                |
| `events.emit(name, data)`                       | Emit locally and publish through the broker               |
| `services.publish(name, majorVersion, service)` | Publish a JavaScript service; returns a disposer          |
| `services.get('package:name', majorVersion)`    | Get an exact interface-major match; throws if unavailable |
| `onDispose(callback)`                           | Register cleanup                                          |

Broker events arrive as `bridge:<name>` with `{package, name, data, seq}`.
Subscriptions recover after a broker restart. Declare a service provider in
`needs` so it initializes before its consumers.

## Management and inspection

| Method                          | Result                                                                        |
| ------------------------------- | ----------------------------------------------------------------------------- |
| `management.list()`             | Installed `{id, title, enabled, available, unavailableReason, needs}` records |
| `management.apply(disabledIds)` | Replace the complete disabled set and wait for activation                     |
| `management.apps()`             | Discover installed scriptlet apps                                             |
| `management.launchApp(script)`  | Start a discovered scriptlet                                                  |
| `packages()`                    | Active `{id, version, title}` records                                         |
| `state()`                       | Broker settings and events                                                    |
| `capabilities()`                | Names of resolved stock adapter modules                                       |
| `log(text)`                     | Log with the package ID                                                       |

`KPPMods.inspect()` lists active packages and contributions in the JS runtime.
Activation and rendering failures produce `ui:error` events. Direct firmware-module
access through `extension.module` is for adapter work; see [runtime internals](internals.md).
