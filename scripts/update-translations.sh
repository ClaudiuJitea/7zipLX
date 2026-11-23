#!/usr/bin/env bash
# Extract translatable strings and build Qt .qm catalogs.
#
# Usage:
#   ./scripts/update-translations.sh               # update existing *.ts files
#   ./scripts/update-translations.sh de fr         # create/update de.ts and fr.ts
#
# Requirements: Qt 6 lupdate and lrelease (qt6-base-dev-tools or qt6-tools).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/resources/translations"
mkdir -p "$DIR"

LUPDATE="$(command -v lupdate6 || command -v lupdate || true)"
if [[ -z "$LUPDATE" && -x /usr/lib/qt6/bin/lupdate ]]; then LUPDATE=/usr/lib/qt6/bin/lupdate; fi
LRELEASE="$(command -v lrelease6 || command -v lrelease || true)"
if [[ -z "$LRELEASE" && -x /usr/lib/qt6/bin/lrelease ]]; then LRELEASE=/usr/lib/qt6/bin/lrelease; fi
if [[ -z "$LUPDATE" || -z "$LRELEASE" ]]; then
  echo "lupdate/lrelease not found. Install qt6-base-dev-tools (or qt6-tools)." >&2
  exit 1
fi

if [[ "$#" -gt 0 ]]; then
  FILES=()
  for code in "$@"; do
    FILES+=("$DIR/7ziplx_${code}.ts")
  done
else
  mapfile -t FILES < <(find "$DIR" -maxdepth 1 -type f -name '7ziplx_*.ts' | sort)
  if [[ "${#FILES[@]}" -eq 0 ]]; then
    FILES=("$DIR/7ziplx_en.ts")
    echo "No catalogs found; creating the English source catalog ${FILES[0]}"
  fi
fi

"$LUPDATE" -recursive "$ROOT/src" -extensions cpp,h -locations relative \
  -ts "${FILES[@]}"

for ts in "${FILES[@]}"; do
  "$LRELEASE" "$ts" -qm "${ts%.ts}.qm"
  echo "Built ${ts%.ts}.qm"
done
