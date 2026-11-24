#!/usr/bin/env bash
# Build a complete Debian binary package from the current source tree.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-0.5.3}"
REVISION="${REVISION:-1}"
ARCH="${ARCH:-$(dpkg --print-architecture)}"
MULTIARCH="${MULTIARCH:-$(dpkg-architecture -qDEB_HOST_MULTIARCH)}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
STAGE="${STAGE:-$BUILD_DIR/deb-root}"
OUT="${OUT:-$ROOT/dist}"
MAINTAINER="${MAINTAINER:-7zipLX contributors <noreply@7ziplx.invalid>}"
HOMEPAGE="${HOMEPAGE:-https://github.com/ClaudiuJitea/7zipLX}"

if [[ ! -f "$ROOT/7z2606-src/CPP/7zip/UI/Common/LoadCodecs.h" ]]; then
  "$ROOT/scripts/fetch-7zip.sh"
fi

CMAKE_ARGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX=/usr
  -DCMAKE_INSTALL_LIBDIR="lib/$MULTIARCH"
  -DCMAKE_INSTALL_DOCDIR=share/doc/7ziplx
  -DSEVENZIP_BUILD_ASM="${SEVENZIP_BUILD_ASM:-OFF}"
)
if [[ -n "${SEVENZIP_ASM_COMPILER:-}" ]]; then
  CMAKE_ARGS+=(-DSEVENZIP_ASM_COMPILER="$SEVENZIP_ASM_COMPILER")
fi
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

rm -rf "$STAGE"
mkdir -p "$STAGE" "$OUT"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" --prefix /usr

# Ensure the package has the expected layout.
test -x "$STAGE/usr/bin/7ziplx"
test -f "$STAGE/usr/bin/7z.so"
test -f "$STAGE/usr/lib/$MULTIARCH/lib7zcore.so"
test -f "$STAGE/usr/share/applications/7ziplx.desktop"
test -f "$STAGE/usr/share/nautilus-python/extensions/7ziplx.py"

mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/share/doc/7ziplx"
cp "$ROOT/packaging/debian/copyright" "$STAGE/usr/share/doc/7ziplx/copyright"
{
  echo "7ziplx (${VERSION}-${REVISION}) unstable; urgency=medium"
  echo
  echo "  * Release ${VERSION}."
  echo
  echo " -- ${MAINTAINER}  $(date -R)"
} | gzip -9n > "$STAGE/usr/share/doc/7ziplx/changelog.Debian.gz"

# Generate shlib dependencies with the host's dpkg-shlibdeps when possible.
DEPENDS=""
SHLIB_TMP="$BUILD_DIR/deb-shlibdeps"
rm -rf "$SHLIB_TMP"
mkdir -p "$SHLIB_TMP/debian"
cat > "$SHLIB_TMP/debian/control" <<'SHLIB_CONTROL'
Source: 7ziplx
Section: utils
Priority: optional
Maintainer: 7zipLX contributors <noreply@7ziplx.invalid>
Standards-Version: 4.7.0

Package: 7ziplx
Architecture: any
Depends: ${shlibs:Depends}
Description: 7zipLX package
 7zipLX package.
SHLIB_CONTROL
cat > "$SHLIB_TMP/debian/changelog" <<EOF
7ziplx (${VERSION}-${REVISION}) unstable; urgency=medium

  * Release ${VERSION}.

 -- ${MAINTAINER}  $(date -R)
EOF
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
  set +e
  DEPENDS="$(cd "$SHLIB_TMP" && dpkg-shlibdeps -O \
      -l"$STAGE/usr/lib/$MULTIARCH" "$STAGE/usr/bin/7ziplx" 2>/dev/null \
      | sed -n 's/^shlibs:Depends=//p')"
  set -e
fi
if [[ -z "$DEPENDS" ]]; then
  DEPENDS="libc6, libgcc-s1, libstdc++6, "
  DEPENDS+="libqt6core6t64 (>= 6.4.0) | libqt6core6 (>= 6.4.0), "
  DEPENDS+="libqt6gui6t64 (>= 6.4.0) | libqt6gui6 (>= 6.4.0), "
  DEPENDS+="libqt6widgets6t64 (>= 6.3.0) | libqt6widgets6 (>= 6.3.0)"
fi

# Ubuntu 24.04 uses the t64 Qt package names while 26.04 uses the plain names.
# Emit both alternatives so one .deb works on either release.
if [[ -n "$DEPENDS" ]] && command -v python3 >/dev/null 2>&1; then
  DEPENDS="$(python3 - "$DEPENDS" <<'PYEOF'
import re
import sys

result = []
for dep in sys.argv[1].split(','):
    dep = dep.strip()
    match = re.fullmatch(r'(libqt6(?:core|gui|widgets)6)(?:t64)?( \(>= [^)]+\))?', dep)
    if match:
        base, version = match.group(1), match.group(2) or ''
        result.append(f"{base}t64{version} | {base}{version}")
    elif dep:
        result.append(dep)
print(', '.join(result))
PYEOF
)"
fi

INSTALLED_SIZE="$(du -sk "$STAGE/usr" | awk '{print $1}')"
{
  echo "Package: 7ziplx"
  echo "Version: ${VERSION}-${REVISION}"
  echo "Architecture: ${ARCH}"
  echo "Maintainer: ${MAINTAINER}"
  echo "Installed-Size: ${INSTALLED_SIZE}"
  echo "Depends: ${DEPENDS}, qt6-svg-plugins"
  echo "Recommends: xdg-utils, diffutils, python3-nautilus"

  echo "Section: utils"
  echo "Priority: optional"
  if [[ -n "$HOMEPAGE" ]]; then
    echo "Homepage: ${HOMEPAGE}"
  fi
  cat <<'CONTROL_DESCRIPTION'
Description: native Linux GUI for the 7-Zip archive engine
 7zipLX is a Qt 6 archive manager built around the portable 7-Zip engine.
 It opens with a single pane and offers a second pane on F9. It supports
 archive browsing, extraction, compression, testing, encrypted archives,
 hashes, benchmarks and in-archive editing.
 .
 Windows-only integration (Explorer shell extension, NTFS streams and drive
 letters) is intentionally not included.
CONTROL_DESCRIPTION
} > "$STAGE/DEBIAN/control"

cp "$ROOT/packaging/debian/postinst" "$STAGE/DEBIAN/postinst"
cp "$ROOT/packaging/debian/postrm" "$STAGE/DEBIAN/postrm"
chmod 0755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"
chmod 0644 "$STAGE/DEBIAN/control"

(
  cd "$STAGE"
  find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
chmod 0644 "$STAGE/DEBIAN/md5sums"

DEB="$OUT/7ziplx_${VERSION}-${REVISION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB" >/dev/null
printf '\nCreated: %s\n' "$DEB"
dpkg-deb --info "$DEB" | sed -n '1,30p'
(
  cd "$OUT"
  find . -maxdepth 1 -type f \( -name '*.deb' -o -name '*.AppImage' \) -printf '%f\n' \
    | sort | xargs -r sha256sum > SHA256SUMS
)
