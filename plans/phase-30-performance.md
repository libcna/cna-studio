# Phase 30 — Large-project performance

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-30001` … `STUDIO-30999` and are never reused.

**Purpose.** Scale to real projects, not to the example.

**Exit criteria.** Benchmarks exist, they run, and regressions are visible.

**Progress:** 1 of 14 complete `█░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-30001` | Background job system: progress, cancellation, errors, clean shutdown | ⬜ | `STUDIO-02050` |
| `STUDIO-30002` | Bounded queues and backpressure for job submission | ⬜ | `STUDIO-30001` |
| `STUDIO-30010` | Virtualised list and tree infrastructure | ⬜ | `STUDIO-03015` |
| `STUDIO-30011` | Incremental update rather than per-frame rebuild throughout | ⬜ | `STUDIO-30010` |
| `STUDIO-30012` | Caching strategy with explicit invalidation | ⬜ | `STUDIO-09004` |
| `STUDIO-30013` | `SceneDocument` child lookup is an index, not a scan of every entity | ✅ | — |
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

**Done, and measured after as well as before.** `--ui-benchmark=outliner`, same machine, same
scenes:

| Entities | Before | After | |
|---------:|-------:|------:|---|
| 250 | 9.1 ms | 4.0 ms | |
| 500 | 24.7 ms | 6.1 ms | |
| 1 000 | 84.3 ms | 11.3 ms | |
| 2 000 | 309.0 ms | 19.4 ms | **16× faster**, and 51 fps rather than 3 |

Doubling the entities now costs about 1.7× rather than about 3.7×, which is the shape the fix was
for.

**A grouping, not an index**, and the distinction is the whole design.
`SceneDocument::getChildrenByParent()` derives parent → children in one pass and one sort per
group, and the caller holds it for one walk. A *cached* index was the obvious answer and is the
wrong one: `findEntity` hands out a mutable `StudioEntity*` and `StudioEntity::setParentId` is
public, so the document cannot know when a parent changes. A stale hierarchy index shows up as
entities that silently vanish from the outliner, which is a far worse failure than the one being
fixed. Rebuilding is O(n log k) and cheap enough that correctness wins.

**The nil Uuid's entry is the roots**, so a walk needs no special case for them and
`getRootEntities()` — which is the same scan under another name — is not called either.

**`getChildren` stays.** Eight callers ask about one parent, where a scan of the document is the
right shape and building a whole map would be worse. The rule is on the new method: use it for
anything that walks the *whole* hierarchy.

**One place already knew.** `SceneValidation` derives its parent set once with a comment saying
`getChildren()` is a scan and asking it per entity would be O(n²). That knowledge stayed local, and
the outliner rediscovered the problem the hard way — which is the argument for the answer living on
the document rather than in each caller.

**Verification.** `ChildrenByParentAgreesWithGetChildrenForEveryParent`, because two orderings of
one hierarchy would show as an outliner whose rows moved when something unrelated rebuilt them; and
`FlatteningTheOutlinerCostsLinearTimeInTheSceneRatherThanQuadratic`, which times 400 entities
against 1 600 and fails above 8× — four times the entities costs about four times as much when the
walk is linear and about sixteen when it is not. A ratio rather than an absolute, because an
absolute is a number about one machine. Verified by putting the per-node `getChildren` back: it
reports 13.2× and fails.

### `STUDIO-30014` — Find what makes the Content Browser cost 23 ms a frame at 1 500 assets

**Acceptance.** The cost is attributed to something specific, and whatever it is either scales or is
recorded as not scaling with a number saying how.

**Measured** (`--ui-benchmark=content`): 23 ms a frame for 1 500 assets against 1.9 ms for the shell
around it, while drawing the same 19 draw calls. Not measured at more than one size, so whether it
is linear or worse is **unknown** — which is why this is worded as a question rather than as a fix.

### `STUDIO-30030` — Establish the interactive frame-rate target and measure against it

**Acceptance.** Measured before optimising, with the benchmark established early so a regression is visible

