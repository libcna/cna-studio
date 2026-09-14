# Phase 8 — Project Hub

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-08001` … `STUDIO-08999` and are never reused.

**Purpose.** A professional entry point: create, open, recent projects and a small set of well-maintained templates.

**Exit criteria.** Studio opens on a hub that can create a working project and open an existing one, with validation that catches mistakes before they become confusing failures.

**Progress:** 0 of 12 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-08001` | Project Hub window and layout | ⬜ | `STUDIO-06015` |
| `STUDIO-08002` | Recent projects list with validity checking | ⬜ | `STUDIO-08001` |
| `STUDIO-08003` | Open Project flow | ⬜ | `STUDIO-08001` |
| `STUDIO-08004` | New Project flow with path, name and validation | ⬜ | `STUDIO-08001` |
| `STUDIO-08005` | Template model | ⬜ | `STUDIO-08004` |
| `STUDIO-08006` | Template: Empty 3D | ⬜ | `STUDIO-08005` |
| `STUDIO-08007` | Template: Empty 2D | ⬜ | `STUDIO-08005` |
| `STUDIO-08008` | Template: XNA-compatible | ⬜ | `STUDIO-08005` |
| `STUDIO-08009` | Template: basic sample | ⬜ | `STUDIO-08005` |
| `STUDIO-08010` | Renderer and platform defaults per template | ⬜ | `STUDIO-08005`, `STUDIO-02040` |
| `STUDIO-08011` | Every template produces a project that builds and runs without Studio | ⬜ | `STUDIO-08009` |
| `STUDIO-08012` | Project validation on open with actionable diagnostics | ⬜ | `STUDIO-08003` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-08002` — Recent projects list with validity checking

**Acceptance.** A moved or deleted project is shown as unavailable rather than failing on click

### `STUDIO-08004` — New Project flow with path, name and validation

**Acceptance.** Rejects invalid names, non-empty directories and unwritable paths, each with a specific message

### `STUDIO-08005` — Template model

**Acceptance.** A template is data plus a file tree, not code. Adding one does not require changing Studio

### `STUDIO-08008` — Template: XNA-compatible

**Acceptance.** Produces a project with its own `Initialize`/`LoadContent`/`Update`/`Draw` and no entity model

### `STUDIO-08011` — Every template produces a project that builds and runs without Studio

**Acceptance.** Tested for each template, in CI, as the concrete form of the central invariant

