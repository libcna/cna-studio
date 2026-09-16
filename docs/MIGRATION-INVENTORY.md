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
| Viewport | `src/panels/ViewportPanel.cpp` | `viewport` | ✅ |

The Viewport is ✅ as of `STUDIO-07009`: the native one composites the 2D scene, navigates, picks,
manipulates, paints tiles, shows the 3D view, and forwards input to a running game. It was the last
🔄 in this table.

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
| Tools | Tilemap tool | `##tool` | `studio.view.tool.paint` | ✅ |
| Tools | Tile to paint | `Tile` | `studio.view.tool.paint` | ✅ |
| Tools | Manipulator | `##gizmo` | `studio.view.translate` | ✅ |
| Tools | Gizmo space | `##space` | `studio.view.toggleGizmoSpace` | ✅ |
| Tools | 2D view | `2D##view` | `studio.view.2d` | ✅ |
| Tools | 3D view | `3D##view` | `studio.view.3d` | ✅ |

**The tilemap tool and the tile index** are ✅ together as of `STUDIO-07003`, which is how they were
⬜ together: the index only appears beside the paint and fill tools, so it arrived with them. Finding
it is what this table was for — it is drawn a hundred lines below the rest of the toolbar, under a
condition, and it had been in no list at all.

The model was never the gap. The grid, `PaintTilesCommand` and its stroke merging have been shared
and tested since the prototype had them; what was missing was a viewport that armed a tool and turned
a press into a cell. Five exclusive checkable commands on the View menu arm it, and the index sits in
an overlay in the viewport's own corner rather than in a toolbar — beside the image it edits, where
the prototype puts it, and where a docked viewport can still show it at any panel size.

Both rows name `studio.view.tool.paint` for the same reason Pause and Resume both name
`studio.play.pause`: the index is not a command, it is a field that exists when a tool that uses one
is armed, and the command that arms it is the honest answer to "what do I press to get this".

The prototype's tool selector is a dropdown with no key behind it, so there is no shortcut to match
here — and because the native ones are commands, they are rebindable in the shortcut editor, which a
dropdown never was.

The native tools differ from the prototype's in two places, both deliberate. A drag is **one** undo
entry rather than one per cell, because forty tiles and forty Ctrl+Zs is a tool nobody uses twice.
And the eyedropper **goes back to painting** once it has taken a tile, because one left holding the
eyedropper is one the user has to put down before they can use what it took.

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

**The gizmo space** was 🔄 for one reason and is ✅ for its answer. The command existed and was on
the same `X` as the prototype, and nothing native *showed* which space was active — the prototype's
button is labelled with the space it is in for exactly that reason, and a toolbar that cannot be
read is half a control.

`STUDIO-35050`'s viewport toolbar shows it, and shows it as the prototype does: **two pictures
rather than one lit button**. A lit toggle says "this is on", which is the wrong sentence when the
alternative is not "off" but "the other one" — so the button draws a globe in world space and a set
of axes in local space, and the two are round against angular because that is the difference that
survives sixteen pixels. It is on the viewport rather than on the window toolbar, which is better
than the prototype managed: the answer is beside the gizmo it describes.

**2D and 3D** are ✅ as of `STUDIO-11001`/`STUDIO-11002`. The model was never the gap here either:
`StudioCamera3D`, `pickEntityAt3D`, `buildSceneModelBatch`, `buildSceneSpriteQuads` and
`buildSceneWireframe` are CNA-free, tested, and have been shared with the prototype since it had a
3D view. What was missing was a native viewport that switched to it and turned a drag into an orbit.

The prototype's dropdown is two exclusive checkable commands here, on `2` and `3` — the keys the
prototype binds and the ones anybody who has used a 3D editor reaches for.

**A press in 3D is a camera gesture first.** Every button navigates, so a release cannot simply mean
"clicked": a release after an orbit that selected whatever the camera happened to stop over is the
thing that makes a 3D viewport feel like it is fighting the user. A gesture that moved is a
navigation and never selects; one that did not is a click, and selects by the same two rules the 2D
view has.

**The first switch frames the scene and no later one does.** The default camera looks straight down
an axis, so an unframed 3D view opens on a grid with the level off the edge of it — but framing on
every switch would be worse than not framing at all, because a user who set up a view, glanced at 2D
and came back would find their angle thrown away.

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
| `2` | 2D view | `studio.view.2d` | ✅ |
| `3` | 3D view | `studio.view.3d` | ✅ |
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

Rows leave this table by being answered, and the prose below says which, because a list that only
ever shrinks is one nobody can tell the difference between "done" and "quietly dropped" in.

