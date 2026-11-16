---
id: K-044
kind: domain
title: "FFmpeg SDK DLLs must be staged beside the Windows executable"
area: ["External Dependencies"]
status: active
supersedes: [K-010]
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: cmake/platform/Windows.cmake
    symbol: afc_copy_windows_ffmpeg_runtime
---

**Fact:** On Windows the CMake target runs `afc_copy_windows_ffmpeg_runtime` after build, copying every DLL from
`${FFmpeg_ROOT}/bin` beside `FlacConvert.exe`; `cmake --install` installs the same DLLs.

**Why it matters:** Without those DLLs beside the executable, `FlacConvert.exe` and native test executables fail at launch with a
loader error such as `avformat-<n>.dll` not found.
