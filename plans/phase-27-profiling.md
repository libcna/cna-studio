# Phase 27 — Profiling and diagnostics

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-27001` … `STUDIO-27999` and are never reused.

**Purpose.** Professional performance and debugging tools.

**Exit criteria.** A developer can find out why their game or their editor session is slow, from inside Studio.

**Progress:** 0 of 14 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-27001` | Frame time and CPU timing | ⬜ | `STUDIO-07011` |
| `STUDIO-27002` | GPU timing where CNA exposes it | ⬜ | `STUDIO-27001` |
| `STUDIO-27003` | Draw calls, triangles, texture and resource counts | ⬜ | `STUDIO-27001` |
| `STUDIO-27004` | Memory reporting | ⬜ | `STUDIO-27001` |
| `STUDIO-27005` | Asset load timing | ⬜ | `STUDIO-10011` |
| `STUDIO-27006` | Build timing | ⬜ | `STUDIO-17009` |
| `STUDIO-27007` | Renderer and platform capability reporting | ⬜ | `STUDIO-02021` |
| `STUDIO-27008` | Live player status | ⬜ | `STUDIO-16002` |
| `STUDIO-27010` | Frame debugger | ⬜ | `STUDIO-27003` |
| `STUDIO-27011` | Render-pass and resource inspection | ⬜ | `STUDIO-27010` |
| `STUDIO-27012` | Draw-call inspection | ⬜ | `STUDIO-27010` |
| `STUDIO-27013` | Remote profiling | ⛔ | `STUDIO-27008` |
| `STUDIO-27020` | Console and Output Log as a production log panel | ⬜ | `STUDIO-07005` |
| `STUDIO-27021` | Very large logs stay responsive | ⬜ | `STUDIO-27020`, `STUDIO-30010` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-27002` — GPU timing where CNA exposes it

**Acceptance.** Uses CNA's `GpuTimer` and the `GpuTimers` renderer feature; absent support is reported, not faked

### `STUDIO-27020` — Console and Output Log as a production log panel

**Acceptance.** Severity, source and category, timestamps, search, filters, clear, copy, pause, auto-scroll, hyperlinks to files, entities and assets; build, game and Studio logs distinguished

### `STUDIO-27021` — Very large logs stay responsive

**Verification.** Stress test with a multi-hundred-thousand-line log

