# Packaging

Implemented in the current MVP:

- `7ziplx.desktop` — desktop entry with archive MIME types.
- `io.github.claudiujitea.7zipLX.appdata.xml` — AppStream metadata.
- hicolor icons at 16, 24, 32, 48, 64, 128 and 256 px, generated from `icon/7ziplx-icon.png`.
- CMake install rules for the binary, `lib7zcore.so`, man page, desktop entry, desktop actions, AppStream metadata, icons, `NOTICE.md` and 7-Zip/unRAR licenses.
- File manager context menus (a "7-Zip" submenu whose entries depend on the
  selection: extract/test/open for archives, compress for anything):
  - GNOME Files: `nautilus/7ziplx.py`, a nautilus-python extension (needs
    `python3-nautilus`; restart Files with `nautilus -q` after installing).
  - GNOME desktop (Desktop Icons NG) and Files: a per-user **Scripts → 7zipLX**
    menu in `~/.local/share/nautilus/scripts/7zipLX`, written by 7zipLX on
    start (Options → Integration, or `7ziplx --install-menus` / `--remove-menus`).
  - KDE/Dolphin: `servicemenus/7ziplx-archive.desktop` and `7ziplx-compress.desktop`.
  - Nemo: `nemo-actions/*.nemo_action`.
  - Thunar: `scripts/install-file-manager-actions.sh` (per user, see `thunar/`).
- Flatpak manifest in `flatpak/`.

Install staging:

```bash
cmake --install build-release --prefix /tmp/7ziplx-final
```

Validated:

```bash
desktop-file-validate packaging/7ziplx.desktop
appstreamcli validate packaging/io.github.claudiujitea.7zipLX.appdata.xml
```

The KDE service menus use KDE-specific `ServiceTypes`, `X-KDE-*` and
`all/allfiles` keys, so `desktop-file-validate` is not the right validator for
those files; install it through the packaging rules or the per-user script.

AppStream reports only informational notices for the number-prefixed component name; the homepage is the live GitHub repository.

## Debian package

```bash
./scripts/build-deb.sh
```

The package is written to `dist/7ziplx_0.5.3-1_amd64.deb` by default. It
contains the executable, `lib7zcore.so`, the optional `7z.so` engine module,
desktop entry, AppStream metadata, hicolor icons, man page, documentation and
7-Zip/unRAR notices. The script generates `Depends` with `dpkg-shlibdeps`,
installs maintainer scripts for desktop/icon cache updates, and uses a
multiarch library directory.

Override `VERSION`, `REVISION`, `ARCH`, `MAINTAINER`, `HOMEPAGE` or `OUT` as
needed.

## AppImage

```bash
./scripts/build-appimage.sh
```

The AppImage is written to `dist/7zipLX-0.5.3-x86_64.AppImage` by default. It
bundles the release binary, `lib7zcore.so`, `7z.so` and the required Qt 6
libraries/plugins. Build tools are downloaded into `build-appimage/tools/` on
first use.

Both scripts fetch 7-Zip 26.06 into the ignored `7z2606-src/` directory if it is
not already present.

The produced `amd64` artifacts are built against the build host (Ubuntu 24.04 /
Qt 6.4 in CI, Ubuntu 26.04 / Qt 6.10 locally). The `.deb` dependency list is
generated on the build host; rebuild it on the target distribution for a
compatible package. The AppImage is self-contained
apart from the system C/C++ runtime (glibc 2.34 or newer).

## AppStream note

The metadata uses the repository homepage
`https://github.com/ClaudiuJitea/7zipLX`. Update the URL in
`io.github.claudiujitea.7zipLX.appdata.xml` if the repository moves.
The AppImage build uses appimagetool's `--no-appstream` switch for consistent
packaging even when the AppStream validator is unavailable.

## File-manager actions

The install rules place shared actions under `/usr/share/kio/servicemenus`,
`/usr/share/nemo/actions` and `/usr/share/nautilus/scripts`. For per-user
installation (including Thunar's `uca.xml`):

```bash
./scripts/install-file-manager-actions.sh
```

## Flatpak

```bash
flatpak-builder --user --install --force-clean build-flatpak \
  packaging/flatpak/io.github.claudiujitea.7zipLX.yml
```

See `packaging/flatpak/README.md` for bundle instructions.

Future packaging:

- RPM via CPack or distro packaging.
- Optional separate `7zz` CLI package.

Do not replace a distro `7z`/`7zz` binary implicitly.
