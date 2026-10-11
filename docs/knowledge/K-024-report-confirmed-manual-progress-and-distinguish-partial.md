---
id: K-024
kind: decision
title: "Report confirmed manual progress and distinguish partial completion from failure"
area: [FlacConvert]
status: active
created: 2026-10-11
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ManualOperationSummary
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: "FolderConvert::ExecuteConversion"
---

**Fact:** Manual actions report Completed, Partially completed or Failed from execution results and confirmed progress. Counts advance only after audio/companion installation, successful installation of album JSON batches, SQLite batch commit, or full duplicate-pair processing. Album JSON counts exclude optional error reports. Summaries include output locations and execution time starting after confirmation; cancellation and Back/Exit produce no success summary. Preflight failure reports that no action started.

**Why it matters:** An error in a later library or batch does not roll back earlier outputs. Preview counts are not completed work. Failures stop at the first error, so unattempted work must not be counted as failed; a failed item may still leave recovery files or an empty database.

**Rejected:** A single success/failure message hides partial writes. Reporting planned counts as completed exaggerates progress. Claiming whole-operation rollback would misrepresent existing per-file and per-batch installation semantics.
