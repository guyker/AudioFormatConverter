---
id: K-052
kind: decision
title: "Quarantine duplicate albums only by explicit per-pair choice"
area: [FlacConvert]
status: active
created: 2026-11-18
reviewed: 2026-11-19
cites:
  - path: FlacConvert/src/DuplicateReview.cpp
    symbol: "DuplicateReview::ReviewPair"
  - path: FlacConvert/src/DuplicateReview.cpp
    symbol: moveAlbum
  - path: FlacConvert/src/DuplicateReview.cpp
    symbol: "DuplicateReview::ReviewGroup"
---

**Fact:** `DuplicateReview::ReviewPair` requires a separate album choice and exact `MOVE` confirmation for each
whole-folder quarantine. `moveAlbum` verifies a complete destination copy before removing originals, including across
volumes, and retains that copy if cleanup fails. Duplicate detection alone never authorizes relocation.

**Why it matters:** Matching album metadata and durations identifies candidates, not which edition the user wants to
keep. Whole folders preserve artwork, playlists and other companion files. Quarantine must remain recoverable rather
than treating a candidate match as proof that its source content is disposable.

**Rejected:** Automatic deletion or a single blanket move approval bypasses the per-match decision. Moving audio files
alone separates album companions. Refusing cross-volume moves does not meet the workflow; deleting originals before
complete verification risks losing the only intact album.
