---
id: K-011
kind: trap
title: "Use SQLite's long-path VFS for Windows database exports"
area: [FlacConvert]
status: active
supersedes: [K-008]
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: "AlbumCollection::ExportToDatabase"
---

**Fact:** `AlbumCollection::ExportToDatabase` opens the UTF-8 database path through SQLite's `win32-longpath` VFS on Windows; other platforms use SQLite's UTF-8 open API.

**Why it matters:** Keep Unicode database names intact and preserve long-path support when changing SQLite's platform-specific file access.
