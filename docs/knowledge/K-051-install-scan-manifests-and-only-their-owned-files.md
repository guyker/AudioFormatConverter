---
id: K-051
kind: decision
title: "Install scan manifests and only their owned files"
area: [FlacConvert]
status: active
supersedes: [K-005]
created: 2026-11-18
reviewed: 2026-11-18
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: installGeneratedFiles
---

**Fact:** `installGeneratedFiles` stages each scan's album JSON, manifest, and referenced metadata archives, then
replaces prior generated JSON and archives only when the prior manifest owns those archive names. Installation
rolls back on failure, retires prior manifest-owned archives after success, and preserves the SQLite database and
unrelated files.

**Why it matters:** A scan refresh must not erase the database or unrelated library output, and must not overwrite
an unowned file that happens to collide with a generated archive name.

**Rejected:** Swapping the entire output directory or deleting archives by filename pattern would remove or
overwrite files the scanner does not own.
