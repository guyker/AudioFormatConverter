# F-003: Preservation audit, report and failure marking

Status: active

## Behaviour
Every conversion (F-001, F-002) writes `conversion-audit.json` to the library output folder, recording each planned
file, its install status and per-resource preservation result (tags, pictures, chapters, metadata blocks). Unchanged
copies are verified byte-for-byte, including unsupported audio retained by FLAC conversion. FLAC reports also list
`skippedUnchangedDirectories` (topmost folders with audio but no conversions, with file/audio counts); `"."`
denotes the library root. While running, a
`.conversion-in-progress` marker exists; it is removed on success.
On failure the report is still written and, if this run created the library folder, the folder is renamed
`ERROR_<AUDIO|PRESERVATION|COPY|IO>_<name>` or `INCOMPLETE_<name>`.

## Entry points
- `FolderConvert.cpp#FolderConvert::ExecuteConversion` (marker, finish, rename), `FolderConvert.cpp#writeAudit`
- `PreservationAudit.cpp#PreservationAudit::Compare`, `PreservationAudit.cpp#PreservationAudit::VerifyCopy`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Files and schemas (`conversion-audit.json`); Contracts (ownership, reserved names)
- K-029 preservation audit, K-030 failure marking and ownership, K-024 sequential stop-on-error,
  K-036 complete folders and skipped unchanged folders

## Acceptance checks
- [ ] Successful run: report `status: completed`, `errorType: ""`, counts equal plan, no marker.
- [ ] Forced audio failure in a new output folder: folder renamed `ERROR_AUDIO_…`, marker retained, report
      `status: failed`.
- [ ] Failure in a pre-existing output folder: folder **not** renamed.
- [ ] Source containing `conversion-audit.json` or `.conversion-in-progress` is rejected at plan time.
- [ ] FLAC reports include copied unsupported audio and skipped unchanged folders, including no-conversion libraries.
- [ ] A copied-file failure does not report unattempted copies as installed.
- [ ] `tests/preservation_audit.cpp` passes when built against the Debug objects.

## Known limitations
- `schemaVersion` 1; no reader in this repository consumes the report.
- A crash (not a handled error) leaves the marker and temporaries without a report.
