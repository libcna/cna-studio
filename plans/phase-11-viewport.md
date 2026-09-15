# Phase 11 — 3D viewport 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-11001` … `STUDIO-11999` and are never reused.

**Purpose.** A professional 3D authoring viewport: navigation, visualisation modes and correctness.

**Exit criteria.** A user can navigate a real scene comfortably and see what they are authoring, without regressing the existing 2D workflow.

**Progress:** 4 of 15 complete `███░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-11001` | Perspective and orthographic cameras | ✅ | `STUDIO-07009` |
| `STUDIO-11002` | Orbit, fly and pan navigation with configurable speed | ✅ | `STUDIO-11001` |
| `STUDIO-11003` | Focus selection | ⬜ | `STUDIO-11001` |
| `STUDIO-11004` | Standard views: front, back, left, right, top, bottom | ⬜ | `STUDIO-11001` |
| `STUDIO-11005` | Adaptive grid | ⬜ | `STUDIO-11001` |
| `STUDIO-11006` | Object picking through the 3D projection | ✅ | `STUDIO-11001` |
| `STUDIO-11007` | Selection outlines | ⬜ | `STUDIO-11006` |
| `STUDIO-11008` | Bounds and collision debug visualisation | ⬜ | `STUDIO-11006` |
| `STUDIO-11009` | Icons and billboards for entities with no geometry | ⬜ | `STUDIO-11006` |
| `STUDIO-11010` | Wireframe mode | ⬜ | `STUDIO-11001` |
| `STUDIO-11011` | Lighting modes, unlit mode, normal and material debug views | ⬜ | `STUDIO-19001` |
| `STUDIO-11012` | Camera preview and game view | ⬜ | `STUDIO-11001` |
| `STUDIO-11013` | Preserve the existing 2D viewport workflow without regression | ✅ | `STUDIO-07009` |
| `STUDIO-11014` | A new CNA-native project opens directly into a 3D world viewport | ⬜ | `STUDIO-11001`, `STUDIO-08006` |
| `STUDIO-11015` | Maya and Blender navigation schemes, not only Studio's own | ⬜ | `STUDIO-11002` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-11013` — Preserve the existing 2D viewport workflow without regression

**Acceptance.** Sprites, tilemaps, layer depth ordering and 2D manipulators all still work and are still tested

### `STUDIO-11001` — Perspective and orthographic cameras

**Done, and mostly already there.** `StudioCamera3D` has held both projections, the pivot/distance/
yaw/pitch model and `screenToRay` since the prototype, CNA-free and tested. What this task actually
needed was a *native viewport* that used it: `studioViewportPanel3D`, a `StudioViewportView` on the
viewport's state, and two exclusive checkable commands on `2` and `3` — the keys the prototype binds.

**Two cameras, not one that switches projection.** The 2D view's pan and zoom and the 3D view's
orbit and distance are different state, and a user who switches to 3D, looks around and switches
back expects to find the 2D view where they left it.

**The first switch frames the scene and no later one does.** The default camera looks straight down
an axis, so an unframed 3D view opens on a grid with the level off the edge of it. Framing on every
switch would be worse than not framing at all: a user who set up a view, glanced at 2D and came back
would find their angle thrown away.

### `STUDIO-11002` — Orbit, fly and pan navigation with configurable speed

**Done.** Left-drag orbits, Shift or middle pans, right-drag turns in place, and W/A/S/D/Q/E fly
while the right button is held — the modifier being what keeps those keys from meaning two things
at once, since they are the gizmo shortcuts everywhere else. The wheel dollies geometrically, so one
notch feels the same close up and far away; a linear step is unusable at both ends of the range.

**The turn rate is radians per pixel**, not per fraction of the panel. A rate derived from the width
would turn faster in a narrow viewport than a wide one, which is the kind of thing nobody reports
and everybody notices.

**Fly speed is proportional to the orbit distance**, so one press crosses the same fraction of what
is on screen whether the camera is inside a room or above a level.

**"Configurable" was the part that was missing, and not only here.** `cameraSpeed` and `invertZoom`
were stored, loaded, given a row in the Preferences panel — and applied by nothing at all, in either
viewport. They reach the camera now, read every frame rather than on change, for the reason the
autosave interval is: preferences also arrive by being *assigned* when the host loads them from
disk, and a setting that only takes effect down one of those two paths is one that works when you
change it and not when you restart.

### `STUDIO-11006` — Object picking through the 3D projection

**Done.** `pickEntityAt3D` casts the ray `screenToRay` builds and returns the entity whose box is
nearest the eye — nearest rather than topmost, because depth is a real quantity here and layer order
is not. No render target, no read-back, and it works headless.

**A press in 3D is a camera gesture first.** Every button navigates, so a release cannot simply mean
"clicked": a release after an orbit that selected whatever the camera happened to stop over is the
thing that makes a 3D viewport feel like it is fighting the user. A gesture that moved is a
navigation and never selects; one that did not is a click, and then the same two selection rules the
2D view has apply — Ctrl adds and removes, and Ctrl on empty space leaves a half-assembled selection
alone.

**An entity with no geometry is still pickable**, because a light or a camera has to be clickable.
That is deliberate, and it is why "click the corner of the viewport" is not a reliable way to test a
miss — which is the first thing the test for this caught, about itself.

### `STUDIO-11013` — Preserve the existing 2D viewport workflow without regression

**Done by construction.** The two views are separate functions over separate cameras, branching once
at the top of the panel rather than threading a mode through everything below it. The 2D view's
tests — navigation, picking, all three manipulators, group drags, the tilemap tools — are unchanged
and still pass, which is the only form this acceptance can take.

### `STUDIO-11015` — Maya and Blender navigation schemes, not only Studio's own

**Acceptance.** `StudioNavigationStyle` selects between Studio's scheme, Maya's (Alt with left,
middle and right) and Blender's (middle orbits, Shift-middle pans), in both viewports.

**Found by finishing `STUDIO-11002`.** The preference exists, is stored, is loaded and has a row in
the Preferences panel; nothing reads it. Speed and inverted zoom were a multiplier and a sign and
are answered; three schemes across two viewports is a piece of work, and pretending otherwise would
have made `STUDIO-11002` a task that closed over a control that still does nothing.
