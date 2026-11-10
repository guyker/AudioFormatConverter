# Feature registry

One file per user-visible feature: `F-xxx-<kebab-name>.md`. An entry says **what the user gets and how to check it**.
How it works lives in `FlacConvert/DESIGN.md`; why lives in `docs/knowledge/` (K-xxx); rules live in
`.github/instructions/`. Do not copy content between them; link instead.

Unlike K-entries, F-entries are **edited in place** when important behaviour changes, in the same change as the code.
Reference the F-id in GitHub issues, PRs and commit messages (`F-002`). For an important new feature, use the next
free id and add a row below. Retired features keep their file with status `removed`.

## Keeping features current

Document important user-visible behaviour, contracts, outputs and acceptance checks, not every code change. Update
the current entry and `FlacConvert/DESIGN.md` when they actually become inaccurate; do not add a per-change history
for formatting, routine renames or behaviour-preserving refactors. Record durable reasons and verified risks in the
knowledge base only when its protocol calls for them.

Once installed, the advisory pre-commit hook issues a short reminder; the committed pull-request workflow does the
same. Neither can decide whether a change is important or update the documentation. For candidate entries and
structural warnings, run
`python tools\doc_impact.py --details` (`--staged --details` for the index, or `--base <ref> --details` for commits).
The candidates come from existing file citations and may omit indirect impacts or new features; inspect the actual
diff and this index. The reports do not write entries or retain a change history.

| Id | Feature | Status |
|---|---|---|
| [F-001](F-001-flac-normalization.md) | Convert a library to 16-bit / 44.1 kHz FLAC | active |
| [F-002](F-002-flac-to-mp3.md) | Convert FLAC to MP3 | active |
| [F-003](F-003-conversion-audit.md) | Preservation audit, report and failure marking | active |
| [F-004](F-004-scan-to-json.md) | Scan libraries to JSON metadata | active |
| [F-005](F-005-duplicate-albums.md) | Find duplicate albums | active |
| [F-006](F-006-sqlite-import.md) | Import JSON metadata into SQLite | active |
| [F-007](F-007-configuration.md) | Configuration loading | active |
| [F-008](F-008-interactive-menu.md) | Interactive menu, preview and confirmation | active |
| [F-009](F-009-menu-option-5.md) | Menu option 5 | not implemented |

## Template

```markdown
# F-xxx: <title>

Status: active | experimental | deprecated | removed | not implemented

## Behaviour
<What the user sees and gets, including outputs and exit codes.>

## Entry points
- <CLI command / menu option> → `path#Symbol`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → <section>
- K-xxx (short topic)

## Acceptance checks
- [ ] <observable check an agent or reviewer can run>

## Known limitations
- <limitation>
```
