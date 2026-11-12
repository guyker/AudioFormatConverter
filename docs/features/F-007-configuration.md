# F-007: Configuration loading

Status: active

## Behaviour
Settings come from `config.json`. CLI order: `--config <file>` → directory recorded in
`AppSettingsJsonPersistent.txt` (current directory) → `./config.json` → built-in defaults with no libraries. Invalid
`LogLevel` (outside 0–6), `AlbumsSplitThreshold <= 0`, or a negative/non-integer
`MetadataScanMaxThreads` fails. `MetadataScanMaxThreads` defaults to `0`, which uses hardware concurrency (fallback 4);
a positive value caps concurrent metadata workers per album. `MetadataSidecars` sets the conversion sidecar
`Format` (`binary`|`json`, default `binary`) and per-target policies `ConvertToFlac` / `ConvertToMp3`
(`disabled`|`optional`|`required`, default `disabled`); other values fail loading. The sample config uses
JSON comments to explain these options. `disabled` never writes a sidecar; `optional` writes by default and can
be disabled for a run; `required` always writes and rejects disable requests. The menu may prompt for a config
directory and offer to create a default file.

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
- [ ] `MetadataScanMaxThreads: -1` is rejected; `0` selects automatic concurrency.
- [ ] `MetadataSidecars` config comments load; omitting its target policies defaults both to `disabled`.
- [ ] `MetadataSidecars.Format: "xml"` is rejected; `--no-metadata-sidecar` fails when the target policy is
      `required` and succeeds without a sidecar when it is `optional`.
- [ ] `mediaName` containing a path separator is rejected by scan/import.

## Known limitations
- `DatabaseFileName`, `resultPath`, `dbPath` and most `FLACSettings` strings are read but unused.
- `AppSettingsJsonPersistent.txt` lives in the current directory.
