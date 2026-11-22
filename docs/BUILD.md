# Build notes

## Requirements

- CMake 3.24+
- Qt 6.5+ Widgets (tested with 6.10.2)
- C++20 compiler (tested with GCC 15.2)
- Ninja or Make
- pthread, dl, `patch`, `sed`, and `make` when the optional `7z.so` plugin is enabled
- `curl` or `wget`, and `tar`, to download the 7-Zip engine source

The build is validated on Ubuntu 26.04 x86_64 with:

- Qt 6.10.2
- GCC 15.2
- CMake 4.4.2

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

## Install

```bash
cmake --install build --prefix /tmp/7ziplx-install
```

Installs:

- `bin/7ziplx`
- `lib/<multiarch>/lib7zcore.so`
- desktop entry and AppStream metadata
- hicolor icons at 16–256 px
- man page, README, user guide and license notices

The executable uses an install RPATH relative to its library directory.
When external codecs are enabled, `7z.so` is installed in the same directory as
the executable, which is where 7-Zip's loader searches for it.

## Packaging

Debian package:

```bash
./scripts/build-deb.sh
# dist/7ziplx_0.1.0-1_amd64.deb
```

AppImage:

```bash
./scripts/build-appimage.sh
# dist/7zipLX-0.1.0-x86_64.AppImage
```

Both scripts fetch the 7-Zip source if needed and reuse `build-release`. Set
`VERSION`, `REVISION`, `ARCH`, `OUT` or `BUILD_DIR` as needed. The AppImage script
downloads linuxdeploy, its Qt plugin and appimagetool into `build-appimage/tools/`.

The `.deb` target follows the build host's distribution (Ubuntu 26.04 amd64 in
the current validation environment). The AppImage bundles Qt and requires glibc
2.34 or newer from the host.

The `.deb` uses a multiarch library directory and installs
`/usr/share/doc/7ziplx`, a man page and Debian maintainer scripts for desktop and
icon cache updates. The AppImage bundles Qt 6, `lib7zcore.so` and `7z.so` and uses
appimagetool with AppStream validation disabled until the project homepage exists.

## Build targets

- `7zcore` — shared library of reused 7-Zip sources
- `7ziplx_backend` — static adapter/callback library
- `7ziplx_ui` — static Qt Widgets library
- `7ziplx` — application
- `7z.so` — optional external codec/format module (built by the `7zplugin` target)

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
