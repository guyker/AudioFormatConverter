# F-006: Import JSON metadata into SQLite

Status: active

## Behaviour
Imports generated album JSON into `OutDirectory/<name>/media_out_<name>.db`, table `TracksDB` (one row per track:
album path, format, tags, first two streams). Missing format tags remain NULL without shifting later values into
other tag columns. Numeric JSON values and ffprobe's numeric strings are accepted; format and stream start times are
stored in seconds. Created if missing. Each JSON batch (menu) or whole directory (CLI) is one transaction, and
malformed album/track structures fail the import rather than being silently skipped.

## Entry points
- `FlacConvert db import <json dir> [--output-dir]` → `FlacConvertMain.cpp#ImportJsonDirectoryToDatabase`
- Menu option 4 → `FlacConvertMain.cpp#ExportJSONToDB`
- `AlbumCollection.cpp#AlbumCollection::ExportToDatabase`, `MediaTrack.cpp#MediaTrack::GetCreateTableSQL`,
  `MediaTrack.cpp#MediaTrack::ExportToDatabase`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Database import; Files and schemas (`TracksDB`)
- K-011 SQLite long-path VFS

## Acceptance checks
- [ ] Row count equals the number of tracks in the imported JSON.
- [ ] String-valued ffprobe numbers, fractional start times, and tracks without format tags import into the correct columns.
- [ ] Non-ASCII and >260-character database paths open on Windows.
- [ ] Failure mid-batch leaves no partial rows from that batch.

## Known limitations
- Re-importing appends duplicate rows, although the menu preview says "existing records will be updated".
- Only two streams per track are stored.
