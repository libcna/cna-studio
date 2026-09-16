# Visual acceptance

`plan.md` STUDIO-00013, STUDIO-07023, STUDIO-35081.

> ## The standard is no longer "better than the prototype"
>
> **`STUDIO-35081`.** Everything below this box is the review that decided the native shell had
> overtaken the Dear ImGui prototype. It had, on every row, and that comparison has stopped telling
> us anything: the prototype is a debug-UI toolkit used as an editor, and clearing it is not
> evidence of anything a user would care about. Kept as the record of how `STUDIO-06015` was
> decided; not kept as the bar.
>
> **The bar now.** At 1920×1080, on first launch, CNA Studio must read as *a serious modern 3D
> game-development environment* rather than as a custom developer tool that is tidier than ImGui.
> The World Outliner, the viewport, the Details panel and the Content Browser have to form one
> coherent professional workspace — not four rectangles of the same colour with different text in
> them.
>
> **The question to ask of a capture:** would a developer opening this for the first time believe it
> is an environment in which a real 3D game can be built for years?
>
> That is a judgement and it is meant to be. What is *not* left to judgement is the set of
> properties underneath it — that a layered UI has distinguishable layers, that a colour coding
> agrees with the gizmo it teaches, that an icon distinguishes the thing it names, that a stripe is
> visible and is not a stripe. Those are `tests/StudioVisualQualityTests.cpp`, and they fail with a
> sentence rather than with a diff.
>
> The work itself is the **CNA Studio Visual Quality 1.0** milestone in
> [`plans/phase-35-polish.md`](../plans/phase-35-polish.md).

---

## Where the current captures are

`docs/reference/native-vq1-1920x1080.png` and `native-vq1-1280x720.png` are the shell as it stands,
taken the same way as the migration captures below — `--ui=studio --workspace=none` on
`examples/HelloSprites` through CNA's `SOFTWARE` renderer, with `--frames=20` so the glyph atlas and
the layout have settled. They are kept *beside* the migration captures rather than replacing them:
the four files below are a matched pair at two resolutions and overwriting half of a comparison
leaves a comparison of two different days.

**What they show, against the bar above.** The panels are distinguishable surfaces rather than four
rectangles of the same colour: the tab strips are recessed with the active tab raised, each panel
has its own outline, and the seams between them are drawn rather than implied. Every outliner row
carries the icon of what the entity *is*, decided from its components. The Details panel's vector
fields carry the gizmo's own axis colours. The Content Browser is a card grid. The viewport has a
toolbar over the scene and composites the scene behind it.

**What they show that is not there yet.** Typography is one face at one weight, so a panel title and
a property label differ only in colour. The Details panel's labels and values do not share a column
alignment. The viewport grid is a flat grid rather than a ground plane, there is no orientation
widget, and a selected entity has no outline. The cards carry icons rather than thumbnails. Those
are `STUDIO-35022`, `35033`, `35051`, `35052`, `35053` and `35041`, and a reader comparing these
captures against the bar should reach the same list.

---

## The migration review (history)

The rest of this document is the `STUDIO-07023` review, unchanged.

`docs/reference/` holds four captures: the Dear ImGui prototype and the native Studio shell, each at
1280x720 and 1920x1080, both showing `examples/HelloSprites` through a real CNA renderer. They are
the "before" of the migration and the "after" beside it, and this is the review.

## How they were taken

```
SDL_VIDEODRIVER=dummy cna-studio --project=examples/HelloSprites/HelloSprites.cnaproject \
    --frames=20 --window-size=1280x720 --screenshot=docs/reference/prototype-1280x720.png

SDL_VIDEODRIVER=dummy cna-studio --ui=studio --workspace=none \
    --project=examples/HelloSprites/HelloSprites.cnaproject \
    --frames=20 --window-size=1280x720 --screenshot=docs/reference/native-1280x720.png
```

Through the `SOFTWARE` renderer, which is a real CNA device and needs no display — the same
configuration CI runs (`STUDIO-33023`). `STUDIO-00013` recorded this as blocked on graphical CI; it
was not, and had not been since the CNA-backed job existed. `--window-size` is new: both UIs open a
real window through CNA and neither could be asked for one of a given size, which made "the same
screen at the same resolution on both" impossible to capture.

`--workspace=none` so the native capture shows the *default* arrangement rather than whatever the
machine taking it last saved.

## What the native shell does better

- **Text.** The prototype draws a fixed bitmap monospace at one size; the native shell draws a
  proportional, anti-aliased face at a real type scale. This is the single largest difference and it
  is visible before anything else: the prototype reads as a debug overlay and the native shell reads
  as an editor.
- **A menu bar with somewhere to put things.** Three menus become nine — File, Edit, View, Project,
  Build, Play, Tools, Window, Help — and every row comes from the action registry, so a command
  cannot be on a menu and do nothing.
- **A toolbar at all.** The prototype has none; its play and tool controls live *inside* the
  Viewport panel, which means they move and resize with it and vanish if it is closed. The native
  toolbar is icons at the top of the window: save, undo, redo, the three gizmos, the grid, play,
  pause, step, stop, build.
- **A status bar at all.** The prototype has none. The native one carries the project and scene, the
  unsaved mark, the target profile and what Studio itself is drawing on — four things a user
  otherwise has to go and look for.
- **Panels the prototype has not got**: Layers, Preferences, and the Material tab awaiting Phase 19.
- **A component summary per outliner row**, which tells a camera from a sprite without selecting it.
- **Quieter chrome.** The prototype boxes every control; the native shell uses space and a single
  accent, so the scene is the brightest thing on the screen rather than the tab strips.

