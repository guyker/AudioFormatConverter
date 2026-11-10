---
id: K-032
kind: trap
title: "Read and write conversion files through configured paths; canonical paths only for safety checks"
area: [FlacConvert]
status: active
supersedes: [K-020]
created: 2026-11-10
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.PrepareConversion
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConvertConfiguredLibraries
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.ConvertAudioFolderToOutput
---

**Fact:** On Windows, `fs::weakly_canonical` resolves a mapped network drive to its server UNC path (for example,
`M:\tmp\24` becomes `\\<server>\<share>\tmp\24`). That UNC path may be unreachable even though the drive letter works;
resolving it again for the MP3 menu output failed with "The network name cannot be found."
`FolderConvert::PrepareConversion` (used by `FolderConvert::ConvertAudioFolderToOutput` and the manual menu) scans,
reads and writes through `fs::absolute` of the configured paths and uses canonical paths only for overlap,
containment, collision and source-equivalence checks. One exception remains: before planning,
`ConvertConfiguredLibraries` (manual options 1 and 6) validates each library with `fs::is_directory` on its
`weakly_canonical` path. Whether that probe fails on a mapped drive whose UNC path is unreachable is unverified.

**Why it matters:** Using a canonical path for file I/O, or canonicalizing an already-canonical path, breaks conversion
of libraries and output roots on mapped network drives. New filesystem access must use the configured path; the
canonical directory probe in `ConvertConfiguredLibraries` is the first place to check if manual conversion of a
mapped-drive library reports "Library path is not a readable directory".
