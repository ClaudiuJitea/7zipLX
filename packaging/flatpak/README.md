# Flatpak

The manifest builds 7zipLX against the KDE Qt 6 runtime and the pinned 7-Zip
26.06 sources. The AppStream metadata, desktop entry, icons and optional
file-manager service menus are installed by the normal CMake install rules.

```bash
flatpak-builder --user --install --force-clean build-flatpak \
  packaging/flatpak/io.github.claudiujitea.7zipLX.yml
flatpak run io.github.claudiujitea.7zipLX
```

The manifest uses the GitHub `main` branch as the application source. For local
development, replace that source with:

```yaml
- type: dir
  path: ../..
```

For a distributable bundle:

```bash
flatpak-builder --repo=repo --force-clean build-flatpak \
  packaging/flatpak/io.github.claudiujitea.7zipLX.yml
flatpak build-bundle repo 7zipLX.flatpak io.github.claudiujitea.7zipLX
```

Assembly optimizations are left off in the Flatpak manifest because the
freedesktop/KDE SDK does not ship `asmc`. If your SDK provides `asmc`, add
`-DSEVENZIP_BUILD_ASM=ON` and `-DSEVENZIP_ASM_COMPILER=/usr/bin/asmc`.
