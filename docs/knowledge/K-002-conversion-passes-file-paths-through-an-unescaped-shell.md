---
id: K-002
kind: defect
title: "Conversion passes file paths through an unescaped shell command"
area: [FlacConvert]
status: active
created: 2026-10-09
reviewed: 2026-10-09
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: "MediaConvertionTask::ConvertFile"
---

**Fact:** `MediaConvertionTask::ConvertFile` concatenates source and temporary-output paths into a quoted command string and executes it with `_wsystem`; paths are not escaped for the Windows command processor.

**Why it matters:** Shell interpretation of path contents can break valid conversions or alter the command. Use direct process creation with separately supplied arguments.
