#!/usr/bin/env bash
# Install 7zipLX file manager actions for the current user.
#
# The .deb already installs the system-wide pieces (GNOME Files extension,
# Dolphin service menus, Nemo actions).  This script adds what has to live
# in your home folder:
#   * the "Scripts > 7zipLX" menu used by the GNOME desktop and Files
#     (7zipLX also refreshes it every time it starts)
#   * the Files extension, Dolphin and Nemo entries when 7zipLX was not
#     installed from the package (for example an AppImage or a local build)
#   * Thunar custom actions in ~/.config/Thunar/uca.xml
#
# Usage: install-file-manager-actions.sh [--remove]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHARE_SRC="/usr/share"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
PROGRAM="$(command -v 7ziplx || true)"
REMOVE=0
[[ "${1:-}" == "--remove" ]] && REMOVE=1

if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
  echo "Run this as your desktop user, not as root." >&2
  exit 1
fi

if [[ -z "$PROGRAM" ]]; then
  echo "7ziplx is not on PATH; install the package or add it to PATH first." >&2
  exit 1
fi

if [[ "$REMOVE" -eq 1 ]]; then
  "$PROGRAM" --remove-menus
else
  "$PROGRAM" --install-menus
fi

# Source of the packaging files: the source tree when run from it.
pkg() { if [[ -f "$ROOT/packaging/$1" ]]; then echo "$ROOT/packaging/$1"; fi; }

user_copy() {
  local src="$1" dest="$2" system="$3"
  if [[ "$REMOVE" -eq 1 ]]; then
    rm -f "$dest"
  elif [[ -n "$src" && ! -e "$system" ]]; then
    mkdir -p "$(dirname "$dest")"
    install -m 0644 "$src" "$dest"
    echo "Installed $dest"
  fi
}

user_copy "$(pkg nautilus/7ziplx.py)" "$DATA_DIR/nautilus-python/extensions/7ziplx.py" \
  "$SHARE_SRC/nautilus-python/extensions/7ziplx.py"
for menu in 7ziplx-archive.desktop 7ziplx-compress.desktop; do
  user_copy "$(pkg "servicemenus/$menu")" "$DATA_DIR/kio/servicemenus/$menu" \
    "$SHARE_SRC/kio/servicemenus/$menu"
done
for action in "$ROOT"/packaging/nemo-actions/*.nemo_action; do
  [[ -f "$action" ]] || continue
  name="$(basename "$action")"
  user_copy "$action" "$DATA_DIR/nemo/actions/$name" "$SHARE_SRC/nemo/actions/$name"
done

UCA="$CONFIG_HOME/Thunar/uca.xml"
if [[ -d "$CONFIG_HOME/Thunar" || -f "$UCA" ]] || command -v thunar >/dev/null 2>&1; then
  mkdir -p "$(dirname "$UCA")"
  python3 - "$UCA" "$REMOVE" <<'PYEOF'
import os
import sys
import xml.etree.ElementTree as ET

path, remove = sys.argv[1], sys.argv[2] == "1"
ARCHIVES = "*.7z;*.zip;*.rar;*.tar;*.gz;*.tgz;*.bz2;*.xz;*.txz;*.zst;*.cab;*.iso;*.wim;*.deb;*.rpm;*.jar;*.apk;*.001"
ACTIONS = [
    ("7-Zip: Extract Here", "7ziplx --extract-here %F", "archive-extract", ARCHIVES, False),
    ("7-Zip: Extract to Folder", "7ziplx --extract-to-folder %F", "archive-extract", ARCHIVES, False),
    ("7-Zip: Extract Files…", "7ziplx --extract %F", "archive-extract", ARCHIVES, False),
    ("7-Zip: Test Archive", "7ziplx --test %F", "dialog-ok", ARCHIVES, False),
    ("7-Zip: Add to Archive…", "7ziplx --add %F", "archive-insert", "*", True),
    ("7-Zip: Compress to 7z", "7ziplx --compress --format 7z %F", "archive-insert", "*", True),
    ("7-Zip: Compress to zip", "7ziplx --compress --format zip %F", "archive-insert", "*", True),
]

if os.path.exists(path):
    tree = ET.parse(path)
    root = tree.getroot()
else:
    root = ET.Element("actions")
    tree = ET.ElementTree(root)

# Drop every action this script added before (current and older names).
for action in list(root.findall("action")):
    if (action.findtext("unique-id") or "").startswith("7ziplx-"):
        root.remove(action)

if not remove:
    for index, (name, command, icon, patterns, dirs) in enumerate(ACTIONS):
        action = ET.SubElement(root, "action")
        ET.SubElement(action, "icon").text = icon
        ET.SubElement(action, "name").text = name
        ET.SubElement(action, "unique-id").text = f"7ziplx-{index}"
        ET.SubElement(action, "command").text = command
        ET.SubElement(action, "description").text = name
        ET.SubElement(action, "range").text = ""
        ET.SubElement(action, "patterns").text = patterns
        if dirs:
            ET.SubElement(action, "directories")
        for tag in ("audio-files", "image-files", "other-files", "text-files", "video-files"):
            ET.SubElement(action, tag)

if os.path.exists(path) and not os.path.exists(path + ".7ziplx.bak"):
    with open(path, "rb") as src, open(path + ".7ziplx.bak", "wb") as dst:
        dst.write(src.read())
ET.indent(root, space="  ")
tree.write(path, encoding="UTF-8", xml_declaration=True)
print(("Removed 7zipLX actions from " if remove else "Updated ") + path)
PYEOF
fi

if [[ "$REMOVE" -eq 0 ]] && ! python3 -c 'import gi; gi.require_version("Nautilus", "4.1")' >/dev/null 2>&1 \
   && ! python3 -c 'import gi; gi.require_version("Nautilus", "4.0")' >/dev/null 2>&1; then
  echo "Tip: install python3-nautilus for the 7-Zip menu inside GNOME Files windows."
fi
echo "Done. Restart the file manager (for GNOME Files: nautilus -q) to see the new menu."
