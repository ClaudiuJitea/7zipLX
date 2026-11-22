#!/usr/bin/env bash
# Build a portable x86_64 AppImage from the release build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-0.4.0}"
ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
APPDIR="${APPDIR:-$ROOT/build-appimage/AppDir}"
TOOLS="${TOOLS:-$ROOT/build-appimage/tools}"
OUT="${OUT:-$ROOT/dist}"
MULTIARCH="${MULTIARCH:-$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo x86_64-linux-gnu)}"

if [[ ! -f "$ROOT/7z2606-src/CPP/7zip/UI/Common/LoadCodecs.h" ]]; then
  "$ROOT/scripts/fetch-7zip.sh"
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBDIR="lib/$MULTIARCH" \
  -DCMAKE_INSTALL_DOCDIR=share/doc/7ziplx
cmake --build "$BUILD_DIR" -j"$(nproc)"

rm -rf "$APPDIR"
mkdir -p "$APPDIR" "$TOOLS" "$OUT"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

cp "$APPDIR/usr/share/applications/7ziplx.desktop" "$APPDIR/7ziplx.desktop"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/7ziplx.png" "$APPDIR/7ziplx.png"
cp "$APPDIR/7ziplx.png" "$APPDIR/.DirIcon"

fetch_tool() {
  local name="$1"
  local url="$2"
  if [[ ! -x "$TOOLS/$name" ]]; then
    echo "Downloading $name..."
    curl --fail --location --retry 3 --output "$TOOLS/$name" "$url"
    chmod +x "$TOOLS/$name"
  fi
}
fetch_tool linuxdeploy-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch_tool linuxdeploy-plugin-qt-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch_tool appimagetool-x86_64.AppImage \
  https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH
export PATH="$TOOLS:$PATH"
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"

"$TOOLS/linuxdeploy-x86_64.AppImage" \
  --appdir "$APPDIR" \
  --plugin qt \
  --desktop-file "$APPDIR/7ziplx.desktop" \
  --icon-file "$APPDIR/7ziplx.png"

# linuxdeploy copies our shared engine into usr/lib and updates the executable
# RPATH. The multiarch copy from CMake is now redundant.
rm -rf "$APPDIR/usr/lib/$MULTIARCH"

# Keep a headless platform plugin available for CI and headless verification.
QT_PLUGINS="${QT_PLUGINS:-$(qtpaths6 --plugin-dir 2>/dev/null || qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || true)}"
PATCHELF="${PATCHELF:-$(command -v patchelf 2>/dev/null || true)}"
if [[ -n "$QT_PLUGINS" && -f "$QT_PLUGINS/platforms/libqoffscreen.so" && -n "$PATCHELF" ]]; then
  cp "$QT_PLUGINS/platforms/libqoffscreen.so" "$APPDIR/usr/plugins/platforms/"
  "$PATCHELF" --set-rpath '$ORIGIN/../../lib:$ORIGIN' \
    "$APPDIR/usr/plugins/platforms/libqoffscreen.so"
fi

APPIMAGE="$OUT/7zipLX-${VERSION}-${ARCH}.AppImage"
rm -f "$APPIMAGE"
"$TOOLS/appimagetool-x86_64.AppImage" --no-appstream "$APPDIR" "$APPIMAGE"
chmod +x "$APPIMAGE"
printf '\nCreated: %s\n' "$APPIMAGE"
file "$APPIMAGE"
