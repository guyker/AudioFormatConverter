---
id: K-053
kind: decision
title: "Keep FLAC scan metadata archives opt-in"
area: [FlacConvert]
status: active
created: 2026-11-19
reviewed: 2026-11-19
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ScanFolderAndCreateJSON
---

**Fact:** `ScanFolderAndCreateJSON` writes readable catalog records independently of the optional byte-exact native
FLAC metadata archives. The user chose archive export to default off, with per-run menu and CLI overrides.

**Why it matters:** Catalog tags and picture descriptors are not a byte-exact metadata backup. Scan archive selection
must remain separate from conversion sidecar policy, which protects against unsupported metadata loss during conversion.

**Rejected:** Requiring archives on every scan would prevent the user's requested opt-out; disabling catalog metadata
along with archives would make ordinary scans unusable for duplicate detection and database import.
