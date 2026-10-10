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
1. The `convert` command uses FFmpeg to write FLAC files into a separate output tree and leaves source files unchanged. The legacy menu remains available without arguments during the CLI transition.

<h3>Duplicate Album Detection and Comparison:</h3>
1. A key feature is finding "duplicate" albums within the scanned collection. This is done by comparing track counts, and then more thoroughly by comparing sorted track durations and potentially audio quality metrics (if enabled).
2. It provides a detailed report when audio quality differences are found between seemingly duplicate albums.
3. The legacy menu opens matching album folders; the `duplicates` command only does so when passed `--open`.

<h3>Legacy interactive menu (no arguments):</h3>
1. The main function provides a console-based menu for the user to select different actions:
2. Re-convert FLAC files.
3. Scan directories and create JSON metadata files.
4. Process existing JSON files to find duplicate albums.
5. Populate a SQLite database from JSON files.

<h2>Command-line usage</h2>

The executable supports explicit commands. Run `FlacConvert --help` or append `--help` to a command for detailed
input, output, configuration, and example information.

```text
FlacConvert convert "D:\Music" --output-dir "E:\Converted"
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
