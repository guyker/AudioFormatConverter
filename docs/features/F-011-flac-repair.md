# F-011: Repair recognized FLAC metadata defects

Status: active

## Behaviour
The `repair` command and menu option 7 inspect native FLAC files and safely remove repeated CDTOC Vorbis comments
only when every occurrence has the same value. The first occurrence is retained. Conflicting values, semicolon-joined
values and unreadable FLACs are left unchanged and reported for manual review.

Sources are never modified and audio is not decoded or re-encoded. Repaired files are written to
`<output-root>/FIXED/<library-name>` (`--output-dir`, else `OutDirectory` for the CLI); only folders with a repair are
copied, retaining all other files unchanged.
`repair-report.json` records findings, file installation status, and whether audio or metadata changed. Existing
destinations are rejected unless `--overwrite` is specified. The CLI returns non-zero for manual-review findings or
operation failures.

## Entry points
- `repair <input> [--output-dir <directory>] [--overwrite]` → `FlacConvert/src/FlacConvertMain.cpp#RunWithArgumentsImpl`
  → `FlacConvert/src/AudioRepair.cpp#AudioRepair::Prepare`, `FlacConvert/src/AudioRepair.cpp#AudioRepair::Execute`
- Menu option 7 → `FlacConvert/src/FlacConvertMain.cpp#RepairConfiguredLibraries`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → FLAC repair
- Knowledge: no separate entry; the stable repair and verification contract is documented in DESIGN.

## Acceptance checks
- [ ] Identical duplicate CDTOC comments are reduced to one in the output; source bytes remain unchanged.
- [ ] Audio payload and all non-comment metadata blocks remain byte-identical.
- [ ] Conflicting, joined and unreadable CDTOC findings are reported without changing source files.
- [ ] Only affected folders are copied and companion files remain byte-identical.
- [ ] Menu option 7 requires `RUN` and reports the output and completed repair count.
- [ ] Existing output is rejected by default; `--overwrite` allows eligible destinations to be replaced.

## Known limitations
- This repair currently handles only duplicate CDTOC comments in native FLAC Vorbis-comment blocks.
- Different values or semicolon-joined values are not automatically reconciled.
