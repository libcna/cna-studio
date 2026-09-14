# Phase 30 — Large-project performance

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-30001` … `STUDIO-30999` and are never reused.

**Purpose.** Scale to real projects, not to the example.

**Exit criteria.** Benchmarks exist, they run, and regressions are visible.

**Progress:** 0 of 12 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-30001` | Background job system: progress, cancellation, errors, clean shutdown | ⬜ | `STUDIO-02050` |
| `STUDIO-30002` | Bounded queues and backpressure for job submission | ⬜ | `STUDIO-30001` |
| `STUDIO-30010` | Virtualised list and tree infrastructure | ⬜ | `STUDIO-03015` |
| `STUDIO-30011` | Incremental update rather than per-frame rebuild throughout | ⬜ | `STUDIO-30010` |
| `STUDIO-30012` | Caching strategy with explicit invalidation | ⬜ | `STUDIO-09004` |
| `STUDIO-30020` | Stress benchmark: 10,000+ scene entities | ⬜ | `STUDIO-13011` |
| `STUDIO-30021` | Stress benchmark: deep hierarchies and large multi-selection | ⬜ | `STUDIO-30020` |
| `STUDIO-30022` | Stress benchmark: 100,000 assets | ⬜ | `STUDIO-09016` |
| `STUDIO-30023` | Stress benchmark: very large logs | ⬜ | `STUDIO-27021` |
| `STUDIO-30024` | Stress benchmark: large property lists and large imported models | ⬜ | `STUDIO-14018` |
| `STUDIO-30025` | Stress benchmark: many thumbnails and many concurrent import jobs | ⬜ | `STUDIO-09003` |
| `STUDIO-30030` | Establish the interactive frame-rate target and measure against it | ⬜ | `STUDIO-30020` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-30001` — Background job system: progress, cancellation, errors, clean shutdown

**Acceptance.** No document mutation races; deterministic handoff to the main thread; testable. Arbitrary detached threads are not scattered across subsystems

### `STUDIO-30030` — Establish the interactive frame-rate target and measure against it

**Acceptance.** Measured before optimising, with the benchmark established early so a regression is visible

