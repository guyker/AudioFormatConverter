---
id: K-015
kind: decision
title: "Preserve original FLAC metadata alongside MP3 output"
area: [FlacConvert]
status: active
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/src/FlacMetadata.cpp
    symbol: "FlacMetadata::Save"
---

**Fact:** `FlacMetadata::Save` retains the original native FLAC marker and all metadata blocks byte-for-byte in a `.mp3.flac-metadata` sidecar. MP3 conversion copies supported tags and pictures into ID3v2.4, copies non-audio companion files, and never modifies the source. Metadata preservation failures fail the conversion.

**Why it matters:** ID3 cannot represent every FLAC structure, and FFmpeg may normalize repeated comments. Original application blocks, cue sheets and picture data must remain available even when they cannot be embedded in MP3.

**Rejected:** Relying solely on FFmpeg tag mapping or ffprobe JSON can lose repeated fields and opaque blocks. Keeping an entire second audio copy in the sidecar defeats the storage goal.
