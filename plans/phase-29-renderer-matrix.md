# Phase 29 — Renderer and platform matrix

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-29001` … `STUDIO-29999` and are never reused.

**Purpose.** Keep Studio correct as CNA's renderer and platform sets grow.

**Exit criteria.** Adding a renderer or platform to CNA does not require hunting through Studio, and a new one cannot slip in unclassified.

**Progress:** 2 of 6 complete `████░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-29001` | Centralised renderer discovery and classification | ✅ | `STUDIO-02030` |
| `STUDIO-29002` | Centralised platform discovery and classification | ✅ | `STUDIO-02031` |
| `STUDIO-29003` | Capability-driven host eligibility rather than a renderer allow-list | ⬜ | `STUDIO-02020` |
| `STUDIO-29004` | Target renderer validation for the game build matrix | ⬜ | `STUDIO-17005` |
| `STUDIO-29005` | Studio-host renderers exercised in CI where infrastructure allows | ⬜ | `STUDIO-33010` |
| `STUDIO-29006` | Document the renderer and platform model for contributors | ⬜ | `STUDIO-29001` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-29001` — Centralised renderer discovery and classification

**Acceptance.** One place decides; no scattered name comparisons anywhere in Studio

