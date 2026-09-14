# Phase 26 — Physics and navigation tooling

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-26001` … `STUDIO-26999` and are never reused.

**Purpose.** Editor integration for runtime physics and navigation — Studio does not become the owner of a physics engine.

**Exit criteria.** Colliders and navigation data can be authored and debugged, with runtime ownership explicit.

**Progress:** 0 of 8 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-26001` | Collider visualisation | ⬜ | `STUDIO-11008` |
| `STUDIO-26002` | Collider editing | ⬜ | `STUDIO-26001` |
| `STUDIO-26003` | Rigid-body property authoring | ⬜ | `STUDIO-14001` |
| `STUDIO-26004` | Physics debug rendering | ⬜ | `STUDIO-26001` |
| `STUDIO-26005` | Navigation volumes | ⬜ | `STUDIO-26001` |
| `STUDIO-26006` | Navmesh generation and preview | ⬜ | `STUDIO-26005` |
| `STUDIO-26007` | Path debugging | ⬜ | `STUDIO-26006` |
| `STUDIO-26008` | Runtime ownership documented and enforced | ⬜ | `STUDIO-26001` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-26008` — Runtime ownership documented and enforced

**Acceptance.** Studio integrates with a project, plugin or runtime physics system; it does not implement one

