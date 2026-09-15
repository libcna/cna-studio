# Phase 5 — Docking and workspace

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-05001` … `STUDIO-05999` and are never reused.

**Purpose.** A first-class panel shell: docking, tab groups, splitters, floating panels and persistent layouts.

**Exit criteria.** A user can rearrange the whole workspace, restore defaults, and have their arrangement survive a restart and a Studio upgrade.

**Progress:** 14 of 14 complete `████████████`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-05001` | Dock node tree model | ✅ | `STUDIO-03015` |
| `STUDIO-05002` | Split nodes horizontally and vertically | ✅ | `STUDIO-05001` |
| `STUDIO-05003` | Resizable splitters with minimum sizes and correct cursor shapes | ✅ | `STUDIO-05002` |
| `STUDIO-05004` | Tab stacks with reordering | ✅ | `STUDIO-05005` |
| `STUDIO-05005` | Dock a panel to an edge or into a tab group by drag, with drop-target preview | ✅ | `STUDIO-05002`, `STUDIO-05013` |
| `STUDIO-05006` | Undock to a floating panel | ✅ | `STUDIO-05005` |
| `STUDIO-05007` | Hide, show and close panels | ✅ | `STUDIO-05001` |
| `STUDIO-05008` | Serialize the workspace layout | ✅ | `STUDIO-05001` |
| `STUDIO-05009` | Restore the default layout | ✅ | `STUDIO-05008` |
| `STUDIO-05010` | Named saved layouts | ✅ | `STUDIO-05008`, `STUDIO-03040` |
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

### `STUDIO-05005` — Dock a panel to an edge or into a tab group by drag

**Acceptance.** Press a tab, drag past a threshold, and the shell shows where the panel would land:
into a tab group, or splitting the panel under the pointer to its left, right, top or bottom.
Releasing does what the preview showed; releasing over nothing does nothing

**Five outcomes rather than one.** "Put this panel somewhere" and "put it *beside* that one" are
different intentions, and a model offering only the first would make every rearrangement a two-step
operation

**The threshold is not a detail.** Without it, selecting a tab on a trackpad rearranges the
workspace, because a click that wobbled by a pixel is a drag

**The zones are proportional, not a fixed band.** A fixed 64-pixel edge would leave a 100-pixel-wide
dock with no middle at all

**The half that is easy to get backwards.** `split()` reuses the split node's id, so the content that
was already there ends up at a *new* id. Putting the dragged panel at the old one drops it exactly
where the existing content went, which looks almost right and is not

**Verification.** `tests/StudioDockDragTests.cpp` — a click not starting a drag, a drag starting, all
five zones resolving from a pointer position, an edge drop splitting and placing the panel in the
half the user aimed at, a tab-strip drop joining that group and becoming visible, a drop over
nothing changing nothing, and the preview drawing. Plus `CnaStudioShellPreviewDockDrag`, which
captures it, and two rejections that stop a capture of a drag that never began passing for one that
did

### `STUDIO-05004` — Tab stacks with reordering

**Acceptance.** A panel dropped back on its own tab strip moves to where it was dropped

**It falls out of `STUDIO-05005`.** The tab strip is a drop zone, and the index comes from the
pointer's position along it — so reordering needed no gesture of its own, which is why this task
now depends on the one that used to depend on it

**Verification.** `DroppingAPanelBackOnItsOwnTabStripReordersIt`

### `STUDIO-05006` — Undock to a floating panel

**Acceptance.** A panel can be dragged out of the dock tree into a window of its own, moved,
resized, given more tabs, docked again and saved — and the workspace round-trips through the layout
file with its floating windows in it.

**A float is not a leaf, and storing it as one would be wrong.** The dock tree stores fractions,
because a docked arrangement should rescale with the window: an inspector that is a fifth of the
width stays a fifth of the width on a larger display. A float is the opposite — it is a palette the
user placed and sized deliberately, and scaling it with the window would grow a small picker into a
quarter of a 4K screen. So its geometry is in logical units, and layout only *clamps* it back into
view: a window saved at (3000, 1800) must not be unreachable on a laptop.

