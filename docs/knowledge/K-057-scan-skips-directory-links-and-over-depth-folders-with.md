---
id: K-057
kind: decision
title: "Scan skips directory links and over-depth folders with warnings instead of failing"
area: [FlacConvert]
status: active
created: 2026-11-21
reviewed: 2026-11-21
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: "AlbumCollection::LoadAlbumsCo"
---

**Fact:** `AlbumCollection::LoadAlbumsCo` does not follow directory symlinks or junctions (`symlink_status()` is not a directory) and does not catalog or descend into folders deeper than `RecursionDirectorySearchDepth` (library-root children are depth 1); both use `disable_recursion_pending()`. Each skipped folder is logged, returned in `ScanDiscoveryCounts`, counted in the menu option 2 preview and written to `scan-report.json` as a warning diagnostic (`scan_linked_folder_skipped` / `scan_depth_limit_skipped`) with `affectedPath`. The scan still succeeds. `tests/scan_root_audio.py` verifies both cases with a junction/symlink to a folder outside the library. The user chose this behaviour on 2026-11-21.

**Why it matters:** Following a link catalogs audio from outside the configured library, which then feeds duplicate moves and database import. Previously the depth setting only skipped cataloging the over-depth folder while its descendants were still traversed and cataloged. Making links fatal would block whole-library scans that merely contain an unrelated link.

**Rejected:** Failing the scan on any directory link (as FLAC/MP3 conversion does): too disruptive for a read-only catalog operation. Following links whose target stays inside the same library root: adds canonicalization cost and still risks double-cataloging the same folder under two paths.
