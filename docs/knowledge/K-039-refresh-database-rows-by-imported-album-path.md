---
id: K-039
kind: decision
title: "Refresh database rows by imported album path"
area: [FlacConvert]
status: active
created: 2026-11-15
reviewed: 2026-11-15
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: "AlbumCollection::ExportToDatabase"
  - path: FlacConvert/src/MediaTrack.cpp
    symbol: "MediaTrack::GetCreateTableSQL"
---

**Fact:** `TracksDB` has no stable per-track filename/path column or unique key. `AlbumCollection::ExportToDatabase`
therefore replaces every row for each imported `album_path` inside the same transaction before inserting that
album's current tracks; re-import does not duplicate rows and removed tracks do not remain.

**Why it matters:** Re-imported scan data must refresh the library catalog without accumulating duplicate or stale
track rows, while albums absent from the input remain untouched.

**Rejected:** Appending rows duplicates tracks on every import. Per-track upsert needs a stable filename/path key and
a database schema migration that the current generated JSON/database contract does not provide.
