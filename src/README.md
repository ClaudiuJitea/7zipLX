# Application source

- `app/` — `QApplication`, main window, actions and operation orchestration.
- `backend/` — 7-Zip adapter and callback bridge. No 7-Zip headers in public Qt
  headers; implementation details stay behind pimpl.
- `models/` — `LocalFsModel`, `RootModel`, archive-folder model.
- `ui/` — panels, dialogs, widgets, menus and toolbar resources.

Rules:

1. Do not include 7-Zip headers from Qt public headers.
2. No UI calls from worker threads; use signals or blocking queued prompts.
3. Centralize all `UString`/`QString` conversion in `backend/Unicode.*`.
4. Route all archive paths through 7-Zip sanitizers; never join untrusted entry
   names manually.
5. Every destructive action must be confirmable.

See `../README.md` and `../docs/BUILD.md` for build and architecture details.
