# 7zipLX User Guide

## Launching

```bash
7ziplx
7ziplx /path/to/archive.7z
```

The window has two panels. The focused panel is the target for toolbar and menu actions.

## Panels

- Type a path in the local path bar and press Enter.
- Double-click a folder to enter it.
- Double-click an archive file to browse it.
- `Backspace` goes up one level; at an archive root, close the archive with `Ctrl+W`.
- `View > Computer` shows mounted filesystems and the home folder.
- `View > Show Hidden Files` and `View > Show Parent Entry (..)` control local lists.

## Right-click menus

Right-click any panel item for a 7-Zip File Manager-style context menu. The local-files menu includes the **7-Zip** submenu (open/extract/test an archive, add files to an archive, or create a `.7z`/`.zip` beside the selection), Open Inside variants, View/Edit, file operations, CRC/hash methods, split/combine, properties and link creation. The archive panel menu adds extraction and testing, comment editing, archive-item hashing and folder creation. Use **Select All** / **Invert Selection** to act on several items at once.

Screenshots: [`context-menu-local.png`](screenshots/context-menu-local.png) and [`context-menu-archive.png`](screenshots/context-menu-archive.png).

## Archives

- **Open Archive** (`Ctrl+O`) opens an archive in the active panel.
- **Extract** (`Ctrl+E`) extracts selected items, or everything when nothing is selected.
- **Test Archive** (`Ctrl+T`) reads and verifies the archive.
- **Add to Archive** (`Alt+A`) creates an archive from selected local files/folders.
- Drag local files onto an archive panel to add them.
- Drag files out of an archive panel to the local panel to extract them.
- `F5` / `F6` copy or move between the two panels.
- `F2` renames; `Del` deletes; `F7` creates a folder; `Ctrl+N` creates a file.
- `Alt+Enter` shows basic properties.
- `File > Create Link...` creates a symbolic or hard link for a selected local file.

Archive editing (rename/delete/create folder) rewrites the archive. Formats that do not support updating show an error.

## Passwords

Encrypted archives prompt for a password when opened or extracted. Enter the password in the dialog. Header-encrypted 7z archives need the password before their contents can be listed.

When creating an archive, enter a password in the Compress dialog. For 7z, enable *Encrypt file names* for header encryption.

## Nested archives and flat view

Double-click an archive file inside an archive panel to browse it as a nested archive. `Backspace` at the nested root returns to the parent archive. `View > Flat View` shows nested local or archive items in one flat list.

## Archive-to-archive copy/move

With two archive panels open, `F5`/`F6` copy or move selected items between them through a temporary extraction. The target archive is refreshed after the operation.

## Comments, diff, viewer and editor

- `File > Comment...` edits a ZIP item comment.
- `File > View` (`F3`) and `File > Edit` (`F4`) launch configured external tools; archive items are extracted to a private temporary directory first.
- `File > Diff` compares exactly two selected local files or archive items with the configured diff command.
- Configure commands under `Tools > Options > Tools`.

## Benchmark

`Tools > Benchmark...` runs an LZMA/LZMA2 compression/decompression benchmark with a configurable dictionary size and thread count. The log is shown in the dialog.

## Hashing

`Tools > Hash...` (`Ctrl+H`) computes a hash for selected local files. Choose the method in the prompt and copy the result from the dialog.

## Split / combine

- Select one local file, then `File > Split File...`.
- Select the first `.001` volume, then `File > Combine Files...`.

## Theme

The application uses a dark desktop theme with rounded panels, menus and dialogs. Modal message boxes and dialog button boxes intentionally use no decorative icons.

## Options

`Tools > Options` contains:

- **General** — hidden files, parent row, grid and flat view.
- **Tools** — viewer, editor and diff command lines.
- **Plugins** — status of the optional `7z.so` external codec/format module.
- **Associations** — request default handlers through `xdg-mime`.
- **Language** — select an installed `7ziplx_<code>.qm` translation; restart after changing it.

## Favorites and settings

- `Favorites > Add Current Folder` stores the active local folder.
- The window size, splitter position, last panel paths and view options are saved automatically.
- Use `File > Exit` or `Ctrl+Q` to quit.

## Keyboard reference

| Action | Shortcut |
|---|---|
| Open archive | `Ctrl+O` |
| Extract | `Ctrl+E` |
| Test | `Ctrl+T` |
| Add to archive | `Alt+A` |
| Copy/move to other panel | `F5` / `F6` |
| Rename | `F2` |
| Delete | `Del` |
| Create folder | `F7` |
| Create file | `Ctrl+N` |
| Up one level | `Backspace` |
| Refresh | `Ctrl+R` |
| Select all | `Ctrl+A` |
| Cut/copy/paste | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` |
| Hash | `Ctrl+H` |
| Close archive | `Ctrl+W` |

## Troubleshooting

- If the window does not start, run from a terminal and check the Qt platform plugin messages.
- For headless sessions, use `QT_QPA_PLATFORM=offscreen`.
- Large directories load synchronously in the current MVP; allow a moment for very large folders.
- If archive editing fails on an encrypted archive, close and reopen it with the password first; full encrypted-archive editing support is a future milestone.
