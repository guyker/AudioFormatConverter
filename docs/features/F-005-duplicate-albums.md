# F-005: Find duplicate albums

Status: active

## Behaviour
Loads generated album JSON v2 (and legacy ffprobe-style batches), finds album pairs with matching track counts and
track durations within `SizeMatchPercentageThreshold` (the legacy setting name does not mean file size), then
re-probes the cataloged tracks and logs stream-property/tag comparisons per pair. Added and missing tracks are reported,
not substituted into the comparison. For v2 records, changes in size or available modification time are flagged, but do
not prove file identity; legacy records do not have this check. Candidate selection reflects the scan, not subsequent
changes. Signal-quality
metrics are not currently computed; the report explicitly says analysis was not run. Read-only.
Malformed album/track structures or catalog records without usable audio technical metadata fail JSON loading rather
than being silently skipped. Manifest-marked scans validate the complete batch set, batch hashes, and referenced
metadata archives before matching. Split album fragments are validated and reassembled before matching; incomplete or
inconsistent fragments fail the operation. Legacy batches without a manifest ID remain readable. With `--open` (CLI)
or always in the menu, both folders are opened in the file manager.

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
- [ ] Missing, extra, or modified manifest-marked batches and missing/corrupt referenced archives fail before matching.
- [ ] Unreadable re-probed tracks fail the command rather than producing a successful comparison.
- [ ] Compilations with the same track artists and different filename order remain candidates; nested album tracks are not included in their parent comparison.
- [ ] If source track counts have changed since scanning (including an empty folder), comparison reports the mismatch without crashing or changing files.
- [ ] Renamed or added tracks are not substituted for cataloged tracks; a missing track does not shift later comparisons.

## Known limitations
- Results are logged, not written to a file.
- Requires the source folders recorded in JSON to still exist.
