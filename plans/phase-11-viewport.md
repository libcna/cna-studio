# Phase 11 — 3D viewport 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-11001` … `STUDIO-11999` and are never reused.

**Purpose.** A professional 3D authoring viewport: navigation, visualisation modes and correctness.

**Exit criteria.** A user can navigate a real scene comfortably and see what they are authoring, without regressing the existing 2D workflow.

**Progress:** 0 of 14 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-11001` | Perspective and orthographic cameras | ⬜ | `STUDIO-07009` |
| `STUDIO-11002` | Orbit, fly and pan navigation with configurable speed | ⬜ | `STUDIO-11001` |
| `STUDIO-11003` | Focus selection | ⬜ | `STUDIO-11001` |
| `STUDIO-11004` | Standard views: front, back, left, right, top, bottom | ⬜ | `STUDIO-11001` |
| `STUDIO-11005` | Adaptive grid | ⬜ | `STUDIO-11001` |
| `STUDIO-11006` | Object picking through the 3D projection | ⬜ | `STUDIO-11001` |
| `STUDIO-11007` | Selection outlines | ⬜ | `STUDIO-11006` |
| `STUDIO-11008` | Bounds and collision debug visualisation | ⬜ | `STUDIO-11006` |
| `STUDIO-11009` | Icons and billboards for entities with no geometry | ⬜ | `STUDIO-11006` |
| `STUDIO-11010` | Wireframe mode | ⬜ | `STUDIO-11001` |
| `STUDIO-11011` | Lighting modes, unlit mode, normal and material debug views | ⬜ | `STUDIO-19001` |
| `STUDIO-11012` | Camera preview and game view | ⬜ | `STUDIO-11001` |
| `STUDIO-11013` | Preserve the existing 2D viewport workflow without regression | ⬜ | `STUDIO-07009` |
| `STUDIO-11014` | A new CNA-native project opens directly into a 3D world viewport | ⬜ | `STUDIO-11001`, `STUDIO-08006` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-11013` — Preserve the existing 2D viewport workflow without regression

**Acceptance.** Sprites, tilemaps, layer depth ordering and 2D manipulators all still work and are still tested

