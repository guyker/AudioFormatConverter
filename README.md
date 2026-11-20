# AudioFormatConverter

<h1>A command-line utility written in modern c++ for audiophiles or media organizers to catalog their music library, check its quality, identify redundant copies, and perform basic conversions.</h1>


<h2>high-level overview of what the program does:</h2>

<h3>Configuration Management:</h3>
1. It reads application settings (output directories, logging levels, scan formats/tiers, conversion sidecar policy, and media library paths) from config.json.
2. Explicit commands use built-in defaults when no config is found; only the temporary legacy menu can prompt for a config location.

<h3>Audio File Discovery and Metadata Collection:</h3>
1. It scans specified directories (recursively) to find audio files (FLAC, MP3, etc.).
  
<h3>For each accepted audio file, the scan uses TagLib to extract catalog metadata, including:</h3>
1. Normalized multi-value tags and source-scoped raw tag entries (including ID3 frame identity where available).
2. Technical format properties, filesystem size/time, and embedded picture descriptors.
3. Explicit reader support and analysis status; the default `catalog` tier does not decode audio, while the
   `integrity` and `analysis` tiers perform decode-based checks and signal measurements.
Generated scan batches use schema v2, documented in `docs/schemas/media-catalog-v2.schema.json`.
FFmpeg container and stream tags are retained separately in the raw inventory when probing succeeds.
`CatalogExtensions` defaults to `.flac` and `.mp3`; M4A/MP4, Ogg/Opus, WavPack/APE, WAV and AIFF can be enabled
explicitly. Native metadata inventory is partial for FLAC/MP3 and unsupported for those additional formats.

<h3>Album Organization and Persistence:</h3>
1. It organizes the discovered audio files into "albums" based on their folder structure.
2. It can save this collected album and track metadata into JSON files, potentially splitting large collections into multiple JSON files for easier management.
3. It can also load album and track metadata from these previously saved JSON files.
4. It can export the collected metadata into a SQLite database for structured storage and querying.

<h3>Audio File Conversion/Processing (Limited Scope):</h3>
1. The `convert` command uses FFmpeg to write FLAC or convert FLAC to MP3 into a separate output tree and leaves source files unchanged. The legacy menu remains available without arguments during the CLI transition.

<h3>Duplicate Album Detection and Comparison:</h3>
1. A key feature is finding "duplicate" albums within the scanned collection (`DuplicateMatcher`). Candidates share a
   normalized album name (case, accents and punctuation ignored), a MusicBrainz release ID, or a stored FLAC MD5.
   Track pairing maximizes matched tracks; its deterministic search order prefers MD5, disc/track number, title, then
   duration (within the larger of `DuplicateDurationToleranceSeconds` and `SizeMatchPercentageThreshold` percent).
   Matches are graded
   exact, strong, probable or partial and grouped, so three copies form one group. The same folder loaded twice
   (overlapping libraries) is compared once. Fuzzy names, edition-text stripping and partial (unequal track count)
   matches are opt-in settings.
2. Candidate pairs are re-probed. A numbered album recap and track-by-track comparison show why each pair matched,
   coverage, edition/release tags, format/resolution, durations and differences, missing/added tracks and full differing
   tags. Stored hashes, artwork and integrity results are clearly scan-only; changed files prompt a rescan warning.
   Unknowns are not proof of equality, and higher bitrate/resolution/size does not prove better source quality.
   No signal-quality analysis or automatic keep/delete recommendation is performed (`ExtraAudioQualityMetrics` is
   deprecated and ignored).
3. Menu option 3 opens one matching group at a time and waits: Enter continues; `STOP` finishes. Choose an album number,
   then confirm the displayed paths with `MOVE`, to move that entire album folder (including artwork and subfolders)
   under `OutDirectory/deleted`. All files are copied and verified before originals are removed, also across drives.
   A unique destination and `move.json` receipt are retained; cleanup failure can leave partial originals beside a
   complete verified copy. Direct match relationships are shown, and a move that disconnects the remaining group is
   refused. Later pairs involving a moved folder are skipped. Rerun option 2 afterward to refresh the
   scan; option 4 reimports current albums but does not purge absent albums from an existing database.
   Library roots, linked/nested pairs and unsafe output overlap cannot be moved.
   The `duplicates` command stays noninteractive/read-only and opens folders only with `--open`. Use
   `--report <new file>` to write a JSON report of groups, evidence and track pairings. Reports must be outside source
   libraries; legacy input without library-root metadata cannot be reported.

