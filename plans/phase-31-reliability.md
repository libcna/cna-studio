# Phase 31 — Reliability

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-31001` … `STUDIO-31999` and are never reused.

**Purpose.** This is a content-authoring application. Losing work is unacceptable.

**Exit criteria.** Interrupted saves, corrupt files and crashes cost a user nothing they cannot recover, and nothing is repaired silently.

**Progress:** 1 of 13 complete `█░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-31001` | Autosave | ⬜ | — |
| `STUDIO-31002` | Crash recovery snapshots, offered rather than silently applied | ⬜ | `STUDIO-31001` |
| `STUDIO-31003` | Atomic writes for every authored file | ⬜ | — |
| `STUDIO-31004` | Undo and redo stability under every editing path | ⬜ | `STUDIO-02035` |
| `STUDIO-31005` | Format migration chain runs on every load | ⬜ | — |
| `STUDIO-31006` | Dirty-state tracking | ⬜ | `STUDIO-31001` |
| `STUDIO-31007` | Crash isolation from the game process | ⬜ | `STUDIO-16003` |
| `STUDIO-31008` | Malformed project and scene diagnostics that permit repair | ⬜ | — |
| `STUDIO-31009` | Unknown plugin components preserved through save and load | ✅ | — |
| `STUDIO-31010` | Tests for interrupted saves and partial files | ⬜ | `STUDIO-31003` |
| `STUDIO-31011` | Nothing is silently repaired; every change to user data is reported | ⬜ | `STUDIO-31008` |
| `STUDIO-31020` | Deterministic, version-control-friendly output throughout | ⬜ | `STUDIO-02037` |
| `STUDIO-31021` | Generated files have explicit ownership and regeneration rules | ⬜ | `STUDIO-15008` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-31001` — Autosave

**Acceptance.** Carried forward from the prototype and retested through the Studio UI

### `STUDIO-31008` — Malformed project and scene diagnostics that permit repair

**Acceptance.** A tool that refuses to open a slightly broken file is one you cannot use to fix a broken file

### `STUDIO-31020` — Deterministic, version-control-friendly output throughout

**Acceptance.** Stable ordering, no unnecessary timestamps, no formatting churn, no opaque binary state for ordinary project metadata

