# Phase 29 — Renderer and platform matrix

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-29001` … `STUDIO-29999` and are never reused.

**Purpose.** Keep Studio correct as CNA's renderer and platform sets grow.

**Exit criteria.** Adding a renderer or platform to CNA does not require hunting through Studio, and a new one cannot slip in unclassified.

**Progress:** 5 of 7 complete `████████░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-29001` | Centralised renderer discovery and classification | ✅ | `STUDIO-02030` |
| `STUDIO-29002` | Centralised platform discovery and classification | ✅ | `STUDIO-02031` |
| `STUDIO-29003` | Capability-driven host eligibility rather than a renderer allow-list | ✅ | `STUDIO-02020` |
| `STUDIO-29004` | Target renderer validation for the game build matrix | ✅ | `STUDIO-02040` |
| `STUDIO-29005` | Studio-host renderers exercised in CI where infrastructure allows | ⬜ | `STUDIO-33010` |
| `STUDIO-29006` | Document the renderer and platform model for contributors | ⬜ | `STUDIO-29001` |
| `STUDIO-29007` | Guard test: Studio's renderer/OS gate table still matches CNA's configure rules | ✅ | `STUDIO-02040` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-29001` — Centralised renderer discovery and classification

**Acceptance.** One place decides; no scattered name comparisons anywhere in Studio

### `STUDIO-29003` — Capability-driven host eligibility rather than a renderer allow-list

**Acceptance.** Whether a renderer can host Studio is decided by asking the device what it can do,
never by matching its name. Delivered as `STUDIO-02020`…`02022`: the requirement set is data, the
evaluation is CNA-free and tested against synthetic devices, and a renderer that gains a capability
becomes eligible with no change to Studio

### `STUDIO-29004` — Target renderer validation for the game build matrix

**Acceptance.** A target profile naming a renderer CNA cannot build for its operating system is
refused *before* the build, with CNA's own reason — rather than after it, with a message about a
missing header. Delivered as part of `STUDIO-02040`

### `STUDIO-29007` — Guard test: Studio's renderer/OS gate table still matches CNA's configure rules

**Acceptance.** The test reads CNA's own `cmake/RendererSelection.cmake` and fails when a renderer
CNA gates to one operating system is one Studio still offers on another. It runs only in the
CNA-backed configuration, because it needs a CNA checkout to read, and it asserts that its own parse
found something — a guard that silently matched nothing would pass vacuously, which is the failure
these guards exist to prevent

**Why Studio holds the table at all.** CNA states these rules only as CMake conditions, so the only
way to learn them is to attempt a configure and read the error. That is CNA gap G-08; this guard is
what keeps the workaround from rotting

