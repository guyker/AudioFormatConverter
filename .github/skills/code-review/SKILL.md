---
name: code-review
description: Repository-specific review of changes in AudioFormatConverter/FlacConvert (C++ FFmpeg/SQLite code, docs). Use when asked to review a diff, PR, branch or files in this repository. Runs in addition to the global code-review skill and overrides its naming/style rules.
---

# Code review (FlacConvert)

Run the global **code-review** skill for general C++ correctness. This skill adds the repository contract and
**overrides** the global style guide: repo names are PascalCase types/methods, `_camelCase` members, camelCase
locals; do not flag them. Ignore C#/WPF guidance.

## Read first

1. The diff (`git --no-pager diff` / PR files)
2. `FlacConvert/DESIGN.md` sections for every touched component
3. `kb.ps1 related -Path <each changed file>`; live K-entries are constraints the diff must respect
4. Affected `docs/features/F-xxx` entries
5. `.github/instructions/audio-cpp.instructions.md`

## Checklist

**Source safety and installation**
- [ ] No write, rename or delete can target the source tree; overlap checks still run before any write.
- [ ] Outputs are written to a unique temporary, verified, audited, then installed; no direct write to the final name.
- [ ] Without overwrite, an existing target is never replaced; sidecar/audio install order and rollback preserved.
- [ ] Reserved names, marker and owned-folder rename semantics unchanged unless DESIGN is updated.

**FFmpeg**
- [ ] FFmpeg/ffprobe run via argument vector (`runFFmpeg`); no new shell strings, `system`, or `popen`.
- [ ] Windows arguments pass through `quoteWindowsArgument` and `PlatformUtils::WindowsLongPath`.
- [ ] A new `AudioTarget` or option updates every site in DESIGN.md → Extension points (no binary `Mp3 ? : ` fallthrough).
- [ ] Output verification (codec, rate, depth/channels, duration) still covers new formats/options.
- [ ] libav resources freed on every path; log callback remains thread-safe.

**Paths, encoding, portability**
- [ ] Paths stay `std::filesystem::path`; UTF-8 conversions via `CommonUtils`/`PlatformUtils` helpers.
- [ ] Configured paths for I/O, canonical paths only for comparisons (K-032); no new filesystem probes on canonical paths.
- [ ] Cited K-entries are live (`status: active`); superseded ones are history, not rules.
- [ ] New platform code isolated in `_WIN32` / POSIX branches or `PlatformUtils`; no new Windows-only APIs in core.

**SQLite and JSON**
- [ ] Databases opened with the `win32-longpath` VFS on Windows; prepared statements with bound values only.
- [ ] Transactions rolled back on failure; statements finalised and handles closed on every path.
- [ ] Generated JSON still replaced via staging (`installGeneratedFiles`).

**Behaviour and docs**
- [ ] CLI and menu stay consistent; previews and summaries report confirmed counts only.
- [ ] DESIGN.md, the F-entry and README match materially changed behaviour; `kb.ps1 affected` entries handled.
      Do not require documentation churn for behaviour-preserving edits.
- [ ] Validation evidence uses generated fixtures; MSBuild x64 Debug passes.

Report findings by severity with `path#Symbol` and the violated DESIGN contract or K-id.
