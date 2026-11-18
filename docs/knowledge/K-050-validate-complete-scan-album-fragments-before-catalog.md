---
id: K-050
kind: trap
title: "Validate complete scan album fragments before catalog operations"
area: [FlacConvert]
status: active
created: 2026-11-18
reviewed: 2026-11-18
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: AlbumCollection.ReassembleAlbums
  - path: FlacConvert/src/CatalogDatabase.cpp
    symbol: CatalogDatabase.Import
---

**Fact:** `AlbumCollection::ReassembleAlbums` rejects incomplete, duplicated or inconsistent split-album
fragment groups when at least one fragment is present, before duplicate detection and SQLite import.
`CatalogDatabase::Import` refuses unreassembled fragments.

**Why it matters:** SQLite removes stale tracks from each imported album. Importing only a fragment as a complete
album could therefore delete valid catalog tracks; duplicate matching would also compare incomplete albums.