<h3>Legacy interactive menu (no arguments):</h3>
The compact menu shows numbered actions; selecting one displays its input, output and file changes. Enter one number and
press Enter; invalid input is retried, `0` exits, and end-of-input exits without running an action.
Option `5` is explicitly unavailable: scan with `2`, then import into SQLite with `4`.
Option `1` writes to `OutDirectory\FLAC\<mediaName>` and leaves the source tree untouched.
It keeps the 16-bit / 44.1 kHz profile with triangular dithering, maps tags and supported embedded pictures,
archives original native FLAC metadata in `<filename>.flac.flac-metadata` sidecars when
`MetadataSidecars.ConvertToFlac` is `optional` or `required` (default `disabled`), and keeps every written
folder complete: other regular files there, including MP3 beside converted tracks, are copied unchanged, as are
folders without audio (artwork, scans, empty folders). A folder whose subtree contains audio but nothing to
convert is skipped entirely and listed in `conversion-audit.json`; a library without conversions produces only
that report. Folder names and relative paths stay intact. Existing outputs are rejected;
use explicit `convert --overwrite` to replace outputs.
No active libraries is an error. `<mediaName>` defaults to the source folder name.
Selected actions show an operation preview with absolute configured input/output paths, input counts,
conversion quality/overwrite policies, and existing or duplicate destinations. Options `1`, `2`, `3`, `4`,
`6` and `7` require `RUN` after the preview (Enter/EOF cancels).
No catalog, conversion or repair output files/directories are written during preview. A scan preflight or discovery
failure attempts a diagnostic report in a safe system temporary directory. All active conversion libraries are planned
before execution, so a conflict in a later library prevents earlier libraries from being converted.
The prepared audio/companion lists, scan album batches and JSON input lists are reused for execution.
Scan previews discover albums without collecting track metadata until confirmed. Source quality/channel
checks remain part of conversion; counts and destination checks reflect preview-time file state.
Enter `?` at the selection prompt to see all action descriptions without scanning or writing.
At the preview confirmation, `BACK` returns to the menu and `EXIT` quits without starting the action.
Starting an action uses the same single `RUN` confirmation. Optional album moves during option 3 require a separate `MOVE`.
Enter, unrecognized confirmation text or EOF still cancels and exits. Option numbers are unchanged.
Option `2` asks whether to export separate byte-exact native FLAC metadata archives; the configured default is
`ScanFlacMetadataArchives` (normally `false`). Enter keeps that default; `YES` or `NO` applies only to the current
operation. The preview states that previous manifest-owned archives are retired after a successful scan.
Finished manual actions report `Completed`, `Partially completed` or `Failed` (`Stopped by user` for an early duplicate-review stop), elapsed execution time,
output locations, completed work and copied files. Counts reflect successfully installed audio,
installed album JSON files (not optional error reports), committed JSON database batches, or fully
processed duplicate pairs; they are not preview estimates. A failure stops at the first error and
does not count unattempted work as failed or imply whole-operation rollback. Preflight failures state
that no action started; cancellation and Back/Exit do not print a success summary.
Option `6` prompts for MP3 VBR quality `0-9` (default `0`, highest quality), multichannel stereo
downmix (default `NO`) and overwrite (default `NO`) before preparing the preview. Enter keeps each
default; invalid values are retried, and `BACK`/`EXIT` or EOF cancels without conversion.
The final preview shows the selected settings and still requires `RUN`. Overwrite applies to MP3,
metadata sidecars and copied companions, never to source files. Settings are per operation and reset
to safe defaults when selecting option `6` again; they do not modify config.json.
Option `7` checks FLAC and MP3 files for common metadata defects (redundant ID3/APE tags, duplicate or empty tags,
wrong picture MIME types or FLAC picture dimensions/depth, invalid FLAC seek tables, ID3v2.3 encoding errors, and more). It writes repaired copies to
`OutDirectory\FIXED\<mediaName>`, copying only affected folders and keeping other files unchanged; audio bytes are
copied unchanged and sources are never modified. Conflicting values and unreadable files are left for manual review
and listed in `repair-report.json`. Redundant MP3 ID3v1 tags are kept unless removal is explicitly enabled, to
preserve compatibility with older players. Four workers inspect and stage files concurrently.

The console menu options are:

