# F-001: Convert a library to 16-bit / 44.1 kHz FLAC

Status: active

## Behaviour
Converts every supported audio file (`.flac .ape .dsf .dff .dsd .wv .wav .m2ts .m4a`) under a source folder to
16-bit / 44.1 kHz FLAC with triangular dither, in a separate tree `<root>/FLAC/<source folder name>/`. Non-audio
companion files are copied unchanged. Sources below 16-bit or 44.1 kHz are rejected. Without `--overwrite`, existing
targets are never replaced. Exit code 0 only if every planned file was installed.

## Entry points
- `FlacConvert convert <input> [--output-dir] [--overwrite]` → `FlacConvertMain.cpp#RunWithArguments` →
  `FolderConvert.cpp#FolderConvert::ConvertAudioFolderToOutput`
- Menu option 1 → `FlacConvertMain.cpp#ConvertFLACToFLAC` → `FlacConvertMain.cpp#ConvertConfiguredLibraries`
- Per file → `MediaConvertionTask.cpp#MediaConvertionTask::ConvertFile`, `MediaConvertionTask.cpp#conversionArguments`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Key flows / Conversion; Contracts and invariants
- K-003 install after success, K-013 CD-quality gate, K-014 FFmpeg argument vector, K-015/K-028 sidecar,
  K-032 configured vs canonical paths, K-026/K-031 output tree layout

## Acceptance checks
- [ ] Source tree byte-identical before and after (hash all files).
- [ ] Every output FLAC probes as 16-bit, 44100 Hz, same channel count, duration within 0.15 s.
- [ ] A 24-bit/96 kHz source converts; an 8-bit or 22.05 kHz source is rejected with a non-zero exit.
- [ ] Re-running without `--overwrite` on an existing output fails without changing it.
- [ ] Non-ASCII and >260-character paths convert on Windows.
- [ ] `conversion-audit.json` exists with `status: completed` (see F-003).

## Known limitations
- Sequential; stops at the first error, earlier outputs remain (K-024).
- Lossy sources (`.m4a` AAC) are re-encoded to FLAC without quality gain.
- Linux/macOS build blocked by K-018.
