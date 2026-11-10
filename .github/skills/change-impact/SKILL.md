---
name: change-impact
description: Answer "how does X work", "where is Y", "what breaks if I change Z", or onboard to AudioFormatConverter/FlacConvert. Use for read-only questions about conversion, scan, duplicates, SQLite import, configuration, menu or platform code before any change is planned.
---

# Change impact and onboarding (FlacConvert)

Answer from the documentation first, then confirm in code. Read-only: do not edit files.

## Procedure

1. **Locate** – `docs/features/index.md` → the F-entry for the feature; `FlacConvert/DESIGN.md` → component map,
   flow and contracts.
2. **Why** – `kb.ps1 search <concept>` and `kb.ps1 related -Path <file>` (global project-memory skill). Note live
   decisions, constraints, traps and defects.
3. **Confirm in code** – go to the cited `path#Symbol` (LSP `goToDefinition`, `findReferences`, `incomingCalls`).
   Code is current behaviour; if docs disagree, report the drift.
4. **Impact** (for "what breaks") – list:
   - callers and both entry modes (CLI and menu) reaching the symbol;
   - DESIGN contracts and K-entries the change would touch;
   - output files/schemas affected (audit report, sidecar, album JSON, `TracksDB`) and their readers;
   - platform branches (`_WIN32` / POSIX) and tests (`tests/`) covering it;
   - F-entry acceptance checks that must be re-run.

## Answer format

- One-paragraph answer, then evidence as `path#Symbol` and F-/K-ids.
- For impact questions: a table of affected item → why → check to run.
- State what is unverified.
