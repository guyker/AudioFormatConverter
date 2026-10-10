---
id: K-017
kind: decision
title: "Scope CD-standard output conversion to the FLAC target"
area: [FlacConvert]
status: active
supersedes: [K-012]
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: conversionArguments
---

**Fact:** `conversionArguments` standardizes FLAC-target output to 16-bit, 44.1 kHz with triangular dithering. This policy does not apply to MP3-target output, which uses its own sample-rate and quality policy.

**Why it matters:** The CD-standard FLAC storage policy must not cause an unnecessary intermediate quantization/resampling step when producing lossy MP3.

**Rejected:** Applying the old fixed FLAC profile before MP3 encoding would reduce source precision before the lossy encoder. Preserving high-resolution FLAC output would reverse the established storage goal.
