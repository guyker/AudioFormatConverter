---
id: K-006
kind: preference
title: "Keep core C++ portable across target platforms"
area: [FlacConvert]
status: active
created: 2026-10-09
reviewed: 2026-10-09
uncited: "This preference was stated by the maintainer and is not derived from repository code."
---

**Fact:** The maintainer requires the shared core C++ sources to compile on Windows, Linux and macOS; Visual Studio solution/project files are only a development/build wrapper.

**Why it matters:** Avoid introducing platform-specific dependencies into shared application logic; isolate them behind platform adapters or conditional implementations.
