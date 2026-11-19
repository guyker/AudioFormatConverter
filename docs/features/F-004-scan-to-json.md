# F-004: Scan libraries to JSON metadata

Status: active

## Behaviour
Discovers album folders under a library, including audio files directly under the library root, and reads the
case-insensitively matched extensions in `CatalogExtensions` (default `.flac` and `.mp3`) with TagLib. M4A/MP4,
Ogg/Opus, WavPack/APE, WAV and AIFF can be enabled explicitly for TagLib property extraction. Each track uses catalog schema v2:
technical properties, normalized tag arrays, raw scoped tag entries, and explicit support and analysis status. FLAC
and MP3 also expose picture descriptors; chapter boundaries and tags are read through the linked FFmpeg libraries. The contract is
`docs/schemas/media-catalog-v2.schema.json`; it does not contain picture payloads or claim byte-complete native
metadata inventory.
This is a TagLib/linked-FFmpeg catalog, not a raw FFmpeg JSON dump. Per-record non-fatal diagnostics are included
with their records; `scan-report.json` records run status, counts, timing, and path-linked diagnostics for both
successful and failed scans using `docs/schemas/scan-report-v2.schema.json`. A failed scan does not install staged
catalog JSON, preserving the previous generated JSON set; the report is written separately when the output directory
is available. Report counts distinguish discovered albums/tracks, successfully serialized albums/tracks, staged
batches, and installed batches. Discovery counts can be partial if enumeration fails. Library-level diagnostics omit
`albumPath`. Filesystem diagnostics carry an `affectedPath` and native error code/category when available; track paths also populate `filePath`. FFmpeg warnings/errors without a thread-local file context are captured
at scan scope and intentionally remain unattributed to a track.
Those warnings/errors are also written to the per-request session log, explicitly marked as unattributed; file-scoped
TagLib/FFmpeg diagnostics include their source path. TagLib's verbose debug messages are kept in the detailed log.
Redirected scan output contains no animated terminal sequences, and `scan-report.json` remains the structured
diagnostic record (F-010).
FFmpeg format and stream tags are included with separate source scopes; older incremental records are reread to
populate this inventory. Each successful scan also writes `scan-manifest.json`, which inventories every batch by
filename, size, and SHA-256. Separate byte-exact native FLAC metadata archives are optional and default to off via
`ScanFlacMetadataArchives`; menu option 2 asks for a per-run choice, and CLI `--archive-flac-metadata` or
`--no-archive-flac-metadata` overrides the configured default. Readable catalog JSON is written in either mode.
When enabled, the manifest lists verified archives; when disabled, it records the policy and lists none. Album records carry the
manifest scan ID. Duplicate detection, database import, and incremental cache reuse reject missing, extra, or changed
batches before using the records. Legacy batches without a manifest ID remain readable. Native FLAC archives retain
the original FLAC marker, metadata blocks, and recognized leading/trailing ID3/APE tags without copying audio;
when archiving is enabled, unsupported formats and counts are listed in the manifest and scan warnings. A successful
rescan retires archives owned by the previous manifest even when archiving is disabled; a failed scan preserves them.

The reader records findings for missing title/artist/album tags, malformed track/disc numbers, non-ISO dates,
malformed MusicBrainz UUIDs, ISRCs and EAN/UPC/GTIN check digits, inconsistent album-level tags within a directory,
and duplicate/gapped per-disc track numbering or inconsistent/mismatched declared totals. These findings do not
discard otherwise usable records or claim that a folder is a complete album. When asynchronous metadata reads are enabled, each album uses a bounded worker group. A positive
`MetadataScanMaxThreads` sets the cap; the default value `0` selects hardware concurrency (falling back to 4).
The scan writes batches of up to `AlbumsSplitThreshold` albums to
`OutDirectory/<name>/media_out_<name>[_<n>].json`. Previous generated JSON is replaced per library through staging,
backup and rollback; databases and sources are not touched. The RapidJSON writer escapes control characters and
omits unavailable optional fields instead of fabricating zero values. The configured `ScanTier` defaults to
`catalog`; `scan --tier catalog|integrity|analysis` selects metadata-only, full-decode integrity, or decode plus
signal-statistics analysis. Decode failures are retained in the record without discarding successfully read tags.
If discovery cannot read a directory, writing a JSON batch fails, or any accepted track has a read error or lacks usable audio technical metadata, the scan fails
before installing staged JSON, leaving the previous generated files unchanged.
Album objects include the source `"Library Root"` for stable downstream database identity. With `Incremental=true`,
unchanged records are reused only when the source path, file size, full-precision modification time match and the cached tier is the requested tier or higher;
`scan --full-rescan` forces metadata rereads.
Set `JsonBatchTargetBytes` to a positive hard byte limit; the default 0 disables size-based splitting. Albums that
exceed it are split into indexed track fragments. If one track and its envelope cannot fit, the scan fails and
preserves prior catalog files. Duplicate detection and database import validate the complete manifest inventory and
then reassemble fragment sets before use.

## Entry points
- `FlacConvert scan <library> [--output-dir] [--full-rescan] [--tier catalog|integrity|analysis] [--archive-flac-metadata|--no-archive-flac-metadata]` → `FlacConvertMain.cpp#RunWithArguments`
- Menu option 2 → `FlacConvertMain.cpp#ScanFolderAndCreateJSON`, `FlacConvertMain.cpp#installGeneratedFiles`
- `AlbumCollection.cpp#AlbumCollection::LoadAlbumsCo`, `AlbumCollection.cpp#AlbumCollection::ImportMetadata`,
  `AlbumCollection.cpp#AlbumCollection::SaveAlbumsToJSON`
