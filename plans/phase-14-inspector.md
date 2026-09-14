# Phase 14 — Details Inspector 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-14001` … `STUDIO-14999` and are never reused.

**Purpose.** A first-class production property editor driven by the descriptor model.

**Exit criteria.** Every property type a component can declare is editable, validated and undoable.

**Progress:** 0 of 18 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-14001` | Component sections with collapse and expand | ⬜ | `STUDIO-07007` |
| `STUDIO-14002` | Add and remove component | ⬜ | `STUDIO-14001` |
| `STUDIO-14003` | Numeric fields: typed entry and drag-to-change | ⬜ | `STUDIO-14001` |
| `STUDIO-14004` | Vector and rotation editors | ⬜ | `STUDIO-14003` |
| `STUDIO-14005` | Colour editor | ⬜ | `STUDIO-14001` |
| `STUDIO-14006` | Enum, boolean and string editors | ⬜ | `STUDIO-14001` |
| `STUDIO-14007` | Asset reference field with drag-and-drop and a picker | ⬜ | `STUDIO-09008` |
| `STUDIO-14008` | Entity reference field | ⬜ | `STUDIO-14007` |
| `STUDIO-14009` | List properties: add, remove, reorder — each its own undo entry | ⬜ | `STUDIO-14001` |
| `STUDIO-14010` | Nested structure editing | ⬜ | `STUDIO-14009` |
| `STUDIO-14011` | Read-only data display | ⬜ | `STUDIO-14001` |
| `STUDIO-14012` | Reset to default | ⬜ | `STUDIO-14001` |
| `STUDIO-14013` | Revert and apply prefab overrides | ⬜ | `STUDIO-14012` |
| `STUDIO-14014` | Copy and paste property values | ⬜ | `STUDIO-03025` |
| `STUDIO-14015` | Validation warnings shown inline | ⬜ | `STUDIO-14001` |
| `STUDIO-14016` | Tooltips and documentation from descriptor metadata | ⬜ | `STUDIO-03021` |
| `STUDIO-14017` | Multi-selection editing where the semantics are unambiguous | ⬜ | `STUDIO-14001` |
| `STUDIO-14018` | Responsive with very large property counts | ⬜ | `STUDIO-30010` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-14004` — Vector and rotation editors

**Acceptance.** Rotation is edited as Euler angles that round-trip through the stored quaternion without drift

### `STUDIO-14017` — Multi-selection editing where the semantics are unambiguous

**Acceptance.** Mixed values are shown as mixed, not as the first value

