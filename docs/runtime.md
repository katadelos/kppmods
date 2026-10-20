# Install and manage extensions

You need root SSH access and a working eink-sdk/opkg installation on the Kindle
or emulator. Use the SDK's installer to set that up first. Build IPKs for the
[device's target profile](building.md#prepare).

## Install packages

From the development machine, install the core, UI host and the packages you want.
For example, to install the launcher from a hard-float build:

```sh
release=$(node -p "require('./release.json').version")
host_version=$(node -p "require('./extensions/ui-host/manifest.json').version")
launcher_version=$(node -p "require('./extensions/launcher/manifest.json').version")
python3 tools/device/deploy.py \
  "dist/$release/kindlehf/kppmods_${release}_kindlehf.ipk" \
  "dist/$release/kindlehf/kppmods-ui-host_${host_version}_kindlehf.ipk" \
  "dist/$release/kindlehf/kppmods-launcher_${launcher_version}_kindlehf.ipk" \
  --host kindle
```

Here `kindle` is a root SSH config entry. For an emulator, pass its assigned port
and key, for example `--host root@127.0.0.1 --port 2494 --identity /path/to/device-key`.
Add `--known-hosts FILE` if you use a separate host-key file.

The deploy helper uploads the IPKs, installs them with opkg, then reloads once for
the whole batch. Include every required package from the manifest's `needs` list.
Installed extensions are enabled by default. Installing all IPKs from a build also
installs optional features such as fake registration and hidden Settings entries.

After installation, on the device:

```sh
. /var/local/kindle-opkg/env
kppmod list
kppmod status
```

Check that your packages appear and that the native diagnostics contain no errors.
Then open the contributed screen or control; a running process alone does not
confirm correct rendering.

## Enable and disable

With `mod-manager` installed, open **Settings → Extensions**, change the switches
and press **Apply**. On firmware using Java Settings, **Extensions** is in the
three-dot menu beside the Store icon. **Discard changes** cancels the draft.

From a root shell with the SDK environment loaded:

| Command                                     | Effect                                                 |
| ------------------------------------------- | ------------------------------------------------------ |
| `kppmod list`                               | Show installed extensions and enabled state            |
| `kppmod enable ID`                          | Enable the extension and its requirements, then reload |
| `kppmod disable ID`                         | Disable the extension and its dependents, then reload  |
| `kppmod reload ID`                          | Reload after changing that package's files             |
| `kppmod reload`                             | Reload all configured processes                        |
| `kppmod apply '["hide-help","hide-legal"]'` | Replace the **complete disabled set**, then reload     |
| `opkg remove kppmods-ID`                    | Uninstall the package                                  |

Re-enabling a dependency does not re-enable its dependents. Disabling affects
runtime contributions; package-owned services and file associations follow opkg
installation and removal. KPP reloads restart the app manager and clear navigation
history. There is no package rollback command.

## Start after boot

The core package installs `/mnt/us/documents/kppmods.sh`, titled **KPP Mods**.
Open it with the device's scriptlet launcher after boot to activate the SDK and
start KPP Mods without SSH. Startup runs in the background because activation
restarts KPP; its output goes to `/var/run/kppmods/start.log`.

For automatic startup, the device's writable boot integration must run these after
userstore and stock jobs are available:

```sh
sh /var/local/kindle-opkg/activate
/var/run/eink-sdk/usr/bin/kppmods-start
```

`kppmods-start` installs a volatile Upstart overlay, starts the broker and activates
the selected extensions. The package's stock job changes live in that overlay;
they do not replace the firmware files on disk. Reload preserves an active
development session, including after reboot.

## Diagnose a failure

Run on the device:

```sh
. /var/local/kindle-opkg/env
initctl status kppmods
kppmod --request health < /dev/null
kppmod status
tail -n 100 /var/log/messages
```

| Symptom                            | Check next                                                                                                                 |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| CLI cannot reach the service       | Broker Upstart state; run `kppmods-start` if the SDK is active but the service is stopped                                  |
| Extension is missing               | `kppmod list`, its installed manifest, and enabled dependencies                                                            |
| Native module or script failed     | `native` diagnostics in `kppmod status`; `/var/run/kppmods/native/PROCESS-PID.status`                                      |
| UI activates but a page fails      | `ui:error` events and KPP logs; compare with the [workbench's limits](../preview/README.md#what-the-browser-does-not-test) |
| Firmware module cannot be resolved | [Inspect the installed Hermes bundle](internals.md#firmware-discovery)                                                     |
| EPUB catalogue service failed      | `initctl status kppmods-epub` and its logs                                                                                 |

Broker request and command failures use the `kppmod` syslog tag. Command
failure logs include exit status, timeout/cancellation flags and up to 4 KiB of
stderr. After an activation failure, the published configuration remains available
for inspection; fix the cause and reload.

## Configuration files

Persistent state lives in `/mnt/us/.eink-sdk/var/lib/kppmods`:

| File                | Purpose                                                            |
| ------------------- | ------------------------------------------------------------------ |
| `disabled.json`     | Disabled extension IDs                                             |
| `options.json`      | Per-extension option overrides; edit then reload                   |
| `data/ID.json`      | Persisted settings, merged over manifest defaults                  |
| `base.json`         | Additional `processes.JOB.preloads` and `processes.JOB.env`        |
| `config.json`       | Generated runtime configuration; inspect it rather than editing it |
| `pending-jobs.json` | Jobs retained until activation succeeds                            |
| `development/`      | Uploaded host and extension bundles for live development           |

Option objects merge recursively; arrays replace defaults. The CLI talks to the
running service. Set `KPPMOD_ROOT` when starting the Upstart job to use a different
persistent data directory; this does not change the SDK payload location.
The status field `operation_pending` indicates an active management operation.

Transient files live in `/var/run/kppmods`: `runtime.lock`, native diagnostics
in `native/`, the scriptlet `app.log`, and live-update messages in `development/`.
These files disappear on reboot. The Upstart overlay lives separately in
`/var/run/kppmods-upstart`.

## Scriptlet apps

The launcher discovers `.sh` files in `/mnt/us/documents` and `/mnt/us/applications`.
Put `# Name: My App` in the first 32 lines; otherwise the filename becomes the title.
A PNG or SVG with the same basename supplies its icon. Press **Refresh** in the
launcher after adding or removing a script.
The KPP Mods startup scriptlet is excluded from Apps and remains available in the
Kindle library.

`kppmod apps` lists discovered apps; `kppmod launch-app SCRIPT` starts one. A single
Upstart job tracks the foreground process. It runs as root in the script's directory
and writes output to `/var/run/kppmods/app.log`. The script is responsible for any
display or service changes its application needs.

## Bundled feature constraints

`hide-help` and `hide-legal` hide KPP Settings entries; they do not affect Java
Settings or prevent direct navigation to stock routes. `fake-registration`
overrides local registration queries in KPP, including supported native Ignite
gates. It does not supply account credentials or Amazon services. Unsupported
native layouts are reported in `kppmod status`.
