---
id: K-023
kind: decision
title: "Keep the manual menu compact with contextual details and one confirmation"
area: [FlacConvert]
status: active
created: 2026-10-11
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: GetUserAction
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConfirmManualAction
---

**Fact:** `GetUserAction` retains menu numbers 0-6 and shows compact labels; selecting an available action displays only its Input/Output/Changes details before preparing its preview. `?` displays every description without preparing an action. `ConfirmManualAction` combines Run/Back/Exit in the existing preview confirmation: `REPLACE` runs option 1, `RUN` runs other actions, `BACK` returns to selection, and `EXIT`, Enter, unrecognized text or EOF cancels and exits. No action starts on Back/Exit.

**Why it matters:** A long initial menu hides the choices. Contextual details and a single confirmation keep the interface readable without weakening source-replacement protection or authorizing writes merely by selecting a number.

**Rejected:** Printing every detailed description on every selection wastes console space. Stacking a generic Run prompt before the existing RUN/REPLACE confirmation adds an unnecessary decision and can obscure which prompt actually authorizes changes.
