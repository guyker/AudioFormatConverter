# F-002: Convert FLAC to MP3

Status: active

## Behaviour
Converts `.flac` files under a source folder to LAME VBR MP3 (`-q:a 0`–`9`, default 0) in
`<root>/MP3/<source folder name>/`, preserving tags (ID3v2.4, no ID3v1), cover art and chapters. 44.1/48 kHz sources
keep their rate; others are resampled to 48 kHz. Multichannel sources are rejected unless downmix is enabled. A
`.flac-metadata` sidecar beside each MP3 preserves the original FLAC metadata byte-for-byte. Non-FLAC audio is
reported as skipped; companions are copied.

## Entry points
- `FlacConvert convert <input> --to mp3 [--mp3-quality N] [--downmix-to-stereo] [--overwrite]` →
  `FlacConvertMain.cpp#RunWithArguments`
- Menu option 6 → `FlacConvertMain.cpp#ConvertFLACToMP3` (prompts in `FlacConvertMain.cpp#ConfigureManualMp3`)
- `include/ConversionOptions.h#ConversionOptions`, `MediaConvertionTask.cpp#conversionArguments`,
  `FlacMetadata.cpp#FlacMetadata::Save`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Conversion; Files and schemas (sidecar)
- K-015/K-028 metadata sidecar, K-016 channel policy, K-017 MP3 encoding settings, K-025 per-operation menu options,
  K-029 preservation audit

## Acceptance checks
- [ ] `python tests\mp3_conversion.py --converter x64\Debug\FlacConvert.exe --ffmpeg <ffmpeg.exe> --ffprobe <ffprobe.exe>` passes.
- [ ] Output probes as `mp3`, rate 44100/48000, channels = min(source, 2) only when downmix is set.
- [ ] Sidecar passes `FlacMetadata::Verify` against the source.
- [ ] 5.1 source without downmix fails; with downmix produces stereo.
- [ ] `--mp3-quality 10` is rejected by the CLI (`CommandLine.cpp#ParseCommandLine`).

## Known limitations
- Requires an `ffmpeg` build with `libmp3lame`.
- Only FLAC input; other lossless formats are skipped, not converted.
