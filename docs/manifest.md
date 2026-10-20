# Extension manifest

Every package has a `manifest.json`. Paths are relative to the package directory.
For a complete UI example, see [Write a UI extension](extensions.md).

```json
{
  "schema_version": 2,
  "id": "reading-tools",
  "version": "1.0.0",
  "title": "Reading tools",
  "needs": ["ui-host"],
  "ui": ["main.jsx"],
  "settings": { "fontSize": 20 },
  "settingsSchema": {
    "fontSize": { "type": "number", "min": 12, "max": 48 }
  }
}
```

## Identity and dependencies

| Field                  | Meaning                                                                                                            |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `schema_version`       | `2`                                                                                                                |
| `id`                   | Unique package ID: lowercase letters, digits, `.` or `-`; starts with a letter or digit; fewer than 128 characters |
| `version`              | opkg version; used unchanged by individual and release builds                                                      |
| `title`, `description` | Display metadata                                                                                                   |
| `needs`                | Required extension IDs; providers initialize first and must be enabled                                             |

`requiresExecutables` maps absolute executable paths to messages explaining why
an extension is unavailable. Missing or non-executable programs keep the extension
and its dependents inactive, with disabled switches in **Settings → Extensions**.
For example:

```json
"requiresExecutables": {
  "/var/run/eink-sdk/usr/bin/supernet": "Install Supernet to enable SSH."
}
```

The IPK name is `kppmods-ID`. Payloads are installed under
`usr/lib/kppmods/extensions/ID` in the SDK store. `needs` becomes opkg dependencies;
version resolution belongs to opkg.

## UI and configuration

| Field            | Meaning                                                                                                     |
| ---------------- | ----------------------------------------------------------------------------------------------------------- |
| `ui`             | At most one entry file exporting `activate(extension)`; staging compiles JSX and local imports into `.js`   |
| `ui_runtime`     | `{ "host": "host.js", "toolkit": "toolkit.js" }`; supplied by `ui-host`, loaded before extension entries    |
| `options`        | Configuration defaults; runtime option overrides merge objects recursively and replace arrays               |
| `settings`       | Mutable setting defaults; persisted values override individual keys                                         |
| `settingsSchema` | Per-setting validation: `type` (`boolean`, `string`, `number`, `integer`), optional numeric `min` and `max` |

Exactly one UI runtime provider must be selected when any enabled package has UI
entries. Ordinary UI extensions declare `needs: ["ui-host"]`.

## Actions and launcher entries

```json
{
  "actions": {
    "show-version": { "argv": ["/bin/cat", "/etc/prettyversion.txt"], "timeout": 10 }
  },
  "apps": [
    {
      "id": "version",
      "title": "Version",
      "icon": "assets/version.svg",
      "action": "show-version"
    }
  ]
}
```

Each action declares `argv`. A relative executable path resolves inside the package.
`user` defaults to `framework`; `root` is also
supported. Timeout defaults to 60 seconds and accepts 1–3600. Arguments are JSON-encoded in `KPPMOD_ACTION_ARGS`; an `argv` element equal to
`{args}` is replaced with that JSON string. Use [the command API](extension-api.md#run-a-command)
for dynamic commands.

`apps` entries have `id`, `title`, optional package-relative `icon`, `action` and
optional `args`. They appear in the launcher without requiring a UI entry.

## Native code

`native` maps stock Upstart jobs to preloaded libraries. These libraries need not
implement the KPP Mods plugin interface:

```json
{
  "native": { "kppmainapp": ["lib/my-preload.so"] }
}
```

`modules` declares plugins exporting `kppmod_init_v2`. The runtime loads the native
host into the requested process:

```json
{
  "modules": [
    { "id": "my-plugin", "process": "kppmainapp", "path": "lib/my-plugin.so" }
  ]
}
```

The interface is in [`kppmods.h`](../include/kppmods/kppmods.h). See the
[statusbar implementation](../extensions/statusbar/native) for a working plugin.

A preload can select a C++ string ABI from firmware symbols:

```json
{
  "abi": {
    "provider": "/path/to/firmware/library.so",
    "old_symbol": "OLD_MANGLED_SYMBOL",
    "new_symbol": "NEW_MANGLED_SYMBOL",
    "optional": true
  },
  "variants": { "0": "abi0/libplugin.so", "1": "abi1/libplugin.so" }
}
```

Use this object instead of a path in a `native` array. `provider: "kppmainapp"`
resolves the installed KPP executable. With `optional: true`, a missing provider
or absence of both symbols skips the preload; both symbols present is still an
error. Without `optional`, missing symbols are errors. ARM floating-point ABI is
chosen at build time, independently of this selection.

## Status-bar items

`statusbar` is an array of `{id, icon, uri}`. Icons are package-relative assets;
`uri` must begin with `app://`. Declare `org.kppmods.statusbar` in `needs`.
These items belong to the native GTK status bar. For dynamic UI contributions,
use [the extension API](extension-api.md#register-ui).

Package hooks and Upstart definitions belong in `packaging/`, not the manifest.
See [Package an extension](packaging.md).
