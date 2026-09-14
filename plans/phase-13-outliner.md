# Phase 13 — World Outliner 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-13001` … `STUDIO-13999` and are never reused.

**Purpose.** Large-hierarchy editing that stays responsive and never loses a mutation.

**Exit criteria.** A scene with tens of thousands of entities browses and edits smoothly.

**Progress:** 0 of 12 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-13001` | Nested entity tree with expand and collapse | ⬜ | `STUDIO-07006` |
| `STUDIO-13002` | Search and filter | ⬜ | `STUDIO-13001` |
| `STUDIO-13003` | Multi-selection, shift-range and Ctrl-additive | ⬜ | `STUDIO-13001` |
| `STUDIO-13004` | Drag to reparent | ⬜ | `STUDIO-03023` |
| `STUDIO-13005` | Visibility and lock toggles | ⬜ | `STUDIO-13001` |
| `STUDIO-13006` | Rename, duplicate and delete | ⬜ | `STUDIO-13001` |
| `STUDIO-13007` | Context menu | ⬜ | `STUDIO-06005` |
| `STUDIO-13008` | Folder and group organisation | ⬜ | `STUDIO-13001` |
| `STUDIO-13009` | Prefab status indication | ⬜ | `STUDIO-13001` |
| `STUDIO-13010` | Type icons and component warnings | ⬜ | `STUDIO-13001` |
| `STUDIO-13011` | Virtualisation for large worlds | ⬜ | `STUDIO-30010` |
| `STUDIO-13012` | Every mutation goes through a command | ⬜ | `STUDIO-02035` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-13004` — Drag to reparent

**Acceptance.** Cycles are rejected; the operation is one undo entry

### `STUDIO-13011` — Virtualisation for large worlds

**Verification.** Stress test at 10,000+ entities with deep nesting

