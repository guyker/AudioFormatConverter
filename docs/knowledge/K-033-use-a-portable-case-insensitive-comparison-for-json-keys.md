---
id: K-033
kind: trap
title: "Use a portable case-insensitive comparison for JSON keys"
area: [FlacConvert]
status: active
created: 2026-11-10
reviewed: 2026-11-10
cites:
  - path: FlacConvert/include/JsonUtils.h
    symbol: "JsonUtils::caseInsensitiveEquals"
---

**Fact:** `JsonUtils::tryParseMember` tries exact JSON member lookup first, then compares member names through `JsonUtils::caseInsensitiveEquals`, which uses `std::tolower` with unsigned bytes instead of a platform-specific CRT function.

**Why it matters:** This shared header is compiled on every supported platform. A Windows-only case-insensitive function prevents Linux compilation even when the remaining platform branches are portable.
