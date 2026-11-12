---
id: K-038
kind: decision
title: "Default metadata sidecars off unless configured"
area: [FlacConvert]
status: active
supersedes: [K-037]
created: 2026-11-12
reviewed: 2026-11-12
cites:
  - path: FlacConvert/include/AppSettings.h
    symbol: MetadataSidecarSettings
---

**Fact:** `MetadataSidecarSettings` defaults both conversion target policies to `disabled`; the bundled config documents
`Format` (`binary` or `json`) and each target's `disabled` / `optional` / `required` policy in comments. The loader
accepts JSON comments via RapidJSON's comment parsing flag. `optional` and `required` retain their existing semantics;
when disabled, `PreservationAudit::Compare` still rejects metadata losses the output cannot carry.

**Why it matters:** Sidecars are opt-in by default, avoiding unexpected archive files while preserving fail-closed
behavior when a conversion would otherwise lose source metadata. Because the example config has explanatory comments,
parsing must accept comments rather than treating the sample as invalid JSON.

**Rejected:** Defaulting to `required` unexpectedly creates sidecars for users who did not configure them. Silently
accepting lossy conversions when sidecars are disabled would violate metadata preservation guarantees.
