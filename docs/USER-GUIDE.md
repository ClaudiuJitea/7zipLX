# 7zipLX User Guide

## Launching

```bash
7ziplx
7ziplx /path/to/archive.7z
```

The window starts with one panel. `View > Two Panels` (`F9`) shows the second
panel; the focused panel is the target for toolbar and menu actions. Launching
with an archive argument always opens in single-pane mode.

## Panels

- Type a path in the local path bar and press Enter.
- Double-click a folder to enter it.
- Double-click an archive file to browse it.
- `Backspace` goes up one level; at an archive root, close the archive with `Ctrl+W`.
- `View > Computer` shows mounted filesystems and the home folder.
- `View > Two Panels` (`F9`) toggles the second pane. The choice is remembered
  with the window geometry and panel paths.
- Large local folders are scanned off the GUI thread and filled into the list in
  batches; the status line shows `Loading...` until the scan finishes.
- `View > Show Hidden Files` and `View > Show Parent Entry (..)` control local lists.

## Right-click menus

Right-click any panel item for a 7-Zip File Manager-style context menu. The local-files menu includes the **7-Zip** submenu (open/extract/test an archive, add files to an archive, or create a `.7z`/`.zip` beside the selection), Open Inside variants, View/Edit, file operations, CRC/hash methods, split/combine, properties and link creation. The archive panel menu adds extraction and testing, comment editing, archive-item hashing and folder creation. Use **Select All** / **Invert Selection** to act on several items at once.

Screenshots: [`two-panes.png`](screenshots/two-panes.png), [`workflows.png`](screenshots/workflows.png) and [`writeback.png`](screenshots/writeback.png).

## Archives

- **Open Archive** (`Ctrl+O`) opens an archive in the active panel.
- **Extract** (`Ctrl+E`) extracts selected items, or everything when nothing is selected.
- **Test Archive** (`Ctrl+T`) reads and verifies the archive.
- **Add to Archive** (`Alt+A`) creates an archive from selected local files/folders.
- Drag local files onto an archive panel to add them.
- Drag files out of an archive panel to the local panel to extract them.
- Drop onto a folder row to copy into that folder. In an archive, dropping onto
  a folder row adds the items inside that archive folder.
- Plain drag or `Ctrl` copies; `Shift` moves.
- `F5` / `F6` copy or move to the second panel. In single-pane mode the second
  pane is revealed first; if both panes point at the same folder, 7zipLX asks
  for a destination folder. `Tab` / `Shift+Tab` switch the active panel.
- `F2` renames; `Del` deletes; `F7` creates a folder; `Ctrl+N` creates a file.
- `Alt+Enter` shows basic properties.
- `File > Create Link...` creates a symbolic or hard link for a selected local file.

Archive editing (rename/delete/create folder) rewrites the archive. Formats that do not support updating show an error.

## Passwords

Encrypted archives prompt for a password when opened or extracted. Enter the password in the dialog. Header-encrypted 7z archives need the password before their contents can be listed.

When creating an archive, enter a password in the Compress dialog. For 7z, enable *Encrypt file names* for header encryption.

## Nested archives and flat view

Double-click an archive file inside an archive panel to browse it as a nested archive. `Backspace` at the nested root returns to the parent archive. `View > Flat View` shows nested local or archive items in one flat list.

Nested archives can be edited. After an add, rename, delete, create-folder or comment operation, 7zipLX writes the modified inner archive back to its parent, recursively for deeper nesting. This works for update-capable containers such as 7z, ZIP, TAR, GZip, BZip2 and XZ.

Nested editing is disabled for split-volume containers and read-only outer formats; the app explains why. Moving items into a nested archive is not supported (copy them instead), because the source would be removed before the outer archive is saved. Extraction, testing, viewing and hashing nested items remain available.

## Dual-pane transfers and clipboard

Select any number of items with `Ctrl`/`Shift`-click or `Ctrl+A`. Starting a
drag keeps the whole selection, including rows that were not under the mouse.
`Ctrl+I` inverts the selection. The status line shows the selected item count
and size.

- Drag between local folders, from local to archive, from archive to local, or
  between two archives.
- Drop onto a folder row to choose the destination. Archive folder rows use
  that folder inside the target archive.
- `Shift`-drag moves; plain or `Ctrl`-drag copies. Moves from archives are
  confirmed before the source entries are deleted.
- `Ctrl+C` / `Ctrl+X` / `Ctrl+V` use the same transfer engine. If the source
  archive panel has been closed or navigated away, paste reopens the archive
  in the background.
