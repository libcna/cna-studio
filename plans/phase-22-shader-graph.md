# Phase 22 — Material and shader graph

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-22001` … `STUDIO-22999` and are never reused.

**Purpose.** A node-based authoring surface for CNA's modern graphics API.

**Exit criteria.** A graph produces a working shader, and errors map back to the node that caused them.

**Progress:** 0 of 10 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-22001` | Graph canvas: pan, zoom, selection | ⬜ | `STUDIO-03015` |
| `STUDIO-22002` | Node and typed pin model | ⬜ | `STUDIO-22001` |
| `STUDIO-22003` | Links with type validation | ⬜ | `STUDIO-22002` |
| `STUDIO-22004` | Search and add node | ⬜ | `STUDIO-22002` |
| `STUDIO-22005` | Copy, paste and undo on the graph | ⬜ | `STUDIO-22002` |
| `STUDIO-22006` | Comments and groups | ⬜ | `STUDIO-22001` |
| `STUDIO-22007` | Generated shader and effect output | ⬜ | `STUDIO-22003` |
| `STUDIO-22008` | Live preview | ⬜ | `STUDIO-22007` |
| `STUDIO-22009` | Compilation errors mapped back to graph nodes | ⬜ | `STUDIO-22007` |
| `STUDIO-22010` | Shader dialect targeting across CNA's supported dialects | ⬜ | `STUDIO-22007` |
