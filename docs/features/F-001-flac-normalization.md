# F-001: Convert a library to 16-bit / 44.1 kHz FLAC

Status: active

## Behaviour
Converts every supported audio file (`.flac .ape .dsf .dff .dsd .wv .wav .m2ts .m4a`) under a source folder to
16-bit / 44.1 kHz FLAC with triangular dither, in a separate tree `<root>/FLAC/<source folder name>/`. Every written
folder is complete: other regular files there are copied unchanged, including unsupported audio such as MP3/AAC/Ogg
beside converted tracks, and folders without audio (artwork, scans, empty folders) are retained. A folder subtree
containing audio but nothing to convert is skipped entirely and listed in `conversion-audit.json`; a library without
conversions writes only that report. Folder names are never changed. Native FLAC sources get an original-metadata
sidecar when enabled by `MetadataSidecars` (binary by default, JSON optional; both target policies default to
disabled, F-007).
Convertible sources below 16-bit or 44.1 kHz are rejected; they are not silently dropped or copied as fallback.
AAC `.m4a` sources are exempt only from the bit-depth check because AAC does not expose a meaningful source PCM bit
depth; they still require a verified sample rate of at least 44.1 kHz and produce a warning that lossy-to-FLAC
conversion cannot restore lost quality.
Without `--overwrite`, existing targets are never replaced. Exit code 0 only if every planned item was preserved.
The console reports conversion start, coarse progress and completion, along with meaningful warnings/errors. Before
converting tracks in a different folder, it shows the full source album folder path; root-level tracks and disc
subfolders use their own containing folder. It also signals when audio conversion ends and unchanged files are being
copied. Per-file success details and expected generated encoder/vendor normalization are available at debug level in
the session log (F-010); other preservation differences remain warnings, and `conversion-audit.json` retains every
check (F-003).

## Entry points
- `FlacConvert convert <input> [--output-dir] [--overwrite]` → `FlacConvertMain.cpp#RunWithArguments` →
  `FolderConvert.cpp#FolderConvert::ConvertAudioFolderToOutput`
- Menu option 1 → `FlacConvertMain.cpp#ConvertFLACToFLAC` → `FlacConvertMain.cpp#ConvertConfiguredLibraries`
- Per file → `MediaConvertionTask.cpp#MediaConvertionTask::ConvertFile`, `MediaConvertionTask.cpp#conversionArguments`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Key flows / Conversion; Contracts and invariants
- K-041 install after success, K-048 codec-aware AAC quality gate, K-056 FFmpeg argument vector, K-038 sidecar policy,
  K-032 configured vs canonical paths, K-026/K-031 output tree layout, K-036 complete folders, skipped unchanged folders

## Acceptance checks
- [ ] Source tree byte-identical before and after (hash all files).
- [ ] Every output FLAC probes as 16-bit, 44100 Hz, same channel count, duration within 0.15 s.
- [ ] A 24-bit/96 kHz source converts; an 8-bit or 22.05 kHz source is rejected with a non-zero exit.
- [ ] AAC `.m4a` at 44.1 kHz converts with a lossy-quality warning; AAC below 44.1 kHz is rejected.
- [ ] Re-running without `--overwrite` on an existing output fails without changing it.
- [ ] Non-ASCII and >260-character paths convert on Windows.
- [ ] A near-limit Windows output directory succeeds when marker and temporary filenames would exceed `MAX_PATH`.
- [ ] Menu conversion accepts extended-prefix output paths with an FFmpeg launcher shim (see DESIGN's output anchoring).
- [ ] Replacing an output directory after validation cannot redirect writes into the source library.
- [ ] `conversion-audit.json` exists with `status: completed` (see F-003).
- [ ] Before converting a folder's tracks, the console identifies the current source album folder without a line per track.
- [ ] Console output stays concise while the session log retains detailed per-file and preservation diagnostics (F-010).
- [ ] A written folder retains unsupported audio, extensionless files and audio-free/empty subfolders byte-for-byte.
- [ ] A folder with audio but no conversions is absent from output and listed once (topmost) with its counts.
- [ ] Libraries without conversions write only the report (listing `"."`) without launching the encoder.
- [ ] Copy failure retains the failure marker/report and reports unattempted copies as not installed.
- [ ] File/directory collisions and directory links fail before any output is written.
- [ ] Explicit `--overwrite` writes a formerly skipped folder completely once it gains a conversion.

## Known limitations
- Sequential; stops at the first error, earlier outputs remain (K-024).
- Lossy sources (`.m4a` AAC) are re-encoded to FLAC without quality gain; the source-rate gate still applies.
- Skipping is per folder subtree: an MP3-only `CD2` beside a converted `CD1` is skipped and only listed.
- Directory links/junctions, broken links and special filesystem entries fail explicitly. Regular-file links are
  materialized as file contents; this is not a link/attribute-preserving filesystem backup.
- Plans describe discovery-time source contents. Existing unrelated output files are retained, not synchronized away.
- Linux builds are not verified until the Ubuntu CI workflow passes; macOS has no build preset yet.
