# Application icon

- `7zipLX.jpeg` — original design mockup supplied for the icon.
- `7ziplx-icon.png` — transparent 1024×1024 master extracted from the mockup.
  The dark presentation background and the “7zipLX” caption are not part of the
  icon.

Runtime PNG sizes in `resources/icons/7ziplx-{16,24,32,48,64,128,256}.png` are
generated from the master with Lanczos downscaling. They are embedded through
`resources/app.qrc` and installed into the hicolor icon theme.
