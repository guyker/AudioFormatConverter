# AudioFormatConverter

<h1>A command-line utility written in modern c++ for audiophiles or media organizers to catalog their music library, check its quality, identify redundant copies, and perform basic conversions.</h1>


<h2>high-level overview of what the program does:</h2>

<h3>Configuration Management:</h3>
1. It reads and manages application settings (like output directories, logging levels, FFmpeg usage preferences, and media library paths) from a config.json file.
2. It can also generate a default config.json if one isn't found and prompts the user for its location.

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
1. The FolderConvert and MediaConvertionTask/MediaConvertionAsyncTask classes suggest a capability to convert audio files. The current explicit configuration points to FLAC to FLAC conversion, likely for re-encoding purposes (e.g., to adjust bit depth or sample rate if needed, or to ensure consistency). It uses FFmpeg for this conversion.

<h3>Duplicate Album Detection and Comparison:</h3>
1. A key feature is finding "duplicate" albums within the scanned collection. This is done by comparing track counts, and then more thoroughly by comparing sorted track durations and potentially audio quality metrics (if enabled).
2. It provides a detailed report when audio quality differences are found between seemingly duplicate albums.
3. It offers an interactive prompt to open the locations of identified duplicate albums in a file explorer.

<h3>User Interaction (Console-based):</h3>
1. The main function provides a console-based menu for the user to select different actions:
2. Re-convert FLAC files.
3. Scan directories and create JSON metadata files.
4. Process existing JSON files to find duplicate albums.
5. Populate a SQLite database from JSON files.



