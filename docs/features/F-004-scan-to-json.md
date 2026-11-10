# F-004: Scan libraries to JSON metadata

Status: active

## Behaviour
Discovers album folders under a library, probes each `.flac`/`.mp3` track and writes batches of up to
`AlbumsSplitThreshold` albums to `OutDirectory/<name>/media_out_<name>[_<n>].json`. Previous generated JSON is
replaced per library through staging, backup and rollback; databases and sources are not touched. Parse problems are
written to `*_error.json` / `*_ffmpeg_error.json`.

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
- [ ] Output directory overlapping the source library is rejected.
- [ ] Batch count = ceil(albums / `AlbumsSplitThreshold`).

## Known limitations
- Only `.flac` and `.mp3` tracks are recorded.
- `UseFFmpegLibraryAPI=false` uses the ffprobe executable through a shell (see DESIGN contracts).
