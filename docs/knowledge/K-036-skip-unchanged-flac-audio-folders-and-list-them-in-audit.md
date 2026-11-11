---
id: K-036
kind: decision
title: "Skip unchanged FLAC audio folders and list them in the audit"
area: [FlacConvert]
status: active
created: 2026-11-11
reviewed: 2026-11-11
cites:
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.PrepareConversion
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: writeAudit
  - path: tests/mp3_conversion.py
    symbol: ConversionTests.test_flac_skips_unchanged_audio_folders_and_keeps_written_folders_complete
---

**Fact:** In FLAC mode `FolderConvert::PrepareConversion` counts files, audio and conversions per source subtree.
The topmost subtrees that contain audio but no conversions are omitted from output and recorded in
`skippedUnchangedDirectories` of `conversion-audit.json` (`"."` when the whole library has no conversions, in which
case only the report is written). Every other folder is written complete: unsupported audio beside conversions and
audio-free folders (artwork, scans, empty folders) are copied unchanged. Folder names are never changed. The cited
integration case verifies the exact output inventory, the skipped list and unchanged sources.

**Why it matters:** The user does not want output folders with missing items, nor marker files inside the library.
Untouched albums are therefore absent and discoverable through the report, while converted albums keep all their
files. The rule is audio-based so `Album\Artwork` and other audio-free subfolders stay with a converted album.
Known limitation: an MP3-only `CD2` next to a converted `CD1` is skipped and only listed.

**Rejected:** `COPIED_UNCHANGED.txt` markers inside copied folders (user rejected clutter in the library); copying
unchanged folders into a separate tree (splits libraries and mixed subtrees); renaming/prefixing folders (breaks
cue/playlist references); skipping every subtree without conversions (drops artwork and empty folders of converted
albums); silently dropping unsupported audio beside conversions (incomplete albums).
