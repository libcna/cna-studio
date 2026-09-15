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

## The verdict

The native shell is ahead of the prototype on everything a user sees first, and was behind it on one
screen's worth of scene-level settings — which this review found and which are now answered.

What `STUDIO-07030` — deleting the Dear ImGui panels — is still waiting for is the inventory's *Not
yet answered* table, repeated here so the two can be compared by a test rather than by eye. The 3D
view left that table when the native viewport gained one, which is why the list is now one row:

- Material editing

**This review is a judgement and says so.** No test can assert that one editor looks better than
another. What the suite does check is that the four captures exist, that they are the sizes this
document claims, that everything the review calls unanswered is also unanswered in the inventory, and
that the list above of what `STUDIO-07030` waits for is exactly the inventory's *Not yet answered*
table — so the two cannot come to disagree about what is missing. That last check is new: the review
went on naming the tilemap tool as missing for a commit after it was answered, because nothing was
comparing the two lists, only three phrases inside them.
