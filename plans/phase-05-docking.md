# Phase 5 — Docking and workspace

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-05001` … `STUDIO-05999` and are never reused.

**Purpose.** A first-class panel shell: docking, tab groups, splitters, floating panels and persistent layouts.

**Exit criteria.** A user can rearrange the whole workspace, restore defaults, and have their arrangement survive a restart and a Studio upgrade.

**Progress:** 10 of 14 complete `████████░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-05001` | Dock node tree model | ✅ | `STUDIO-03015` |
| `STUDIO-05002` | Split nodes horizontally and vertically | ✅ | `STUDIO-05001` |
| `STUDIO-05003` | Resizable splitters with minimum sizes and correct cursor shapes | ✅ | `STUDIO-05002` |
| `STUDIO-05004` | Tab stacks with reordering | ⬜ | `STUDIO-05001` |
| `STUDIO-05005` | Dock a panel to an edge or into a tab group by drag, with drop-target preview | ⬜ | `STUDIO-05004` |
| `STUDIO-05006` | Undock to a floating panel | ⬜ | `STUDIO-05005` |
| `STUDIO-05007` | Hide, show and close panels | ✅ | `STUDIO-05001` |
| `STUDIO-05008` | Serialize the workspace layout | ✅ | `STUDIO-05001` |
| `STUDIO-05009` | Restore the default layout | ✅ | `STUDIO-05008` |
| `STUDIO-05010` | Named saved layouts | ⬜ | `STUDIO-05008` |
| `STUDIO-05011` | Layout migration across Studio versions | ✅ | `STUDIO-05008` |
| `STUDIO-05012` | A corrupt layout file never prevents Studio from starting | ✅ | `STUDIO-05011` |
| `STUDIO-05013` | Tab strips that switch the active panel on click | ✅ | `STUDIO-03031` |
| `STUDIO-05014` | Store the workspace layout in user preferences on disk | ✅ | `STUDIO-05008` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-05001` — Dock node tree model

**Acceptance.** The arrangement is an explicit tree — a leaf is a tab group, a split is two children
with an orientation and a fraction — and every gesture a user can make is an operation on it with a
testable postcondition. Not a reimplementation of Dear ImGui docking, which infers a layout from
submission order and keeps it in an opaque blob: this one is the shell's only source of truth for
where anything is, and `isWellFormed()` states the invariants directly

**How it was met.** Nodes live in one arena and refer to each other by index, so a tree is a value
that copies without deep-cloning, serializes without inventing identity, and turns a stale
reference into an out-of-range index — which is checkable — rather than a dangling pointer, which
is not. Minimum sizes propagate **up** the tree: a split's minimum along its axis is the sum of its
children's, which is what makes "drag the splitter as far as it goes" stop somewhere sensible

**Verification.** `tests/StudioDockTests.cpp`: split, move, remove and collapse; a panel living in
exactly one place; leaves tiling the area without overlapping; and the default workspace built from
the same operations a user's gestures use, so the default is never an arrangement no gesture can
reach

### `STUDIO-05003` — Resizable splitters with minimum sizes and correct cursor shapes

**Acceptance.** A splitter drags, stops at both neighbours' minimums, and shows a resize cursor

**How it was met.** The grab area is wider than the drawn divider on both sides — a 4-pixel
splitter drawn at 4 pixels is a target people miss — and the drag is expressed in pixels, which is
what a drag produces, with the conversion to a fraction done in the one place that knows the
minimums. The cursor is requested by the widget and honoured only while it holds the mouse, so it
does not flicker back to an arrow when the pointer crosses a panel mid-drag

**Verification.** `tests/StudioDockTests.cpp` for the model, `tests/StudioShellInteractionTests.cpp`
for the gesture: a drag resizes, the cursor is a horizontal resize while it holds the mouse, and
40 consecutive over-drags leave every leaf with a positive extent

### `STUDIO-05007` — Hide, show and close panels

**Acceptance.** Closing a panel collapses the leaf it emptied, so no dead stripe is left behind, and
a panel the descriptor marks non-closable refuses to close. Reopening docks it somewhere visible

### `STUDIO-05009` — Restore the default layout

**Acceptance.** Reachable from the Window menu and actually implemented — the shell owns the
workspace, so it attaches this handler itself rather than leaving the menu entry reporting
"not implemented"

### `STUDIO-05013` — Tab strips that switch the active panel on click

**Acceptance.** Each dock region draws a tab per panel, the active one is marked by an accent rule
rather than a fill alone, and clicking a tab makes its panel active. The modified marker is a dot
rather than an asterisk in the label, so editing a document does not shift every tab in the strip

**Verification.** `tests/StudioShellInteractionTests.cpp`

### `STUDIO-05006` — Undock to a floating panel

**Acceptance.** Floating windows only where the platform supports them; degrades to an in-shell floating layer otherwise

### `STUDIO-05008` — Serialize the workspace layout

**Acceptance.** Deterministic, versioned, and stored in user preferences rather than in shared project data

### `STUDIO-05011` — Layout migration across Studio versions

**Acceptance.** A layout from an older version opens, with unknown panels dropped and reported — never a failure to start

**Verification.** Test with a synthetic old-version layout and an unknown panel id

### `STUDIO-05012` — A corrupt layout file never prevents Studio from starting

**Acceptance.** Falls back to the default layout and reports what it could not read

### `STUDIO-05014` — Store the workspace layout in user preferences on disk

**Acceptance.** An arrangement a user made is there when they open Studio again, in the place their
platform keeps configuration — `$XDG_CONFIG_HOME`, `%APPDATA%`, `$HOME/.config` — not beside the
executable and not in the project. A workspace is the user's, not the install's and not the
project's

**How it was met.** `StudioWorkspaceStore` owns the file, the enclosing format version, and the ways
writing to disk goes wrong. It knows nothing about panels, leaves or splits: reconciling a stored
arrangement against the panels a build actually has is `StudioShell::loadLayout`'s job already
(`STUDIO-05011`), and answering that question in two places would mean two answers. The seam between
them is a `JsonValue`

**Losing a layout is never losing work.** Every failure here is recoverable by definition — the
worst outcome is the default arrangement, one menu item away. So nothing throws, nothing refuses to
start, and nothing is silent. The file is written to a temporary beside itself and renamed over the
original, so a Studio killed mid-save leaves the previous layout rather than a truncated one: one
rename, and the only way this could lose something is gone

**Note on the dependency.** Originally listed as depending on `STUDIO-06010`, preferences
persistence. It does not: a layout file is its own file, in the same directory preferences will
live in, and waiting for the preferences system would have meant shipping a shell that forgets its
arrangement for no reason

**Verification.** `tests/StudioWorkspaceStoreTests.cpp` — the round trip through a changed
arrangement, a first run reporting nothing, a truncated file costing the arrangement and saying so,
a file from a newer Studio refused rather than half-read, no temporary left behind by either a first
or a second save, forgetting twice, a store with nowhere to write failing with a reason, and
configuration kept apart from state. Plus `CnaStudioWorkspacePersistence`, which runs the real
binary four times against one file: the failure it catches — a shell that saves nothing and silently
starts fresh every time — is invisible to any test that never exits
