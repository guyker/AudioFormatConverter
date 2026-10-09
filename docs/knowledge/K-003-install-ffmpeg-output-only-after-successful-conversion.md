---
id: K-003
kind: trap
title: "Install FFmpeg output only after successful conversion"
area: [FlacConvert]
status: active
created: 2026-10-09
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: "MediaConvertionTask::InstallTemporaryOutput"
---

**Fact:** `MediaConvertionTask::InstallTemporaryOutput` verifies the temporary output exists as a regular file before installing it with `MoveFileExW`; it never removes the source first.

**Why it matters:** Keep conversion output installation after successful encoding so an FFmpeg or rename failure cannot delete the original file.
