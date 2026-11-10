---
id: K-030
kind: decision
title: "Mark failed newly created library outputs and retain crash markers"
area: [FlacConvert]
status: active
created: 2026-10-11
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.ExecuteConversion
---

**Fact:** `FolderConvert::ExecuteConversion` owns only output library folders it creates during execution.
On failure it renames those folders with `ERROR_AUDIO_`, `ERROR_PRESERVATION_`, `ERROR_COPY_`, `ERROR_IO_` or
`INCOMPLETE_`, adding a numeric suffix on collisions. Pre-existing folders and completed libraries are not renamed.
Reports and manual summaries use the actual location; a failed rename is logged and leaves the original path.
The reserved `.conversion-in-progress` marker directory remains on failure and blocks reuse. It is removed only
after planned outputs and the final report are complete. Preflight failures create no library output or marker.

**Why it matters:** Completed files remain useful after a stopped batch, but a media folder must not appear complete.
A crash or power loss cannot execute renaming cleanup, so the marker identifies unfinished output independently of
the report. Renaming a folder that existed before the run could move unrelated user files.

**Rejected:** Renaming every failed destination disregards folder ownership. Deleting partial output loses completed
work. Relying only on final cleanup leaves crashed output unmarked. Automatic marker removal on retry hides incomplete runs.
