# Prototype inventory — the migration checklist

`plan.md` STUDIO-00014.

Every panel, menu item, toolbar control and keyboard shortcut the Dear ImGui prototype offers, with
the source file that owns it and what the native Studio shell does about it. Phase 7 proves parity
**against this list**, item by item, rather than by impression — and the list is checked by the test
suite in both directions (`STUDIO-00014`, `STUDIO-07020`), so neither an item that quietly loses its
native counterpart nor a piece of the prototype nobody wrote down can survive to be noticed by
somebody a year later.

## How to read it

| Column | Meaning |
|--------|---------|
| Native | The shell panel id, the action id, or `—` when nothing native answers it yet |
| Status | ✅ answered natively · 🔄 answered but not yet proven equivalent · ⬜ no native answer |

An item with status ✅ **must** resolve: a panel id must be registered and have content, and an
action id must exist in the registry. That is what makes this a checklist rather than a record of
intentions. An item with ⬜ carries the reason it has none, in the same discipline as the
unimplemented-command and empty-panel guards.

The other direction matters more and is easier to forget. The tests read the prototype's own source
— its panel files, its menu bar, its two toolbars and its shortcut dispatcher — and require every
item they find to appear here. Without that, a control nobody listed passes every check above, for
the reason that the list is what those checks read. That is how the tile-index control was found:
drawn a hundred lines below the rest of the toolbar, under a condition, and in no list at all.

What none of it can check is whether the two *behave* the same. That is `STUDIO-07021` for input,
`STUDIO-07022` for docking and `STUDIO-07023` against the reference screenshots.

The prototype is `src/panels/*.cpp` and `src/app/StudioApplication.cpp`; it is a *temporary*
compatibility fallback and is deleted by `STUDIO-07030`, at which point this file becomes the record
of what was replaced.

---

## Panels

| Prototype panel | Source | Native | Status |
|-----------------|--------|--------|--------|
| Scene Hierarchy | `src/panels/HierarchyPanel.cpp` | `outliner` | ✅ |
| Inspector | `src/panels/InspectorPanel.cpp` | `details` | ✅ |
| Assets | `src/panels/AssetBrowserPanel.cpp` | `content` | ✅ |
| Console | `src/panels/ConsolePanel.cpp` | `output` | ✅ |
| Build | `src/panels/BuildPanel.cpp` | `build` | ✅ |
| Validation | `src/panels/ValidationPanel.cpp` | `problems` | ✅ |
| History | `src/panels/HistoryPanel.cpp` | `history` | ✅ |
| Diagnostics | `src/panels/DiagnosticsPanel.cpp` | `diagnostics` | ✅ |
| Backends | `src/panels/ComparisonPanel.cpp` | `comparison` | ✅ |
| Viewport | `src/panels/ViewportPanel.cpp` | `viewport` | 🔄 |

The Viewport is 🔄 rather than ✅ because the native one composites the 2D scene, navigates, picks
and manipulates, and the prototype's also has the 3D view and tilemap painting. Those are the
substance of what keeps `STUDIO-06015` from being true, and they are listed under *Not yet answered*
below rather than hidden inside a ✅.

Panels the native shell adds, which the prototype has no equivalent for: `layers`, `preferences`,
`material` (registered, no content yet — Phase 19).

---

## Menu items

| Menu | Item | Shortcut | Native | Status |
|------|------|----------|--------|--------|
| File | New Scene | `Ctrl+N` | `studio.file.newScene` | ✅ |
| File | Save Scene | `Ctrl+S` | `studio.file.save` | ✅ |
| File | Recover Unsaved Scene | — | `studio.file.recoverScene` | ✅ |
| File | Discard Recovered Scene | — | `studio.file.discardRecovered` | ✅ |
| File | Exit | `Alt+F4` | `studio.file.quit` | ✅ |
| Edit | Undo | `Ctrl+Z` | `studio.edit.undo` | ✅ |
| Edit | Redo | `Ctrl+Y` | `studio.edit.redo` | ✅ |
| Edit | Duplicate | `Ctrl+D` | `studio.edit.duplicate` | ✅ |
| Edit | Delete | `Delete` | `studio.edit.delete` | ✅ |
| View | Frame Selected | `F` | `studio.view.focusSelected` | ✅ |
| View | 2D View / 3D View | `2` / `3` | — | ⬜ |
| View | Grid on Scene Plane / Grid on Ground Plane | — | — | ⬜ |
| View | Translate Gizmo | `W` | `studio.view.translate` | ✅ |
| View | Rotate Gizmo | `E` | `studio.view.rotate` | ✅ |
| View | Scale Gizmo | `R` | `studio.view.scale` | ✅ |
| View | Use Local Space / Use World Space | `X` | `studio.view.toggleGizmoSpace` | ✅ |
| *plugin* | Plugin menus and commands | `studio.plugin.*` | ✅ |

