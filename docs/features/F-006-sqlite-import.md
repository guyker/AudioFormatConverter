# F-006: Import JSON metadata into SQLite

Status: active

## Behaviour
Imports schema-v2 catalog JSON into `OutDirectory/<name>/catalog-v2.sqlite` (`PRAGMA user_version=2`). The database
uses normalized searchable tags and source-scoped raw tags, plus file properties, pictures, chapters, measurements,
findings, and queryable AI-facing schema documentation. File identity is `(library_id, relative_path)`, where the
library root is carried by each album JSON object. Re-imports update existing file rows without duplication and
remove tracks missing from albums included in the import; albums absent from the import remain untouched. All JSON
batches for a menu library or the complete CLI directory import share one transaction and roll back together on
failure. The old `.db` file is never migrated, overwritten, or deleted; incompatible files at the new catalog path
are rejected without modification.

## Entry points
- `FlacConvert db import <json dir> [--output-dir]` → `FlacConvertMain.cpp#ImportJsonDirectoryToDatabase`
- Menu option 4 → `FlacConvertMain.cpp#ExportJSONToDB`
- `AlbumCollection.cpp#AlbumCollection::ExportToDatabase`, `CatalogDatabase.cpp#CatalogDatabase::Import`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Database import; Files and schemas (SQLite catalog schema v2)
- K-042 SQLite long-path VFS; K-040 library-relative file identity

## Acceptance checks
- [ ] Imported files have stable `(library_id, relative_path)` identity; re-import does not duplicate rows.
- [ ] Tracks removed from an imported album disappear while albums absent from the import remain untouched.
- [ ] A failed import rolls back all album and child-row changes.
- [ ] Every table/view and column has a `schema_docs` entry and schema v2 AI guide metadata exists.
- [ ] Every finding code has an `enum_docs` entry, and each table/view DDL carries an inline interpretation comment.
- [ ] Non-ASCII and >260-character database paths open on Windows.
- [ ] CLI import through a directory alias uses the supplied directory name for its output, with or without a trailing separator.
- [ ] An incompatible catalog database is refused without modification; the old `.db` remains untouched.

## Known limitations
- Only schema-v2 catalog JSON is accepted; legacy JSON must be regenerated with `scan`.
