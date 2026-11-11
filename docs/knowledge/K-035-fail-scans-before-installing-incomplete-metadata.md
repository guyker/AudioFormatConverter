---
id: K-035
kind: trap
title: "Fail scans before installing incomplete metadata"
area: [FlacConvert]
status: active
created: 2026-11-10
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: "AlbumCollection::SaveAlbumsToJSON"
  - path: tests/mp3_conversion.py
    symbol: ConversionTests.test_incomplete_scan_preserves_previous_json_and_accepts_uppercase_extension
---

**Fact:** `AlbumCollection::SaveAlbumsToJSON` fails a scan batch when an accepted audio track has an extraction/parse
error or lacks a usable audio stream. `ConversionTests.test_incomplete_scan_preserves_previous_json_and_accepts_uppercase_extension`
verifies that a failed rescan leaves the previous generated JSON byte-for-byte unchanged.

**Why it matters:** Skipping an unreadable track while reporting scan success can replace a complete catalog with an
incomplete one, affecting later duplicate detection and database imports.
