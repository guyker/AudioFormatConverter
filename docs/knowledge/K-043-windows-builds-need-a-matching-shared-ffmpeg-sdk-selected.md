---
id: K-043
kind: constraint
title: "Windows builds need a matching shared FFmpeg SDK selected by FFmpeg_ROOT"
area: ["External Dependencies"]
status: active
supersedes: [K-009]
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: cmake/platform/Windows.cmake
    symbol: FFmpeg_ROOT
---

**Fact:** `cmake/platform/Windows.cmake` resolves `FFmpeg_ROOT` (cache variable, else the `FFmpeg_ROOT` environment
variable, else `external/ffmpeg/ffmpeg-master-latest-win64-gpl-shared`) and fails configuration unless it has
`include/libavformat/avformat.h`, the `avformat`, `avcodec`, `avfilter`, `avutil` and `swresample` import
libraries, runtime DLLs in `bin`, and `ffmpeg.exe`/`ffprobe.exe`. Linux uses pkg-config instead. The Visual
Studio fallback still hard-codes the default path.

**Why it matters:** Headers, import libraries, DLLs and the runtime `ffmpeg.exe` must come from one SDK build; mixing versions links
but fails at load time or gives different decode behaviour. CI pins the SDK by setting `FFmpeg_ROOT`.
