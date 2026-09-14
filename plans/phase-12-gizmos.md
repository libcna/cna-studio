# Phase 12 — Selection and gizmos 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-12001` … `STUDIO-12999` and are never reused.

**Purpose.** Production-quality transform manipulation.

**Exit criteria.** Transforming objects feels precise and predictable, and every drag is exactly one undo entry.

**Progress:** 0 of 11 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-12001` | Translate gizmo | ⬜ | `STUDIO-11006` |
| `STUDIO-12002` | Rotate gizmo | ⬜ | `STUDIO-11006` |
| `STUDIO-12003` | Scale gizmo | ⬜ | `STUDIO-11006` |
| `STUDIO-12004` | Local and world transform spaces | ⬜ | `STUDIO-12001` |
| `STUDIO-12005` | Multi-selection transforms about a shared pivot | ⬜ | `STUDIO-12001` |
| `STUDIO-12006` | Pivot editing | ⬜ | `STUDIO-12005` |
| `STUDIO-12007` | Snapping: grid, angle and scale increments | ⬜ | `STUDIO-12001` |
| `STUDIO-12008` | One undo entry per drag, returning exactly to the drag start | ⬜ | `STUDIO-12001` |
| `STUDIO-12009` | Box selection | ⬜ | `STUDIO-11006` |
| `STUDIO-12010` | Duplicate, delete, parent and reparent from the viewport | ⬜ | `STUDIO-12005` |
| `STUDIO-12011` | Drag and drop placement from the Content Browser into the scene | ⬜ | `STUDIO-09008` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-12008` — One undo entry per drag, returning exactly to the drag start

**Acceptance.** Carried forward from the prototype and retested through the Studio UI

