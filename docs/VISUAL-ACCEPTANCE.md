# Visual acceptance — the two UIs, same project, same size

`plan.md` STUDIO-00013, STUDIO-07023.

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
| Scene Environment: ambient colour and fog | `InspectorPanel::drawSceneEnvironment` | — | ⬜ |
| Grid Snap, editable | `InspectorPanel`, idle | — | ⬜ |
| The project's layer list, with add | `InspectorPanel::drawLayers` | — | ⬜ |

The native Layers panel is not an answer to the third: it lists what is *on* each layer, which is a
different question from what the layers are called.

Everything else the captures differ by is already recorded in the inventory as unanswered — the
tilemap tool strip, the tile index, and 2D/3D — or is a deliberate difference: the prototype's
Play button and backend chooser are on the viewport's own toolbar, and natively they are on the
application toolbar and in the Backends panel.

## The verdict

The native shell is ahead of the prototype on everything a user sees first and behind it on one
screen's worth of scene-level settings. That is the right shape for the migration to be in, and the
three rows above are what `STUDIO-07030` — deleting the Dear ImGui panels — is waiting for, together
with the 3D view and tilemap painting the inventory already names.

**This review is a judgement and says so.** No test can assert that one editor looks better than
another. What the suite does check is that the four captures exist, that they are the sizes this
document claims, and that everything the review calls unanswered is also unanswered in the
inventory — so the two cannot come to disagree about what is missing.
