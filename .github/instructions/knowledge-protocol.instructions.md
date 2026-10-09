---
applyTo: "**"
---

# Knowledge protocol

The repository keeps durable knowledge in `docs/knowledge/`: one Markdown file per entry, indexed in
`docs/knowledge/index.md`. Tooling is the globally installed Project Memory skill; do not copy its script into this
repository. In PowerShell, invoke it using the command name and arguments, for example:

```powershell
& "$HOME\.copilot\skills\project-memory\scripts\kb.ps1" discover
```

## Read before

Changing a contract, a schema, threading, process lifetime or composition; removing or "simplifying" something that looks
redundant; or concluding that existing code is wrong. Run `kb.ps1 related -Path <file>` for the files you touch and
`kb.ps1 search <words>` for the concept. If a live entry contradicts your plan, stop and say so.

## Must write when

| Trigger | Kind |
|---|---|
| A user-visible workflow, behavior, or contract that should remain stable | `decision` or `domain` |
| Product or deployment context needed to understand behavior | `domain` |
| A technical constraint or trap with a demonstrated recurring risk to product behavior | `constraint` or `trap` |
| A choice between real alternatives a future reader could not re-derive | `decision` |
| An unresolved, verified defect materially affecting an important workflow | `defect` |

The knowledge base is not a development diary. Do not record routine language/runtime/toolset migrations, dependency
updates, refactors, type/structure changes, or build churn unless they alter product behavior or expose a verified
recurring risk. Preferences spanning repositories belong in the global skill or user instructions, not each project.
If unsure whether a feature or behavior is important from the product point of view, ask before recording it.

Not for: routine progress, what the diff already shows, anything already in an instruction file or `DESIGN.md`, or
speculation.

## How

1. `kb.ps1 new -Kind <kind> -Title "<one line>" -Area <area> -Cite "<path>#<Type.Member>"`; fill in **Fact** (evidence
   first) and **Why it matters**; a decision also lists what was rejected. Use the `areas` in `kb.config.json`.
2. Cite code by path and symbol, never by line number. Nothing to cite: `uncited: "<reason>"`. Write for a reader with
   no memory of this conversation. Never record secrets, credentials, private library details or sensitive media
   metadata.
3. One fact per entry. Never edit an old entry's meaning: supersede it (`kb.ps1 status -Id <old> -Status superseded
   -By <new>`) or mark it fixed (`-Status fixed -By <fix entry>`).
4. Run `kb.ps1 check -Id <id>`, then tell the user the id and title.

## After changing code

Before reporting the task done, run `kb.ps1 affected` (or `affected -Staged` before a commit). For each entry it lists,
re-read it against the new code: still true -> `kb.ps1 reviewed -Id <id>`; moved -> `kb.ps1 cite`; no longer true ->
new entry and supersede. Update an owning `DESIGN.md` in the same change when one exists; create one only when the code
change warrants durable design documentation.

## When the knowledge is missing or wrong

- An empty lookup alone does not block work supported by code or documentation. If intended behaviour remains
  consequentially ambiguous after `related`, two searches and the owning design document (if present), check
  `git log -S`, then ask one precise question and record the confirmed answer. Do not guess intent.
- An entry contradicts the code or `DESIGN.md`: say so; the code is current behaviour, `DESIGN.md` wins on intended
  behaviour; supersede the stale entry once confirmed.
- `kb.ps1` unavailable: read `docs/knowledge/index.md` and the entry files directly, and write entries by hand from
  `docs/knowledge/README.md`.
- Generated documentation (wikis, `llms.txt`, exports) is derived and never outranks `DESIGN.md` or an entry.
