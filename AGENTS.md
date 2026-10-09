# AGENTS.md

<!-- Router for AI agents and new developers. Keep it short; put detail in the files it points to. -->

## Project

AudioFormatConverter is a Windows C++ audio conversion and library-management utility. The single application,
`FlacConvert`, currently runs conversions through the FFmpeg executable; metadata and audio analysis also use FFmpeg
libraries. See `README.md` for the feature overview.

## Map

| Folder / file | What it is | Read first |
|---|---|---|
| `MediaBoost.sln` | The Visual Studio solution | `README.md`, then `FlacConvert/FlacConvert.vcxproj` |
| `FlacConvert/src/` | Application implementation | The relevant `.cpp` file and matching headers in `FlacConvert/include/` |
| `FlacConvert/include/` | Application headers | The relevant header and implementation |
| `external/` | Third-party source/SDK dependencies | `FlacConvert/FlacConvert.vcxproj` for expected paths |

## Build and test

From a Visual Studio Developer PowerShell/Command Prompt with the C++ v143 toolset:

```text
msbuild .\MediaBoost.sln /m /p:Configuration=Debug /p:Platform=x64
```

Prefer x64 (C++20, v143); Win32 Debug contains a machine-specific Boost path. The project expects the FFmpeg SDK under
`external/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/` and SQLite amalgamation sources under
`external/sqlite-amalgamation/`. Check prerequisites before building; use matching FFmpeg headers, libraries and runtime
DLLs, and record the exact build/version when changing dependencies rather than relying on the `latest` folder name.
The solution has no test project or automated test command. See `.github/instructions/audio-cpp.instructions.md` for
audio validation and source-file safety.

## Where knowledge lives

| Kind | Where | Changes how |
|---|---|---|
| What the system is (design, flows, contracts) | `DESIGN.md` next to a component, when present | Kept aligned with behaviour changes |
| Rules for changing it | `.github/instructions/*.instructions.md` (scoped by `applyTo`) | Rewritten |
| Why: decisions, traps, constraints, preferences, domain facts, known defects | `docs/knowledge/` (one file per entry; start at `docs/knowledge/index.md`) | Appended; the fact is never rewritten, only its status, review date and citations |
| How-to procedures | `.github/skills/*/SKILL.md` | Versioned with the tooling |
| Issues and change history | Work items, commits, pull requests | Closed, not edited |

Use the globally installed Project Memory skill and its `kb.ps1` script; do not copy the skill or script into this
repository. Start with `docs/knowledge/index.md`. This repository currently has no `DESIGN.md`.

Read applicable scoped instructions and owning design documents before edits. Follow
`.github/instructions/knowledge-protocol.instructions.md` for knowledge lookup, recording and post-change maintenance.
