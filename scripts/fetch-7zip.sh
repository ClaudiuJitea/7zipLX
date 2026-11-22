#!/usr/bin/env bash
# Fetch the pinned 7-Zip source tree used by the 7zipLX build.
#
# The source is intentionally not vendored in this repository. CMake needs it
# at ./7z2606-src; this script downloads the official GitHub mirror archive and
# extracts it there. The directory is listed in .gitignore.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/7z2606-src"
VERSION="${SEVENZIP_SOURCE_VERSION:-26.06}"
URL="${SEVENZIP_SOURCE_URL:-https://codeload.github.com/ip7z/7zip/tar.gz/refs/tags/${VERSION}}"
FORCE=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [--force]

Download 7-Zip ${VERSION} source into:
  $DEST

Options:
  --force   replace an existing 7z2606-src directory
EOF
}

for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -f "$DEST/CPP/7zip/UI/Common/LoadCodecs.h" && "$FORCE" -eq 0 ]]; then
  echo "7-Zip source already present at $DEST"
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading 7-Zip ${VERSION} source..."
if command -v curl >/dev/null 2>&1; then
  curl --fail --location --retry 3 --output "$TMP/7zip.tar.gz" "$URL"
elif command -v wget >/dev/null 2>&1; then
  wget --tries=3 --output-document="$TMP/7zip.tar.gz" "$URL"
else
  echo "Error: curl or wget is required to download 7-Zip source." >&2
  exit 1
fi

tar -xzf "$TMP/7zip.tar.gz" -C "$TMP"
SRC="$(find "$TMP" -mindepth 1 -maxdepth 1 -type d -name '7zip-*' -print -quit)"
if [[ -z "$SRC" || ! -f "$SRC/CPP/7zip/UI/Common/LoadCodecs.h" ]]; then
  echo "Error: downloaded archive does not look like the 7-Zip source tree." >&2
  exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST"
cp -a "$SRC/." "$DEST/"
echo "7-Zip source ready at $DEST"
