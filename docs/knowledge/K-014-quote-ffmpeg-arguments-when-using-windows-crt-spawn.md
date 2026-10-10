---
id: K-014
kind: trap
title: "Quote FFmpeg arguments when using Windows CRT spawn"
area: [FlacConvert]
status: active
supersedes: [K-004]
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: quoteWindowsArgument
---

**Fact:** `quoteWindowsArgument` applies Windows CRT quoting and backslash escaping to FFmpeg's argument vector before `_wspawnvp`. The POSIX `execvp` branch passes native path bytes as separate, unquoted arguments.

**Why it matters:** Windows CRT spawn joins arguments without escaping them, which truncated filenames at their first space. An argument vector alone does not preserve Windows paths; adding those quotes on POSIX would instead make them literal filename characters. Neither branch uses a shell.
