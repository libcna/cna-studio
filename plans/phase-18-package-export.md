# Phase 18 — Cook, package and export

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-18001` … `STUDIO-18999` and are never reused.

**Purpose.** Produce a standalone CNA game that does not know CNA Studio exists.

**Exit criteria.** A packaged build runs on a clean machine, contains no editor code, and the project rebuilds from source without Studio.

**Progress:** 0 of 11 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-18001` | Content cooking | ⬜ | `STUDIO-17009` |
| `STUDIO-18002` | Packaging and staging to an output directory | ⬜ | `STUDIO-18001` |
| `STUDIO-18003` | Executable discovery and required runtime libraries | ⬜ | `STUDIO-18002` |
| `STUDIO-18004` | Asset and configuration staging | ⬜ | `STUDIO-18002` |
| `STUDIO-18005` | Licence and third-party notice staging | ⬜ | `STUDIO-18002` |
| `STUDIO-18006` | Launch the packaged build from Studio | ⬜ | `STUDIO-18002` |
| `STUDIO-18010` | Shipping builds exclude all editor-only code | ⬜ | `STUDIO-18002` |
| `STUDIO-18011` | Guard test: no editor-only symbol appears in a Shipping game binary | ⬜ | `STUDIO-18010` |
| `STUDIO-18020` | The standalone export test | ⬜ | `STUDIO-18002`, `STUDIO-02051` |
| `STUDIO-18021` | Any generated build tooling the project needs is project-owned or a normal CNA tool | ⬜ | `STUDIO-18020` |
| `STUDIO-18022` | Packaged-build smoke test per supported target | ⬜ | `STUDIO-18020` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-18005` — Licence and third-party notice staging

**Acceptance.** The packaged build carries the notices its dependencies require

### `STUDIO-18010` — Shipping builds exclude all editor-only code

**Acceptance.** No Studio UI, gizmos, selection state, panels, recovery system, plugin host, legacy UI or Studio diagnostics in a Shipping output

### `STUDIO-18020` — The standalone export test

**Acceptance.** Export a sample project; copy it to a clean temporary directory; make the Studio installation unavailable; configure it with its own CMakeLists.txt; compile it; run an automated smoke test. This is the concrete form of the invariant the whole product rests on

**Verification.** Runs in CI on every change

### `STUDIO-18021` — Any generated build tooling the project needs is project-owned or a normal CNA tool

**Acceptance.** Studio is never assumed to be installed on the build machine