- `1` converts audio to FLAC in a separate output tree.
- `2` scans directories and creates JSON metadata files.
- `3` processes existing JSON files to find duplicate albums.
- `4` imports JSON files into the SQLite catalog.
- `5` is unavailable (see above).
- `6` converts active configured libraries from FLAC to MP3, writing to
   `OutDirectory\MP3\<mediaName>` (defaults to the source folder name). It defaults to VBR quality 0,
   preserves metadata/companions, never changes sources, and rejects existing destinations or
   multichannel input unless overwrite or stereo downmix is explicitly enabled.
- `7` validates and repairs recognized FLAC/MP3 metadata defects into `OutDirectory\FIXED\<mediaName>` without
   changing sources; it requires `RUN` after preview.

## Build

CMake is the only build system for Windows and Linux; Ninja runs the builds and CTest runs the tests.
In Visual Studio, open the repository folder (File > Open > Folder) and choose a Windows preset; Visual Studio
reads `CMakePresets.json` directly, so no solution or project files are kept.

Initialize submodules (`git submodule update --init --recursive`) and install CMake 3.28+, Ninja, Python 3
and a C++20 toolchain. CMake fetches the pinned TagLib revision for metadata catalog reads.

On Windows, use an x64 Visual Studio Developer PowerShell/Command Prompt. Set the `FFmpeg_ROOT` environment
variable or pass `-DFFmpeg_ROOT=<path>` for a matching shared FFmpeg SDK containing `include`, `lib` and `bin`
directories; by default CMake checks
`external\ffmpeg\ffmpeg-master-latest-win64-gpl-shared\`. The SDK must include the five FFmpeg import
libraries and runtime DLLs used by the application.

On Linux, the supported CI reference is Ubuntu 24.04 x86-64 with GCC 13. Install FFmpeg runtime tools and
development packages for libavformat, libavcodec, libavfilter, libavutil and libswresample, plus pkg-config.
The FFmpeg executable must include the `libmp3lame` encoder for MP3 integration tests.

Configure, build and test using the matching preset:

```text
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

Use `linux-debug` on Linux; `windows-release` and `linux-release` select Release builds. Build output is
isolated under `out\build\<preset>`. To stage an executable, use
`cmake --install out\build\<preset>`; choose another location with `--prefix <directory>`. Linux uses
installed system FFmpeg libraries. Windows stages the matched runtime DLLs beside the executable. Do not copy
the Windows-path sample `config.json` into an active Linux configuration without editing its library paths.

## Design and feature documentation

`FlacConvert\DESIGN.md` describes the application; `docs\features\index.md` lists its important features and
`docs\knowledge\index.md` records durable decisions and risks. Keep these current when material behaviour, contracts
or design change; routine edits do not need a documentation entry or change log.

Install the optional, warn-only reminder for this local repository with `python tools\install_doc_hook.py`. It refuses
to overwrite an existing hook; Git may share the hook with linked worktrees. Once committed, the pull-request workflow
also runs an advisory check. Neither reminder changes files or determines whether a change is important. For candidate
documents and structural warnings, use
`python tools\doc_impact.py --details` (add `--staged` for a commit or `--base <ref>` for committed changes).

<h2>Command-line usage</h2>

The executable supports explicit commands. Run `FlacConvert --help` or append `--help` to a command for detailed
input, output, configuration, and example information.
Each invocation writes a session log under `%LOCALAPPDATA%\AudioFormatConverter\logs` on Windows or the platform
state directory elsewhere. Console output stays concise; `--log-level info|debug|trace` controls detail in the log
file, not console warnings/errors. The configured `LogLevel` is the default file-log threshold. Structured conversion
audits and scan reports remain separate outputs. Logs can contain library paths and diagnostic metadata; inactive
session logs older than 30 days or beyond the 1 GiB retention limit are removed.

```text
FlacConvert convert "D:\Music" --output-dir "E:\Converted"
FlacConvert convert "D:\Music" --to mp3 --output-dir "E:\MP3"
FlacConvert convert "D:\Music" --to mp3 --mp3-quality 2 --output-dir "E:\MP3"
FlacConvert scan "D:\Music" --output-dir "E:\Catalog"
FlacConvert scan "D:\Music" --full-rescan --tier integrity
FlacConvert scan "D:\Music" --archive-flac-metadata
FlacConvert duplicates "E:\Catalog\Music"
FlacConvert duplicates "E:\Catalog\Music" --report "E:\Reports\duplicates.json"
FlacConvert db import "E:\Catalog\Music" --output-dir "E:\Databases"
FlacConvert repair "D:\Music" --output-dir "E:\Repaired"
FlacConvert repair "D:\Music" --rules flac.duplicate-comments,mp3.redundant-id3v1
FlacConvert repair "D:\Music" --dry-run --jobs 2
FlacConvert repair "D:\Music" --remove-id3v1
```

