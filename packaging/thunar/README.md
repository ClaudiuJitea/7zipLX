# Thunar custom actions

Thunar does not read custom actions from a system-wide directory. Use
`scripts/install-file-manager-actions.sh` (as your desktop user) to merge the
two actions below into `~/.config/Thunar/uca.xml`, or add them manually in
Thunar under **Edit → Configure custom actions**:

| Name | Command | Selection |
|---|---|---|
| Extract Here | `7ziplx --extract-here %f` | Archive files |
| Compress to 7z | `7ziplx --compress %f` | Any files/folders |

Both commands are non-interactive apart from the progress dialog and the
standard overwrite/password prompts.
