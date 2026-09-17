# Phase 30 — Large-project performance

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-30001` … `STUDIO-30999` and are never reused.

**Purpose.** Scale to real projects, not to the example.

**Exit criteria.** Benchmarks exist, they run, and regressions are visible.

**Progress:** 7 of 16 complete `█████░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-30001` | Background job system: progress, cancellation, errors, clean shutdown | ✅ | `STUDIO-02050` |
| `STUDIO-30002` | Bounded queues and backpressure for job submission | ⬜ | `STUDIO-30001` |
| `STUDIO-30010` | Virtualised list and tree infrastructure | ✅ | `STUDIO-03015` |
| `STUDIO-30011` | Incremental update rather than per-frame rebuild throughout | ⬜ | `STUDIO-30010` |
| `STUDIO-30012` | Caching strategy with explicit invalidation | ✅ | — |
| `STUDIO-30013` | `SceneDocument` child lookup is an index, not a scan of every entity | ✅ | — |
| `STUDIO-30014` | Find what makes the Content Browser cost 23 ms a frame at 1 500 assets | ✅ | `STUDIO-04028` |
| `STUDIO-30015` | The Content Browser stops asking the filesystem about every asset every frame | ✅ | `STUDIO-30012`, `STUDIO-30014` |
| `STUDIO-30016` | The Details panel stops opening files to draw itself | ✅ | `STUDIO-30012` |
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

**Done.** `StudioJobSystem` in `cna-studio-core`, owned by `StudioShellPanels` and drained once per
`poll()`.

**"No document mutation races" is a shape, not a rule.** A job body receives a `StudioJobContext`
and nothing else: it cannot be handed a `StudioContext`, a `SceneDocument` or an `AssetDatabase`,
because there is no parameter to pass one through. What happens *to* the document is the completion
handler's, and a completion handler runs on the main thread inside `drain()`. The code that could
race has nothing to race against — which is a guarantee that survives the next contributor, and a
convention is not.

**`drain()` is the only crossing, and that is what makes it testable.** Nothing a worker produced is
visible until it has been called, so a test submits, waits for idle, drains and asserts. No test in
this suite sleeps, and none of them can be flaky on a slow machine, because there is no window in
which the answer depends on timing.

**A drain is bounded by what was already waiting**, even with no budget. A completion may submit
another job — which is the shape every real use has, a walk finishing and asking for the next piece
— and on a fast machine that job can finish before the loop comes round again. An unbounded drain
would then run a self-feeding chain to its end inside one frame, which is the opposite of what a
background job system is for. Work produced *by* a drain belongs to the next one. This was found by
a test that expected one completion and got two.

**Cancellation is cooperative, because the alternative does not exist.** There is no portable way to
stop a thread part-way that leaves its memory in a state anybody can reason about. A cancelled job
still delivers its completion, with the state set to cancelled: a caller that allocated something
for the job needs the handler to run, and one that cleaned up only on success would leak on every
cancellation — the path taken most, because cancelling is what happens when the user looks
elsewhere.

**A body that throws is a failed job.** A background walk that hits an unreadable directory must not
take the editor down with it, and the reason has to survive to the handler that will say so. The
first reported reason wins: a body reporting two failures on its way out has one cause and one
consequence.

**Progress is a snapshot, not a stream.** The latest report wins and earlier ones are overwritten. A
queue would fall behind a fast loop and then deliver the backlog to a progress bar with nothing left
to say. An empty message leaves the previous one, so a loop reporting a fraction every iteration and
a message every hundred does not blank the line in between. A negative fraction is a real answer —
"working" — and a better one than a bar sitting at zero because the job invented a denominator.

**Shutdown cancels, joins, and runs nothing.** Undrained completions exist to touch the document,
and at teardown the document may already be gone; a handler that ran there would be the kind of
crash that reproduces on one machine in ten.

**`StudioJobMode::Immediate` is a real mode, not a test double.** Bodies run inside `drain()`, one
per call — so a frame that submits a hundred jobs does not become a frame that runs a hundred, which
is exactly the difference that would make a substitute behave unlike the thing it substitutes for.
It makes Studio buildable and correct with no threads at all.

**What it does not yet do, and why.** Nothing has been *moved* onto it: the dependency index, the
relink search and the thumbnail work all still run on the frame. Moving them is not a wiring change,
because each reads the `AssetDatabase` and a worker reading it while the main thread moves an asset
is precisely the race the design forbids. Each needs a snapshot handed to the worker by value, which
is its own task's work — `STUDIO-09003` for thumbnails, and the dependency index's rebuild with it.

**Verification.** `tests/StudioJobTests.cpp`, every guarantee asserted against **both** modes where
the guarantee applies to both: the result arriving only in `drain`, a cancelled job still delivering
its completion and never starting its body, a running job seeing the flag and stopping at its first
check, progress as a snapshot, a throwing body becoming a failed job with its reason intact, the
drain budget spreading a burst over frames, a completion submitting another job without deadlocking
on the system's own lock, shutdown running no handler, the worker count never being zero, immediate
mode running one body per drain, and sixty-four jobs over four workers each arriving exactly once.

**Run under ThreadSanitizer**, in a build configured for it: 1 401 tests, no data races reported.

### `STUDIO-30012` — Caching strategy with explicit invalidation

**Its dependency on `STUDIO-09004` was wrong and has been dropped**, which is the first thing to
record because it changes the plan. The row read "depends on the thumbnail cache", and that bundled
two different caches into one task: a thumbnail cache is invalidated by a *reimport*, and the cache
that actually costs something today is the asset **presence** cache, invalidated by a file appearing
or disappearing. The second needs nothing from the first. Doing this now is what unblocks
`STUDIO-30015`, which `STUDIO-30014` measured as 61% of the Content Browser's frame cost. The
thumbnail cache still follows the strategy stated here when `STUDIO-09004` arrives.

**The question this had to answer**, in `STUDIO-30015`'s words: *when does a file deleted outside
the editor become visible as missing?* The answer:

- **Immediately** for anything Studio does itself — a scan, an add, a move, a delete, a relink. Those
  all go through `AssetDatabase`, so the cache cannot be behind them.
- **Within half a second** for anything else, because `AssetWatcher` already stats every tracked
  file on its poll. Keeping the cache in step there costs nothing that was not being spent.
- **On demand**, through `AssetDatabase::refreshPresence`, for an explicit Refresh.

"Never" was rejected: *what did I break when I moved that folder* is the question a content browser
is most often opened to answer. "Every frame" is what used to happen and is what made it expensive.

**The watcher moved from the CNA-backed host into `StudioShellPanels`.** It was polled by
`CnaStudioShellHost` and by nothing else, which was tolerable while it only reloaded textures and is
not now that it is what invalidates the cache: an invalidation that ran in one of the two builds
would make the cache right in one of them — and the headless preview and every test are the other.
What the panels cannot do themselves, dropping a rendered texture, is a seam the host now fills.

**`getPresenceProbeCount()` is the instrument.** Every filesystem presence check this class makes
goes through one private function that counts it, so "drawing performs no filesystem access
proportional to the number of assets" is something a test asserts rather than something a benchmark
implies. A number whose *not* going up is the property under test.

**`add()` probes rather than trusting the caller.** A record handed in by a test, by an undone
delete or by a scan has no reliable idea whether its file is on disk, and a cache seeded from a
guess is worse than no cache. A scan still pays exactly one probe per record overall: this one for
the files it walked, and the pass at its end for the rest.

**`getMissingCount()` is maintained incrementally**, so the browser can ask for the number without
anybody building the list.

**Verification.** `tests/ProjectAndAssetTests.cpp` asserts the strategy directly — a file removed
outside the editor is *not* missing until something looks, is missing after `refreshPresence`, and a
second refresh reports no change. `tests/StudioContentBrowserTests.cpp`: the probe count unchanged
across five frames of drawing 400 assets in **both** views, one probe per record on a refresh and
exactly one for a single asset, the watcher making an outside deletion and its return visible, and
`StudioShellPanels::poll` doing that in a build with no CNA at all.

### `STUDIO-30015` — The Content Browser stops asking the filesystem about every asset every frame

**Acceptance.** Drawing the Content Browser performs no filesystem access proportional to the number
of assets, and a test says so rather than a benchmark implying it.

**Done, and the test says so.** `DrawingTheContentBrowserAsksTheFilesystemAboutNothing` draws five
frames over 400 assets in both views and asserts `AssetDatabase::getPresenceProbeCount()` is
unchanged — not smaller, unchanged. It also asserts the browser drew all 400 rows, so it cannot pass
by drawing nothing.

**Both halves of `STUDIO-30014`'s finding are gone.** `isMissing` reads the cached flag instead of
calling `exists()` once per row per pass, and `missingCount` reads `getMissingCount()` instead of
building the whole missing list to take its `.size()` — which was a second full pass with another
stat per asset.

**The cost has moved rather than vanished**, and that is recorded rather than glossed: it is now one
probe per record on a watcher poll (twice a second at most, on a loop that was already stat-ing every
file) or on an explicit refresh. The design decision that makes that acceptable is
`STUDIO-30012`'s, which is why this waited for it.

**Measured, at 1 500 assets in a real project, `--ui-benchmark=content`:**

| | Per frame |
|---|---:|
| `STUDIO-30014`, real project root | 21.5 ms |
| `STUDIO-30014`, both `exists()` calls stubbed out | 8.3 ms |
| **Now, real project root, nothing stubbed** | **9.5 ms** |

Which is the predicted landing zone rather than a surprise: the finding said 61% of the panel's cost
was the filesystem, and removing it leaves the rest.

**The benchmark itself was measuring the wrong case and now does not.** `fillAssets` added records
with no project root, so every path resolved under a directory that does not exist and every `stat`
failed early — 8.6 ms against the 21.5 ms a real project cost. It now writes real files under a real
root. A benchmark that only ever sees the fast path cannot see the problem, which is the thing this
one exists for; that it now reports 9.5 ms *with* a root where it reported 9.4 ms without one is
itself the result, because with the cache having a project stopped costing anything.

### `STUDIO-30010` — Virtualised list and tree infrastructure

**Half of it was already there, and saying which half is the honest part.** `StudioScrollResult::visibleRows`
has existed since `STUDIO-03015`'s scroll region, and `StudioTreeView` and the Output Log both build
only the rows in its window — a scene with fifty thousand entities already costs what one with five
costs. What had no equivalent was the **grid**, and the grid is what a content browser is.

**Culling inside the loop is not virtualisation.** The Content Browser's grid walked every card and
skipped the ones off screen. That stops the drawing and still walks the list — a hundred thousand
iterations a pass to show forty — and, more importantly, it cannot stop the model being built,
because by then it has been. `StudioScrollResult::visibleCells` answers the window *before* any item
exists, which is the shape a caller needs to build only what it draws.

**The extent and the layout now share one function.** `studioGridColumns` and
`studioGridContentHeight` are computed in different places by different callers, and a grid whose
two ideas of the column count disagreed would scroll past its own last row. One function, used by
both.

**What this does not fix, and which task does.** At 1 500 assets the loop was never the cost:
`--ui-benchmark=content` reads 9.6–10.4 ms across runs, against 9.5 ms before — inside the noise,
because the dominant cost is now `studioContentCards` building all 1 500 cards on every pass. That
is `STUDIO-09016`'s work, and this is the infrastructure it needs: the window is the thing that lets
a listing be built for forty items instead of a hundred thousand.

**Verification.** `tests/StudioVirtualisationTests.cpp` tests both windows as arithmetic rather than
through a frame, because that is what they are: the list window's size and its one row of slack at
each end, the same window at a hundred thousand rows as at a thousand, the column count either side
of each boundary and floored at one, the extent matching the rows the window produces, the grid
window moving with the scroll without growing, and the awkward cases — nothing to show, a zero-height
cell, scrolled past the end, and the negative offset an over-scroll produces for a frame.

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

**Done.** `StudioAssetDocumentCache` holds the material and the prefab, and the panel takes it as a
seam.

**Invalidated the same two ways the presence cache is**, which is the point of having a strategy
rather than a cache each: the record's stamp moving (what a watcher poll updates, compared with no
syscall), and an explicit drop after anything writes an asset file. The binder drops the whole cache
on any command — coarse on purpose, because too much costs one reload of what is on screen and too
little is an editor showing a file it has already overwritten.

**A failure is cached as a failure.** A material that will not parse is one somebody is looking at
*because* it will not parse, and reopening it every frame is exactly the case this exists to stop.

**A copy comes out, not a pointer.** The prefab section keeps its document across both passes, and
the cache may reload underneath it on the frame a command invalidates — a section holding a pointer
into it would be a crash on the frame somebody presses Apply.

**The seam may be unset**, and then the panel reads the file as it did before. A test that builds
the panel with no services gets the same picture, more slowly — which is what keeps every existing
test honest rather than needing the cache to pass.

**Verification.** `tests/AssetDependencyTests.cpp`: one open then ten asks with no further opens,
an explicit invalidation costing exactly one more, a stamp moving causing a re-read that returns the
*new* contents, a broken document opened once rather than five times, and a texture asked for as a
material costing no open at all. `tests/StudioDetailsPanelTests.cpp` drives the real panel through a
shell frame and asserts `getFileReadCount()` is unchanged across five frames while the material
editor is showing its fields.

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

