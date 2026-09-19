# Phase 12 — Selection and gizmos 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-12001` … `STUDIO-12999` and are never reused.

**Purpose.** Production-quality transform manipulation.

**Exit criteria.** Transforming objects feels precise and predictable, and every drag is exactly one undo entry.

**Progress:** 1 of 11 complete `█░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-12001` | Translate gizmo | ⬜ | `STUDIO-11006` |
| `STUDIO-12002` | Rotate gizmo | ⬜ | `STUDIO-11006` |
| `STUDIO-12003` | Scale gizmo | ⬜ | `STUDIO-11006` |
| `STUDIO-12004` | Local and world transform spaces | ⬜ | `STUDIO-12001` |
| `STUDIO-12005` | Multi-selection transforms about a shared pivot | ⬜ | `STUDIO-12001` |
| `STUDIO-12006` | Pivot editing | ⬜ | `STUDIO-12005` |
| `STUDIO-12007` | Snapping: grid, angle and scale increments | ⬜ | `STUDIO-12001` |
| `STUDIO-12008` | One undo entry per drag, returning exactly to the drag start | ⬜ | `STUDIO-12001` |
| `STUDIO-12009` | Box selection | ✅ | `STUDIO-11006` |
| `STUDIO-12010` | Duplicate, delete, parent and reparent from the viewport | ⬜ | `STUDIO-12005` |
| `STUDIO-12011` | Drag and drop placement from the Content Browser into the scene | ⬜ | `STUDIO-09008` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-12009` — Box selection

**Acceptance.** A drag over empty space sweeps a rubber band and selects what it covers, in both
viewports, without changing what a click does.

**Overlap, not enclosure**, and this is the decision the task turns on. Requiring an entity to be
wholly inside the band is the tidier rule and the wrong one: a level's backdrop is larger than the
viewport, so nothing could ever band it, and a user would learn that the rubber band works on small
things only. The cost is real and worth naming — that same backdrop is caught by every band drawn
over it — and it is the lesser of the two, because a selection with one thing too many in it can be
seen and corrected while one that silently cannot include an object cannot.

**The threshold is what makes this safe to add at all.** Both viewports already treat a press as a
selection, and a click on empty space is how a user deselects. Without a minimum travel every click
becomes a band a fraction of a pixel wide, and that deselecting click becomes a band that selects
whatever happens to sit under the pixel. Four pixels: below a hand's own tremor on a press, above
nothing. The case that proves it is the one that turns the manipulator off and Ctrl-clicks a
selected entity — a *click* toggles and removes it, a *band* adds as a union and would leave it
selected, so a press with no movement that became a band shows up immediately.

**Adding is a union and never a toggle**, which is the one place a band differs from a Ctrl-click.
A band sweeps an area, and an area that happens to cover something already selected should not
remove it: a user widening a band would watch entities drop out of the selection as the band grew
over them. An empty band with no modifier still replaces, so sweeping nothing clears — the same
thing a click on nothing does.

**An icon is boxed at the icon's own size.** An entity with no geometry has no world bounds, so a
band written only against bounds would sweep straight past every camera, light and marker in the
scene — the entities a user most often wants to gather up, and exactly the ones the click picker
already finds by their icons.

**A box in the world is not a box on the screen**, so the 3D form takes the screen extent of all
eight projected corners. Two of them is the fast wrong answer, invisible until something is viewed
off-axis; the case that catches it puts an isometric camera on a cube and bands a one-pixel strip
beyond where `min` and `max` project. A corner behind the eye is dropped rather than clamped, so an
entity the camera is standing inside is measured by the part of it the user can actually see.

**The band goes where a left drag already means "select".** Maya's scheme puts every gesture behind
Alt and Blender's puts them on the middle button, so under both an unmodified left drag is a
selection and the band needs nothing moved to make room. Studio's own scheme spends the plain left
drag on the orbit, so the band goes on **Ctrl+left** there — consistent rather than invented,
because Ctrl already means "add to what is selected" on a click, and Ctrl is no part of Studio's
navigation vocabulary, so nothing was taken away to make room.

**Drawn in the UI layer rather than by the renderer.** A rubber band is editor chrome over the
viewport image, like the toolbar, so it is a `fillRect` and a `strokeRect` on the frame's draw list
— which means where it goes is testable in a headless build, and no CNA-side pass was needed for it
at all. The band is held in *panel* pixels so a wheel notch mid-drag does not stretch it, and so a
docked panel that moves carries its band with it.

**Verification.** `tests/ViewportTests.cpp`: a band takes what it overlaps in either drag direction,
takes a sprite it merely clips, takes nothing over empty space, passes over a disabled entity, and
takes a camera by its icon while a band clear of that icon takes nothing.
`tests/SceneTests.cpp`: the 3D form sweeps what the camera can see, and catches a cube beyond where
two corners project. `tests/StudioViewportPanelTests.cpp`: a drag bands and a press that does not
move is still a click, including the Ctrl case that tells the two apart; and the rectangle is not
drawn until the press has become a band, comes out the right way round dragged backwards, and moves
with the panel. `tests/StudioViewport3DTests.cpp`: the band is the left drag under Maya's scheme and
Ctrl+left under Studio's, where the plain drag still orbits. Checked by causing four: enclosure
instead of overlap, two projected corners instead of eight, no threshold, and Ctrl+left orbiting in
Studio's scheme. Each fails by name.

### `STUDIO-12008` — One undo entry per drag, returning exactly to the drag start

**Acceptance.** Carried forward from the prototype and retested through the Studio UI