**Undocking is a gesture, then a command.** Releasing a dragged tab where no dock leaf is — the menu
bar, the status bar, the toolbar — undocks it, which needs no explaining to anyone who has already
dragged a tab across the workspace. The tab's context menu and the Window menu name it as well,
because a feature reachable only by discovering a gesture is a feature most users never have. The
way back is one command, `Dock All Windows`, greyed out when there is nothing to recover: without
it the answer to "my window has gone off the edge" becomes "reset the layout", which costs the user
everything else they arranged.

**The invisible failure is input, not drawing.** The router's layers are a *modal* stack rather than
a z-order — exactly one layer takes input at a time — so a float drawn on top would still let the
button underneath it light up as the pointer crossed the window covering it. The shell raises the
floating layer only while the pointer is over a float, or while a gesture that began on one is
still running: a float that blocked the workspace whenever it existed would make the panels under
it unusable, and one that never blocked would be worse than either.

**Dragging a tab moves the panel; dragging the space beside it moves the window.** That is the
distinction every editor with floating panels makes, and it is why a float needs no second title bar
above its tabs.

**Verification.** `tests/StudioFloatingPanelTests.cpp`: undocking leaving the rest of its leaf
alone, the last panel of a leaf collapsing it, a float left holding nothing being removed, a panel
open in exactly one place across docks and floats, raising as a rotation, the front-most float being
the one under the pointer, the off-screen clamp, a float keeping its size where a dock rescales, the
index shift when a move deletes the source float, docking being the same operation as moving,
the JSON round trip, a pre-float layout file loading and writing none back, an empty floating window
being dropped rather than rejecting the document, a duplicate claim being refused, title-bar drag
with raise, the corner grip and its minimum, a float blocking the tab beneath it, closing a float
closing every tab in it, Dock All and its enablement, and a float surviving save and restore. Plus
`ADragThatEndsOverNothingUndocksIntoAFloatingWindow` in `StudioDockDragTests.cpp`, which asserts the
preview against the outcome

### `STUDIO-05010` — Named saved layouts

**Acceptance.** An arrangement can be saved under a name, applied from the Window menu, and deleted
— and the names survive a restart, in the same file the current arrangement lives in.

**The file is read before it is written, every time.** Saving the current arrangement runs on exit
and saving a named one runs whenever the user asks, and a Studio that rewrote the whole document
from what it happened to hold in memory would throw away a layout saved by a second Studio running
beside it. The rename is still atomic, so an interrupted save leaves the previous file rather than
half of a new one. The envelope went to version 2; a version 1 file — the current arrangement and
nothing else — still opens, and saving into it upgrades it rather than refusing.

**The shell keeps the documents, not just the names.** Applying a layout is then a call rather than
a round trip through the file, so what is applied is exactly what the menu named, whatever has
happened to the file since. Persisting is a seam (`StudioWorkspaceServices`), for the same reason
the clipboard is: where a workspace lives is the application's question, and a shell that refused to
arrange itself because nobody gave it a file would be worse than one that forgets on exit.

**Persisted first, listed second.** A menu that listed a layout the file never received would offer
it again after a restart and find nothing there — so a refused write leaves the menu unchanged and
says so, rather than the command quietly doing nothing.

**Deleting asks.** A workspace somebody spent ten minutes on is not undoable, and a menu row one
place lower than expected is exactly how it would go. The commands naming a deleted layout are
removed with it, or the Window menu keeps a row that names nothing and the shortcut table a chord
that arranges the workspace as something that no longer exists — which is what
`StudioActionRegistry::remove` is for.

**One place decides what a name is.** A name the menu accepts and the file rejects is a save that
appears to work and is gone at the next start, so `sanitizeName` is the single answer and is applied
on the way in as well as on the way out: this is plain JSON in the user's configuration directory,
so a name that never went through `saveNamed` is an ordinary thing to find.

**Verification.** `tests/StudioSavedLayoutTests.cpp`: the round trip, a named save keeping the
current arrangement and vice versa, a same-name save replacing rather than duplicating, name
ordering, a version 1 file opening and upgrading, names refused once, a rubbish name read past, a
removal taking only its own — then the menu half: every saved layout a command and a row, applying
one rearranging the workspace, Save Layout As asking and saving through the seam, Cancel saving
nothing, Delete asking first and a cancel keeping it, a shell with no file still working, and a
refused write leaving the menu alone
