---
id: K-005
kind: decision
title: "Scan replaces generated JSON files without swapping the library directory"
area: [FlacConvert]
status: active
created: 2026-10-09
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: installGeneratedFiles
  - path: FlacConvert/include/AppSettings.h
    symbol: "MediaDirectoryElement::getMediaDBPath"
---

**Fact:** `installGeneratedFiles` stages scan output and replaces only generated `media_out_*.json` files. `MediaDirectoryElement::getMediaDBPath` places the SQLite database in the same library output directory.

**Why it matters:** Preserve databases and unrelated files when refreshing a library scan.

**Rejected:** Swapping the entire library directory would remove the database and other non-JSON output stored beside the scan files.
