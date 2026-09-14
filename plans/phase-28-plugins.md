# Phase 28 — Plugins and SDK

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-28001` … `STUDIO-28999` and are never reused.

**Purpose.** Third-party extensibility that fails safely.

**Exit criteria.** A plugin can contribute real capability, and a bad plugin produces a message rather than a crash.

**Progress:** 0 of 11 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-28001` | Plugin API versioning and explicit ABI checks | ⬜ | — |
| `STUDIO-28002` | A bad plugin fails to load with a useful message and never crashes discovery | ⬜ | `STUDIO-28001` |
| `STUDIO-28003` | Plugins contribute asset importers | ⬜ | `STUDIO-10002` |
| `STUDIO-28004` | Plugins contribute component descriptors | ⬜ | `STUDIO-15002` |
| `STUDIO-28005` | Plugins contribute panels | ⬜ | `STUDIO-07001` |
| `STUDIO-28006` | Plugins contribute commands and menu items | ⬜ | `STUDIO-06001` |
| `STUDIO-28007` | Plugins contribute inspectors | ⬜ | `STUDIO-14001` |
| `STUDIO-28008` | Plugins contribute gizmos | ⬜ | `STUDIO-12001` |
| `STUDIO-28009` | Plugins contribute exporters and validators | ⬜ | `STUDIO-18002` |
| `STUDIO-28010` | Plugins contribute build integration | ⬜ | `STUDIO-17009` |
| `STUDIO-28011` | Plugin SDK documentation and a worked example | ⬜ | `STUDIO-28009` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-28001` — Plugin API versioning and explicit ABI checks

**Acceptance.** Carried forward; the `editorApiVersion` manifest key stays pinned for compatibility

