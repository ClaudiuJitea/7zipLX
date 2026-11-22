# Resources

Current content:

- `icons/7ziplx-{16,24,32,48,64,128,256}.png` — application/desktop icons, generated from the transparent master at `icon/7ziplx-icon.png`.
- `app.qrc` — Qt resource collection exposing the icons under `:/icons/`.

The application uses `QIcon::fromTheme` for file/folder icons and falls back to Qt's icon provider. Archive items currently use generic folder/document theme icons; extension-specific archive icons are a future polish item.

Translations are not yet bundled. The UI is English-only for the MVP.
