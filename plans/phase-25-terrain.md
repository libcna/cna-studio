# Phase 25 — Terrain and world tools

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-25001` … `STUDIO-25999` and are never reused.

**Purpose.** Large environment authoring, well after core scene editing is robust.

**Exit criteria.** A large world can be authored and organised without the tool falling over.

**Progress:** 0 of 7 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-25001` | Terrain import | ⬜ | `STUDIO-10004` |
| `STUDIO-25002` | Height editing and sculpting | ⬜ | `STUDIO-25001` |
| `STUDIO-25003` | Painting and material layers | ⬜ | `STUDIO-19001` |
| `STUDIO-25004` | Foliage placement | ⬜ | `STUDIO-25001` |
| `STUDIO-25005` | Procedural placement | ⬜ | `STUDIO-25004` |
| `STUDIO-25006` | Large-world organisation | ⬜ | `STUDIO-13008` |
| `STUDIO-25007` | Streaming and partitioning, if a real project needs it | ⛔ | `STUDIO-25006` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-25007` — Streaming and partitioning, if a real project needs it

**Acceptance.** Deferred until a real project demonstrates the need. Building world partitioning speculatively would be a large subsystem serving nobody

