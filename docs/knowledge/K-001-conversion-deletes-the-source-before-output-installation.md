---
id: K-001
kind: defect
title: "Conversion deletes the source before output installation"
area: [FlacConvert]
status: fixed
fixed_by: K-003
created: 2026-10-09
reviewed: 2026-10-09
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: "MediaConvertionTask::InstallTemporaryOutput"
---

**Fact:** `MediaConvertionTask::RenameAndRemoveTMPFile` removes the source before renaming the temporary output to the target, and does not check the rename result.

**Why it matters:** If output installation fails after removal, the source is lost. Preserve the original until replacement succeeds and report installation errors.
