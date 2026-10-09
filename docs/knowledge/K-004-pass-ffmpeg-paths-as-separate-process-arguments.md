---
id: K-004
kind: trap
title: "Pass FFmpeg paths as separate process arguments"
area: [FlacConvert]
status: active
created: 2026-10-09
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: "MediaConvertionTask::ConvertFile"
---

**Fact:** `MediaConvertionTask::ConvertFile` starts FFmpeg with `_wspawnvp` and an argument vector containing the executable, source path, options, and temporary output path separately.

**Why it matters:** Do not route file paths through a shell command string; separate process arguments preserve spaces and prevent shell metacharacters from changing the command.
