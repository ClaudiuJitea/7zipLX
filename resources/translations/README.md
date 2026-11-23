# Translations

Place Qt Linguist compiled translation files here as `7ziplx_<language>.qm` (for example `7ziplx_de.qm`).

At startup 7zipLX looks for translations in:

- `<application dir>/translations`
- `<application dir>/../share/7ziplx/translations`
- `/usr/share/7ziplx/translations`

The Options → Language page lists discovered files. User-facing strings are
wrapped in `tr()`; generate catalogs with:

```bash
./scripts/update-translations.sh de fr
```

This writes `7ziplx_de.ts`/`7ziplx_fr.ts` and the corresponding `.qm` files here.
Additional `.qm` files can be installed without rebuilding.
