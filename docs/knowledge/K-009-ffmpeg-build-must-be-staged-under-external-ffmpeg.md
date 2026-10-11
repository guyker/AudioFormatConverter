---
id: K-009
kind: constraint
title: "FFmpeg build must be staged under external\\ffmpeg"
area: ["External Dependencies"]
status: active
created: 2026-10-09
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: RunWithArguments
---

**Fact:** The project build and VS task files reference the repo-local `external\ffmpeg\ffmpeg-master-latest-win64-gpl-shared` tree for FFmpeg headers and libraries; when that tree was missing, the build failed with `C1083` on `libavformat/avformat.h` from `FlacConvert/include/FFmpegWrapper.h`.

**Why it matters:** If FFmpeg is unpacked somewhere else, the project cannot resolve the headers or link the shared libraries and the build fails before any source compilation finishes.
