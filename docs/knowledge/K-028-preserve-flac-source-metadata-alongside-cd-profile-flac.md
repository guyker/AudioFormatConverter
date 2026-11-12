---
id: K-028
kind: decision
title: "Preserve FLAC source metadata alongside CD-profile FLAC output"
area: [FlacConvert]
status: superseded
superseded_by: K-037
created: 2026-10-11
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: MediaConvertionTask.ConvertFile
  - path: FlacConvert/src/FlacMetadata.cpp
    symbol: FlacMetadata.Save
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.PrepareConversion
---

**Fact:** Manual and explicit FLAC output map supported tags/pictures and copy non-audio companions unchanged.
Native FLAC sources additionally produce `<filename>.flac.flac-metadata` through `FlacMetadata::Save`,
preserving every original metadata block and recognized leading/trailing tags byte-for-byte, as for MP3.
The installer protects the previous sidecar on output replacement failure. Non-FLAC inputs have no native
metadata sidecar. Unsupported pictures, repeated comments, chapters and opaque blocks remain in the archive
even if the output container or FFmpeg mapping cannot embed them faithfully.

**Why it matters:** CD-profile resampling changes audio: original seek offsets, checksums and technical
metadata must not be inserted blindly into the new FLAC. The sidecar describes the original and contains
no audio; it cannot recover lost precision. Mapped fields/picture dimensions may be normalized. Copied
cue sheets/playlists are not rewritten and can retain old file names or source properties.

**Rejected:** Audio-only mapping loses artwork. FFmpeg mapping alone can lose repeated/opaque metadata.
Replacing the output's generated STREAMINFO/seek data with source blocks would misdescribe the new audio.
