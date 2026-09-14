# Phase 33 — Documentation, templates and CI

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-33001` … `STUDIO-33999` and are never reused.

**Purpose.** Real developer documentation, and the test infrastructure that keeps all of it true.

**Exit criteria.** A new contributor can build, test and extend Studio from the documentation alone.

**Progress:** 0 of 13 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-33001` | Getting-started documentation | ⬜ | `STUDIO-08011` |
| `STUDIO-33002` | User guide for the core authoring workflow | ⬜ | `STUDIO-12011` |
| `STUDIO-33003` | Architecture documentation kept current | 🔄 | `STUDIO-02001` |
| `STUDIO-33004` | Plugin SDK documentation | ⬜ | `STUDIO-28011` |
| `STUDIO-33005` | Public API documentation coverage | ⬜ | — |
| `STUDIO-33010` | Graphical CI with a real CNA build and a display | ⬜ | — |
| `STUDIO-33011` | Screenshot and golden-image test infrastructure | ⬜ | `STUDIO-04013` |
| `STUDIO-33012` | Canonical visual test scenes | ⬜ | `STUDIO-33011` |
| `STUDIO-33013` | Visual tests at multiple resolutions | ⬜ | `STUDIO-33012` |
| `STUDIO-33014` | Visual tests at multiple DPI scales | ⬜ | `STUDIO-33013`, `STUDIO-03028` |
| `STUDIO-33015` | Visual regressions surface as CI artifacts | ⬜ | `STUDIO-33011` |
| `STUDIO-33020` | Headless test seams maintained for every core subsystem | ⬜ | — |
| `STUDIO-33021` | CI matrix: Linux, Windows, macOS as infrastructure allows | ⬜ | — |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-33010` — Graphical CI with a real CNA build and a display

**Acceptance.** Unblocks the screenshot tests and the real-device smoke tests

### `STUDIO-33011` — Screenshot and golden-image test infrastructure

**Acceptance.** Tolerant image comparison, because two renderers are never bit-identical and exact equality would make the tests useless

### `STUDIO-33012` — Canonical visual test scenes

**Acceptance.** Project Hub, empty Studio, full scene, selected entity, Inspector, Content Browser, menus, modal, Build dialog, Play state, errors and warnings

### `STUDIO-33013` — Visual tests at multiple resolutions

**Acceptance.** 1280x720, 1600x900, 1920x1080, 2560x1440 and an ultrawide

### `STUDIO-33020` — Headless test seams maintained for every core subsystem

**Acceptance.** Document model, undo, asset database, serialization, migration, project model, UI layout and state, command system, player protocol and build planning all testable with no GPU

### `STUDIO-33021` — CI matrix: Linux, Windows, macOS as infrastructure allows

**Acceptance.** Linux development is never blocked waiting for macOS or Windows infrastructure

