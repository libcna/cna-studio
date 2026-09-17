# Phase 13 — World Outliner 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-13001` … `STUDIO-13999` and are never reused.

**Purpose.** Large-hierarchy editing that stays responsive and never loses a mutation.

**Exit criteria.** A scene with tens of thousands of entities browses and edits smoothly.

**Progress:** 1 of 12 complete `█░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-13001` | Nested entity tree with expand and collapse | ⬜ | `STUDIO-07006` |
| `STUDIO-13002` | Search and filter | ⬜ | `STUDIO-13001` |
| `STUDIO-13003` | Multi-selection, shift-range and Ctrl-additive | ⬜ | `STUDIO-13001` |
| `STUDIO-13004` | Drag to reparent | ⬜ | `STUDIO-03023` |
| `STUDIO-13005` | Visibility and lock toggles | ⬜ | `STUDIO-13001` |
| `STUDIO-13006` | Rename, duplicate and delete | ⬜ | `STUDIO-13001` |
| `STUDIO-13007` | Context menu | ⬜ | `STUDIO-06005` |
| `STUDIO-13008` | Folder and group organisation | ⬜ | `STUDIO-13001` |
| `STUDIO-13009` | Prefab status indication | ⬜ | `STUDIO-13001` |
| `STUDIO-13010` | Type icons and component warnings | ⬜ | `STUDIO-13001` |
| `STUDIO-13011` | Virtualisation for large worlds | ✅ | `STUDIO-30010` |
| `STUDIO-13012` | Every mutation goes through a command | ⬜ | `STUDIO-02035` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-13004` — Drag to reparent

**Acceptance.** Cycles are rejected; the operation is one undo entry

### `STUDIO-13011` — Virtualisation for large worlds

**Verification.** Stress test at 10,000+ entities with deep nesting

**Done.** The World Outliner builds the rows it shows and counts the rest, and
`tests/StudioLargeProjectTests.cpp` holds it there at twenty thousand entities in chains fifty
deep.

**It had looked virtualised since `STUDIO-03034`, and was not.** The tree widget has culled its
drawing from the first day, so `rowsDrawn` was already a screenful and every draw-call assertion
passed. What the widget cannot bound is the model the panel hands it: `studioOutlinerRows` flattened
the whole scene into `StudioTreeRow`s — three strings apiece — twice a frame, to show forty. This is
the same defect the Content Browser's list view had (`STUDIO-09016`), found the same way, and the
reason `StudioOutlinerResult` now reports `rowsBuilt` beside `rowsDrawn`: a panel that builds fifty
thousand rows and draws forty has a perfectly bounded `rowsDrawn`.

**One function counts and builds.** `walk` takes a running index, a window and an optional output
vector; counting is the same traversal with nowhere to put the rows. Two functions agreeing by
inspection is exactly how a scrollbar and its rows end up quietly out of step, and the symptom —
the last entity in a large scene being unreachable — is one nobody reports, because nobody can tell
it is missing. `TheOutlinersWindowIsTheSameRowsTheWholeListWouldHaveHadThere` asserts slice equality
at the start, the middle, across a chain boundary and running off the end.

**A hierarchy has no index to seek into**, so reaching row *n* still costs *n* steps of the walk.
That is not the same as costing *n* rows: a step is two hash lookups and an increment, where a row
is three string allocations. And the walk no longer formats an id per node when nothing is
collapsed — which is every tree until the user closes something — because `collapsedCount()` is
cheaper to ask than a thirty-six-character string is to build.

**Measured** (`--ui-benchmark=outliner`, Release, 120 frames at 1920×1080, median µs/frame):

| scenario | before | after |
|---|---:|---:|
| `outliner-2000` | 2587 | 660 |
| `outliner-scrolling` | 2384 | 779 |
| `outliner-20000` | 30 490 | 8101 |
| `outliner-20000-deep` | 37 934 | 20 674 |
| `outliner-20000-scrolling` | 44 721 | 20 798 |

**What is left is not virtualisation, and it is named.** Twenty thousand entities still cost about
20 ms a frame, and the remaining cost is `SceneDocument::getChildrenByParent()`: a pass over every
entity building a map of child vectors, once per drawing pass. It is uncached deliberately —
`findEntity` hands out a mutable entity and `setParentId` is public, so a cached hierarchy would go
stale silently, and a stale hierarchy index presents as entities vanishing from the outliner. The
panel already derives it once and shares it between the count and the window; making it not happen
at all is `STUDIO-30011`, which is about exactly this and is where it belongs. `STUDIO-30020` is the
benchmark that says so with a number.

