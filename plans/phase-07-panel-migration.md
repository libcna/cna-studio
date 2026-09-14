# Phase 7 — Existing-panel migration

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-07001` … `STUDIO-07999` and are never reused.

**Purpose.** Port every prototype panel onto the Studio UI and retire the Dear ImGui presentation.

**Exit criteria.** Feature, input, docking and visual parity, proven panel by panel against the Phase 0 inventory — then ImGui is removed deliberately.

**Progress:** 0 of 21 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-07001` | Compatibility adapter so unported panels keep working during the migration | ⬜ | `STUDIO-06015` |
| `STUDIO-07002` | Port the main menu bar | ⬜ | `STUDIO-06003` |
| `STUDIO-07003` | Port the toolbar | ⬜ | `STUDIO-06006` |
| `STUDIO-07004` | Port the status bar | ⬜ | `STUDIO-06007` |
| `STUDIO-07005` | Port the Console / Output Log | ⬜ | `STUDIO-07001` |
| `STUDIO-07006` | Port the Hierarchy panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07007` | Port the Inspector panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07008` | Port the Content Browser | ⬜ | `STUDIO-07001` |
| `STUDIO-07009` | Port the viewport container | ⬜ | `STUDIO-04012` |
| `STUDIO-07010` | Port the Build panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07011` | Port the Diagnostics panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07012` | Port the Validation panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07013` | Port the History panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07014` | Port the Comparison panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07020` | Prove parity against the Phase 0 panel and shortcut inventory | ⬜ | `STUDIO-00014`, `STUDIO-07014` |
| `STUDIO-07021` | Prove input parity: keyboard, mouse, drag and drop, clipboard, text editing | ⬜ | `STUDIO-07020` |
| `STUDIO-07022` | Prove docking parity | ⬜ | `STUDIO-07020` |
| `STUDIO-07023` | Visual acceptance review against the Phase 0 reference screenshots | ⬜ | `STUDIO-00013`, `STUDIO-07020` |
| `STUDIO-07030` | Remove the Dear ImGui panel implementations | ⬜ | `STUDIO-07021`, `STUDIO-07022`, `STUDIO-07023` |
| `STUDIO-07031` | Remove the `CNA_STUDIO_WITH_IMGUI` option and the vendored source | ⬜ | `STUDIO-07030` |
| `STUDIO-07099` | Guard test: production Studio UI has no dependency on Dear ImGui | ⬜ | `STUDIO-07031` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-07001` — Compatibility adapter so unported panels keep working during the migration

**Acceptance.** Both UIs coexist in one running Studio; the strangler migration proceeds panel by panel with tests green throughout

### `STUDIO-07020` — Prove parity against the Phase 0 panel and shortcut inventory

**Acceptance.** Every inventoried panel, menu item, toolbar control and shortcut ticked off item by item, not by impression

### `STUDIO-07031` — Remove the `CNA_STUDIO_WITH_IMGUI` option and the vendored source

**Acceptance.** Removed deliberately, with `THIRD_PARTY_NOTICES.md` updated to match what is actually shipped

### `STUDIO-07099` — Guard test: production Studio UI has no dependency on Dear ImGui

**Acceptance.** Fails the build if the dependency returns, whether through code or through CMake

