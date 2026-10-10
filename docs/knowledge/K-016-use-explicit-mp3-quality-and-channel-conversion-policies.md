---
id: K-016
kind: decision
title: "Use explicit MP3 quality and channel conversion policies"
area: [FlacConvert]
status: active
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: conversionArguments
---

**Fact:** `conversionArguments` uses libmp3lame VBR quality 0 by default, configurable from 0 through 9. MP3 retains 44.1/48 kHz source rates and converts other accepted rates to 48 kHz without first quantizing to 16-bit. Multichannel input is rejected unless stereo downmix is explicitly enabled; downmixing and resampling are logged.

**Why it matters:** MP3 only supports mono/stereo and limited sample rates. High-resolution FLAC must not undergo unnecessary FLAC-target quantization or an unrequested channel reduction before lossy encoding.

**Rejected:** Automatic downmixing was rejected in favor of an explicit opt-in. Always forcing 44.1 kHz would unnecessarily resample supported 48 kHz sources.