**Recover / Discard Unsaved Scene** are ✅ as of `StudioRecoverySession`, which is the flow itself
rather than a second copy of it: the snapshot timer, the scan after a project opens, and the two
answers. Both UIs drive the same object, so the native shell writes snapshots, offers what a previous
session left — as a sticky notification as well as a log line — and greys both rows out when there is
nothing to answer for.

**Exit** is ✅ as of `StudioShell::setQuitHandler`. It had been the odd one out: the command existed,
the CNA host closed the window, and the two had nothing to do with each other — the host watched
`invokedActions()` for the id and called `Exit()`, so the command's own handler never ran and there
was nowhere for "the scene has unsaved changes" to be asked. It is a seam now, like the clipboard
and the workspace, and quitting with unsaved work asks first.

**Plugin menus** were the one item here that was genuinely architectural, and are ✅. The prototype
*draws* them: it walks the extension registry every frame and calls `beginMenu` and `menuItem`. That
works, and it is exactly why a plugin command there can never have a shortcut, never be greyed out,
never appear on a toolbar and never show up in the shortcut editor — it is not a command, it is a
row.

Natively each becomes a registry action with an id under `studio.plugin.`, and the menu names it.
Everything the registry offers then applies to it without plugin menus being a special case
anywhere. A menu a plugin asks for by a name Studio already uses *is* that menu, with one separator
before the plugin's rows — rather than a second menu of the same name beside it, which is what Dear
ImGui's `BeginMenu` produces because it has no opinion about a title it has already seen. A menu
only the plugin knows the name of is created before Help, which is last in every application anybody
has used.

Rebuilt whenever the extension registry's revision changes, so unloading a plugin takes its rows and
its commands with it — a row left behind would call an `invoke` pointing into a library the host has
closed.

---

## Toolbar controls

`ViewportPanel` draws two toolbars above the image: the play controls and the tool controls. They are
the prototype's only toolbars, and they hold four things the menus do not — Pause, Step, the backend
to launch on, and the tilemap tool — so a parity claim that looked only at panels, menus and
shortcuts would miss them.

The Widget column is the literal the prototype passes to the widget, `##` suffix and all, because
that is what the test extracts from the two functions. It is ugly on purpose: matching on a prettier
name would be matching on something nobody can check.

| Toolbar | Control | Widget | Native | Status |
|---------|---------|--------|--------|--------|
| Play | Start the game | `Play` | `studio.play.play` | ✅ |
| Play | Stop the running game | `Stop` | `studio.play.stop` | ✅ |
| Play | Pause the running game | `Pause` | `studio.play.pause` | ✅ |
| Play | Resume a paused game | `Resume` | `studio.play.pause` | ✅ |
| Play | Advance one frame | `Step` | `studio.play.step` | ✅ |
| Play | Which backend to launch on | `Backend` | `studio.window.showPanel.comparison` | ✅ |
| Tools | Tilemap tool | `##tool` | — | ⬜ |
| Tools | Tile to paint | `Tile` | — | ⬜ |
| Tools | Manipulator | `##gizmo` | `studio.view.translate` | ✅ |
| Tools | Gizmo space | `##space` | `studio.view.toggleGizmoSpace` | 🔄 |
| Tools | 2D view | `2D##view` | — | ⬜ |
| Tools | 3D view | `3D##view` | — | ⬜ |

**The tilemap tool and the tile index** are ⬜ together: the index only appears beside the paint
and fill tools, so it arrives with them. Finding it is what this table was for — it is drawn a
hundred lines below the rest of the toolbar, under a condition, and it had been in no list at all.

**Pause, Resume and Step** are ✅ as of `STUDIO-16015`. The protocol was always there — the player
has honoured `Pause`, `Resume` and `StepFrame` since play mode existed, with its own tests — and what
was missing was an editor that sent them. One checkable Pause answers two of the prototype's rows:
a button that renames itself between Pause and Resume is one a user cannot find twice, and a toolbar
has to *show* whether the game is paused, because the window is there either way.

Restart is new rather than ported: the prototype has none, and stopping and starting is how a user
sees the edits they have made since pressing Play.

**The backend** is ✅, answered in two halves. The native Play launches the renderer the project's
active target profile names, falling back to whatever player build is installed, so the common case
is answered *better* than a dropdown: the choice is a project decision rather than a control the user
has to get right each time. And the Backends panel — where a user comes to think about renderers —
now has a "Play on" strip that overrides it for the session, which is the half that was missing.

Not persisted, deliberately. The project's renderer is what the game ships on; an override is a
thing somebody did to one session to look at something, and a Studio that remembered it across
restarts would quietly ship a different answer from the one in the project.

**The manipulator** is ✅ although the shapes differ: the prototype has one dropdown, the native
toolbar has three checkable buttons, so it shows which mode is on without being opened.

