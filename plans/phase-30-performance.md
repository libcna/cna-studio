# Phase 30 — Large-project performance

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-30001` … `STUDIO-30999` and are never reused.

**Purpose.** Scale to real projects, not to the example.

**Exit criteria.** Benchmarks exist, they run, and regressions are visible.

**Progress:** 2 of 16 complete `█░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-30001` | Background job system: progress, cancellation, errors, clean shutdown | ⬜ | `STUDIO-02050` |
| `STUDIO-30002` | Bounded queues and backpressure for job submission | ⬜ | `STUDIO-30001` |
| `STUDIO-30010` | Virtualised list and tree infrastructure | ⬜ | `STUDIO-03015` |
| `STUDIO-30011` | Incremental update rather than per-frame rebuild throughout | ⬜ | `STUDIO-30010` |
| `STUDIO-30012` | Caching strategy with explicit invalidation | ⬜ | `STUDIO-09004` |
| `STUDIO-30013` | `SceneDocument` child lookup is an index, not a scan of every entity | ✅ | — |
| `STUDIO-30014` | Find what makes the Content Browser cost 23 ms a frame at 1 500 assets | ✅ | `STUDIO-04028` |
| `STUDIO-30015` | The Content Browser stops asking the filesystem about every asset every frame | ⬜ | `STUDIO-30012`, `STUDIO-30014` |
| `STUDIO-30016` | The Details panel stops opening files to draw itself | ⬜ | `STUDIO-30012` |
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

**Answered. It is the filesystem, and it is linear.**

| Assets | Cost per frame |
|-------:|---------------:|
| 200 | 2.2 ms |
| 400 | 3.0 ms |
| 800 | 5.0 ms |
| 1 600 | 9.1 ms |

Roughly 1.8× per doubling, so linear rather than quadratic — this is not `STUDIO-30013`'s shape.
What makes the constant large is that **the browser asks the operating system about every asset on
every frame**:

- `AssetDatabase::isMissing` is a `std::filesystem::exists()`, and `studioContentRows` calls it once
  per row;
- `result.missingCount = assets.getMissingAssets().size()` is a *second* full pass over the
  database, with another `exists()` per asset.

That is about 3 000 stat calls a frame at 1 500 assets, synchronously, in the middle of describing
the UI.

**Attributed by measurement, not by reading.** With both `exists()` calls stubbed out and nothing
else changed, the same scenario falls from **21.5 ms to 8.3 ms**: 61% of the panel's frame cost is
the filesystem.

**And it is worse in real use than in the benchmark.** With no project open the same 1 500 assets
cost 8.6 ms rather than 21.5, because the paths resolve under a root that does not exist and the
stat fails early. A real project root is the slow case, and a project on a network share or a cold
cache is slower still — this is synchronous disk I/O in the render loop, which is a different kind
of problem from a slow loop.

**The fix is not this task.** Halving the syscalls by deriving the missing set once per build would
be a clear improvement and would still leave the editor stat-ing every asset every frame. What it
actually needs is the missing set held with explicit invalidation, which is `STUDIO-30012`'s
question — when a file deleted outside the editor should be noticed is a design decision, not an
optimisation. Filed as `STUDIO-30015`.

### `STUDIO-30016` — The Details panel stops opening files to draw itself

**Acceptance.** Selecting a material or a prefab instance does not open a file on every frame. The
document is read when it changes rather than when it is drawn, and an edit made outside Studio is
still noticed — which is the same design decision `STUDIO-30012` has to make for the asset database
and is why this waits for it.

**Where it comes from.** `STUDIO-07046` and `STUDIO-07042` each added an editor whose document is a
*file*: the material editor reads the `.cnamaterial` it is showing, and the prefab section loads the
`.cnaprefab` and walks both subtrees to find the overrides. Both already halve the obvious cost by
working on the input pass and keeping what they found for the draw pass — the prefab section packs
its summary into the widget state — so it is one read per frame rather than two. It is still a read
per frame, and the prefab one also walks a subtree that could be hundreds of entities.

Smaller than `STUDIO-30015` by a lot: those are two files while one thing is selected, against three
thousand `stat` calls per frame on every frame the Content Browser is visible. Filed so it is not
forgotten rather than because it is urgent.

### `STUDIO-30015` — The Content Browser stops asking the filesystem about every asset every frame

**Acceptance.** Drawing the Content Browser performs no filesystem access proportional to the number
of assets, and a test says so rather than a benchmark implying it.

**What it has to decide**, which is why it waits for `STUDIO-30012` rather than being a smaller edit:
when a file deleted outside the editor becomes visible as missing. Per frame is what happens today
and is what makes this expensive; never is wrong, because "what did I break when I moved that
folder" is the question a content browser is most often opened to answer. A watch, a rescan on
window focus, and a rescan on an explicit refresh are all defensible and they are not the same
product.

### `STUDIO-30030` — Establish the interactive frame-rate target and measure against it

**Acceptance.** Measured before optimising, with the benchmark established early so a regression is visible

