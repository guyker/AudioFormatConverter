# F-005: Find duplicate albums

Status: active

## Behaviour
Loads generated album JSON v2 (and legacy ffprobe-style batches), finds album pairs with matching track counts and
track durations within `SizeMatchPercentageThreshold` (the legacy setting name does not mean file size), then
re-probes the cataloged tracks and logs stream-property/tag comparisons per pair. Added and missing tracks are reported,
not substituted into the comparison. For v2 records, changes in size or available modification time are flagged, but do
not prove file identity; legacy records do not have this check. Candidate selection reflects the scan, not subsequent
changes. Signal-quality
metrics are not currently computed; the report explicitly says analysis was not run. The CLI remains read-only.
Malformed album/track structures or catalog records without usable audio technical metadata fail JSON loading rather
than being silently skipped. Manifest-marked scans validate the complete batch set, batch hashes, and referenced
metadata archives separately for each input directory before matching across all active libraries.
Split album fragments are validated and reassembled before matching; incomplete or
inconsistent fragments fail the operation. Legacy batches without a manifest ID remain readable. With `--open` (CLI)
or once for the current pair in the menu, both folders are opened in the file manager.

Menu option 3 pauses after each pair. Press Enter (or type `CONTINUE`) for the next pair, or `STOP` to finish;
EOF also stops safely. Choose `1` or `2` to preview moving that entire album folder, including artwork, playlists,
and subfolders, to a unique folder under `OutDirectory/deleted`. Only typing `MOVE` at the second confirmation
executes the move. The complete copy is verified byte-for-byte before originals are removed, including when output
is on a different drive. No existing destination is overwritten. A failed copy leaves originals intact;
a cleanup failure stops review and retains the verified copy, possibly with partial originals.
Each move has a `move.json` receipt (`copying`, `verified`, or `completed`). The review waits again after a move;
later pairs involving the moved folder or its descendants are skipped.
Library roots, nested/aliased pairs, album links and linked contents, and source/output overlap are refused.
Catalog JSON and databases are not changed by a move: rerun option 2 afterward. Option 4 reimports current albums
but retains database albums absent from the import; it does not automatically purge moved albums.

## Entry points
- `FlacConvert duplicates <json dir> [--open]` → `FlacConvertMain.cpp#RunWithArguments`
- Menu option 3 → `FlacConvertMain.cpp#ScanFolderProcessJSONAndFindDuplicates`
- `AlbumCollection.cpp#AlbumCollection::FindDuplicateAlbums`, `MediaTrack.cpp#MediaTrack::CompareAudioTracks`
- `DuplicateReview.cpp#DuplicateReview::ReviewPair`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Duplicates
- Knowledge: K-052 (explicit per-pair quarantine instead of automatic deletion)
- Related config: `MinMatchingTracksForDuplicate`, `SizeMatchPercentageThreshold`; `ExtraAudioQualityMetrics`
  currently warns and is ignored.

## Acceptance checks
- [ ] Two copies of the same album in different folders are reported once.
- [ ] CLI duplicates and menu review without a confirmed move create or change no files.
- [ ] CLI without `--open` opens no folders.
- [ ] Empty JSON directory exits non-zero.
- [ ] Malformed track metadata fails loading instead of producing partial duplicate results.
- [ ] Missing, extra, or modified manifest-marked batches and missing/corrupt referenced archives fail before matching.
- [ ] Menu option 3 accepts multiple libraries with separate manifests and rejects a missing or changed batch in either library before matching.
- [ ] Unreadable re-probed tracks fail the command rather than producing a successful comparison.
- [ ] Compilations with the same track artists and different filename order remain candidates; nested album tracks are not included in their parent comparison.
- [ ] If source track counts have changed since scanning (including an empty folder), comparison reports the mismatch without crashing or changing files.
- [ ] Renamed or added tracks are not substituted for cataloged tracks; a missing track does not shift later comparisons.
- [ ] Menu review waits for a valid choice after each pair; EOF/STOP does not process further pairs.
- [ ] Only `MOVE` after selecting album 1 or 2 relocates it, with all bytes and nested/empty folders preserved.
- [ ] Cancellation, invalid choices, copy failures, and unsafe move paths leave originals unchanged.
- [ ] Source-cleanup failure retains the complete verified copy and stops review.
- [ ] Moving an album skips later pairs involving it or its descendants without probing nonexistent paths.
- [ ] Repeated names use separate destinations; existing deleted output and the kept album remain unchanged.
- [ ] A `deleted` output alias into any active library is refused without writing into that library.

## Known limitations
- Results are logged, not written to a file.
- Requires the source folders recorded in JSON to still exist.
- Menu moves require enough free output space for a complete album copy; originals are removed only after verification.
- No automatic restore or catalog rewrite is performed; inspect `move.json` after interruption. Do not edit folders during a move.