`convert` writes FLAC files to a separate output tree and preserves source files. Sources below 16-bit or 44.1 kHz, or
whose required properties cannot be verified, are rejected and fail the command. AAC sources (`.m4a`) are exempt from
the bit-depth check because encoded AAC has no meaningful source PCM bit depth, but still require a verified rate of at
least 44.1 kHz; a warning notes that lossy-to-FLAC conversion cannot restore lost quality. Accepted audio is converted
to 16-bit, 44.1 kHz FLAC with triangular dithering. Existing destinations cause an error unless `--overwrite` is
specified. `scan` replaces the generated JSON output for that library only after a new scan completes. Incremental
scans reuse metadata when file path, size, and modification time are unchanged; pass
`--full-rescan` to force rereads. `--tier` accepts `catalog` (default), `integrity`, or `analysis`; configure the
menu/default tier with `ScanTier`. `CatalogExtensions` selects accepted scan formats; only FLAC and MP3 are enabled
by default. Set `JsonBatchTargetBytes` to a positive hard byte limit for JSON batching (default `0` disables it);
oversized albums split into track fragments, and a scan fails if a track cannot fit. Unlimited batches stream one
track record at a time. Successful scans also write `scan-manifest.json`, which protects the complete JSON batch set
with SHA-256. Separate byte-exact native FLAC metadata archives are disabled by default; enable them with
`ScanFlacMetadataArchives: true` or `scan --archive-flac-metadata`, and override an enabled setting for one run with
`--no-archive-flac-metadata`. Catalog JSON still contains extracted FLAC and MP3 tags in either mode, but not artwork
bytes or every original metadata block. When enabled, the manifest lists verified archives and unsupported formats;
when disabled, it records that no archives were made. A successful rescan retires previous manifest-owned archives
even when archiving is disabled; a failed scan preserves the previous set. Conversion sidecars are configured
separately by `MetadataSidecars`. Duplicate detection, database import, and incremental cache reuse validate the
manifest before using marked batches. `DecodeWorkers` caps the independent full-decode stage. `duplicates` reports matches without
opening folders; pass `--open` to open matched folders in Explorer. `db import` creates or updates a library-named
`catalog-v2.sqlite` database and leaves legacy `.db` files untouched. `repair` writes metadata-only FLAC/MP3 fixes to
`<output-root>\FIXED\<input-folder-name>`; `--rules` limits it to listed rule ids (see `FlacConvert repair --help`).
`--dry-run` prints a complete JSON report without writing repair output, and `--jobs` selects 1–16 workers
(default 4). Normal session logs still apply. It verifies byte-identical audio before installation and reports
unresolved findings rather than guessing.

### FLAC output preservation

`convert --output-dir` is a **base root**, matching the manual menu: output is
`<root>\<FLAC|MP3>\<input-folder-name>` (manual libraries may use their configured name).
For example, `convert D:\Music\AlbumA --output-dir E:\Converted --to flac` writes
`E:\Converted\FLAC\AlbumA`. Relative paths inside the library are retained.

Both manual option `1` and explicit `convert --to flac` use the same folder planning and installation path.
All libraries are checked before manual execution; source/output overlap, duplicate destinations and existing
outputs block the run. Files are processed sequentially and stop at the first error. Completed outputs remain;
the source tree is never modified. Manual overwrite is disabled; explicit `--overwrite` only replaces outputs.

Tags and supported attached pictures are mapped into FLAC without image re-encoding. When enabled by the
`MetadataSidecars` policy, native FLAC inputs also produce `<filename>.flac.flac-metadata`: the same
byte-preserving original metadata archive used by MP3.
This retains repeated comments, application blocks, cue sheets, original chapters and unsupported pictures
even if FFmpeg cannot embed them faithfully. Picture dimensions and mapped tags can be normalized.
Original seek tables, checksums and technical fields in the sidecar describe the source, not the new
16-bit / 44.1 kHz audio; they must not be inserted blindly into the converted FLAC.
Non-FLAC inputs rely on FFmpeg tag/picture mapping and do not produce this native-FLAC sidecar.

Sidecars are configured in `config.json`:

```json
"MetadataSidecars": {
  "Format": "binary",
  "ConvertToFlac": "disabled",
  "ConvertToMp3": "disabled"
}
```

