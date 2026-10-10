---
id: K-020
kind: trap
title: "Read and write conversion files through configured paths, not canonical paths"
area: [FlacConvert]
status: active
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.ConvertAudioFolderToOutput
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConvertFLACToMP3
---

**Fact:** On Windows, `fs::weakly_canonical` resolves a mapped network drive to its server UNC path (for example,
`M:\tmp\24` becomes `\\<server>\<share>\tmp\24`). That UNC path may be unreachable even though the drive letter works.
Resolving it again for the MP3 menu output failed with "The network name cannot be found."
`ConvertFLACToMP3` and `FolderConvert::ConvertAudioFolderToOutput` now scan, read and write through the configured
(absolute) paths. They use canonical paths only for overlap and containment checks.

**Why it matters:** Using a canonical path for file I/O, or canonicalizing an already-canonical path, breaks conversion
of libraries and output roots on mapped network drives.