- `TagLibReader.cpp#TagLibReader::Read`, `MediaRecordJson.cpp#SerializeMediaRecord`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Scan to JSON; Files and schemas (album JSON)
- Schema: `docs/schemas/media-catalog-v2.schema.json`
- Manifest schema: `docs/schemas/scan-manifest-v1.schema.json`
- Scan report schema: `docs/schemas/scan-report-v2.schema.json` (version 1 remains available for older reports)
- K-051 manifest-owned scan output replacement; K-053 opt-in FLAC scan archives; K-035 fail scans before installing incomplete metadata

## Acceptance checks
- [ ] Output JSON validates against the v2 schema and contains one object per album with non-empty `Tracks`.
- [ ] A failed scan leaves the previous `media_out_*.json` files unchanged.
- [ ] Successful scans write a completed `scan-report.json` with discovered, serialized, staged, installed, and diagnostic counts.
- [ ] Failed scans preserve prior generated JSON and identify the failing album/track plus the underlying diagnostic in `scan-report.json`.
- [ ] Successful and failed integration reports validate against the scan-report JSON Schema.
- [ ] An unreadable album directory or failed JSON write fails the scan without replacing previous batches.
- [ ] Supported extensions are matched case-insensitively.
- [ ] Repeated tag values remain separate array values; raw tag scope and ID3 frame identities are retained where available.
- [ ] Embedded FLAC/MP3 picture descriptors are present without duplicating image payload bytes; chapter timing and tags are retained when supported.
- [ ] Generated malformed numbers, dates, MBIDs, ISRCs and barcodes produce findings without dropping their audio records.
- [ ] Different album-level tags among tracks in one directory produce findings without choosing an authoritative value.
- [ ] Generated duplicate/gapped track-number fixtures and declared-total mismatches produce album findings without dropping tracks.
- [ ] Audio files directly under the library root are included.
- [ ] Control characters in tag names/values round-trip through generated JSON.
- [ ] Missing optional values remain absent; catalog-tier integrity and analysis statuses are `not_run`.
- [ ] Integrity scans report decode status, decoded samples, and file/PCM SHA-256; decode failures retain a failure position when available.
- [ ] FLAC STREAMINFO zero sample counts and all-zero MD5 signatures are treated as unknown, not corruption.
- [ ] Analysis scans emit per-channel signal measurements and report zero-padded-hi-res evidence without claiming universal quality.
- [ ] Generated sine, leading/trailing silence, and full-scale integer fixtures verify loudness, silence, and clipping measurements within tolerances.
- [ ] A malformed or unusable accepted track fails the scan instead of installing an incomplete catalog.
- [ ] A TagLib extraction failure reports the source path and underlying diagnostic.
- [ ] Session logs retain contextual TagLib/FFmpeg diagnostics without assigning unscoped errors to a track; redirected output has no terminal control sequences (F-010).
- [ ] Output directory overlapping the source library is rejected.
- [ ] On Windows, repeated scans work under near-limit and longer output paths, including toggling and retiring archives whose full paths exceed `MAX_PATH`.
- [ ] With the byte target disabled, batch count = ceil(albums / `AlbumsSplitThreshold`).
- [ ] With a byte limit, all batches fit, large albums reassemble correctly, and an oversized track fails safely.
- [ ] Archive export defaults to off; menu choice and CLI flags override the configured default without changing catalog tags.
- [ ] A successful scan writes a manifest whose batch hashes/sizes match the complete batch set and whose enabled archives verify against native FLAC sources.
- [ ] Missing or modified batches, missing archives, and a missing manifest for manifest-marked albums fail before duplicate, database, or cache use.
- [ ] With archiving enabled, unsupported formats are identified and counted without claiming byte-exact archive coverage.
- [ ] A failed scan/install preserves the previous manifest, JSON batches, and metadata archives; unrelated files and databases remain unchanged.
- [ ] A successful rescan retires only archives owned by the previous manifest, including when export is disabled.
- [ ] With `JsonBatchTargetBytes=0`, serialization streams the album wrapper and one track record at a time.
- [ ] Missing, duplicate, inconsistent or mixed album fragments are rejected before database import or duplicate detection.
- [ ] With async metadata reads enabled, scan succeeds when the configured worker cap is lower than the album's track count.
- [ ] Same-size source edits within one second invalidate incremental cache records, and album-level findings are recomputed after cache reuse.

## Known limitations
- `CatalogExtensions` defaults to `.flac` and `.mp3`; other formats require explicit opt-in.
- `rawTags` contains decoded tag entries, not byte-exact source metadata. When enabled, archives cover native FLAC only; MP3 and other formats are listed as unsupported for byte-exact archive coverage.
- Files and paths skipped because their extensions are not configured are not currently emitted in a separate discovery inventory.
- Scan diagnostics cover surfaced read, probe, decode, serialization, and installation errors; they are not a complete dump of all FFmpeg log levels or an exhaustive inventory of skipped files.
- Unscoped FFmpeg warnings/errors are recorded at scan scope, not guessed to belong to a track. Scan preflight and
  report-write failures attempt a report in a safe system temporary directory; failures before the scan is
  initialized, or when that directory is unavailable or overlaps a source library, can only be logged.
- The manifest detects output changes relative to the generated inventory; it is not a signed authenticity mechanism.
- Legacy catalogs without a manifest can still be consumed, but cannot provide manifest-level detection of a missing complete batch.
- Compressed ID3 frame handling depends on whether the build includes ZLIB.
- Analysis includes per-track EBU R128 values when the filter is available, sample statistics and a fixed -60 dBFS leading/trailing silence threshold; it does not calculate album loudness or detect upsampling/lossy-origin cutoffs.
- True effective resolution, Chromaprint fingerprints, full native block inventory, and some planned quality heuristics remain unavailable.
