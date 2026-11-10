---
id: K-029
kind: decision
title: "Audit converted resources before installation and retain verified original metadata"
area: [FlacConvert]
status: active
created: 2026-10-11
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/PreservationAudit.cpp
    symbol: PreservationAudit.Compare
  - path: FlacConvert/src/FlacMetadata.cpp
    symbol: FlacMetadata.Verify
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.ExecuteConversion
---

**Fact:** `PreservationAudit::Compare` checks temporary audio metadata before installation, including native FLAC
comment occurrences, picture bytes/descriptors, chapters and opaque blocks. `FlacMetadata::Verify` compares required
archives exactly against source metadata/trailers. Observed normalization and archive-backed omissions warn;
supported picture payload loss, unarchived missing resources and verification failures stop the run. Companion
bytes are verified before installation. `FolderConvert::ExecuteConversion` writes per-file states and resource
classifications to `conversion-audit.json`, including failed and unattempted files; counts advance after installation.

**Why it matters:** Successful encoding does not prove preservation. Source checksums/seek offsets do not describe
transformed audio, and copied ReplayGain/R128 values may be stale. Non-FLAC opaque fields and unsupported inspection
representations must be reported as limitations rather than claimed preserved.

**Rejected:** Treating every normalization as fatal rejects valid cross-container mappings. Treating a sidecar as
sufficient for missing supported artwork hides broken playable output. Logging every unchanged field obscures losses.
