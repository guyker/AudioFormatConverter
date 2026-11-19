---
id: K-046
kind: decision
title: "Match duplicate tracks by duration rather than compressed size"
area: [FlacConvert]
status: superseded
superseded_by: K-054
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: FlacConvert/src/AlbumCollection.cpp
    symbol: "AlbumCollection::FindDuplicationInGroup"
---

**Fact:** `AlbumCollection::FindDuplicationInGroup` applies the legacy `SizeMatchPercentageThreshold`
to sorted track durations, not compressed file sizes. The maintainer confirmed duration-only matching.

**Why it matters:** Copies of the same recording can have different compressed sizes, so a size filter
would exclude candidates that the duration-based search is meant to find.

**Rejected:** Matching on file size alone or requiring both duration and file size would change the
confirmed matching policy.
