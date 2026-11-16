---
id: K-040
kind: decision
title: "Key catalog files by library-relative path"
area: [FlacConvert]
status: active
supersedes: [K-039]
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: FlacConvert/src/CatalogDatabase.cpp
    symbol: "CatalogDatabase::Import"
---

**Fact:** `CatalogDatabase::Import` identifies each file by `(library_id, relative_path)`, upserts the record on that
key, and removes stale files only from albums included in the import. Re-importing the same catalog is idempotent;
albums absent from the input remain unchanged.

**Why it matters:** The file identity must survive album grouping changes while still allowing an import to remove
tracks intentionally removed from an included album without deleting unrelated albums.

**Rejected:** Album-path-only replacement cannot identify an individual file across regrouping. Appending rows
duplicates every track on re-import, while replacing the entire library would remove albums not present in a partial
import.
