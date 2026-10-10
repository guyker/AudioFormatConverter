---
id: K-018
kind: defect
title: "Linux compilation is blocked by Windows-only JSON key comparison"
area: [FlacConvert]
status: active
created: 2026-10-10
reviewed: 2026-10-10
cites:
  - path: FlacConvert/include/JsonUtils.h
    symbol: "JsonUtils::tryParseMember"
---

**Fact:** `JsonUtils::tryParseMember` calls Windows-only `_stricmp` without a non-Windows implementation. A GNU C++20 syntax check of the conversion sources in WSL fails on this declaration; an isolated `strcasecmp` compatibility definition lets those sources compile. The call predates the MP3 extension and is not fixed by it.

**Why it matters:** Portable conversion branches cannot establish a working Linux build while this shared header remains uncompilable. A compatibility-shim syntax check is not a complete Linux build or end-to-end conversion.
