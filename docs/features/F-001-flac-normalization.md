# F-001: Convert a library to 16-bit / 44.1 kHz FLAC

Status: active

## Behaviour
Converts every supported audio file (`.flac .ape .dsf .dff .dsd .wv .wav .m2ts .m4a`) under a source folder to
16-bit / 44.1 kHz FLAC with triangular dither, in a separate tree `<root>/FLAC/<source folder name>/`. Every written
folder is complete: other regular files there are copied unchanged, including unsupported audio such as MP3/AAC/Ogg
beside converted tracks, and folders without audio (artwork, scans, empty folders) are retained. A folder subtree
containing audio but nothing to convert is skipped entirely and listed in `conversion-audit.json`; a library without
conversions writes only that report. Folder names are never changed.
Convertible sources below 16-bit or 44.1 kHz are rejected; they are not silently dropped or copied as fallback.
Without `--overwrite`, existing targets are never replaced. Exit code 0 only if every planned item was preserved.

## Entry points
- `FlacConvert convert <input> [--output-dir] [--overwrite]` → `FlacConvertMain.cpp#RunWithArguments` →
  `FolderConvert.cpp#FolderConvert::ConvertAudioFolderToOutput`
- Menu option 1 → `FlacConvertMain.cpp#ConvertFLACToFLAC` → `FlacConvertMain.cpp#ConvertConfiguredLibraries`
- Per file → `MediaConvertionTask.cpp#MediaConvertionTask::ConvertFile`, `MediaConvertionTask.cpp#conversionArguments`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Key flows / Conversion; Contracts and invariants
- K-003 install after success, K-013 CD-quality gate, K-014 FFmpeg argument vector, K-015/K-028 sidecar,
  K-032 configured vs canonical paths, K-026/K-031 output tree layout, K-036 complete folders, skipped unchanged folders

## Acceptance checks
- [ ] Source tree byte-identical before and after (hash all files).
- [ ] Every output FLAC probes as 16-bit, 44100 Hz, same channel count, duration within 0.15 s.
- [ ] A 24-bit/96 kHz source converts; an 8-bit or 22.05 kHz source is rejected with a non-zero exit.
- [ ] Re-running without `--overwrite` on an existing output fails without changing it.
- [ ] Non-ASCII and >260-character paths convert on Windows.
- [ ] `conversion-audit.json` exists with `status: completed` (see F-003).
- [ ] A written folder retains unsupported audio, extensionless files and audio-free/empty subfolders byte-for-byte.
- [ ] A folder with audio but no conversions is absent from output and listed once (topmost) with its counts.
- [ ] Libraries without conversions write only the report (listing `"."`) without launching the encoder.
- [ ] Copy failure retains the failure marker/report and reports unattempted copies as not installed.
- [ ] File/directory collisions and directory links fail before any output is written.
- [ ] Explicit `--overwrite` writes a formerly skipped folder completely once it gains a conversion.

## Known limitations
- Sequential; stops at the first error, earlier outputs remain (K-024).
- Lossy sources (`.m4a` AAC) are re-encoded to FLAC without quality gain.
- Skipping is per folder subtree: an MP3-only `CD2` beside a converted `CD1` is skipped and only listed.
- Directory links/junctions, broken links and special filesystem entries fail explicitly. Regular-file links are
  materialized as file contents; this is not a link/attribute-preserving filesystem backup.
- Plans describe discovery-time source contents. Existing unrelated output files are retained, not synchronized away.
- Linux builds are not verified until the Ubuntu CI workflow passes; macOS has no build preset yet.
