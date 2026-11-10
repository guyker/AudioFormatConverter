---
id: K-027
kind: decision
title: "Confirm every manual operation with RUN after reusable previews"
area: [FlacConvert]
status: active
supersedes: [K-022, K-023]
created: 2026-10-11
reviewed: 2026-11-10
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConfirmPreview
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: GetUserAction
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert.PrepareConversion
---

**Fact:** Manual options 1/2/3/4/6 require one exact `RUN` confirmation after a preview of configured absolute
paths, prepared input counts, quality/overwrite policy and destination conflicts. Enter, unknown text or EOF
cancels; BACK returns to the compact menu, EXIT quits. Numbers 0-6 stay unchanged and `?` shows contextual
Input/Output/Changes descriptions without preparing work. All conversion libraries are prepared before
writing; plans, metadata-free scan batches and JSON input lists are reused rather than rediscovered.

**Why it matters:** FLAC no longer replaces sources, so its old REPLACE token is misleading. Preview must
authorize the actual prepared work without adding a second prompt or writing outputs during discovery.
Preview-time counts are snapshots; source properties and filesystem failures are checked during execution.

**Rejected:** Retaining REPLACE for non-destructive FLAC output, running immediately after selection,
rediscovering inputs after confirmation, and stacking another generic confirmation were rejected.
