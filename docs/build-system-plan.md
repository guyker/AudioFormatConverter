# Windows and Linux build-system plan

Status: implemented. Windows Debug/Release builds and CTest pass locally; Linux compilation and runtime validation remain pending in CI.

## Recommendation

Adopt CMake as the authoritative build description, Ninja as the default build
backend on both operating systems, CMake Presets for separate Windows/Linux
workflows, and CTest for test registration.

Split platform configuration, dependency discovery and runtime deployment, not
the application source list into two independently maintained build systems.
Keep MSVC on Windows and use GCC on Linux initially. Visual Studio can open the
CMake project directly; a generated solution can be supported separately if
needed. Ninja is a backend, not a replacement compiler.

Use an incremental migration, not an application rewrite. Keep the existing
solution temporarily as a Windows fallback, establish CMake parity, then retire
the hand-maintained solution/project in a separately approved cleanup.

## Findings from this repository

| Evidence | Implication |
|---|---|
| `FlacConvert\FlacConvert.vcxproj` owns the legacy Windows source lists, C++20 settings, libraries, manifest and DLL copy commands | Shared source targets and platform dependency/deployment rules now live in CMake; the solution remains a Windows fallback |
| Debug/Release x64 have the complete dependency configuration; Win32 Debug contains an absolute Boost path | x64 is the migration baseline; do not describe the existing Win32 configurations as validated support |
| CLI11 and spdlog are Git submodules; RapidJSON is vendored; SQLite 3.50.0 amalgamation is tracked | Reuse existing dependencies rather than introducing a package-manager migration |
| FFmpeg's local SDK identifies itself as `N-127252-ga25ba44c0c-20261008`, with libavcodec major 63 | Stock Linux FFmpeg packages cannot be assumed compatible with the current Windows SDK |
| `JsonUtils::tryParseMember` previously called `_stricmp` | `JsonUtils::caseInsensitiveEquals` now provides case-insensitive lookup without a Windows-only CRT dependency (K-033) |
| Sources use `std::format`, coroutines and ranges | Validate standard-library support, not just acceptance of a C++20 compiler flag |
| Conversion, file installation, entry points and platform utilities already have Windows/POSIX branches | Retain the shared implementation and exercise existing POSIX paths instead of duplicating the application |
| Tests include a Python integration suite and two manually linked C++ executables | Register existing tests with CTest and replace manual linking against Debug objects |
| Existing CI only reminded contributors about documentation impact | A separate Windows/Linux Debug/Release build and test workflow is now present |

K-006 supports this architecture. K-009 describes the current Windows SDK layout;
retain it during migration, not as a Linux dependency requirement. Preserve
K-010's Windows runtime-DLL requirement. Reassess affected knowledge entries when
implementation changes their facts; do not change their status during planning.

## Implemented structure

```text
CMakeLists.txt                 Shared project, C/C++ languages, CTest
CMakePresets.json              Windows/Linux Debug and Release presets
cmake\platform\
  Windows.cmake                MSVC options, SDK discovery, manifest, deployment
  Linux.cmake                  pkg-config FFmpeg discovery and Linux tools
FlacConvert\
  CMakeLists.txt              Explicit source list; core and executable targets
tests\
  CMakeLists.txt              Native tests, Python integration, fixture setup
out\build\<preset>\            Ignored, separate configuration outputs
```

`CMakeUserPresets.json` is ignored and holds local SDK locations or compiler
overrides. Keep platform modules small; use target-local settings and normal
CMake package discovery rather than creating a general-purpose framework.
Presets select native environments; they do not imply cross-compilation.

Targets:

- `FlacConvertCore`: internal static library containing the existing application
  implementation except `FlacConvertMain.cpp`; no new public SDK.
- `FlacConvert`: executable linking the core and containing the current entry
  point/menu/CLI orchestration.
- SQLite: compile only the existing `sqlite3.c` as C, with its existing effective
  settings preserved; link platform requirements through CMake.
- Existing CLI11 and spdlog targets, preserving header-only spdlog use; a small
  interface target for vendored RapidJSON.
- Existing native test executables linking the core, not build-directory object
  files.

Use `Threads::Threads` and appropriate platform libraries, including Windows
Shell32 and SQLite's platform link requirements. Keep MSVC flags off GCC and GCC
flags off MSVC. Require C++20 with extensions disabled and probe `std::format`.
Do not introduce a global warnings-as-errors policy for third-party code.

## Initial support scope

Proposed first verified configurations:

| Platform | Toolchain | Configurations |
|---|---|---|
| Windows x64 | MSVC and Windows SDK (local v145; hosted runner toolset) | Debug, Release |
| Ubuntu 24.04 x86-64 | GCC 13 and libstdc++ | Debug, Release |

CMake 3.28+ and Ninja are the documented baseline. Linux Clang, other distributions,
ARM64, Win32 and macOS are not first-release support promises.

Ubuntu 24.04 is the Linux CI reference. Its FFmpeg compatibility remains
unverified locally and must be established by the workflow before Linux support
is treated as verified.

## Dependency policy

Preserve CLI11/spdlog submodule revisions and vendored RapidJSON/SQLite during
the migration. Document recursive submodule initialization. Disable dependency
examples/tests. Missing dependencies produce actionable configure errors; no
silent substitute implementations or automatic unpinned downloads.

Windows:

- Discover the existing SDK through an overridable `FFmpeg_ROOT`, initially
  defaulting to the current repository-local SDK.
- Match headers, import libraries and runtime DLLs from the same SDK.
- Preserve the long-path manifest, Unicode entry point, UTF-8 compilation and
  required Windows system libraries.
- Use CMake deployment commands for required runtime DLLs rather than `xcopy`.
- Resolve `ffmpeg`/`ffprobe` separately from the libraries; make tools available
  explicitly to tests and document application runtime discovery.

