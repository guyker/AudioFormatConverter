---
id: K-048
kind: decision
title: "Use codec-aware source quality checks for AAC"
area: [FlacConvert]
status: active
supersedes: [K-013]
created: 2026-11-17
reviewed: 2026-11-17
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: isAtLeastCDQuality
  - test: ConversionTests.test_aac_m4a_at_cd_sample_rate_converts_with_lossy_warning
  - test: ConversionTests.test_aac_m4a_below_cd_sample_rate_is_rejected
---

**Fact:** `isAtLeastCDQuality` requires a verified sample rate of at least 44.1 kHz for every source, but skips the
source bit-depth check for AAC and warns that lossy encoding quality cannot be restored by conversion. Generated
integration tests accept 44.1 kHz AAC `.m4a` and reject 32 kHz AAC while checking that sources remain unchanged.

**Why it matters:** AAC's encoded stream does not establish the original PCM bit depth, and a decoder's output depth
is not evidence of source quality. The sample-rate requirement still prevents unsupported upsampling from presenting
low-rate AAC as meeting the application's minimum source profile.

**Rejected:** Requiring FFprobe to report AAC bit depth blocks supported lossy `.m4a` inputs despite their verified
sample rate; treating decoder output depth as source depth would make an unsupported quality claim. Bypassing the
sample-rate check would likewise make a below-threshold AAC source appear acceptable.
