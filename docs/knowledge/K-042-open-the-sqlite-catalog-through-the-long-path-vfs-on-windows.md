---
id: K-042
kind: trap
title: "Open the SQLite catalog through the long-path VFS on Windows"
area: [FlacConvert]
status: active
supersedes: [K-011]
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: FlacConvert/src/CatalogDatabase.cpp
    symbol: "CatalogDatabase::Import"
---

**Fact:** `CatalogDatabase::Import` opens the UTF-8 catalog path with `sqlite3_open_v2(..., "win32-longpath")` on Windows and
`sqlite3_open` elsewhere. `AlbumCollection::ExportToDatabase` only delegates to it.

**Why it matters:** Without the long-path VFS, Windows imports fail for output paths over `MAX_PATH`; using a narrow-character API
would corrupt Unicode database names.
