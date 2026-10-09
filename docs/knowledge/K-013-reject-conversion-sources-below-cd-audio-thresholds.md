---
id: K-013
kind: decision
title: "Reject conversion sources below CD audio thresholds"
area: [FlacConvert]
status: active
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: isAtLeastCDQuality
---

**Fact:** `isAtLeastCDQuality` rejects a source before encoding if its first audio stream is below 16-bit or 44.1 kHz, or if either property cannot be verified. The conversion command logs the reason and fails.

**Why it matters:** Upsampling or increasing sample depth cannot restore source information; accepting such files would make the standard output parameters imply quality the source did not contain.

**Rejected:** Upsampling below-threshold files to the target would conceal their lower source resolution; accepting unverifiable properties would claim compliance without evidence.
