# KPP Mods

Extend Kindle's stock interface with React components and native plugins. Add
launcher tabs, Settings pages and Quick Settings controls, or hook a stock process
from C++.

<p>
  <a href="docs/img/app-launcher.png"><img src="docs/img/app-launcher.png" alt="App launcher" width="180"></a>
  <a href="docs/img/extension-manager.png"><img src="docs/img/extension-manager.png" alt="Extension manager" width="180"></a>
  <a href="docs/img/quick-settings.png"><img src="docs/img/quick-settings.png" alt="Quick Settings with the Apps shortcut" width="180"></a>
  <a href="docs/img/settings-integration.png"><img src="docs/img/settings-integration.png" alt="Extensions entry in Kindle Settings" width="180"></a>
</p>

## Start developing

You can work on UI extensions in a browser without a Kindle, firmware image or SDK.
Use Node 22.18+ on the 22.x line, or Node 24.11+.

```sh
git clone https://github.com/katadelos/kppmods.git
cd kppmods
git submodule update --init vendor/react-kindle
npm ci
npm run dev
```

Open **http://127.0.0.1:4173** to try the bundled extensions and component gallery.
Then [write your first extension](docs/extensions.md): add a tab, save a setting
and compile it for Kindle.

## Work on a device

Binary builds need an ARM toolchain. Creating installable IPKs also requires the
private eink-sdk. UI extensions need firmware with KPPMainApp and its Hermes bundle.

1. [Build binaries or packages](docs/building.md) for your firmware.
2. [Install and manage them](docs/runtime.md) on a Kindle or emulator with eink-sdk.
3. [Edit over SSH](docs/development.md) without restarting KPP for every UI change.

## Reference

- [Extension API](docs/extension-api.md) — React hooks, UI contributions and device commands.
- [Manifest](docs/manifest.md) — dependencies, settings, actions and native libraries.
- [Browser workbench](preview/README.md) — package selection, debugging and simulation limits.
- [Runtime internals](docs/internals.md) — source map, activation, native host and firmware discovery.

For a working example, start with [custom-icons](extensions/custom-icons) or
[quick-settings](extensions/quick-settings). The [statusbar extension](extensions/statusbar)
shows the native plugin interface.
