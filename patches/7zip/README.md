# 7-Zip POSIX patches

This directory contains the minimal patch series needed to build the reused 7-Zip code on Linux.

Rules:

- Keep `7z2606-src/` pristine.
- One topic per patch, with a short rationale and affected upstream files.
- Apply patches to a copied build tree at configure time.
- Prefer a compatibility header for trivial definitions; patch only where behavior differs.
- Every patch must be covered by a successful build of the application.

Current patches:

- `0001-agent-posix.patch` — `INVALID_FILE_ATTRIBUTES`, `HMODULE`, alt-stream helper,
  `CFileInfo` fields, icon/resource loading, `AgentOut.cpp` / `ArchiveFolderOut.cpp`
  POSIX conversions, and `UINT64`/`UInt64` fixes.
- `0002-update-path-prefix.patch` — exposes the existing `EnumerateItems()`
  path prefix through `CUpdateOptions::AddPathPrefix`, so files dropped onto an
  archive folder row are added inside that folder.
