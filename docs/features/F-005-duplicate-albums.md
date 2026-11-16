# F-005: Find duplicate albums

Status: active

## Behaviour
Loads generated album JSON v2 (and legacy ffprobe-style batches), finds album pairs with matching track counts and
track durations/sizes within `SizeMatchPercentageThreshold`, then re-probes both folders and logs format/tag
comparisons per pair. Signal-quality
metrics are not currently computed; the report explicitly says analysis was not run. Read-only.
Malformed album/track structures or catalog records without usable audio technical metadata fail JSON loading rather
than being silently skipped. With `--open` (CLI) or always in the menu, both folders are opened in the file manager.

## Entry points
- `FlacConvert duplicates <json dir> [--open]` → `FlacConvertMain.cpp#RunWithArguments`
- Menu option 3 → `FlacConvertMain.cpp#ScanFolderProcessJSONAndFindDuplicates`
- `AlbumCollection.cpp#AlbumCollection::FindDuplicateAlbums`, `MediaTrack.cpp#MediaTrack::CompareAudioTracks`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Duplicates
- Related config: `MinMatchingTracksForDuplicate`, `SizeMatchPercentageThreshold`; `ExtraAudioQualityMetrics`
  currently warns and is ignored.

## Acceptance checks
- [ ] Two copies of the same album in different folders are reported once.
- [ ] No files are created or changed.
- [ ] Without `--open`, no folder is opened.
- [ ] Empty JSON directory exits non-zero.
- [ ] Malformed track metadata fails loading instead of producing partial duplicate results.
- [ ] If source track counts have changed since scanning (including an empty folder), comparison reports the mismatch without crashing or changing files.

## Known limitations
- Results are logged, not written to a file.
- Requires the source folders recorded in JSON to still exist.
