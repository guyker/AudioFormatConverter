# F-008: Interactive menu, preview and confirmation

Status: active

## Behaviour
Running with no arguments shows a numbered menu over the configured active libraries. Every mutating option prints
an operation preview (inputs, outputs, counts, overwrite effects) and runs only after the user types exactly `RUN`;
`BACK` returns to the menu; `EXIT`, Enter, any other text or EOF cancels without writing and ends the program.
Conflicts block the run before any write. After a started or failed operation a summary reports Completed, Partially
completed or Failed with confirmed counts, and the program exits. `0` or EOF at the menu exits; `?` shows help.

## Entry points
- `FlacConvertMain.cpp#RunLegacyMenu`, `FlacConvertMain.cpp#GetUserAction`, `FlacConvertMain.cpp#ConfirmPreview`,
  `FlacConvertMain.cpp#ManualOperationSummary`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Entry points; Key flows
- K-024 confirmed progress and partial completion, K-025 MP3 options per operation, K-026 separate FLAC output tree,
  K-027 `RUN` after reusable previews, compact menu and contextual help

## Acceptance checks
- [ ] Answering anything but `RUN` writes nothing.
- [ ] `BACK` returns to the menu; EOF on stdin exits cleanly.
- [ ] A conflict in one library blocks all libraries.
- [ ] Summary counts match files actually installed.

## Known limitations
- Option 3 always opens duplicate folders.
- Option 5 is not implemented (F-009).