## What the native shell still lacks

Three of them, all in the same place, and none of them found by the panel inventory — which is why
this review exists. `docs/MIGRATION-INVENTORY.md` accounts for *panels*, and the Inspector is ported;
what it cannot see is that the prototype's Inspector shows something else entirely when **nothing is
selected**, and the native Details panel shows only "Select an entity to see its details."

| What | Where in the prototype | Native | Status |
|------|------------------------|--------|--------|
| Scene Environment: ambient colour and fog | `InspectorPanel::drawSceneEnvironment` | `details`, idle | ✅ |
| Grid Snap, editable | `InspectorPanel`, idle | `details`, idle | ✅ |
| The project's layer list, with add | `InspectorPanel::drawLayers` | `details`, idle | ✅ |

The native Layers panel is not an answer to the third: it lists what is *on* each layer, which is a
different question from what the layers are called.

**Closed by the review that found them.** The Details panel standing idle now shows the project, an
editable grid snap, the scene environment and the project's layer names with add, rename and remove
— each through the command history, so Ctrl+Z reaches them like any other edit. The fog's colour and
range appear only when fog is on, on the same rule as the prototype's grid-plane menu item: a
control that changes nothing visible is a bug report waiting to be filed. The reference captures
above predate the change and are left as they were; they are the "before" of the migration, and
re-taking them to hide what the review found would be the wrong kind of tidy.

Everything else the captures differ by is a deliberate difference: the prototype's Play button and backend chooser are on the viewport's
own toolbar, and natively they are on the application toolbar and in the Backends panel; and its
tilemap tool strip is a dropdown on that same toolbar, where natively the tools are commands on the
View menu and the tile index is an overlay in the viewport's own corner, beside the image it edits.
The same goes for its 2D/3D dropdown: natively those are two exclusive commands on `2` and `3`.

The captures are of the 2D view on both sides, which is what each opens on.

## Reviewed again, at the end of the service-extraction and benchmark session

Looked at rather than assumed, at 1920×1080 with the example project open and `Player` selected, and
separately at the surfaces this session changed. What follows is what the capture actually shows.

**Holding.** The visibility toggle `STUDIO-35060` added and `STUDIO-35063` guards **is still on
screen**: hovering a row draws the eye at its right-hand end, checked on a 700×220 capture of the
outliner alone rather than inferred from the test passing. That is worth re-checking by eye each
session precisely because the failure mode is a feature that works, passes its tests, and is
invisible.

**New this session and reviewed.** The Details panel's asset inspector (`STUDIO-07045`) reads as a
panel about a file: the name in the heading face beside its kind's icon, then path, type and id, then
the importer's own heading and its settings as real controls — two drop-downs, two checkboxes and a
read-only pixel size. Two things were wrong on first look and are fixed: a property the importer
declares read-only rendered as "(not editable yet)", which is the message for a kind with no editor
and reads as an unimplemented feature, where the honest answer is `32, 32`; and a `.cnamaterial` said
"No importer handles this file type", which is true, useless, and reads as a fault — it names
`STUDIO-07046` now.

**Still below the bar, and visible in the capture.**

| What | Task |
|------|------|
| The Content Browser is a single folder card in a very large empty area at 1920 wide; no thumbnails | `STUDIO-35041` |
| The property grid's label column is a fixed 38%, so at panel width the labels and their values are separated by a gap wider than either | `STUDIO-35033` |
| The viewport grid is a flat lattice rather than a ground plane, and there is no orientation widget | `STUDIO-35051`, `STUDIO-35052` |
| Nothing marks the selected entity in the viewport | `STUDIO-35053` |
| The outliner cannot be searched or filtered | `STUDIO-35061` |

None of these is new and all of them are `Phase 35` rows already. They are repeated here because a
review that only lists what improved is a review that stops being read.

**What this session did not change.** No new reference capture was added to `docs/reference/`. The
shell's visual language is where `STUDIO-35060` left it; this session's work was architecture,
measurement and one panel section, and adding a third pair of "current state" captures that differ
from the last by one panel would make the directory harder to read rather than more honest.

## The verdict

The native shell is ahead of the prototype on everything a user sees first, and was behind it on one
screen's worth of scene-level settings — which this review found and which are now answered.

What `STUDIO-07030` — deleting the Dear ImGui panels — is still waiting for is the inventory's *Not
yet answered* table, repeated here so the two can be compared by a test rather than by eye.

**The list grew, then began to shrink.** It was one row — material editing — and `STUDIO-07041`
found five, because this review and that inventory were both looking at the level of *panels* and
the gap was a level below: the controls inside one. The prototype's Inspector draws eight sections
and the native Details panel had three. `docs/MIGRATION-INVENTORY.md` has the correction and what
changed so it cannot recur. All five have since been answered — the asset inspector
(`STUDIO-07045`), the audio preview (`STUDIO-07044`), the sprite animation preview
(`STUDIO-07043`), the material editor (`STUDIO-07046`) and the prefab section (`STUDIO-07042`) —
and the list is empty:

- *(nothing)*

**This review is a judgement and says so.** No test can assert that one editor looks better than
another. What the suite does check is that the four captures exist, that they are the sizes this
document claims, that everything the review calls unanswered is also unanswered in the inventory, and
that the list above of what `STUDIO-07030` waits for is exactly the inventory's *Not yet answered*
table — so the two cannot come to disagree about what is missing. That last check is new: the review
went on naming the tilemap tool as missing for a commit after it was answered, because nothing was
comparing the two lists, only three phrases inside them.