`Format` is `binary` (`.flac-metadata`) or `json` (`.flac-metadata.json`: readable tags/streams/chapters in the
scan-JSON shape plus the exact archive as Base64; only the archive is verified). Each target policy is
`disabled` (never written), `optional` (written by default; the menu asks, CLI `--no-metadata-sidecar` turns it off)
or `required` (always written; `--no-metadata-sidecar` is an error); both default to `disabled`. The sample
`config.json` uses `//` comments, accepted by the config loader, to explain these choices. Without a sidecar,
metadata the output cannot carry (application blocks, repeated tags, unsupported pictures, etc.) stops the
conversion instead of being lost.
Scan, duplicates and database import are unaffected.
Every regular file not selected for FLAC conversion in a written folder is copied unchanged, including MP3, AAC,
Ogg and other unsupported audio beside converted tracks. Folders without audio, including empty folders, stay with
their written parent. Converted
sources are represented by their new `.flac` files, not additional copies of their original audio containers.
Cue sheets/playlists are not rewritten and may still refer to old names or source audio properties.
Native metadata sidecars do not contain audio or restore precision lost through resampling/dithering.

A folder whose source subtree contains audio but no file to convert (for example an MP3-only album) is skipped
entirely, so the output never holds a partial copy of an untouched album. Only the topmost such folder is listed
in the audit's `skippedUnchangedDirectories`, with its file and audio counts. A library without conversions writes
only `FLAC\<mediaName>\conversion-audit.json` listing `"."`. Because the rule is per folder, an MP3-only `CD2` beside a
converted `CD1` is skipped. Directory symlinks/junctions, broken links and special filesystem entries fail FLAC
planning explicitly, even inside a skipped folder. Regular-file links are materialized as file contents, not
preserved as links.
Explicit `--overwrite` replaces existing targets; unrelated existing output files (including folders that are now
skipped) are retained, so this is not a directory-mirroring/delete operation.
MP3 conversion keeps its narrower policy: other audio formats are skipped, and empty/copy-only trees are not exported.

### Preservation audit and incomplete output

Before installation, conversions compare observed tags (including native FLAC comment multiplicity),
attached picture payloads/descriptors, chapters and opaque FLAC metadata. Required metadata archives are
verified byte-for-byte against the original. Companion files are compared byte-for-byte before installation.
Each installed audio file logs a compact summary and resource differences, without dumping tag values.
`conversion-audit.json` in the library output contains per-file installation states, resource classifications,
counts and the actual output location. Unattempted files are listed explicitly. For FLAC, unchanged file copies
include unsupported audio; `skippedUnchangedDirectories` lists folders omitted because they contain audio but nothing
to convert. Root-relative `"."` identifies the library folder itself.

`Preserved` means observed fields match; `Normalized` identifies changed representations;
`SidecarOnly` means the original is retained in a verified archive; `MissingOrChanged` identifies loss;
`NotVerified` identifies inspection limitations; `SourceOnly` identifies source technical data such as seek
offsets/checksums that cannot describe transformed audio. ReplayGain/R128 tags may be stale after conversion.
Normalization and archive-backed metadata omissions warn; supported picture payload loss, missing unarchived
resources, failed archive verification, copy errors and installation/report errors stop the run.
Non-FLAC opaque metadata cannot be completely audited; mapped fields are checked but that limitation is reported.

Only library folders **created by the current execution** are renamed on failure, with prefixes
`ERROR_AUDIO_`, `ERROR_PRESERVATION_`, `ERROR_COPY_`, `ERROR_IO_` or `INCOMPLETE_`.
Existing folders stay in place; completed libraries stay unchanged. Name collisions receive numbered suffixes.
Rename failures are logged with the unchanged location. Failure reports and completed outputs are retained.
The reserved `.conversion-in-progress` marker directory is removed only after all planned output and the final
report are complete. A crash/power loss cannot run renaming cleanup, so this marker identifies unfinished output.
Existing markers block reuse: inspect the report/output and move the incomplete tree or explicitly remove the marker
before retrying. Reports require `--overwrite` when reusing completed output. Source files bearing either reserved
state filename at the library root block preflight, before any output is created.

### FLAC to MP3

`--to mp3` converts native FLAC files using **libmp3lame VBR quality 0** (highest-quality VBR).
`--mp3-quality 0..9` controls the quality/size tradeoff; larger numbers mean lower quality.
The FFmpeg executable must include the `libmp3lame` encoder. Files recognized as other audio formats are
not converted or copied in this mode. The default `--to flac` and legacy menu retain their existing FLAC conversion behavior.

