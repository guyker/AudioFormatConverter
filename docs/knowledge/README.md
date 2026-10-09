# Knowledge base

Durable "why" knowledge for this repository: decisions, traps, constraints, preferences, domain facts and known
defects. One Markdown file per entry; `index.md` lists them (generated, do not edit). Design ("what the system is")
lives in `DESIGN.md` files; rules live in `.github/instructions/`; issue and change history lives in work items and
commits. Tooling: `kb.ps1` from the project-memory skill.

## Entry format

```markdown
---
id: K-001
kind: trap                 # decision | trap | constraint | defect | preference | domain
title: "A short, specific summary of the verified fact"
area: [FlacConvert]        # one or more of the areas in kb.config.json; detail goes in the title
status: active             # active | fixed | superseded | archived
supersedes: []             # ids this entry replaces
created: YYYY-MM-DD
reviewed: YYYY-MM-DD       # last time someone confirmed it is still true
cites:                     # path + symbol (or test); never line numbers
  - path: FlacConvert/src/FolderConvert.cpp
    symbol: FolderConvert::ScanAudioFiles
---

**Fact:** what is true, with evidence (file, symbol, error code, observed behaviour).

**Why it matters:** the failure or cost if this is forgotten.

**Rejected:** (decisions) the alternatives considered and why each was not chosen.
```

## Rules

- Append, never rewrite the meaning of an entry (the fact). Maintenance fields change through `kb.ps1`: `reviewed`, citations (`cite`), and the status: `fixed` (with `fixed_by` when another entry
  records the fix), `superseded` (with `superseded_by`), `archived` (moved to `archive/` once its lesson lives
  elsewhere).
- One fact per entry. Evidence before interpretation. Written for a reader with no memory of the conversation.
- Cite by path and symbol so `kb.ps1 check` can verify the entry against the code; a broken citation on an active entry
  is an error. `kb.ps1 stale` lists entries whose cited files changed after they were last reviewed; re-read them and
  run `kb.ps1 reviewed -Id <id>` (or supersede them).
- A defect entry is short-lived: when it is fixed, keep the lesson as a `trap` or `decision` and archive the defect.
- After a code change, `kb.ps1 affected` lists the entries whose cited code changed (`-Staged` before a commit). A moved
  symbol is repaired with `kb.ps1 cite -Id <id> -Cite <new> -Remove <old>`.
- Never record secrets, credentials, private library details or sensitive media metadata; `kb.ps1 check` warns on
  anything that looks like a key.
