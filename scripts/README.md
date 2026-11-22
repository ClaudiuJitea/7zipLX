# Scripts

- `fetch-7zip.sh` — download the pinned 7-Zip 26.06 source into `7z2606-src/`.
  The directory is ignored by Git.
- `gen-7zcore-sources.sh` — regenerate `cmake/7zcore_sources.cmake` from the
  fetched upstream build rules. Only needed when changing the reused engine
  source list.

Packaging:

- `build-deb.sh` — build the release tree and create
  `dist/7ziplx_<version>-<revision>_<arch>.deb`.
- `build-appimage.sh` — build the release tree and create
  `dist/7zipLX-<version>-x86_64.AppImage`. It downloads linuxdeploy,
  linuxdeploy-plugin-qt and appimagetool into `build-appimage/tools/` on first
  use.

Run `./scripts/fetch-7zip.sh` once before configuring the project.
