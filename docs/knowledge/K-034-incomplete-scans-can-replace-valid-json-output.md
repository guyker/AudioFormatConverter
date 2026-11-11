---
id: K-034
kind: defect
title: "Incomplete scans can replace valid JSON output"
area: [FlacConvert]
status: fixed
fixed_by: K-035
created: 2026-11-10
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: "AlbumCollection::SaveAlbumsToJSON"
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ScanFolderAndCreateJSON
---

**Fact:** `AlbumCollection::SaveAlbumsToJSON` omits tracks with uppercase audio extensions or unparseable
probe JSON but still returns success. `ScanFolderAndCreateJSON` installs the resulting batch over previous
generated JSON; a library containing only `.FLAC` tracks can produce an empty array with exit code 0.

**Why it matters:** A rescan can silently remove albums or tracks from the catalog used by duplicate detection
and SQLite import while replacing the previous good scan. The source audio is not modified.
