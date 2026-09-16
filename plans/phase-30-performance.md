# Phase 30 — Large-project performance

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-30001` … `STUDIO-30999` and are never reused.

**Purpose.** Scale to real projects, not to the example.

**Exit criteria.** Benchmarks exist, they run, and regressions are visible.

**Progress:** 0 of 14 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-30001` | Background job system: progress, cancellation, errors, clean shutdown | ⬜ | `STUDIO-02050` |
| `STUDIO-30002` | Bounded queues and backpressure for job submission | ⬜ | `STUDIO-30001` |
| `STUDIO-30010` | Virtualised list and tree infrastructure | ⬜ | `STUDIO-03015` |
| `STUDIO-30011` | Incremental update rather than per-frame rebuild throughout | ⬜ | `STUDIO-30010` |
| `STUDIO-30012` | Caching strategy with explicit invalidation | ⬜ | `STUDIO-09004` |
| `STUDIO-30013` | `SceneDocument` child lookup is an index, not a scan of every entity | ⬜ | — |
| `STUDIO-30014` | Find what makes the Content Browser cost 23 ms a frame at 1 500 assets | ⬜ | `STUDIO-04028` |
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

### `STUDIO-30013` — `SceneDocument` child lookup is an index, not a scan of every entity

**Acceptance.** Flattening the outliner over a scene of *n* entities is O(*n*), and a measurement
says so at several sizes rather than at one.

**Measured, not suspected** (`STUDIO-04028`'s benchmark, `--ui-benchmark=outliner`): 250 entities
cost 9 ms a frame, 500 cost 25 ms, 1 000 cost 84 ms, 2 000 cost 309 ms. Four times the cost for
twice the entities, which is the signature. At 2 000 entities — not a large scene — the whole editor
runs at three frames a second.

**The cause is one line.** `SceneDocument::getChildren(parent)` scans every entity in the document
and sorts the matches; `StudioOutlinerPanel`'s `flatten` calls it once per row it visits. Entities
are already indexed by id, so the lookup *by id* is O(1) and the lookup *by parent* is O(*n*) —
and the second one is the one a tree walk does for every node.

**Rows are virtualised, so this is invisible in the draw counts.** The benchmark reports 20 draw
calls and 7 317 vertices whether the scene holds 5 entities or 2 000: the drawing is flat and the
walk is not. Nothing in a screenshot, a golden image or a draw-call assertion could have shown it,
which is the argument for a benchmark that reports CPU time beside the counts.

**Ahead of `STUDIO-30010`/`STUDIO-30011` and independent of both.** Those are about not rebuilding
per frame; this is about the rebuild being quadratic when it happens, which is a defect rather than
a strategy. Filed here rather than in Phase 13 because it is `SceneDocument`'s to fix.

### `STUDIO-30014` — Find what makes the Content Browser cost 23 ms a frame at 1 500 assets

**Acceptance.** The cost is attributed to something specific, and whatever it is either scales or is
recorded as not scaling with a number saying how.

**Measured** (`--ui-benchmark=content`): 23 ms a frame for 1 500 assets against 1.9 ms for the shell
around it, while drawing the same 19 draw calls. Not measured at more than one size, so whether it
is linear or worse is **unknown** — which is why this is worded as a question rather than as a fix.

### `STUDIO-30030` — Establish the interactive frame-rate target and measure against it

**Acceptance.** Measured before optimising, with the benchmark established early so a regression is visible

