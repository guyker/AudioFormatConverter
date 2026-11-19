---
id: K-054
kind: decision
title: "Match duplicate albums by name, MusicBrainz ID or FLAC MD5, with duration-tolerant track pairing"
area: [FlacConvert]
status: active
supersedes: [K-046]
created: 2026-11-19
reviewed: 2026-11-19
cites:
  - path: FlacConvert/src/DuplicateMatcher.cpp
    symbol: "DuplicateMatcher::EvaluatePair"
---

**Fact:** `DuplicateMatcher::EvaluatePair` accepts a pair only with a shared normalized album name, MusicBrainz
release ID or stored FLAC MD5 (fuzzy names opt-in). Tracks pair by MD5, disc/track number, title, then duration within
`max(DuplicateDurationToleranceSeconds, SizeMatchPercentageThreshold% of the longer track)`; compressed size is still
not used. Equal track counts are required unless partial matching is enabled. The user approved the plan with these
defaults (MD5 and MusicBrainz as evidence, 2 s / 1% tolerance, fuzzy and partial opt-in).

**Why it matters:** Sorted-duration comparison alone mispaired reordered tracks, missed accent/case variants and
ignored identifiers that prove identity; a percentage-only tolerance was too strict for short tracks.

**Rejected:** Duration-only sorted matching with a percentage threshold (K-046) for the reasons above; file-size
matching (re-encodes differ); fuzzy and partial matching by default (more false positives); acoustic fingerprinting
(deferred: needs a new dependency and decoding every track).
