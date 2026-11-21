# F-005: Find duplicate albums

Status: active

## Behaviour
Loads generated album JSON v2 (and legacy ffprobe-style batches) and finds duplicate albums that share a normalized
album name (case, accents and punctuation ignored; most common track tag, folder name fallback), a MusicBrainz release
ID, or a stored FLAC MD5. Tracks are paired by MD5, disc/track number, title, then duration within the larger of
`DuplicateDurationToleranceSeconds` and `SizeMatchPercentageThreshold` percent (the legacy name does not mean file
size). Pairing maximizes the number of matched tracks; its deterministic search order prefers stronger evidence and
closer durations. Each match is graded exact, strong, probable or partial with its evidence, and matches are grouped so three
copies form one group. Different artists are rejected unless MusicBrainz or MD5 agree. The same physical folder loaded
twice is compared once; if its catalog content differs between the two loads (track names or full scanned records,
ignoring the library-relative path), a warning is printed. Fuzzy names, edition-text stripping and partial matches (unequal track counts, minimum
coverage) are opt-in. Matched tracks are then re-probed, with concurrent reads bounded by `MetadataScanMaxThreads`
(0 = hardware concurrency), and stream-property/tag comparisons logged per pair. Added and
missing tracks are reported, not substituted into the comparison. For v2 records, changes in size or available
modification time are flagged, but do not prove file identity; legacy records do not have this check. Candidate
selection reflects the scan, not subsequent changes. The CLI remains read-only.

The decision-focused console output includes a group recap immediately before the menu choice, with album numbers
matching the move menu. It shows current album/release/artist/date/edition/label/catalog/MusicBrainz tags (including
variants across tracks), scanned/current/readable track counts, format/resolution variants, and total bytes and
duration for the readable scanned tracks only. Each direct pair starts with a prominent banner marking its scan-time
rule-based confidence and showing the relative scale; the active label uses normal-intensity black text with a distinct background colour on
interactive terminals. Redirected logs use the same plain-text banner, with no colour codes in the confidence line.
Exact/strong/probable are ordered tiers; partial is called out separately as coverage-only. The label is not a
probability. Each direct pair also shows confidence/reasons, matched coverage,
pairing methods, stored FLAC MD5 agreement/conflicts and recorded decoded-PCM hashes when available.
Track details always show current codec, sample rate, declared bit depth, channels/layout, reported bitrate, duration
delta/tolerance and file bytes, even when equal. Filenames, titles and disc/track tags expose reordered mappings;
unmatched tracks and missing paths are explicit. Current container and first-audio-stream tag differences are
printed without truncating values; equal tags are counted rather than repeatedly dumped. Multiple audio streams are
warned; artwork is not treated as audio.
Scan-only artwork descriptors, integrity statuses/errors and findings are summarized separately from current probes.
Changed/missing/added files trigger a rescan warning; added tracks remain uncompared.
Other immediate files/folders (such as artwork, booklets and nested discs) are listed as uncompared; inspect them
before a whole-folder move.
Unknown values are not proof of equality. Exact confidence means equal stored FLAC MD5s, not byte-identical files or
fresh verification. Higher bitrate/resolution/size or a lossless format does not establish original source quality.
No signal-quality analysis, automatic quality ranking or keep/delete recommendation is performed.
Malformed album/track structures or catalog records without usable audio technical metadata fail JSON loading rather
than being silently skipped. Manifest-marked scans validate the complete batch set, batch hashes, and referenced
metadata archives separately for each input directory before matching across all active libraries.
Split album fragments are validated and reassembled before matching; incomplete or
inconsistent fragments fail the operation. Legacy batches without a manifest ID remain readable. With `--open` (CLI)
or once for the current group in the menu, the folders are opened in the file manager. `--report <file>` writes a
new JSON report with statistics, aliases, groups, evidence and track pairings; an existing file is never overwritten.
The report must be outside every source library. Legacy input without trusted library-root metadata cannot be reported.

Menu option 3 pauses after each group. Press Enter (or type `CONTINUE`) for the next group, or `STOP` to finish;
EOF also stops safely. Choose an album number (while more than one album remains) to preview moving that entire album
folder, including artwork, playlists and subfolders, to a unique folder under `OutDirectory/deleted`. Only typing
`MOVE` at the second confirmation
executes the move. The complete copy is verified byte-for-byte before originals are removed, including when output
is on a different drive. No existing destination is overwritten. A failed copy leaves originals intact;
a cleanup failure stops review and retains the verified copy, possibly with partial originals.
Each move has a `move.json` receipt (`copying`, `verified`, or `completed`). The group list is shown again after a move;
direct match relationships are displayed, and a move that would disconnect the remaining group is refused. Later
pairs involving the moved folder or its descendants are skipped.
Library roots, nested/aliased pairs, album links and linked contents, and source/output overlap are refused.
Catalog JSON and databases are not changed by a move: rerun option 2 afterward. Option 4 reimports current albums
but retains database albums absent from the import; it does not automatically purge moved albums.

## Entry points
- `FlacConvert duplicates <json dir> [--open] [--report <file>]` → `FlacConvertMain.cpp#RunWithArguments`
- Menu option 3 → `FlacConvertMain.cpp#ScanFolderProcessJSONAndFindDuplicates`
- `DuplicateMatcher.cpp#DuplicateMatcher::FindDuplicates`
- `DuplicateComparison.cpp#DuplicateComparison::DescribeAlbum`, `DuplicateComparison.cpp#DuplicateComparison::DescribePair`
- `DuplicateReview.cpp#DuplicateReview::ReviewGroup`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Duplicates
- Knowledge: K-052 (explicit confirmation for each quarantined album); K-055 (preserve direct-match connectivity)
- Related config: `MinMatchingTracksForDuplicate`, `SizeMatchPercentageThreshold`, `DuplicateDurationToleranceSeconds`,
  `DuplicateFuzzyNames`, `DuplicateStripEditionText`, `DuplicatePartialMatches`, `DuplicatePartialMinCoverage`; `ExtraAudioQualityMetrics`
  currently warns and is ignored.

## Acceptance checks
- [ ] Two copies of the same album in different folders are reported once; three copies form one group of three pairs.
- [ ] An album folder loaded twice through overlapping libraries is not reported as its own duplicate.
- [ ] Accent/case differences and reordered tracks still match; pairing maximizes matched tracks when duration
      tolerances overlap; unequal track counts match only with partial matching.
- [ ] Same album name with different artists is not reported unless MusicBrainz or FLAC MD5 agree.
- [ ] `--report` writes the groups and track pairings, refuses an existing report file and any path inside a source
      library, and fails safely for legacy inputs without trusted library roots.
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
- [ ] Console recap album numbers match the menu; coverage and scan-only evidence are distinct from current properties.
- [ ] Each candidate highlights its active rule-based confidence; partial is labeled coverage-only, and redirected
      confidence output contains no ANSI colour codes.
- [ ] Equal technical properties remain visible; unknown bit depth is not printed as zero-bit or declared equal.
- [ ] Full differing tags, including long UTF-8 values and first-audio-stream tags, are readable; tag controls are escaped.
- [ ] Stale/missing/added files, unmatched tracks, hash conflicts and integrity findings are visible before a move choice.
- [ ] Menu review waits for a valid choice after each group; EOF/STOP does not process further groups, and bridge
      removal cannot split the remaining direct-match graph.
- [ ] Only `MOVE` after selecting an album number relocates it; the last remaining album cannot be selected; removing a
      bridge album that disconnects the remaining direct-match graph is refused; all bytes and nested/empty folders
      are preserved.
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
