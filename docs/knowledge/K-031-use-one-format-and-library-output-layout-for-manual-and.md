---
id: K-031
kind: decision
title: "Use one format and library output layout for manual and explicit conversion"
area: [FlacConvert]
status: active
created: 2026-10-11
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: RunWithArguments
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConvertConfiguredLibraries
---

**Fact:** `RunWithArguments` treats explicit conversion `--output-dir` (or configured OutDirectory) as a base root,
not the exact destination. Both manual and explicit commands append `FLAC` or `MP3` and a library folder name.
Explicit conversion uses the input folder name; manual conversion uses the configured mediaName when supplied.
Relative source paths are retained within that tree. The lower-level FolderConvert API still accepts an exact
library destination.

**Why it matters:** Switching between manual and explicit conversion should not silently change folder structure
or mix targets. Scripts expecting the old exact-root explicit layout must use the new format/library subtree.

**Rejected:** Keeping the explicit exact-root behavior made equivalent manual and explicit runs produce different
layouts. Removing the manual format/library grouping would mix configured libraries and conversion targets.