| What | Prototype home | What it needs |
|------|----------------|---------------|
| Prefab overrides | `InspectorPanel::drawPrefabSection` | Report, revert and apply, in the native Details panel (`STUDIO-07042`) |
| Material asset editor | `InspectorPanel::drawMaterialAsset` | The editor the prototype already has, ported (`STUDIO-07046`) |

Six rows have left this table since it was written. **The sprite animation preview** is answered by
`studioAnimationPreview` in `src/shell-panels/StudioDetailsPanel.cpp` (`STUDIO-07043`): transport,
a frame readout, the frame itself sampled out of the sheet, and a snapshot published to the host so
the viewport draws the frame the preview is showing. The playback lives in the widget state store,
keyed by the component — never in the document, which is what this row asked for. The native panel
also has a trap the prototype's object could not: it is a *function called twice a frame*, and a
clip advanced on both passes runs at double speed with the picture a frame ahead of the transport.
**The audio preview** is answered by
`studioAudioPreviewRow` in `src/shell-panels/StudioDetailsPanel.cpp` (`STUDIO-07044`): Play and
Stop under an audio source's own properties, playing that source's clip at its own volume, pan and
pitch, and the same control on a selected sound asset with neutral settings. It differs from the
prototype's on purpose. `CNA.AudioSource` is declared `unique = false`, so an entity may carry
several sources; the prototype draws **one preview per entity**, found with `findComponent`, and can
only ever play the first of them. The native panel draws one per *source*. **The asset inspector**
is answered by
`studioAssetInspector` in `src/shell-panels/StudioDetailsPanel.cpp` (`STUDIO-07045`): the asset's
identity and kind, and its importer's declared settings edited through the command history. It was
not only a missing section — the native Content Browser and the native Details panel had *different*
ideas of which asset was selected, because the browser wrote into a member of `StudioShellPanels`
while `StudioContext::selectedAsset_` was only ever written by the prototype. The selection is the
context's now, which also brings the exclusivity rule with it: selecting an asset clears the entity
selection. **The 3D view** is answered by
`studioViewportPanel3D` and the host's `renderSceneIn3D`, described under *Toolbar controls* above.
**Plugin menus** are answered by
`bindStudioPluginMenus`: a plugin registers commands under `studio.plugin.` and the menus draw them
like any others, rather than a menu bar calling back into a plugin to draw its own rows.
**Tilemap painting** is answered by `StudioViewportTool` and `studioViewportToolOverlay`, described
under *Toolbar controls* above.

---

## What this inventory could not see, and the correction

**This document was wrong**, and the way it was wrong is worth more than the list above.

It accounted for **panels, menu items, toolbar controls and shortcuts** — every level at which a
*surface* can go missing — and concluded that "the inventory no longer names anything the prototype
does that the native shell does not". Every row was honest. The conclusion was not, because there is
a level below a panel: **the controls inside it.**

The prototype's Inspector draws eight sections. Until `STUDIO-07040` the native Details panel had
three. One of the five missing was **Add Component** — so an entity created in the native shell
could never be given anything to do, and nothing in this document or its tests said so. It was found
by asking, before deleting the prototype, what the prototype's Inspector actually draws.

**The material row was wrong in a second way.** It read "there is no `.cnamaterial` editor to port;
the `material` panel is registered and empty (Phase 19)". There is one:
`InspectorPanel::drawMaterialAsset`. It is not a `.cnamaterial` *panel*, which is what this document
was looking for — it is a section of the Inspector that appears when a material asset is selected,
and it is a working editor. Looking for a panel and concluding there was nothing to port is exactly
the shape of the larger mistake.

**What changed so that it cannot happen again.** `STUDIO-07041` adds the missing level as a
checklist in `tests/StudioMigrationInventoryTests.cpp`: every section the prototype's Inspector
draws is either recorded as answered — with the native file and the symbol that answers it, checked
against the file — or recorded as a gap with the task that closes it, checked against the plan. It
counts the gaps and asserts the number, so closing the last one is a deliberate edit rather than
something nobody notices.

**Dear ImGui cannot be deleted while that number is above zero.** `STUDIO-07030` now depends on all
five, which is the dependency that should have been there from the start.

**And the level below that one is behaviour**, which no inventory reaches: `STUDIO-07021`–`07023`
are for that, and `tests/ApplicationTests.cpp` holds twenty-eight cases that exercise the
prototype's panels and have no native counterpart yet. They are not a gap in the *product* — they
test document behaviour through whichever panel was available — but they are coverage that would be
deleted with the prototype, and the deletion should say so rather than discover it.
