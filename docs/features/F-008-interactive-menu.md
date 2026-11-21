# F-008: Interactive menu, preview and confirmation

Status: active

## Behaviour
Running with no arguments shows a numbered menu over the configured active libraries. Every mutating option prints
an operation preview (inputs, outputs, counts, overwrite effects) and runs only after the user types exactly `RUN`;
`BACK` returns to the menu; `EXIT`, Enter, any other text or EOF cancels without writing and ends the program.
Conflicts block the run before installing output. After a started or failed operation a summary reports Completed, Partially
completed or Failed with confirmed counts, and the program exits. Duplicate review also reports `Stopped by user`
when ended early. `0` or EOF at the menu exits; `?` shows help.
Option 7 validates and repairs recognized FLAC/MP3 metadata issues into `OutDirectory/FIXED/<mediaName>` without
modifying sources; it copies only folders with safe repairs and requires `RUN` after preview. A repair output that
overlaps another active library's source or another library's repair output (for example, duplicate `mediaName`
values) blocks the run before any repair is installed. Redundant MP3 ID3v1
removal is offered per run and defaults to NO. See F-011 for the
supported repair and manual-review cases.
After the initial `RUN`, option 3 pauses per duplicate group, with a numbered evidence/metadata recap beside the
choice prompt; an optional whole-folder move to `deleted` requires
its own path preview and exact `MOVE` confirmation (F-005). Enter continues the review rather than canceling it.
Before the option 2 preview, a per-run YES/NO prompt selects byte-exact FLAC metadata archives; Enter keeps the
configured default (normally NO), and BACK/EXIT/EOF cancels before the scan. The preview shows the selected policy
and warns that prior manifest-owned archives are retired after success (F-004). The preview also counts folders that
will be skipped as directory links or beyond `RecursionDirectorySearchDepth`; the completion summary counts installed
JSON batch files (fragmented albums can produce more than one file per album).
An exception is a scan preview preflight or discovery failure: it attempts to write a diagnostic report to a safe
system temporary directory and logs its location; it does not install a catalog.

## Entry points
- `FlacConvertMain.cpp#RunLegacyMenu`, `FlacConvertMain.cpp#GetUserAction`, `FlacConvertMain.cpp#ConfirmPreview`,
  `FlacConvertMain.cpp#ManualOperationSummary`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Entry points; Key flows
- K-024 confirmed progress and partial completion, K-025 MP3 options per operation, K-026 separate FLAC output tree,
  K-049 `RUN` after reusable previews and scan failure diagnostics, compact menu and contextual help

## Acceptance checks
- [ ] Canceling at the confirmation writes nothing; a scan preview failure may write a temporary diagnostic report.
- [ ] `BACK` returns to the menu; EOF on stdin exits cleanly.
- [ ] Option 2 can enable or disable FLAC archives before the preview without changing the saved configuration.
- [ ] Option 7 previews FLAC/MP3 repair findings and requires `RUN` before writing repaired copies.
- [ ] Option 7 rejects two active libraries whose repair outputs collide without creating `FIXED` output.
- [ ] A conflict in one library blocks all libraries.
- [ ] Summary counts match files actually installed.

## Known limitations
- Option 3 always opens duplicate folders.
- Option 5 is not implemented (F-009).
