---
id: K-022
kind: decision
title: "Preview manual operations before writing and reuse prepared inputs"
area: [FlacConvert]
status: active
created: 2026-10-11
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConfirmPreview
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: "FolderConvert::PrepareConversion"
---

**Fact:** Manual actions preview configured absolute input/output paths, file counts, relevant quality/overwrite policies and destination conflicts before writing. Option 1 requires `REPLACE`; options 2/3/4/6 require `RUN`, with cancellation on Enter or EOF. Conversion plans, metadata-free scan album batches and JSON input lists are reused during execution. All conversion libraries are prepared before writing; conflicting destinations prevent execution.

**Why it matters:** Users need to understand the actual scope and changes before starting. Preview must not mutate outputs or scan a different file set again during execution. Preview-time counts are snapshots; audio properties are checked during conversion and write-time filesystem errors still fail explicitly.

**Rejected:** Running immediately after menu selection gives no opportunity to inspect destinations. Rediscovering inputs after confirmation duplicates work and can process files the preview never included.
