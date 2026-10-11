---
id: K-019
kind: decision
title: "Manual MP3 conversion uses separate named library output directories"
area: [FlacConvert]
status: active
created: 2026-10-10
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConvertFLACToMP3
---

**Fact:** `ConvertFLACToMP3` implements manual menu selection 6 for active configured libraries. Each library writes to `OutDirectory/MP3/<mediaName>`, falling back to its source folder name. It uses VBR quality 0, rejects multichannel input, and does not overwrite existing output. Library names and cross-library output/source overlaps are checked before conversion.

**Why it matters:** Manual conversion must keep libraries separate, leave sources unchanged, and preserve the same metadata policy as the explicit MP3 command.

**Rejected:** Prompting for an output directory every time was rejected in favor of configured, predictable destinations. Sharing one output folder between libraries risks filename collisions.
