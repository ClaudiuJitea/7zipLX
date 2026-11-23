# Build notes

## Requirements

- CMake 3.24+
- Qt 6.4+ Widgets (tested with 6.10.2)
- C++20 compiler (tested with GCC 15.2)
- Ninja or Make
- pthread, dl, `patch`, `sed`, and `make` when the optional `7z.so` plugin is enabled
- `asmc`, `jwasm` or `uasm` for `SEVENZIP_BUILD_ASM=ON` (Debian/Ubuntu: `asmc-linux`
  where available; otherwise build https://github.com/nidud/asmc_linux)
- `curl` or `wget`, and `tar`, to download the 7-Zip engine source

The build is validated locally on Ubuntu 26.04 x86_64 with Qt 6.10.2, GCC
15.2 and CMake 4.4.2. CI uses Ubuntu 24.04 with its Qt 6.4 packages, which is
also the minimum supported Qt version.

## Fetch the 7-Zip source

The repository does not vendor the 7-Zip engine. Fetch the pinned source once:

```bash
./scripts/fetch-7zip.sh
```

This downloads the official `ip7z/7zip` GitHub mirror tag `26.06` into
`7z2606-src/`. The directory is covered by `.gitignore`. Re-run with `--force` to
replace an existing copy, or set `SEVENZIP_SOURCE_VERSION` / `SEVENZIP_SOURCE_URL`.

An existing source tree can be used instead:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSEVENZIP_SOURCE_DIR=/path/to/7z2606-src
```

## Configure and build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

By default `SEVENZIP_EXTERNAL_CODECS=ON` builds `7z.so` from
`CPP/7zip/Bundles/Format7zF/makefile.gcc` and installs it next to the
executable. Disable it with `-DSEVENZIP_EXTERNAL_CODECS=OFF` to build only the
static built-in codecs.

Assembly optimizations are optional:

```bash
cmake -S . -B build-asm -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSEVENZIP_BUILD_ASM=ON \
  -DSEVENZIP_ASM_COMPILER="$(command -v asmc)"
cmake --build build-asm -j"$(nproc)"
```

This assembles 7zCrcOpt, XzCrc64Opt, SHA-1/SHA-256, Sort, AES, LzFindOpt and
LzmaDecOpt. The CMake option replaces the corresponding portable C translation
units (and defines `Z7_LZMA_DEC_OPT` for the LZMA decoder). It is x86-64 only.

At configure time, `cmake/PrepareUpstream.cmake`:

1. copies the fetched `7z2606-src/` to `build/upstream/`;
2. applies `patches/7zip/0001-agent-posix.patch`;
3. leaves the fetched upstream tree untouched.

The first configure/build takes longer because the whole 7-Zip engine is
compiled. Incremental builds are fast.

## Run

```bash
./build/7ziplx
./build/7ziplx path/to/archive.7z
```

For a headless environment:

```bash
QT_QPA_PLATFORM=offscreen ./build/7ziplx path/to/archive.7z
```

CLI modes:

```bash
./build/7ziplx --extract-here path/to/archive.7z
./build/7ziplx --extract-to /tmp/out path/to/archive.zip
./build/7ziplx --compress file-or-folder
./build/7ziplx --compress-to /tmp/out.7z file1 file2
```

## Tests and sanitizers

CTest is enabled by default (`BUILD_TESTING=ON`). The suite creates archives,
edits nested and single-file archives, exercises password/overwrite/cancel
paths with a fake `PromptDispatcher`, verifies streaming hashes and checks that
`../`, absolute and symlink tar entries cannot escape the extraction directory.

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The `debug-asan` preset builds with AddressSanitizer and UndefinedBehaviorSanitizer
(alignment checks are disabled because upstream 7-Zip intentionally performs
unaligned loads/stores):

```bash
cmake --preset debug-asan
cmake --build --preset debug-asan -j"$(nproc)"
ctest --test-dir build-asan --output-on-failure
```

CI (`.github/workflows/ci.yml`) runs the sanitizer suite, builds the x86-64
assembly configuration with `asmc`, and on `v*` tags builds/uploads the `.deb`
and AppImage.

## Install

```bash
cmake --install build --prefix /tmp/7ziplx-install
```

Installs:

- `bin/7ziplx`
- `lib/<multiarch>/lib7zcore.so`
- desktop entry, AppStream metadata and desktop actions
- hicolor icons at 16–256 px
- Nemo actions, KDE/Dolphin service menu, Nautilus scripts, the
  file-manager action installer and the translation updater
- man page, README, user guide and license notices

The executable uses an install RPATH relative to its library directory.
When external codecs are enabled, `7z.so` is installed in the same directory as
the executable, which is where 7-Zip's loader searches for it.

## Packaging

Debian package:

```bash
./scripts/build-deb.sh
# dist/7ziplx_0.5.0-1_amd64.deb
```

AppImage:

```bash
./scripts/build-appimage.sh
# dist/7zipLX-0.5.0-x86_64.AppImage
```

Both scripts fetch the 7-Zip source if needed and reuse `build-release`. Set
`VERSION`, `REVISION`, `ARCH`, `OUT` or `BUILD_DIR` as needed. The AppImage script
downloads linuxdeploy, its Qt plugin and appimagetool into `build-appimage/tools/`.

The `.deb` target follows the build host's distribution (Ubuntu 24.04 amd64 in
CI; Ubuntu 26.04 in the current local validation environment). The AppImage bundles Qt and requires glibc
2.34 or newer from the host. Both scripts pass `SEVENZIP_BUILD_ASM=ON` and
`SEVENZIP_ASM_COMPILER` through to CMake when set; the release workflow builds
with assembly enabled.

Flatpak:

```bash
flatpak-builder --user --install --force-clean build-flatpak \
  packaging/flatpak/io.github.claudiujitea.7zipLX.yml
```

See `packaging/flatpak/README.md`.

The `.deb` uses a multiarch library directory and installs
`/usr/share/doc/7ziplx`, a man page and Debian maintainer scripts for desktop and
icon cache updates. The AppImage bundles Qt 6, `lib7zcore.so` and `7z.so` and uses
appimagetool with AppStream validation disabled until the project homepage exists.

## Build targets

- `7zcore` — shared library of reused 7-Zip sources
- `7ziplx_backend` — static adapter/callback library
- `7ziplx_ui` — static Qt Widgets library
- `7ziplx` — application
- `7ziplx_tests` — QtTest suite (when `BUILD_TESTING` and Qt6 Test are present)
- `7z.so` — optional external codec/format module (built by the `7zplugin` target)
- `7zasm` — custom target that assembles the optimized routines

### Why `7zcore` is shared

7-Zip registers archive handlers with static constructors. A normal static
library can drop unreferenced `*Register.cpp` objects, leaving the application
with no formats. Building `7zcore` as a shared library guarantees all
constructors run and also makes LGPL relinking practical.

If a static build is needed, use an OBJECT library and link every object
directly, or use `-Wl,--whole-archive`.

## Compiler flags

`7zcore`:

```cmake
-D_FILE_OFFSET_BITS=64
-D_LARGEFILE_SOURCE
-D_REENTRANT
-fPIC
```

C++17-compatible upstream code; the Qt application uses C++20.

Do **not** define:

- `Z7_EXTRACT_ONLY` globally (it disables archive creation)
- `Z7_SFX`
- `UNDER_CE`
- `_WIN32`
- `Z7_EXTERNAL_CODECS` manually; the `SEVENZIP_EXTERNAL_CODECS` CMake option
  controls this definition

## Upstream patch

`patches/7zip/0001-agent-posix.patch` fixes:

- `INVALID_FILE_ATTRIBUTES` / `HMODULE` / `HINSTANCE` on POSIX
- Windows-only alt-stream helper
- `CFileInfo::Attrib` / `IsDevice` access
- icon/resource loading in `ArchiveFolderOpen.cpp`
- `CDirItem` field and `FILETIME` conversion in `AgentOut.cpp`
- `SetFileAttrib` / `CDirEntry::IsDir` in `ArchiveFolderOut.cpp`
- `UINT64` → `UInt64` in `UpdateCallbackAgent.cpp`

`src/backend/WorkDirSettingsPosix.cpp` supplies the small `NWorkDir::CInfo`
settings API used by archive editing without compiling the Windows-registry
`ZipRegistry.cpp`.

## Optional upstream 7zz build

For an independent CLI oracle after fetching the source:

```bash
cd 7z2606-src/CPP/7zip/Bundles/Alone2
make -f makefile.gcc -j"$(nproc)"
./_o/7zz i
```

Remove `_o/` afterwards if you want to keep the fetched tree clean.
