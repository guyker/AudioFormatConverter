---
id: K-021
kind: decision
title: "Require explicit confirmation before manual source replacement"
area: [FlacConvert]
status: active
created: 2026-10-11
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConfirmSourceReplacement
---

**Fact:** `ConfirmSourceReplacement` lists active configured libraries before manual option 1 and requires the exact token `REPLACE` (surrounding whitespace is ignored). Empty input, any other response or end-of-input cancels before conversion. No active libraries or an input read error fails the action. The explicit conversion subcommand is unaffected.

**Why it matters:** Manual FLAC conversion can replace source audio, reduce resolution and omit embedded artwork. Selecting a menu number alone must not authorize those irreversible changes.

**Rejected:** Defaulting to proceed, accepting a generic Enter/Y response, or relying only on the menu warning makes accidental source replacement too easy.
