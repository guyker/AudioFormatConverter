---
id: K-012
kind: decision
title: "Standardize converted audio to 16-bit 44.1 kHz FLAC"
area: [FlacConvert]
status: active
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: runFFmpeg
---

**Fact:** `runFFmpeg` converts the first audio stream to 16-bit, 44.1 kHz FLAC and requests triangular dithering during sample conversion.

**Why it matters:** This intentional standardization reduces storage compared with higher-resolution sources while targeting CD-standard sample depth and rate.

**Rejected:** Preserving the source bit depth and sample rate would not meet the storage and standardization goal; omitting dither would leave 24-to-16-bit quantization without the selected dither treatment.
