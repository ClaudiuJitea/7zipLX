# Thunar custom actions

Thunar does not read custom actions from a system-wide directory. Run
`/usr/share/7ziplx/install-file-manager-actions.sh` as your desktop user to
merge the actions below into `~/.config/Thunar/uca.xml` (run it again with
`--remove` to take them out), or add them under **Edit → Configure custom
actions**:

| Name | Command | Appears for |
|---|---|---|
| 7-Zip: Extract Here | `7ziplx --extract-here %F` | Archives |
| 7-Zip: Extract to Folder | `7ziplx --extract-to-folder %F` | Archives |
| 7-Zip: Extract Files… | `7ziplx --extract %F` | Archives |
| 7-Zip: Test Archive | `7ziplx --test %F` | Archives |
| 7-Zip: Add to Archive… | `7ziplx --add %F` | Any files and folders |
| 7-Zip: Compress to 7z | `7ziplx --compress --format 7z %F` | Any files and folders |
| 7-Zip: Compress to zip | `7ziplx --compress --format zip %F` | Any files and folders |
