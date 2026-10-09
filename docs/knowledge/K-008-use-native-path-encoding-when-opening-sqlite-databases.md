---
id: K-008
kind: trap
title: "Use native path encoding when opening SQLite databases"
area: [FlacConvert]
status: superseded
superseded_by: K-011
created: 2026-10-09
reviewed: 2026-10-09
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: "AlbumCollection::ExportToDatabase"
---

**Fact:** `AlbumCollection::ExportToDatabase` uses `sqlite3_open16` for Windows wide paths and UTF-8 `sqlite3_open` elsewhere.

**Why it matters:** `std::filesystem::path::c_str()` is UTF-16 on Windows but narrow on POSIX; passing a POSIX path to `sqlite3_open16` misinterprets its bytes and can prevent opening the intended database.
