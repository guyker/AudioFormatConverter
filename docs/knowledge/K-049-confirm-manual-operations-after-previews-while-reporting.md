---
id: K-049
kind: decision
title: "Confirm manual operations after previews while reporting scan discovery failures"
area: [FlacConvert]
status: active
supersedes: [K-027]
created: 2026-11-18
reviewed: 2026-11-18
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConfirmPreview
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ScanFolderAndCreateJSON
---

**Fact:** Manual options 1/2/3/4/6 require one exact `RUN` confirmation after a preview of configured absolute
paths, prepared input counts, quality/overwrite policy and destination conflicts. Enter, unknown text or EOF
cancels; BACK returns to the compact menu, EXIT quits. Numbers 0-6 stay unchanged and `?` shows contextual
Input/Output/Changes descriptions without preparing work. Conversion plans, metadata-free scan batches and JSON
input lists are reused rather than rediscovered. A scan preview preflight or discovery failure may write a
diagnostic report in a safe system temporary directory before confirmation; it never installs a catalog.

**Why it matters:** Confirmation authorizes the prepared operation, not a rediscovered one. The temporary
report exception makes scan preview failures diagnosable without modifying the source or installing output.

**Rejected:** Running immediately, rediscovering after confirmation, and suppressing temporary reports for
scan preview failures would either weaken confirmation or hide diagnostics.
