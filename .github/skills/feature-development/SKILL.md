---
name: feature-development
description: Implement a new feature or behaviour change in AudioFormatConverter/FlacConvert (C++20, FFmpeg, SQLite) - e.g. "add an output format", "add a CLI option", "change how conversion/scan/duplicates/DB import works", "fix bug in FlacConvert". Use before editing FlacConvert/src or FlacConvert/include.
---

# Feature development (FlacConvert)

Use the global **project-memory** skill for every knowledge-base step (`kb.ps1`); do not copy it here.

## Read first

1. `AGENTS.md`, `.github/instructions/audio-cpp.instructions.md`, `.github/instructions/knowledge-protocol.instructions.md`
2. `FlacConvert/DESIGN.md` – the component, flow and contract you will touch
3. `docs/features/index.md` and the affected `F-xxx` entry (or the template for a new one)
4. `kb.ps1 related -Path <each file you will touch>` and `kb.ps1 search <concept>`; read every live K-entry returned

If a live K-entry or a DESIGN contract conflicts with the request, stop and tell the user before coding.
If intent is still ambiguous, ask one question (ask_user).

## Plan

Write a short plan covering:
- user-visible behaviour (CLI and menu), defaults, and what happens on failure;
- entry points (`path#Symbol`) and which DESIGN flow/contract changes;
- source-safety and install-after-success impact; temporary names; overwrite semantics;
- platform impact (`_WIN32` branches, long paths, UTF-8);
- validation fixtures (generated, never the user's library).

## Implement

- Follow existing naming: PascalCase types/methods, `_camelCase` members, camelCase locals; int/bool returns logged
  with spdlog. Repo conventions override the global code-review style guide.
- Add options to `include/ConversionOptions.h#ConversionOptions`, `CommandLine.cpp#ParseCommandLine` and
  `FlacConvertMain.cpp#ConfigureManualMp3` together so CLI and menu stay consistent. For a new output target or
  option, walk every site in DESIGN.md → Extension points.
- New FFmpeg arguments go into the argument vector (`MediaConvertionTask.cpp#conversionArguments`); never a shell string.
- New outputs go through a temporary + verification + `installFile`; never write into the source tree.

## Build and validate

```text
msbuild .\MediaBoost.sln /m /p:Configuration=Debug /p:Platform=x64
```
Then validate per `audio-cpp.instructions.md` (generated fixtures, probe outputs, hash sources). For conversion work
run `tests\mp3_conversion.py` (command in `README.md`) and extend it for new behaviour. Run the feature entry's
acceptance checks.

## Finish (same change)

- [ ] Assess whether the change materially affects behaviour, design, contracts, configuration, schemas or risks.
      Update `FlacConvert/DESIGN.md` and the affected F-entry only when their current content needs to change; create
      a new indexed F-entry for an important new feature, not for routine edits.
- [ ] `README.md` updated when a material CLI, menu or configuration change makes it inaccurate
- [ ] `kb.ps1 affected`: review each entry (`reviewed` / `cite` / supersede); record new decisions per the protocol
- [ ] Follow the **doc-sync** procedure, even if the skill is not auto-discovered. Use
      `python tools\doc_impact.py --details` when candidate documents need identifying.
- [ ] Do not create per-change history or knowledge entries for formatting or behaviour-preserving refactors
- [ ] Do not commit unless the user asks
