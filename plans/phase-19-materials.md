# Phase 19 — Materials

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-19001` … `STUDIO-19999` and are never reused.

**Purpose.** Make Studio the primary authoring environment for CNA's modern rendering.

**Exit criteria.** A property-based material editor good enough that a node graph is an addition rather than a rescue.

**Progress:** 0 of 9 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-19001` | PBR material asset model | ⬜ | `STUDIO-10007` |
| `STUDIO-19002` | Texture slots: base colour, normal, roughness, metalness, emissive, occlusion | ⬜ | `STUDIO-19001` |
| `STUDIO-19003` | Scalar and vector material parameters | ⬜ | `STUDIO-19001` |
| `STUDIO-19004` | Transparency modes | ⬜ | `STUDIO-19001` |
| `STUDIO-19005` | Material instances and parameter overrides | ⬜ | `STUDIO-19001` |
| `STUDIO-19006` | Live material preview in the viewport | ⬜ | `STUDIO-11011` |
| `STUDIO-19007` | Material preview thumbnail rendering | ⬜ | `STUDIO-09003` |
| `STUDIO-19008` | Renderer capability diagnostics for materials | ⬜ | `STUDIO-02021` |
| `STUDIO-19009` | Material assignment to mesh entities | ⬜ | `STUDIO-19001` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-19008` — Renderer capability diagnostics for materials

**Acceptance.** A material using a feature the target renderer lacks is reported at authoring time

