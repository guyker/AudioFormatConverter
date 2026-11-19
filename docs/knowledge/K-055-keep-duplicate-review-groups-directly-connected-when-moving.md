---
id: K-055
kind: decision
title: "Keep duplicate review groups directly connected when moving albums"
area: [FlacConvert]
status: active
created: 2026-11-19
reviewed: 2026-11-19
cites:
  - path: FlacConvert/src/DuplicateReview.cpp
    symbol: "DuplicateReview::ReviewGroup"
---

**Fact:** `DuplicateReview::ReviewGroup` displays the direct duplicate edges for each connected group and refuses
an album move that would disconnect the remaining induced graph. Every accepted move still requires an individual album
selection and exact `MOVE` confirmation.

**Why it matters:** Candidate groups are connected components, not necessarily cliques. Moving a bridge album without
this check could leave two remaining albums that do not directly match, making the group unsafe to review as one set.

**Rejected:** Allowing any selected move can split a candidate group into unrelated survivors. Requiring every pair
to match (a clique) would unnecessarily prevent safe removals from larger connected groups.
