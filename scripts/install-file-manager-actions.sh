#!/usr/bin/env bash
# Install optional file-manager integration for 7zipLX.
#
# Installs:
#   * KDE/Dolphin service menu
#   * Nemo actions
#   * Nautilus scripts
#   * Thunar custom actions (per-user only)
#
# Run as the desktop user to install into ~/.local/share and ~/.config.
# Run with sudo/root to install the shared desktop files into /usr/share
# (Thunar's per-user uca.xml is skipped in that case).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYSTEM=0
if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
  SYSTEM=1
fi

if [[ "$SYSTEM" -eq 1 ]]; then
  DATA_DIR="/usr/share"
  echo "Installing shared file-manager actions into $DATA_DIR"
else
  DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
  echo "Installing per-user file-manager actions into $DATA_DIR"
fi

install_file() {
  local src="$1" dest="$2"
  mkdir -p "$(dirname "$dest")"
  install -m 0644 "$src" "$dest"
}

install_script() {
  local src="$1" dest="$2"
  mkdir -p "$(dirname "$dest")"
  install -m 0755 "$src" "$dest"
}

install_file "$ROOT/packaging/servicemenus/7ziplx-servicemenu.desktop" \
  "$DATA_DIR/kio/servicemenus/7ziplx-servicemenu.desktop"
install_file "$ROOT/packaging/nemo-actions/7ziplx-extract-here.nemo_action" \
  "$DATA_DIR/nemo/actions/7ziplx-extract-here.nemo_action"
install_file "$ROOT/packaging/nemo-actions/7ziplx-compress.nemo_action" \
  "$DATA_DIR/nemo/actions/7ziplx-compress.nemo_action"
install_script "$ROOT/packaging/nautilus-scripts/Extract Here" \
  "$DATA_DIR/nautilus/scripts/Extract Here"
install_script "$ROOT/packaging/nautilus-scripts/Compress to 7z" \
  "$DATA_DIR/nautilus/scripts/Compress to 7z"

if [[ "$SYSTEM" -eq 1 ]]; then
  echo "Skipping Thunar custom actions: they live in each user's ~/.config/Thunar/uca.xml."
  echo "Run this script as the desktop user, or add the actions manually (see packaging/thunar/README.md)."
  exit 0
fi

CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
UCA="$CONFIG_HOME/Thunar/uca.xml"
mkdir -p "$(dirname "$UCA")"
python3 - "$UCA" <<'PYEOF'
import os
import sys
import time
import xml.etree.ElementTree as ET

path = sys.argv[1]
actions = []
if os.path.exists(path):
    try:
        tree = ET.parse(path)
        root = tree.getroot()
        actions = list(root.findall('action'))
    except ET.ParseError as exc:
        raise SystemExit(f"Cannot parse {path}: {exc}")
else:
    root = ET.Element('actions')

existing = {a.findtext('name', '') for a in actions}


def add(name, command, icon, description):
    if name in existing:
        return False
    action = ET.SubElement(root, 'action')
    ET.SubElement(action, 'icon').text = icon
    ET.SubElement(action, 'name').text = name
    ET.SubElement(action, 'unique-id').text = f"7ziplx-{name.lower().replace(' ', '-')}-{int(time.time())}"
    ET.SubElement(action, 'command').text = command
    ET.SubElement(action, 'description').text = description
    ET.SubElement(action, 'range').text = ''
    ET.SubElement(action, 'patterns').text = '*'
    for tag in ('directories', 'audio-files', 'image-files', 'other-files', 'text-files', 'video-files'):
        ET.SubElement(action, tag)
    return True


changed = False
changed |= add('Extract Here with 7zipLX', '7ziplx --extract-here %f',
               'archive-extract', 'Extract the selected archive into its own folder')
changed |= add('Compress to 7z with 7zipLX', '7ziplx --compress %f',
               'archive-insert', 'Create a 7z archive from the selection')

if not changed:
    print(f"Thunar actions already present in {path}")
    raise SystemExit(0)

if os.path.exists(path):
    backup = path + '.7ziplx.bak'
    if not os.path.exists(backup):
        with open(path, 'rb') as src, open(backup, 'wb') as dst:
            dst.write(src.read())

ET.indent(root, space='  ')
tree = ET.ElementTree(root)
tree.write(path, encoding='UTF-8', xml_declaration=True)
print(f"Updated {path}")
PYEOF

echo "Done. Log out/in or restart the file manager if the actions do not appear immediately."
