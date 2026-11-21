# F-011: Repair recognized FLAC/MP3 metadata defects

Status: active

## Behaviour
The `repair` command and menu option 7 inspect `.flac` and `.mp3` files against a catalog of metadata rules (see
DESIGN → Metadata repair). Fixable defects include redundant ID3/APE/Lyrics3 tags, duplicate or empty tags/frames,
multiple FLAC comment blocks, wrong picture MIME types or FLAC picture dimensions/depth, invalid FLAC seek tables and ID3v2.3 frames using
v2.4-only text encodings. Tags are removed only when empty, exact duplicates, or fully redundant; conflicting values
(including conflicting or semicolon-joined CDTOC), invalid comments, missing FLAC MD5, MP3 stream garbage and
unreadable files are reported for manual review. `--rules <id,...>` limits the CLI to selected rules.
MP3 ID3v1 tags are kept by default for older players; remove redundant ones only with `--remove-id3v1`,
an explicit `--rules mp3.redundant-id3v1` selection, or the menu's per-run opt-in. Existing FLAC padding is
preserved and metadata shrinkage is absorbed into padding when possible.

Sources are never modified and audio bytes are copied unchanged, never decoded or re-encoded. Repaired files are written to
`<output-root>/FIXED/<library-name>` (`--output-dir`, else `OutDirectory` for the CLI); only folders with a repair are
copied, retaining all other files unchanged. In menu option 7, repair outputs of different active libraries may not
overlap each other or another library's source; the run fails before installing anything.
`repair-report.json` records findings, file installation status, and whether audio or metadata changed. Existing
destinations are rejected unless `--overwrite` is specified. The CLI returns non-zero for manual-review findings or
operation failures.
`--dry-run` prints the full JSON findings and planned actions without writing repair output; normal session logging
still applies. Inspection and staging use four workers by default; CLI `--jobs <1-16>` changes the limit.
Before the preview, progress identifies the source folder and the discovery, inspection and planning phases.
Inspection shows completed/total files and a percentage; discovery/planning show counts without an invented total.
During execution, progress shows verified files installed out of all planned repairs and unchanged copies.
Each phase shows elapsed time and remains visibly active during long files. Progress is per library and per phase;
phases with a known total show a rough ETA. Interactive updates reuse one line; redirected output uses periodic snapshots instead
of terminal control sequences. Phase boundaries are retained in the session log in both modes.

## Entry points
- `repair <input> [--output-dir <directory>] [--overwrite] [--rules <ids>] [--remove-id3v1] [--dry-run] [--jobs <1-16>]` → `FlacConvert/src/FlacConvertMain.cpp#RunWithArgumentsImpl`
  → `FlacConvert/src/AudioRepair.cpp#AudioRepair::Prepare`, `FlacConvert/src/AudioRepair.cpp#AudioRepair::Execute`
- Menu option 7 → `FlacConvert/src/FlacConvertMain.cpp#RepairConfiguredLibraries`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Metadata repair; rules in `FlacConvert/src/RepairRules.cpp#RepairRules::Analyze`
- Knowledge: no separate entry; the stable repair and verification contract is documented in DESIGN.

## Acceptance checks
- [ ] Identical duplicate CDTOC comments are reduced to one in the output; source bytes remain unchanged.
- [ ] FLAC leading/trailing ID3, empty comments, picture MIME/width/height/depth and invalid seek table are fixed; audio bytes unchanged.
- [ ] MP3 duplicate/empty frames, v2.3 text, custom-text, comment and lyrics encodings, and APIC MIME are fixed; audio bytes unchanged.
- [ ] MP3 ID3v1 stays by default and is removed only when redundant and explicitly selected.
- [ ] FLAC padding remains available; metadata shrinkage preserves the audio offset where padding can absorb it.
- [ ] Dry-run prints every finding without creating repair output, and parallel and single-worker runs agree.
- [ ] Conflicting, joined CDTOC, non-redundant tags and unreadable files are reported without changing source files.
- [ ] Re-running repair on repaired output finds nothing to repair; unknown `--rules` ids are rejected.
- [ ] Only affected folders are copied and companion files remain byte-identical.
- [ ] Menu option 7 requires `RUN` and reports the output and completed repair count.
- [ ] Existing output is rejected by default; `--overwrite` allows eligible destinations to be replaced.
- [ ] Preparation displays progress before confirmation without writing repair output; cancellation keeps sources unchanged.
- [ ] Installation progress advances only after verification/installation and stops short of 100% on failure.

## Known limitations
- Only FLAC and MP3 are supported; only the first leading ID3v2 tag of an MP3 is rewritten.
- Different values are never reconciled automatically; audio-stream problems (missing MD5, MP3 garbage) are report-only.
