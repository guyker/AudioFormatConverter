---
id: K-045
kind: decision
title: "CMake presets are the only build definition; no hand-maintained Visual Studio solution"
area: [Repository]
status: active
created: 2026-11-16
reviewed: 2026-11-16
cites:
  - path: CMakePresets.json
    symbol: configurePresets
---

**Fact:** The root `CMakeLists.txt`, `FlacConvert/CMakeLists.txt`, `cmake/platform/*.cmake` and `CMakePresets.json` are the
only build description for Windows and Linux. The former `MediaBoost.sln`/`FlacConvert.vcxproj` was removed by
maintainer decision after it stopped linking (MSBuild `LNK1120`: it lacked `CatalogDatabase.cpp`,
`DecodeAnalyzer.cpp`, `MediaRecordJson.cpp`, `TagLibReader.cpp` and TagLib). Visual Studio users open the
repository folder and select a Windows preset.

**Why it matters:** A second, hand-maintained source/dependency list silently drifts from CMake; new sources or
dependencies must be added only to the CMake targets.

**Rejected:** Patching the vcxproj to match CMake (duplicates source lists, cannot follow the CMake-fetched TagLib
pin); keeping it as an unsupported fallback (misleads contributors into a build that does not link). A solution can
still be generated on demand with a CMake Visual Studio generator; it is not committed.
