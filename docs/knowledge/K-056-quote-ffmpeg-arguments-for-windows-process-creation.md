---
id: K-056
kind: trap
title: "Quote FFmpeg arguments for Windows process creation"
area: [FlacConvert]
status: active
supersedes: [K-014]
created: 2026-11-19
reviewed: 2026-11-19
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: quoteWindowsArgument
---

**Fact:** `quoteWindowsArgument` escapes every FFmpeg argument using Windows CRT parsing rules before
`CreateProcessW` receives the command line. The POSIX `execvp` branch instead passes separate unquoted arguments.

**Why it matters:** Windows process creation takes one command-line string; without correct quoting, spaces, quotes
and trailing backslashes can change paths or arguments. POSIX treats those quotes as literal characters. Neither
branch uses a shell.
