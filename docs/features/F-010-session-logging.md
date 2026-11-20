# F-010: Per-request session logging

Status: active

## Behaviour
Each process invocation (CLI command or interactive-menu session) writes a unique `.log` file under
`%LOCALAPPDATA%\AudioFormatConverter\logs` on Windows, `$XDG_STATE_HOME/audioformatconverter/logs` or
`$HOME/.local/state/audioformatconverter/logs` elsewhere, falling back to the system temporary directory when the
platform-specific location is unavailable. The console remains concise: informational progress (including the current
source album folder during FLAC conversion), completion, meaningful warnings, and errors; per-file details and
expected generated-field normalization are reserved for the session log. Redirected output contains no animated
progress control sequences. Scan folder discovery and track collection report counts, discovered folders/albums,
elapsed time and (for track collection) an ETA on one updating line, with periodic snapshots when redirected; scan
metadata progress spans all batches of a library.
Before any menu action prepares work, it logs all active source library paths. CLI commands log the input folder,
explicitly labeled as catalog JSON for duplicates/database import. Conversion and scan library transitions and
repair preparation identify the current source folder, including before long preprocessing begins.

The config `LogLevel` (integer spdlog level 0–6) controls the detailed file log; CLI commands can override it with
`--log-level info|debug|trace`. Start and finish entries are always written when the log is available. The file
threshold does not suppress console warnings or errors. Conversion audit and scan-report
JSON remain the authoritative structured records; session logs supplement them. TagLib and FFmpeg diagnostics are
logged with the file path when known. FFmpeg warnings without file context remain explicitly unattributed rather than
being assigned to a track.

Inactive application-owned logs older than 30 days are removed; the logger also prunes oldest inactive logs when their combined size exceeds
1 GiB. Active requests are not pruned. If a session log cannot be created, the request continues with console logging
and reports the failure.

## Entry points
- CLI commands → `FlacConvertMain.cpp#RunWithArguments` → `RunLogging.cpp#RunLogging::Start` / `RunLogging::Finish`
- Menu option 1 → `FolderConvert.cpp#FolderConvert::ExecuteConversion`
- Menu option 2 → `FlacConvertMain.cpp#ScanFolderAndCreateJSON`, `TagLibReader.cpp#TagLibReader::Read`
- Detailed log override → `CommandLine.cpp#add_config_option`, `FlacConvertMain.cpp#RunWithArgumentsImpl`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Run logging; Configuration
- Structured reports: F-003 (`conversion-audit.json`), F-004 (`scan-report.json`)

## Acceptance checks
- [ ] Each CLI invocation creates a distinct session log with start and finish status.
- [ ] The menu uses the same logger and configured file threshold.
- [ ] `--log-level info|debug|trace` overrides the file threshold; console warnings and errors remain visible.
- [ ] Conversion console output omits per-file `Processing:` messages, FFmpeg progress stats, and expected generated-field normalization.
- [ ] FFmpeg child diagnostics and TagLib debug messages are associated with their source file in the detailed log.
- [ ] Scan warnings/errors remain in `scan-report.json`; unscoped FFmpeg entries remain unattributed and are not lost from the session log.
- [ ] Redirected scan output contains no terminal control sequences.
- [ ] Log-file creation failure is explicit and does not prevent the requested operation.
- [ ] Retention preserves active logs and prunes expired or oldest inactive logs within the configured bounds.

## Known limitations
- TagLib debug output depends on how TagLib was built; release builds may omit those messages.
- Forced termination may leave an active marker; stale markers are retained when process identity cannot be verified.