**The gizmo space** is 🔄 because the command exists and is on the same `X` as the prototype, but
nothing native *shows* which space is active. The prototype's button is labelled with the space it
is in for exactly that reason, and a toolbar that cannot be read is half a control.

**2D and 3D** are ⬜ for the same reason the Viewport is 🔄: there is no native 3D view yet.

---

## Panel content the surface tables cannot see

A panel can be ported, appear above as ✅, and still be missing something, because these tables
account for panels rather than for what is *in* one. The Inspector is the case: it shows something
else entirely when **nothing is selected**, and the native Details panel shows only a sentence
asking the user to select something.

Found by `docs/VISUAL-ACCEPTANCE.md` — by putting the two editors side by side at the same size and
looking, which is the one thing the machine-checked lists cannot do.

| Panel | What | Native | Status |
|-------|------|--------|--------|
| Inspector | Scene Environment: ambient colour and fog | `details` | ✅ |
| Inspector | Grid Snap, editable | `details` | ✅ |
| Inspector | The project's layer list, with add | `details` | ✅ |

The native Layers panel is not an answer to the third: it lists what is *on* each layer, which is a
different question from what the layers are called. All three are answered now, in the Details panel
standing idle, which is where the prototype put them and where a setting belonging to no entity has
to live. Each goes through the command history, so Ctrl+Z reaches them like every other edit.

---

## Keyboard shortcuts

Taken from `StudioApplication::handleShortcuts`. Each must resolve to a native action bound to the
**same** chord: a shortcut that moved is a shortcut every existing user has to relearn.

| Chord | Prototype action | Native | Status |
|-------|------------------|--------|--------|
| `Ctrl+Z` | Undo | `studio.edit.undo` | ✅ |
| `Ctrl+Y` | Redo | `studio.edit.redo` | ✅ |
| `Ctrl+N` | New scene | `studio.file.newScene` | ✅ |
| `Ctrl+S` | Save scene | `studio.file.save` | ✅ |
| `Ctrl+D` | Duplicate selection | `studio.edit.duplicate` | ✅ |
| `Delete` | Delete selection | `studio.edit.delete` | ✅ |
| `F2` | Rename selection | `studio.edit.rename` | ✅ |
| `F` | Frame selection | `studio.view.focusSelected` | ✅ |
| `2` | 2D view | — | ⬜ |
| `3` | 3D view | — | ⬜ |
| `W` | Translate gizmo | `studio.view.translate` | ✅ |
| `E` | Rotate gizmo | `studio.view.rotate` | ✅ |
| `R` | Scale gizmo | `studio.view.scale` | ✅ |
| `X` | Toggle gizmo space | `studio.view.toggleGizmoSpace` | ✅ |

`F2` is ✅ as of the tree view's editable row. It had to take `F2` back from Build, which had it
natively: two commands on one chord means one of them has quietly stopped working, and the user who
finds out is whichever one presses it expecting the other. Build is `Ctrl+B` now.

### What writing this list down found

Four chords disagreed with the prototype's, which is exactly the class of thing an inventory exists
to catch and nothing else would have:

- **`Ctrl+N` meant a different thing.** New *Scene* in the prototype, New *Project* natively. The
  frequent one keeps the plain chord; New Project moved to `Ctrl+Shift+N`, as in most IDEs. There
  was no native New Scene command at all, so it was added.
- **`Ctrl+D` was taken by Open Project.** It is Duplicate in the prototype and in every editor that
  has a duplicate; native Duplicate had been pushed to `Ctrl+Shift+D`. Open Project moved to the
  conventional `Ctrl+O`, which needed a key the vocabulary did not have.
- **`X` had no native command.** The gizmo space could be changed from nothing but code. It is a
  toggle now, as it is in the prototype, and checkable so a toolbar can show which space is on.
- **`F2` was Build.** It is Rename in the prototype, in every file manager and in most editors, and
  there was no native rename at all. Rename took it back and Build moved to `Ctrl+B`. This one was
  found later than the other three, because closing it needed a widget rather than a binding.

Each would have shipped as "the shortcut I have used for a year does something else now", which is
the worst kind of regression: it works, so nothing reports it.

---

## Not yet answered, and what each needs

These are the reason the native shell is not the default (`STUDIO-06015`). None is a research
project; each is a panel or a mode with a known shape.

| What | Prototype home | What it needs |
|------|----------------|---------------|
| The 3D view | `ViewportPanel`, `StudioApplication` | A 3D camera and mesh drawing in the native viewport (Phase 11) |
| Tilemap painting | `ViewportPanel`, `StudioTool` | Paint, erase, pick and fill as viewport tools with a brush (Phase 25 adjacent) |
| Material editing | — | There is no `.cnamaterial` editor to port; the `material` panel is registered and empty (Phase 19) |
| Plugin menus | `MainMenuBar::drawPluginMenus` | Plugins registering actions and menu definitions rather than drawing rows (Phase 28) |
