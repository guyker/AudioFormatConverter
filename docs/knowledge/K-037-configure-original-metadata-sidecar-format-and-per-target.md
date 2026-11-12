---
id: K-037
kind: decision
title: "Configure original-metadata sidecar format and per-target policy; fail closed without an archive"
area: [FlacConvert]
status: superseded
supersedes: [K-015, K-028]
superseded_by: K-038
created: 2026-11-12
reviewed: 2026-11-12
cites:
  - path: FlacConvert/include/AppSettings.h
    symbol: MetadataSidecarSettings.Resolve
  - path: FlacConvert/src/FlacMetadata.cpp
    symbol: FlacMetadata.Save
  - path: FlacConvert/src/MediaConvertionTask.cpp
    symbol: MediaConvertionTask.ConvertFile
---

**Fact:** For MP3 targets and native FLAC sources, conversion archives the original FLAC marker, every metadata block and recognized leading/trailing tags byte-for-byte (`FlacMetadata::Save`). `config.json` `MetadataSidecars.Format` selects `binary` (`<audio>.flac-metadata`, default) or `json` (`<audio>.flac-metadata.json`: readable scan-schema metadata, informational only, plus the archive in Base64, which `FlacMetadata::Verify` checks). `ConvertToFlac` / `ConvertToMp3` are `disabled`, `optional` (written by default; menu prompt or CLI `--no-metadata-sidecar` turns it off) or `required` (default; a disable request is an error). `MetadataSidecarSettings::Resolve` sets `ConversionOptions::sidecarFormat` before planning. Without an archive `PreservationAudit::Compare` treats otherwise `SidecarOnly` losses (opaque blocks, repeated tags, chapters, unsupported pictures) as fatal. Only menu options 1/6 and CLI `convert` write sidecars.

**Why it matters:** ID3 and FFmpeg mapping cannot carry every FLAC structure; the sidecar is the lossless preservation record when converting between formats. Disabling it must never silently drop metadata, so the audit fails instead. Source checksums/seek data in the archive describe the original, not the converted audio. Supersedes the always-on statements in K-015 and K-028.

**Rejected:** A readable-only JSON sidecar (ffprobe-style) loses repeated fields and opaque blocks, so JSON embeds the exact archive. A single global on/off switch cannot express targets that must always archive. Making optional/disabled runs tolerate unarchived loss would hide metadata loss.
