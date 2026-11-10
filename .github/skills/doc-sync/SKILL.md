---
name: doc-sync
description: Detect and fix drift between FlacConvert code, FlacConvert/DESIGN.md, docs/features F-entries, README.md and the knowledge base after a change, or when asked to "check docs", "sync docs" or "are the docs up to date".
---

# Doc sync (FlacConvert)

## Procedure

1. **Scope and materiality** – inspect the relevant staged, unstaged and untracked changes. Decide whether important
   behaviour, design, contracts, schemas or risks changed. Formatting, routine renames and behaviour-preserving
   refactors do not need a documentation entry or change log. When the impact is material or unclear, run
   `python tools\doc_impact.py --details` (or `--staged --details` / `--base <ref> --details`) for candidate documents.
2. **Knowledge** – `kb.ps1 affected` (`-Staged` before a commit) and `kb.ps1 check`; handle each entry per the
   knowledge protocol (`reviewed`, `cite`, or new entry + supersede).
3. **Citations** – for affected `path#Symbol` references in `FlacConvert/DESIGN.md` and `docs/features/*.md`, confirm
   the symbols still exist (grep or LSP `workspaceSymbol`). Fix moved or renamed citations. Every K-id they cite must
   be live in `docs/knowledge/index.md`; replace a superseded id with its successor.
4. **Content** – for materially affected symbols, or when their impact is unclear, compare against:
   - DESIGN: component map, flows, contracts, configuration table, file/DB schemas, platform boundaries;
   - F-entries citing it: behaviour, entry points, acceptance checks, limitations;
   - README: CLI options, menu text, config keys, output layout.
   Look especially for new CLI options, config keys, output files, audit fields, `TracksDB` columns, menu options.
5. **Fix** – update the document that owns each materially changed fact (what → DESIGN, user value/checks → F-entry,
   why → K-entry, rule → instructions). Remove duplicates rather than syncing two copies. Record no per-change
   history and do not manufacture documentation edits when nothing important changed.

## Report

Report material drift and its fix; if none, do not produce a change diary. Do not commit.
