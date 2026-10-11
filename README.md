# AudioFormatConverter

<h1>A command-line utility written in modern c++ for audiophiles or media organizers to catalog their music library, check its quality, identify redundant copies, and perform basic conversions.</h1>


<h2>high-level overview of what the program does:</h2>

<h3>Configuration Management:</h3>
1. It reads application settings (like output directories, logging levels, FFmpeg usage preferences, and media library paths) from config.json.
2. Explicit commands use built-in defaults when no config is found; only the temporary legacy menu can prompt for a config location.

<h3>Audio File Discovery and Metadata Collection:</h3>
1. It scans specified directories (recursively) to find audio files (FLAC, MP3, etc.).
  
<h3>For each audio file, it uses FFprobe (either via a shell command or directly through FFmpeg's C API) to extract detailed metadata, including:</h3>
1. Standard tags (artist, album, title, year, genre, etc.).
2. Technical format information (codec, sample rate, bitrate, duration, file size).
3. Optionally, detailed audio quality metrics like peak amplitude, RMS, dynamic range, and noise floor.

<h3>Album Organization and Persistence:</h3>
1. It organizes the discovered audio files into "albums" based on their folder structure.
2. It can save this collected album and track metadata into JSON files, potentially splitting large collections into multiple JSON files for easier management.
3. It can also load album and track metadata from these previously saved JSON files.
4. It can export the collected metadata into a SQLite database for structured storage and querying.

<h3>Audio File Conversion/Processing (Limited Scope):</h3>
1. The `convert` command uses FFmpeg to write FLAC or convert FLAC to MP3 into a separate output tree and leaves source files unchanged. The legacy menu remains available without arguments during the CLI transition.

<h3>Duplicate Album Detection and Comparison:</h3>
1. A key feature is finding "duplicate" albums within the scanned collection. This is done by comparing track counts, and then more thoroughly by comparing sorted track durations and potentially audio quality metrics (if enabled).
2. It provides a detailed report when audio quality differences are found between seemingly duplicate albums.
3. The legacy menu opens matching album folders; the `duplicates` command only does so when passed `--open`.

<h3>Legacy interactive menu (no arguments):</h3>
The compact menu shows numbered actions; selecting one displays its input, output and file changes. Enter one number and
press Enter; invalid input is retried, `0` exits, and end-of-input exits without running an action.
Option `5` is explicitly unavailable: scan with `2`, then import into SQLite with `4`.
Option `1` warns that existing FLAC sources are replaced and embedded artwork is omitted.
Before running option `1`, the menu lists the affected active libraries and requires typing `REPLACE`
to confirm source replacement. Enter, any other response, or end-of-input cancels without starting
conversion. No active libraries is an error. The explicit `convert` subcommand is unaffected.
Selected actions show an operation preview with absolute configured input/output paths, input counts,
conversion quality/overwrite policies, and existing or duplicate destinations. Option `1` retains its
`REPLACE` confirmation; options `2`, `3`, `4` and `6` require `RUN` after the preview (Enter/EOF cancels).
No output files/directories are written during preview. All active conversion libraries are planned
before execution, so a conflict in a later library prevents earlier libraries from being converted.
The prepared audio/companion lists, scan album batches and JSON input lists are reused for execution.
Scan previews discover albums without collecting track metadata until confirmed. Source quality/channel
checks remain part of conversion; counts and destination checks reflect preview-time file state.
Enter `?` at the selection prompt to see all action descriptions without scanning or writing.
At the preview confirmation, `BACK` returns to the menu and `EXIT` quits without starting the action.
Running uses the same single confirmation: `REPLACE` for option `1`, `RUN` for other actions.
Enter, unrecognized confirmation text or EOF still cancels and exits. Option numbers are unchanged.
Finished manual actions report `Completed`, `Partially completed` or `Failed`, elapsed execution time,
output locations, completed work and copied companions. Counts reflect successfully installed audio,
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

1. The main function provides a console-based menu for the user to select different actions:
2. Re-convert FLAC files.
3. Scan directories and create JSON metadata files.
4. Process existing JSON files to find duplicate albums.
5. Populate a SQLite database from JSON files.
6. Menu selection `6` converts active configured libraries from FLAC to MP3, writing to
   `OutDirectory\MP3\<mediaName>` (defaults to the source folder name). It defaults to VBR quality 0,
   preserves metadata/companions, never changes sources, and rejects existing destinations or
   multichannel input unless overwrite or stereo downmix is explicitly enabled.

<h2>Command-line usage</h2>

The executable supports explicit commands. Run `FlacConvert --help` or append `--help` to a command for detailed
input, output, configuration, and example information.

```text
FlacConvert convert "D:\Music" --output-dir "E:\Converted"
FlacConvert convert "D:\Music" --to mp3 --output-dir "E:\MP3"
FlacConvert convert "D:\Music" --to mp3 --mp3-quality 2 --output-dir "E:\MP3"
FlacConvert scan "D:\Music" --output-dir "E:\Catalog"
FlacConvert duplicates "E:\Catalog\Music"
FlacConvert db import "E:\Catalog\Music" --output-dir "E:\Databases"
```

`convert` writes FLAC files to a separate output tree and preserves source files. Sources below 16-bit or 44.1 kHz, or
whose properties cannot be verified, are rejected and fail the command. Accepted audio is converted to 16-bit, 44.1 kHz
FLAC with triangular dithering. Existing destinations cause an error unless `--overwrite` is specified. `scan` replaces
the generated JSON output for that library only after a new scan
completes. `duplicates` reports matches without opening folders; pass `--open` to open matched folders in Explorer.
`db import` creates or updates a library-named SQLite database.

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

Each MP3 has a required `<filename>.mp3.flac-metadata` binary sidecar: the original FLAC marker and every
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

Generated-fixture integration checks (Python standard library only):

```text
python tests\mp3_conversion.py --converter x64\Debug\FlacConvert.exe --ffmpeg external\ffmpeg\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe --ffprobe external\ffmpeg\ffmpeg-master-latest-win64-gpl-shared\bin\ffprobe.exe
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