Linux:

- Prefer distribution FFmpeg development packages, discovered with CMake's
  `FindPkgConfig` and imported targets for libavformat, libavcodec, libavfilter,
  libavutil and libswresample.
- Install the `ffmpeg`/`ffprobe` runtime tools separately and verify libmp3lame
  availability for MP3 tests.
- Use the system loader for distribution libraries, not copied Windows DLLs or
  a hard-coded global loader-path override.
- If distribution packages fail the compatibility spike, first assess a small,
  isolated compatibility adaptation. If not reasonable, use a pinned upstream
  FFmpeg build in a separate prefix discovered through `PKG_CONFIG_PATH`.
  Record its exact revision, configuration and checksum/source provenance.
- Do not silently downgrade FFmpeg or mix headers and binaries across releases.

Keep vcpkg/Conan out of this first change: both are standard options, but neither
is needed to replace the build system, and introducing either would also change
dependency acquisition and potentially FFmpeg features/licensing. Revisit only
if dependency maintenance demonstrates a concrete need.

## Developer workflow

After installing documented prerequisites and initializing submodules, the same
three commands apply on either OS, changing only the preset name:

```text
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

Linux uses `linux-debug`; release presets use `windows-release` and
`linux-release`. Windows Ninja commands run in an x64 MSVC developer environment.
Presets must match build directories/configurations for configure, build and test.
Local developer paths stay out of committed presets.

Add standard `cmake --install` support with `GNUInstallDirs` and `--prefix`.
Linux initially uses system runtime dependencies; Windows stages the executable
and its required runtime components. Do not copy the Windows-path sample
configuration into an automatically active Linux config: explicit commands work
with defaults, and examples should remain opt-in.

Install/staging support is in scope. Installers, `.deb`/`.rpm`, AppImage,
cross-compilation and fully self-contained release bundles are separate work.

## Implementation status and gates

1. **Compatibility:** Windows uses the local matching shared FFmpeg SDK. Linux
   targets Ubuntu 24.04 distribution FFmpeg packages. The current WSL machine is
   Ubuntu 22.04/GCC 11 and cannot validate the documented Linux baseline, so this
   gate depends on CI.
2. **Shared build:** CMake core/application targets, platform discovery/deployment
   modules and Windows/Linux Debug/Release presets are implemented. Windows
   Debug and Release both configure, build and pass CTest locally.
3. **Linux portability:** Linux pkg-config discovery and the portable JSON key
   comparison are implemented. Full Linux compile/runtime validation remains
   pending; do not treat POSIX branches or Windows validation as proof.
4. **Tests and CI:** CTest runs the native preservation/warning tests and existing
   generated-fixture integration suite. A Windows/Linux Debug/Release workflow is
   added; its clean-run result remains pending.
5. **Documentation/install:** README, AGENTS and DESIGN describe the CMake path;
   Windows install/staged executable launch passed locally. The Visual Studio
   solution remains a fallback until parity is established and its removal is
   separately approved.

Windows CI uses the MSVC toolset on the pinned `windows-2025` image and downloads
the shared FFmpeg SDK from the retained FFmpeg-Builds monthly release dated
2026-10-31, verifying its published SHA-256 digest.
Local Visual Studio builds continue to use v145. Linux CI pins Ubuntu 24.04.

## Acceptance criteria

- Fresh recursive checkouts configure/build through documented presets, with no
  personal absolute paths, stale `.obj` files or preexisting IDE state.
- Windows Debug/Release builds and all registered tests pass locally. Linux
  Debug/Release builds remain unverified until the workflow completes.
- Missing SDK headers/libraries/submodules produce clear configure errors.
- CTest runs both native checks with generated fixtures and the portable Python
  integration cases, using target paths rather than assumed `.exe` locations.
- Windows-specific sharing/locking tests remain Windows-only; add meaningful
  Linux permission/install-failure coverage instead of claiming skipped cases
  validate Linux behavior.
- Generated audio exercises FLAC and MP3, metadata/sidecars, Unicode/spaced paths,
  corrupt input, overwrite/collision handling and source preservation.
- Exercise scan, duplicates without folder opening, database import, CLI errors,
  and menu EOF/cancellation. Run headless CI without requiring `xdg-open`;
  qualify folder opening separately in a Linux desktop session.
- Check the optional ffprobe-executable metadata path as well as the default
  libav path; existing POSIX code alone is not evidence that both work.
- Installed/staged executables must launch independently of the source/build
  working directory with their documented runtime dependencies. Windows Debug
  and Release launches passed locally; Linux launch validation is pending CI.
- Preserve conversion settings, output layouts, database schema, Windows
  long-path behavior and source-safety contracts.

## Why this approach

CMake/Ninja/CTest is a common open-source C++ toolchain, supports Visual Studio
and command-line development, and gives developers and CI the same entry points.
Native MSBuild plus a new handwritten Makefile would duplicate source/dependency
definitions. Meson is credible but offers less migration benefit here given the
existing Visual Studio workflow and CMake-aware dependencies. Bazel would add
unnecessary infrastructure for one application.

Future macOS support adds a preset, discovery/deployment policy and CI coverage
to the same targets; it does not require redesigning the build again.

References:

- [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
- [CMake FindPkgConfig and imported targets](https://cmake.org/cmake/help/latest/module/FindPkgConfig.html)
- [CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html)

The available WSL distribution is Ubuntu 22.04 with GCC 11 and lacks CMake,
pkg-config and FFmpeg development packages; it does not match the Ubuntu 24.04/GCC
13 reference configuration. Linux support is not verified until the Ubuntu 24.04
CI build and generated-fixture CTest suite pass.
