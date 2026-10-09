---
id: K-010
kind: domain
title: "FFmpeg DLLs must be copied beside the executable for local runs"
area: ["External Dependencies"]
status: active
created: 2026-10-09
reviewed: 2026-10-10
cites:
  - path: FlacConvert/FlacConvert.vcxproj
    symbol: PostBuildEvent
---

**Fact:** The Visual Studio project now copies FFmpeg shared DLLs from `external\ffmpeg\ffmpeg-master-latest-win64-gpl-shared\bin` into `$(TargetDir)` for the x64 Debug and Release configurations, and `avformat-63.dll` appears next to the built executable after the build.

**Why it matters:** Without those DLLs beside the EXE, launching `FlacConvert.exe` fails immediately with the Windows loader error that `avformat-63.dll` was not found.
