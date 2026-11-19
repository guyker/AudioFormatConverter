# F-005: Find duplicate albums

Status: active

## Behaviour
Loads generated album JSON v2 (and legacy ffprobe-style batches) and finds duplicate albums that share a normalized
album name (case, accents and punctuation ignored; most common track tag, folder name fallback), a MusicBrainz release
ID, or a stored FLAC MD5. Tracks are paired by MD5, disc/track number, title, then duration within the larger of
`DuplicateDurationToleranceSeconds` and `SizeMatchPercentageThreshold` percent (the legacy name does not mean file
size). Each match is graded exact, strong, probable or partial with its evidence, and matches are grouped so three
copies form one group. Different artists are rejected unless MusicBrainz or MD5 agree. The same physical folder loaded
twice is compared once. Fuzzy names, edition-text stripping and partial matches (unequal track counts, minimum
coverage) are opt-in. Matched tracks are then re-probed and stream-property/tag comparisons logged per pair. Added and missing tracks are reported,
not substituted into the comparison. For v2 records, changes in size or available modification time are flagged, but do
not prove file identity; legacy records do not have this check. Candidate selection reflects the scan, not subsequent
changes. Signal-quality
metrics are not currently computed; the report explicitly says analysis was not run. The CLI remains read-only.
Malformed album/track structures or catalog records without usable audio technical metadata fail JSON loading rather
than being silently skipped. Manifest-marked scans validate the complete batch set, batch hashes, and referenced
metadata archives separately for each input directory before matching across all active libraries.
Split album fragments are validated and reassembled before matching; incomplete or
inconsistent fragments fail the operation. Legacy batches without a manifest ID remain readable. With `--open` (CLI)
or once for the current group in the menu, the folders are opened in the file manager. `--report <file>` writes a
new JSON report with statistics, aliases, groups, evidence and track pairings; an existing file is never overwritten.

Menu option 3 pauses after each group. Press Enter (or type `CONTINUE`) for the next group, or `STOP` to finish;
EOF also stops safely. Choose an album number (while more than one album remains) to preview moving that entire album folder, including artwork, playlists,
and subfolders, to a unique folder under `OutDirectory/deleted`. Only typing `MOVE` at the second confirmation
executes the move. The complete copy is verified byte-for-byte before originals are removed, including when output
is on a different drive. No existing destination is overwritten. A failed copy leaves originals intact;
a cleanup failure stops review and retains the verified copy, possibly with partial originals.
Each move has a `move.json` receipt (`copying`, `verified`, or `completed`). The group list is shown again after a move;
later pairs involving the moved folder or its descendants are skipped.
Library roots, nested/aliased pairs, album links and linked contents, and source/output overlap are refused.
Catalog JSON and databases are not changed by a move: rerun option 2 afterward. Option 4 reimports current albums
but retains database albums absent from the import; it does not automatically purge moved albums.

## Entry points
- `FlacConvert duplicates <json dir> [--open] [--report <file>]` → `FlacConvertMain.cpp#RunWithArguments`
- Menu option 3 → `FlacConvertMain.cpp#ScanFolderProcessJSONAndFindDuplicates`
- `DuplicateMatcher.cpp#DuplicateMatcher::FindDuplicates`, `MediaTrack.cpp#MediaTrack::CompareAudioTracks`
- `DuplicateReview.cpp#DuplicateReview::ReviewGroup`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Duplicates
- Knowledge: K-052 (explicit per-pair quarantine instead of automatic deletion)
- Related config: `MinMatchingTracksForDuplicate`, `SizeMatchPercentageThreshold`, `DuplicateDurationToleranceSeconds`,
  `DuplicateFuzzyNames`, `DuplicateStripEditionText`, `DuplicatePartialMatches`, `DuplicatePartialMinCoverage`; `ExtraAudioQualityMetrics`
  currently warns and is ignored.

## Acceptance checks
- [ ] Two copies of the same album in different folders are reported once; three copies form one group of three pairs.
- [ ] An album folder loaded twice through overlapping libraries is not reported as its own duplicate.
- [ ] Accent/case differences and reordered tracks still match; unequal track counts match only with partial matching.
- [ ] Same album name with different artists is not reported unless MusicBrainz or FLAC MD5 agree.
- [ ] `--report` writes the groups and track pairings and refuses an existing report file.
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
- [ ] Only `MOVE` after selecting an album number relocates it; the last remaining album of a group cannot be selected; with all bytes and nested/empty folders preserved.
- [ ] Cancellation, invalid choices, copy failures, and unsafe move paths leave originals unchanged.
- [ ] Source-cleanup failure retains the complete verified copy and stops review.
- [ ] Moving an album skips later pairs involving it or its descendants without probing nonexistent paths.
- [ ] Repeated names use separate destinations; existing deleted output and the kept album remain unchanged.
- [ ] A `deleted` output alias into any active library is refused without writing into that library.

## Known limitations
- Menu results are logged only; use the CLI `--report` for a JSON report.
- Stored FLAC MD5s are trusted as recorded; they are not re-verified by decoding. No acoustic fingerprinting.
- Requires the source folders recorded in JSON to still exist.
- Menu moves require enough free output space for a complete album copy; originals are removed only after verification.
- No automatic restore or catalog rewrite is performed; inspect `move.json` after interruption. Do not edit folders during a move.
