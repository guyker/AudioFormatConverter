# F-004: Scan libraries to JSON metadata

Status: active

## Behaviour
Discovers album folders under a library, probes each `.flac`/`.mp3` track (extension matching is case-insensitive).
When asynchronous metadata reads are enabled, each album uses a bounded worker group. A positive
`MetadataScanMaxThreads` sets the cap; the default value `0` selects hardware concurrency (falling back to 4).
The scan writes batches of up to `AlbumsSplitThreshold` albums to
`OutDirectory/<name>/media_out_<name>[_<n>].json`. Previous generated JSON is
replaced per library through staging, backup and rollback; databases and sources are not touched. Scan failures log
the track path and reason; captured FFmpeg diagnostics can accompany a successfully saved batch as
`*_ffmpeg_error.json`. JSON preserves format and stream metadata, stream dispositions, and chapter timing/tags. If
any accepted track has an extraction/parse error or no usable audio stream, the scan fails before installing staged
JSON, leaving the previous generated files unchanged.

## Entry points
- `FlacConvert scan <library> [--output-dir]` → `FlacConvertMain.cpp#RunWithArguments`
- Menu option 2 → `FlacConvertMain.cpp#ScanFolderAndCreateJSON`, `FlacConvertMain.cpp#installGeneratedFiles`
- `AlbumCollection.cpp#AlbumCollection::LoadAlbumsCo`, `AlbumCollection.cpp#AlbumCollection::ImportMetadata`,
  `AlbumCollection.cpp#AlbumCollection::SaveAlbumsToJSON`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Scan to JSON; Files and schemas (album JSON)
- K-005 staged JSON replacement

## Acceptance checks
- [ ] Output JSON parses and contains one object per album with non-empty `Tracks`.
- [ ] A failed scan leaves the previous `media_out_*.json` files unchanged.
- [ ] Supported extensions are matched case-insensitively; chapters and attached-picture disposition are retained.
- [ ] A malformed or unusable accepted track fails the scan instead of installing an incomplete catalog.
- [ ] Output directory overlapping the source library is rejected.
- [ ] Batch count = ceil(albums / `AlbumsSplitThreshold`).
- [ ] With async metadata reads enabled, scan succeeds when the configured worker cap is lower than the album's track count.

## Known limitations
- Only `.flac` and `.mp3` tracks are recorded.
- `UseFFmpegLibraryAPI=false` uses the ffprobe executable with a separate argument vector and captures its output.
