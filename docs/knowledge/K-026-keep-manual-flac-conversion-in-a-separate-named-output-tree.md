---
id: K-026
kind: decision
title: "Keep manual FLAC conversion in a separate named output tree"
area: [FlacConvert]
status: active
supersedes: [K-021]
created: 2026-10-11
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConvertFLACToFLAC
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConvertConfiguredLibraries
---

**Fact:** `ConvertFLACToFLAC` uses the same prepared folder execution as MP3 and writes active libraries to
`OutDirectory/FLAC/<mediaName>` (falling back to the source directory name). All libraries are preflighted
before writing. Sources remain untouched, existing destinations are rejected, relative paths are retained,
and the first error stops processing while completed outputs remain. The FLAC profile stays 16-bit/44.1 kHz
with triangular dithering; changing output location does not restore information lost in that profile.

**Why it matters:** The maintainer explicitly replaced the in-place workflow to protect source libraries,
including when a later file fails. A partial batch must not leave the source library partly resampled.

**Rejected:** In-place replacement was rejected even with a REPLACE confirmation. Continuing past errors was
rejected in favor of matching MP3's stop-at-first-error behavior. Automatic output overwrite remains disabled.
