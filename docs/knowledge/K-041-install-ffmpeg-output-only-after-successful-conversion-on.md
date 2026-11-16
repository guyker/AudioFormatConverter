---
id: K-041
kind: trap
title: "Install FFmpeg output only after successful conversion on every platform"
area: [FlacConvert]
status: active
supersedes: [K-003]
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: "MediaConvertionTask::InstallTemporaryOutput"
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: installFile
---

**Fact:** `MediaConvertionTask::InstallTemporaryOutput` verifies that the verified, audited temporary output is a regular
file before installing it through `installFile`: `MoveFileExW` on Windows; on POSIX, `rename` when overwriting, or
hard-link then copy (never replacing an existing target) without overwrite. The source is never removed first.

**Why it matters:** An FFmpeg, verification or install failure must not delete or replace the original, and a non-overwrite run must
not clobber an existing target. On install failure the temporary is retained for recovery.
