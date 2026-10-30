# Build binaries and packages

The build uses CMake 3.25 or newer and an ARM cross-toolchain. The private eink-sdk
is needed only for IPK packaging. For UI-only work, use the
[browser workbench](../preview/README.md).

## Prepare

Initialize the public source dependencies and install the JavaScript tools:

```sh
tools/build/init-dependencies.sh
npm ci
```

Full builds also require Python 3, Node (see `package.json`), `patch` and
`rsvg-convert`. Antidox's Java Settings integration requires a JDK with `javac`
and JNI headers. Set `JAVA_HOME` or pass `-DKPPMODS_JAVA_HOME=/path/to/jdk` when
the JDK cannot be located from `javac`. The bundled container includes a JDK.
A core-only build does not need Node or Python.

Choose a target and supply its Linux toolchain, including sysroot and binutils:

| Preset      | ARM ABI    | Tested GCC | Firmware policy |
| ----------- | ---------- | ---------- | --------------- |
| `kindlepw2` | Soft float | 14.4.0     | 5.16.2.1.1      |
| `kindlehf`  | Hard float | 14.2.0     | 5.18.1.1.1      |

Soft float uses `arm-kindlepw2-linux-gnueabi` with glibc 2.12; hard float uses
`arm-kindlehf-linux-gnueabihf` with glibc 2.19. C++ string ABI is separate: the
build includes both string ABI variants and activation selects between them.
KT6 uses `kindlehf`; there is no separate device build. The hard-float packaging
policy uses the 5.18.1.1.1 library limits for the shared output. A profile does not
guarantee that every extension works on every firmware.

## Configure, build and install

On Linux, from the repository root:

```sh
export KPPMODS_TOOLCHAIN_ROOT=/path/to/arm-kindlehf-linux-gnueabihf
cmake --preset kindlehf
cmake --build --preset kindlehf
cmake --install build/kindlehf
```

The preset installs into `dist/kindlehf/`. Use `--prefix /other/directory` on the install
command to change it. The installation tree contains:

- `usr/bin/`: `kppmod` executable and startup script.
- `usr/lib/kppmods/`: host libraries and compiled extension payloads.
- `usr/share/`: licenses and the Upstart service definition.

No SDK checkout, patched `patchelf` or IPK generation is involved. Device paths
and activation still follow the eink-sdk layout; this is a build and staging
interface, not a standalone device installer.

To build just the C++ runtime and native modules:

```sh
cmake --preset kindlehf -DKPPMODS_BUILD_UI=OFF
cmake --build --preset kindlehf
cmake --install build/kindlehf --component core
```

Native extension libraries remain in `build/kindlehf/native/modules/`. Set
`KPPMODS_BUILD_UI=ON` when returning to a full build. Local preset overrides belong
in the ignored `CMakeUserPresets.json`.

Native targets follow the extension selection in `release.json`. With
`KPPMODS_BUILD_UI=OFF`, native-only extensions can still be built and installed;
UI extensions and runtime providers are excluded.

## Use the Linux container

On macOS, or to use the supplied Linux environment:

```sh
docker build --platform linux/amd64 -t kppmods-build tools/build
export KPPMODS_TOOLCHAIN_ROOT=/path/to/arm-kindlehf-linux-gnueabihf
tools/build/container.sh tools/build/init-dependencies.sh
tools/build/container.sh npm ci
tools/build/container.sh cmake --preset kindlehf
tools/build/container.sh cmake --build --preset kindlehf
tools/build/container.sh cmake --install build/kindlehf
```

The wrapper mounts the source, toolchain and a separate npm dependency volume,
then runs the supplied command. It does not select targets or build anything
itself. The Dockerfile pins Node; Ubuntu packages are not snapshot-pinned.

## Package with eink-sdk

The SDK consumes the CMake installation tree. Build and install first, then run:

```sh
python3 tools/packaging/release.py dist/kindlehf --target kindlehf \
  --toolchain "$KPPMODS_TOOLCHAIN_ROOT" \
  --sdk /path/to/eink-sdk \
  --patchelf /path/to/host/bin/patchelf
```

The core version, SDK revision, packager patches and ELF policy come from
`release.json`. Each extension uses its manifest version unchanged.
It creates IPKs in `dist/VERSION/TARGET/` without compiling or reinstalling.
The SDK working tree is not modified. The supplied patched `patchelf` must match
`release.json` and support `--make-rpath-relative` and `--no-standard-lib-dirs`.

For container packaging, mount the two extra inputs:

```sh
export KPPMODS_SDK_ROOT=/path/to/eink-sdk
export KPPMODS_PATCHELF_DIR=/path/to/host/bin
tools/build/container.sh python3 tools/packaging/release.py dist/kindlehf \
  --target kindlehf --toolchain /inputs/toolchain \
  --sdk /inputs/sdk --patchelf /inputs/patchelf/patchelf
```

SDK build systems can also configure CMake directly with the appropriate
toolchain file and `KPPMODS_ARCHITECTURE`, then stage with a conventional `DESTDIR`:

```sh
DESTDIR=/path/to/staging cmake --install build/kindlehf --prefix /
```

[Install the IPKs](runtime.md#install-packages), or see
[individual packaging and EPUB](packaging.md).

## Rebuild and clean

CMake tracks native sources, UI bundles and extension inputs. Repeat the build
and install commands after edits. Use `cmake --build --preset kindlehf --parallel 8`
to change compilation concurrency.

| Directory               | Contents                                    |
| ----------------------- | ------------------------------------------- |
| `build/TARGET/runtime/` | Runtime executable                          |
| `build/TARGET/native/`  | Host libraries and native extension modules |
| `build/TARGET/ui/`      | Shared toolkit bundle                       |
| `build/TARGET/staging/` | Compiled extension payloads                 |
| `dist/TARGET/`          | CMake installation tree                     |
| `build/TARGET/sdk/`     | Exported and patched SDK packager           |
| `dist/VERSION/TARGET/`  | IPKs                                        |

Remove `build/TARGET/` for a clean build. CMake installation does not remove old
files: clear `dist/TARGET/` before installing when extensions are removed or
renamed. Use separate build and install directories for each architecture.

Release packaging requires all
selected extensions to be installed and ignores unselected directories left in
the installation prefix.
