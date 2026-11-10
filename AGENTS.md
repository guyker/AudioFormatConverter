# AGENTS.md

<!-- Router for AI agents and new developers. Keep it short; put detail in the files it points to. -->

## Project

AudioFormatConverter is a Windows C++ audio conversion and library-management utility. The single application,
`FlacConvert`, currently runs conversions through the FFmpeg executable; metadata and audio analysis also use FFmpeg
libraries. See `README.md` for the feature overview.

The core C++ sources must remain portable across Windows, Linux and macOS. Visual Studio solution/project files are a
Windows development/build wrapper, not a requirement that the application logic use Windows APIs. Isolate
platform-specific code behind narrow platform abstractions or conditional implementations.

## Map

| Folder / file | What it is | Read first |
|---|---|---|
| `CMakeLists.txt`, `CMakePresets.json` | Shared Windows/Linux build | `README.md`, then platform CMake files |
| `MediaBoost.sln` | Retained Windows fallback | `README.md`, then `FlacConvert/FlacConvert.vcxproj` |
| `FlacConvert/` | The application: components, flows, contracts, schemas | `FlacConvert/DESIGN.md` |
| `FlacConvert/src/`, `FlacConvert/include/` | Implementation and headers | `FlacConvert/DESIGN.md` component map, then the file |
| `docs/features/` | User-visible features (F-xxx): behaviour, entry points, acceptance checks | `docs/features/index.md` |
| `tests/` | Integration and ad-hoc C++ tests (not in the solution) | `README.md` test section |
| `external/` | Third-party source/SDK dependencies | `FlacConvert/FlacConvert.vcxproj` for expected paths |

## Skills

| Task | Repo skill |
|---|---|
| New feature or behaviour change | `.github/skills/feature-development` |
| Review a diff or PR | `.github/skills/code-review` (with the global code-review skill) |
| "How does X work / what breaks if I change Y" | `.github/skills/change-impact` |
| Check docs after a change | `.github/skills/doc-sync` |

## Build and test

The shared CMake presets use Ninja. Initialize submodules (`git submodule update --init --recursive`) and
install CMake 3.28+, Ninja, Python 3 and a C++20 toolchain first. Configure, build and run CTest with the
platform preset (`windows-debug`, `windows-release`, `linux-debug` or `linux-release`):

```text
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

Windows requires a matching shared FFmpeg SDK, selected with `FFmpeg_ROOT` (default:
`external/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/`). Linux uses pkg-config to find the FFmpeg development
libraries and requires the `ffmpeg`/`ffprobe` tools. SQLite amalgamation, CLI11 and spdlog are supplied in the
repository/submodules. Build artifacts live under ignored `out/build/`.
The existing Visual Studio solution remains a Windows fallback:
`msbuild .\MediaBoost.sln /m /p:Configuration=Debug /p:Platform=x64`.
CTest builds and runs the native regression executables and `tests/mp3_conversion.py` against disposable
generated fixtures. See `.github/instructions/audio-cpp.instructions.md` for audio validation and source safety.

## Where knowledge lives

| Kind | Where | Changes how |
|---|---|---|
| What the system is (design, flows, contracts) | `FlacConvert/DESIGN.md` | Kept aligned with behaviour changes |
| What users get (features, acceptance checks) | `docs/features/F-xxx-*.md` | Edited when important behaviour changes |
| Rules for changing it | `.github/instructions/*.instructions.md` (scoped by `applyTo`) | Rewritten |
| Why: decisions, traps, constraints, preferences, domain facts, known defects | `docs/knowledge/` (one file per entry; start at `docs/knowledge/index.md`) | Appended; the fact is never rewritten, only its status, review date and citations |
| How-to procedures | `.github/skills/*/SKILL.md` | Versioned with the tooling |
| Issues and change history | Work items, commits, pull requests | Closed, not edited |

Use the globally installed Project Memory skill and its `kb.ps1` script; do not copy the skill or script into this
repository. Start with `docs/knowledge/index.md`.

Read applicable scoped instructions and owning design documents before edits. Follow
`.github/instructions/knowledge-protocol.instructions.md` for knowledge lookup, recording and post-change maintenance.
For code changes, assess whether important behaviour, design, contracts, schemas or risks changed; update the
relevant DESIGN and feature entries only when needed. Do not create change-history entries for routine edits.
For material or uncertain impact, `python tools\doc_impact.py --details` helps locate candidate documents; it is
advisory, not a substitute for review.