- `Tab` / `Shift+Tab` switch the active panel; the active panel is highlighted.
  `Ctrl+L` focuses the path bar.

## Archive-to-archive copy/move

With two archive panels open, `F5`/`F6` or drag and drop copy or move selected items between them through a temporary extraction. The target archive is refreshed after the operation.

## Comments, diff, viewer and editor

- `File > Comment...` edits a ZIP item comment.
- `File > View` (`F3`) and `File > Edit` (`F4`) launch configured external tools; archive items are extracted to a private temporary directory first. The extraction uses the normal progress dialog and can be canceled.
- `F4` copies are watched. When the editor saves, 7zipLX asks **Update archive?**. Choose **Update Archive** to write the change back (nested archives are rebuilt bottom-up), **Keep Changes** to retry later with `File > Update Archive from Edited File(s)`, or **Discard** to delete the copy.
- View and drag copies are capped at 512 MiB by default (`tools/externalTempLimitMiB`); idle view copies are removed after 30 minutes (`tools/externalTempMaxAgeMinutes`). Edited copies stay until write-back or discard.
- `File > Diff` compares exactly two selected local files or archive items with the configured diff command. Diff runs asynchronously and can be canceled by closing it/the timeout; the GUI is not blocked.
- Configure commands under `Tools > Options > Tools`.

## Benchmark

`Tools > Benchmark...` runs an LZMA/LZMA2 compression/decompression benchmark with a configurable dictionary size and thread count. The log is shown in the dialog.

## Hashing

`Tools > Hash...` (`Ctrl+H`) computes a hash for selected local files or archive items. Archive items are hashed by streaming decoded data through the 7-Zip hashers, without writing a temporary copy. Choose the method in the prompt and copy the result from the dialog.

## Split / combine

- Select one local file, then `File > Split File...`.
- Select the first `.001` volume, then `File > Combine Files...`.

## Theme

The application follows the system light/dark preference by default and also offers explicit Light and Dark choices in `Tools > Options > Appearance`. Modal message boxes and dialog button boxes intentionally use no decorative icons.

## Options

`Tools > Options` contains:

- **General** — hidden files, parent row, grid, flat view and theme.
- **Tools** — viewer, editor and diff command lines.
- **Plugins** — status of the optional `7z.so` external codec/format module.
- **Associations** — request default handlers through `xdg-mime`.
- **Language** — select an installed `7ziplx_<code>.qm` translation; restart after changing it. User-facing strings are wrapped in `tr()`; run `scripts/update-translations.sh` to generate catalogs.

## Favorites and settings

- `Favorites > Add Current Folder` stores the active local folder.
- The window size, splitter position, single/two-pane choice, last panel paths and view options are saved automatically.
- Use `File > Exit` or `Ctrl+Q` to quit.

## Keyboard reference

| Action | Shortcut |
|---|---|
| Open archive | `Ctrl+O` |
| Extract | `Ctrl+E` |
| Test | `Ctrl+T` |
| Add to archive | `Alt+A` |
| Copy/move to other panel | `F5` / `F6` |
| Show/hide second panel | `F9` |
| Rename | `F2` |
| Delete | `Del` |
| Create folder | `F7` |
| Create file | `Ctrl+N` |
| Up one level | `Backspace` |
| Switch active panel | `Tab` / `Shift+Tab` |
| Focus path bar | `Ctrl+L` |
| Invert selection | `Ctrl+I` |
| Refresh | `Ctrl+R` |
| Select all | `Ctrl+A` |
| Cut/copy/paste | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` |
| Hash | `Ctrl+H` |
| Close archive | `Ctrl+W` |

## Troubleshooting

- If the window does not start, run from a terminal and check the Qt platform plugin messages.
- For headless sessions, use `QT_QPA_PLATFORM=offscreen`.
- Very large folders are scanned in the background, but the list is still filled in batches; the first rows appear as soon as the scan completes.
- If archive editing fails on an encrypted archive, close and reopen it with the password first; full encrypted-archive editing support is a future milestone.

## Command line and desktop integration

```bash
7ziplx --extract-here archive.7z
7ziplx --extract-to /path/to/output archive.zip
7ziplx --compress file-or-folder
7ziplx --compress-to output.7z file1 file2
```

These modes show the normal progress dialog, can prompt for passwords and
overwrites, and exit without opening the main window. They are used by the
optional file-manager actions:

```bash
./scripts/install-file-manager-actions.sh
```

This installs a Dolphin/KDE service menu, Nemo actions, Nautilus scripts and
(per-user) Thunar custom actions, or use the system packages. A Flatpak manifest
is available under `packaging/flatpak/`.
