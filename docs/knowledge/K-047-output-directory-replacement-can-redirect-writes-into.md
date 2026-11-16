---
id: K-047
kind: defect
title: "Output directory replacement can redirect writes into source libraries"
area: [FlacConvert]
status: fixed
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: "FolderConvert::ExecuteConversion"
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: installGeneratedFiles
---

**Fact:** Conversion checks source/output containment during planning, and scan checks before staging, but
subsequent path-based writes are not anchored to those validated directories. In disposable Windows fixtures,
replacing the output with a junction to the source after the check caused conversion to overwrite source audio
and scan installation to delete source JSON.

**Why it matters:** This violates the source-preservation contract even when the initial paths are safe.
Rechecking a pathname alone does not close the race between the check and the write.
