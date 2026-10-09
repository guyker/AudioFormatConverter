---
id: K-007
kind: decision
title: "Use platform-appropriate default output directories"
area: [FlacConvert]
status: active
created: 2026-10-09
reviewed: 2026-10-10
cites:
  - path: FlacConvert/include/AppSettings.h
    symbol: "AppSettingsJson::DefaultOutDirectory"
---

**Fact:** `AppSettingsJson::DefaultOutDirectory` retains `C:\tmp` on Windows and uses the platform temporary directory on Linux and macOS.

**Why it matters:** The CLI remains compatible with its selected Windows default without passing a Windows-only path to builds on other platforms.

**Rejected:** A single hard-coded Windows path is not portable; using the system temporary directory on Windows would change the selected CLI default.
