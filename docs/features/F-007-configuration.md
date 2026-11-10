# F-007: Configuration loading

Status: active

## Behaviour
Settings come from `config.json`. CLI order: `--config <file>` → directory recorded in
`AppSettingsJsonPersistent.txt` (current directory) → `./config.json` → built-in defaults with no libraries. Invalid
`LogLevel` (outside 0–6) or `AlbumsSplitThreshold <= 0` fails. The menu may prompt for a config directory and offer
to create a default file.

## Entry points
- `AppSettings.cpp#AppSettingsJson::LoadForCommandLine` (CLI), `AppSettings.cpp#AppSettingsJson::AppSetting` (menu)
- `AppSettings.cpp#AppSettingsJson::GetDefaultSettings`, `include/AppSettings.h#MediaDirectoryElement`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Configuration
- K-007 default output directory

## Acceptance checks
- [ ] `--config missing.json` exits non-zero with a clear message.
- [ ] With no config file, `convert` works using defaults.
- [ ] `AlbumsSplitThreshold: 0` is rejected.
- [ ] `mediaName` containing a path separator is rejected by scan/import.

## Known limitations
- `DatabaseFileName`, `resultPath`, `dbPath` and most `FLACSettings` strings are read but unused.
- `AppSettingsJsonPersistent.txt` lives in the current directory.