MP3 keeps 44.1/48 kHz source rates; other accepted rates are resampled to 48 kHz, without first reducing
the source to 16-bit. The minimum source-quality rule still applies. Mono/stereo are retained;
multichannel inputs fail unless `--downmix-to-stereo` explicitly permits a logged stereo downmix.

Standard and custom tags, chapters and supported embedded pictures are mapped to **ID3v2.4**.
Pictures are copied without image re-encoding, including their types/descriptions. Xing/LAME timing
information remains enabled for seeking and gapless playback. Unsupported pictures remain in the
sidecar, rather than being silently discarded. Not every player displays every ID3 field or multiple pictures.

When `MetadataSidecars.ConvertToMp3` is `optional` or `required` (the default is `disabled`, see above), each
MP3 has a `<filename>.mp3.flac-metadata` sidecar (binary, or JSON per `MetadataSidecars.Format`): the original
FLAC marker and every
metadata block, byte-for-byte, plus leading ID3 and recognized trailing APE/ID3v1 tags when present.
It preserves repeated Vorbis comments, vendor data, application blocks, cue sheets, seek tables and
all picture payloads that ID3 or FFmpeg cannot faithfully represent. This is **not playable audio** or
a metadata description of the MP3: source checksums, offsets, ReplayGain values and technical fields
refer to the original FLAC. It cannot restore audio lost through MP3 encoding. Keep it with the MP3;
no metadata restore command is currently provided. Ogg FLAC and headerless FLAC fail explicitly.

Non-audio companion files (artwork, booklets, cue sheets, etc.) are copied unchanged with relative paths
preserved. Copied cue sheets/playlists are not rewritten and may still name the original FLAC files.
Metadata preservation, copying, encoding or output-verification failures fail the command and are logged.
Existing MP3, sidecar or companion destinations require `--overwrite`. Installation protects existing
files until conversion succeeds and restores the previous sidecar if MP3 installation fails; a folder
batch is not an all-or-nothing transaction. Sources are never changed.

Programmatic use reuses the existing task lifecycle:

```cpp
ConversionOptions options{ AudioTarget::Mp3, 0, false };
MediaConvertionTask task(sourceFlac, targetMp3, uniqueTemporaryMp3, false, options);
if (task.Run() != 0 || task.PostRun() != 0) {
    // Handle the logged conversion/installation failure.
}
```

Use distinct source/target paths and an unused temporary filename ending in `.mp3`, in the destination
directory. `PostRun()` installs the MP3 and sidecar. The folder API accepts the same `ConversionOptions`
as its fourth argument. Options default to FLAC for existing callers.

Metadata design references: [FLAC format (RFC 9639)](https://www.rfc-editor.org/rfc/rfc9639.html),
[FFmpeg MP3 muxer](https://ffmpeg.org/ffmpeg-formats.html#mp3), and the official FFmpeg
[ID3 writer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/id3v2enc.c).

CTest registers the native regression checks and the generated-fixture Python integration suite. To run the
Python suite directly:

```text
python tests\mp3_conversion.py --converter out\build\windows-debug\FlacConvert\FlacConvert.exe --ffmpeg external\ffmpeg\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe --ffprobe external\ffmpeg\ffmpeg-master-latest-win64-gpl-shared\bin\ffprobe.exe
```

Configuration is optional. Pass `--config <file>` to select a JSON settings file; otherwise the command checks the
persisted config location, then `./config.json`, and finally uses built-in defaults (output goes to the operating
system's temporary directory on Linux/macOS or `C:\tmp` on Windows). CLI input and output paths take precedence.
Launching without arguments temporarily retains the legacy interactive menu.

`FlacConvert/config.json` is a sample configuration with Windows library paths; adjust those paths before use.
`FLACSettings.ffmpeg_exe_name` selects the conversion executable. The `ffmpeg_convert_24bit_flac` and
`ffmpeg_get_metadata_tags` strings are compatibility fields, not runtime overrides: conversion and metadata options
are defined in code. Library output filenames are generated from `mediaName`; old `resultPath` and `dbPath` fields
are not read.

On Windows, the executable opts into long-path handling. The operating system's
**Enable Win32 long paths** policy must also be enabled; the filesystem's per-component
filename limit still applies. Command-line and configuration paths are interpreted as UTF-8,
then passed to Windows file APIs as native wide paths.
