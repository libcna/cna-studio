# Phase 20 — Lighting

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-20001` … `STUDIO-20999` and are never reused.

**Purpose.** Lighting authoring that matches what the runtime can actually execute.

**Exit criteria.** The viewport and the game preview agree, and no light type exists in Studio that the runtime cannot render.

**Progress:** 0 of 8 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-20001` | Directional light authoring | ⬜ | `STUDIO-19001` |
| `STUDIO-20002` | Point light authoring | ⬜ | `STUDIO-20001` |
| `STUDIO-20003` | Spot light authoring | ⬜ | `STUDIO-20001` |
| `STUDIO-20004` | Ambient and environment lighting | ⬜ | `STUDIO-20001` |
| `STUDIO-20005` | Sky and environment map authoring | ⬜ | `STUDIO-10010` |
| `STUDIO-20006` | Shadow configuration | ⬜ | `STUDIO-20001` |
| `STUDIO-20007` | Viewport lighting matches the game preview as closely as the runtime allows | ⬜ | `STUDIO-20001` |
| `STUDIO-20008` | No light type is offered that the runtime cannot render | ⬜ | `STUDIO-20001` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-20007` — Viewport lighting matches the game preview as closely as the runtime allows

**Acceptance.** Where an approximation is unavoidable, it is documented rather than hidden

