# Phase 11 — 3D viewport 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-11001` … `STUDIO-11999` and are never reused.

**Purpose.** A professional 3D authoring viewport: navigation, visualisation modes and correctness.

**Exit criteria.** A user can navigate a real scene comfortably and see what they are authoring, without regressing the existing 2D workflow.

**Progress:** 13 of 15 complete `██████████░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-11001` | Perspective and orthographic cameras | ✅ | `STUDIO-07009` |
| `STUDIO-11002` | Orbit, fly and pan navigation with configurable speed | ✅ | `STUDIO-11001` |
| `STUDIO-11003` | Focus selection | ✅ | `STUDIO-11001` |
| `STUDIO-11004` | Standard views: front, back, left, right, top, bottom | ✅ | `STUDIO-11001` |
| `STUDIO-11005` | Adaptive grid | ✅ | `STUDIO-11001` |
| `STUDIO-11006` | Object picking through the 3D projection | ✅ | `STUDIO-11001` |
| `STUDIO-11007` | Selection outlines | ✅ | `STUDIO-11006` |
| `STUDIO-11008` | Bounds and collision debug visualisation | ✅ | `STUDIO-11006` |
| `STUDIO-11009` | Icons and billboards for entities with no geometry | ✅ | `STUDIO-11006` |
| `STUDIO-11010` | Wireframe mode | ✅ | `STUDIO-11001` |
| `STUDIO-11011` | Lighting modes, unlit mode, normal and material debug views | ⬜ | `STUDIO-19001` |
| `STUDIO-11012` | Camera preview and game view | ⬜ | `STUDIO-11001` |
| `STUDIO-11013` | Preserve the existing 2D viewport workflow without regression | ✅ | `STUDIO-07009` |
| `STUDIO-11014` | A new CNA-native project opens directly into a 3D world viewport | ✅ | `STUDIO-11001`, `STUDIO-08006` |
| `STUDIO-11015` | Maya and Blender navigation schemes, not only Studio's own | ✅ | `STUDIO-11002` |

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

### `STUDIO-11003` — Focus selection

**Acceptance.** F frames the selection in whichever view is showing.

**The command already existed and moved the wrong camera.** `studio.view.focusSelected` has been on
the View menu and bound to F since the shell existed, and its handler called
`studioFrameSelection`, which takes a `StudioCamera2D&`. In the 3D view that rearranged a camera
nobody was looking through, so the key appeared to do nothing at all — the worst kind of broken,
because there is nothing to see and nothing to report.

**`studioFrameSelection3D` is the counterpart, and deliberately a second function rather than a
template.** The two share a shape and nothing else: one combines `WorldBounds2D` and the other
`WorldBounds3D`, and the 2D one flattens a transform to a point in the XY plane while the 3D one
keeps all three components. A single function over both would be one where every reader has to work
out which half they are in — the same reasoning that keeps `studioViewportPanel` and
`studioViewportPanel3D` apart.

**Framing moves the camera and leaves its angle alone.** `StudioCamera3D::frame` already worked that
way; what matters is that it is the right meaning. A focus key that also levelled the view would
throw away the shot the user was composing, and there is a separate command for choosing an angle —
`STUDIO-11004`'s six.

**An entity with nothing to draw is framed by its position**, as in 2D and for the same reason: a
camera or an empty grouping node has a place in the world, and a key that appears not to work is
worse than one that works modestly.

**Verification.** `tests/StudioViewportPanelTests.cpp`: the pivot landing on the sprite's extent
rather than its transform origin, the distance coming down, the yaw and pitch untouched, a point
entity framed by its position without collapsing the distance to zero, and nothing selected leaving
the camera alone. `TheFocusKeyMovesTheCameraOfTheViewThatIsShowing` is the defect itself, end to end
through the real shell and the real keystroke: it moves the 2D camera somewhere absurd before
pressing F in the 3D view, so a focus that moved the wrong one shows up as the 2D camera jumping
back rather than as nothing happening. Checked by causing it — restoring the old handler fails that
case by name.

### `STUDIO-11004` — Standard views: front, back, left, right, top, bottom

**Acceptance.** Six commands point the camera along an axis, and a top view is a top view.

**The camera could not represent a top view, and the reason was a magic constant.** Pitch was clamped
to 89 degrees "so the view direction never becomes `up`", because `getRight` computed
`normalize(cross(forward, Y))` — the zero vector when `forward` *is* Y — and `getViewMatrix` passed
`(0, 1, 0)` to a look-at that degenerates when the view direction is parallel to it. The clamp was
holding two functions away from a singularity rather than the functions being total.

**Both are now written so the pole is an ordinary point.** The cross product works out to
`(cos yaw · cos pitch, 0, −sin yaw · cos pitch)`, so normalising it gives `(cos yaw, 0, −sin yaw)`
for any pitch whose cosine is positive — the pitch cancels. Computing that directly is not an
approximation of the old expression; it is the old expression with a removable singularity removed,
identical everywhere the old one was defined and defined where it was not. The view matrix takes the
camera's own up vector, which is `(0, 1, 0)` already orthogonalised, so the matrix is unchanged
wherever it worked. With those two total, `kMaxPitchRadians` becomes a right angle exactly: orbiting
stops when you are looking straight down, which is where a user expects it to stop.

**Named for where the camera is, not where it looks.** Front puts the camera in front and looks
backwards along −Z, which is how every editor names these and the opposite of how the view direction
reads. The tests assert on the *eye*, because the yaw is the implementation and where the camera
ends up is the promise.

**Orientation only, and the projection is left alone.** The pivot and the distance are what the user
framed; a standard view is a question about angle, so changing them would make each of these six a
navigation as well as a rotation — and they compose with Focus Selected precisely because they do
not. Some editors also switch to orthographic here, on the grounds that an axis-aligned view is
usually wanted for measuring. This does not: the projection toggle exists separately, and a command
that silently did two things is one a user cannot undo half of.

**Top and Bottom zero the yaw rather than keeping it.** The other four fix the yaw anyway. Leaving it
alone at the poles would make Top mean six different framings depending on where the user happened
to be orbiting, and the point of a standard view is that pressing it twice from different places
gives the same picture.

**Unbound by default, which is a decision rather than an omission.** The keys a user's hands already
know for these are the numpad's 1, 3 and 7; this build's key vocabulary does not carry the numpad,
and the plain digits beside them are already the 2D and 3D toggles. Inventing a third scheme nobody
knows would be worse than a menu entry somebody can bind for themselves, which the shortcut editor
(`StudioShortcutEditor.cpp`) lets them do. They are disabled in the 2D view rather than hidden, like
the ground-plane toggle: a user who went looking should find them and see why they are greyed out.

**Verification.** `tests/SceneTests.cpp` walks all six and asserts the eye's offset from the pivot,
that the camera is actually looking *at* the pivot from there — an eye in the right place looking
the wrong way is a view of nothing — and that the pivot and distance survive. A separate case pins
that Top from two different orbits gives one orientation.
`LookingStraightDownIsAnOrdinaryPointRatherThanASingularity` asserts the basis at the pole is finite,
unit-length and orthogonal, that a projection through the view matrix lands on the pivot at screen
centre, and that the right vector agrees with the limit approached from just below — which is what
makes the pole continuous rather than merely defined.
`tests/StudioViewportPanelTests.cpp` covers the commands through the registry, including their being
disabled in the 2D view. Checked by causing it: reverting `getRight` to the singular form and
swapping the Left and Right yaws each fail by name.

### `STUDIO-11005` — Adaptive grid

**Acceptance.** The grid stays useful at any distance and does not draw an edge across the scene.

**Half of this was already done, and saying which half is the honest part.** `buildSceneGrid` has
chosen its *spacing* from the camera since it was written, through the same `chooseGridSpacing` the
2D grid uses — one answer to "how far apart are the lines", so a 2D and a 3D view of one scene never
disagree about what a grid square means. It also already marks the world axes and every tenth line,
which is what gives a 3D view its one landmark.

**What did not adapt was where the grid *stops*.** Forty-nine lines each way at full strength and
then nothing: a bright square edge across the middle of a scene, and — in any view that is not
straight down — a solid aliased band at the far side where the lines converge into fewer pixels than
they need. The spacing was adaptive and the presence was not.

**The grid now fades radially to nothing at its rim**, which turns the square into a disc that
dissolves. Radial from the grid's centre rather than measured from the eye, and that is the
non-obvious choice: a fade that depended on where the camera was would shimmer as the user orbited,
and the far edge of the grid *is* the horizon in a grazing view — so the simpler, view-independent
rule covers the case the harder one was for.

**A line has to be cut up to fade along its length**, because a `WireSegment` carries one colour.
`gridFadeSteps` is that subdivision, defaulting to six, and it is a straight multiplier on the
segment count — which is why it is an option rather than a constant, and why a test counts what the
grid costs rather than trusting it. With the fade on, the corners of the square fall outside the
disc and are dropped, so the count lands *below* the full multiple.

**Nothing is emitted at zero strength.** A fully transparent segment is work the renderer does to
draw nothing, and the grid is the one thing in a 3D frame there are hundreds of.

**`gridFadeStart = 0` gives the old grid back exactly**, one strength everywhere. That is what makes
this an option rather than a new opinion baked in, and the test asserts it rather than assuming it.

**Verification.** `tests/SceneTests.cpp`: the grey lines spanning full strength near the centre to
nearly gone further out, nothing emitted at zero, and the fade switching off cleanly. The assertion
that matters is the one about the **X axis line**, and it is there because the first version of this
test passed against a wrong implementation: a fade measured from each *line* to the centre, rather
than from each *piece* of it, leaves the axis — which passes through the centre — at full strength
end to end, and every other assertion still held. Checked by causing exactly that: the corrected
case fails by name, the earlier one did not.

### `STUDIO-11007` — Selection outlines

**Acceptance.** A selected model reads as an outline, at any triangle count.

**What it replaces was not an outline.** A selected entity had every one of its edges recoloured and
thickened. On a crate that looks like a selection; on anything denser it looks like the object has
turned into a solid block of the selection colour, because at a few hundred triangles the edges
cover the silhouette they were meant to trace. The denser the mesh, the worse the mark.

**An outline is the silhouette, and the silhouette is a function of where you are looking from.** An
edge is on it when the two triangles sharing it face opposite ways — one towards the camera, one
away — or when only one triangle claims it at all, which is the rim of an open shell. Everything
else is interior detail an outline is not about. That is why the test that matters compares a cube
square on to a face against the same cube from a corner: four edges against six. A fixed set of
edges marked once would give the same answer to both, and the case says so in those words.

**Orthographic and perspective see different silhouettes**, and using the eye for both would be
wrong on the projection somebody lining geometry up is most likely to be in. Under orthographic
every triangle is seen along one direction; under perspective each is seen from a point. Both are
handled, because the alternative is an outline that drifts off the shape in exactly the view where
precision is the reason you chose it.

**The outline is what makes the shaded mode usable.** `STUDIO-11010` gave the viewport a mode that
draws no mesh edges at all, and in that mode a selected model would otherwise be marked by nothing
but its bounds box. So the outline is drawn even when `drawMeshEdges` is off — it is the selection
marker, not a variety of wireframe.

**Selecting replaces the bounds box rather than adding to it.** A box and an outline together would
be two marks for one selection, and the outline traces the object where the box only says roughly
where it is. The test pins that the segment count goes *down* on selection, which is the surprising
direction and therefore the one worth asserting.

**Fewer segments, not more.** An outline is a subset of the edges and it grows with the shape rather
than with the triangle count — so the marking of a selected model now costs less on exactly the
dense meshes where the old behaviour cost most and read worst.

**Verification.** `tests/SceneTests.cpp` builds a real twelve-triangle cube wound the way the
importer guarantees, and asserts: nothing wears the selection colour until something is selected;
the outline is between four and eleven of the cube's twelve edges; selection replaces the box; the
outline changes when the camera moves to a corner; and turning `drawSelectionOutline` off gives the
box back. Checked by causing it — emitting every edge instead of the silhouette fails four
assertions across both cases, including the one that names the mistake.

### `STUDIO-11010` — Wireframe mode

**Acceptance.** The 3D view can be shaded, wireframe, or both, and the user chooses.

**The interesting part is what this task found rather than what it added.** The 3D view built three
batches every frame — solid meshes, sprite quads and the wireframe — and the wireframe drew *every
imported model's own edges* unconditionally. So the viewport was permanently in "shaded wireframe":
any scene with real geometry in it was hatched over, and there was no way to get a clean shaded
picture to judge lighting or materials by. The missing mode was not Wireframe, it was **Shaded**.

**Three modes, because the third is not a toggle of either other.** Shaded draws the solid geometry
with no edges; Wireframe draws the edges and no solid geometry; Shaded Wireframe is what the
viewport did before there was a choice, kept so the old picture is still reachable rather than
replaced. Shaded is the default, which is a change to what a user sees and the right one.

**Wireframe drops the sprites as well as the solid meshes**, and that is the half worth stating. A
sprite has no edges of its own beyond the quad its bounds box already draws, so leaving them
textured would produce a "wireframe" that is half wireframe and half picture.

**The decision is a CNA-free function and the device call is wiring.** `studioShadingPlan` turns a
mode into three booleans; the host that owns the graphics device turns those into calls and does
nothing else. That is what makes the choice testable in a headless build — the only half a headless
build can reach — and it is the same division the rest of the 3D view already uses, where every
batch is built by a tested function and handed over to be uploaded.

**`WireframeOptions::drawMeshEdges` defaults to true**, so every existing caller and every test that
pins the edge drawing keeps meaning what it meant. The viewport passes false in its shaded mode,
which is where the new opinion belongs — a library default changed underneath its callers would
have made this a behaviour change disguised as an option.

**Verification.** `tests/StudioViewportPanelTests.cpp` pins each mode's plan, including Wireframe
dropping the sprites, and drives the commands through the registry: exclusive, checked so a toolbar
can show which picture is on screen, and disabled in the 2D view where there are no meshes and no
choice to make. `tests/ModelImportTests.cpp` pins that `drawMeshEdges = false` really does fall back
to the box — the same segment count as an entity with no mesh at all. Checked by causing both: a
Wireframe plan that keeps textured sprites, and a wireframe that ignores `drawMeshEdges`, each fail
by name.

### `STUDIO-11008` — Bounds and collision debug visualisation

**Acceptance.** A user can see the volumes the editor measures with and a CNA game collides with,
and those volumes are the truth about the object.

**Two real defects, found by asking what the overlay would be drawing.**

**One: an imported model was measured as an eight-unit box at its origin.**
`computeEntityBounds3D` had no way to ask for a model's geometry, so a `ModelRenderer` fell through
to the box every icon-drawn entity gets. The model was *drawn* at whatever size its mesh is. So a
click landed on a large model only near its middle, Focus Selected framed eight units and put the
camera inside the thing it had been asked to look at, and the first switch into 3D framed a scene of
models as a cluster of specks. The bounds now come from the mesh, and the picker, both framing paths
and the wireframe all pass a `MeshProvider`.

**Two: the shell built the 3D wireframe with no mesh provider at all.** So every option that needs
geometry silently did nothing in the real editor — `STUDIO-11010`'s Wireframe mode drew no model
edges, and `STUDIO-11007`'s selection outline never appeared. Both were implemented, both were
tested, and neither reached the screen, because the one line handing the meshes over was never
written. That is a defect a test could not have caught where the options were being assembled, which
is why they are not assembled there any more: `studioViewportWireframeOptions` is a CNA-free
function, the host calls it, and a case asserts the provider is carried.

**A rotated box is not a box.** The mesh's extent is in model space, so placing it means
transforming all eight corners and re-bounding. Transforming `min` and `max` alone is the fast wrong
answer — under any rotation those two corners no longer span the shape — and it is invisible until
something is rotated, which is why the case rotates an eighth of a turn and checks every vertex
lands inside.

**The overlay is the box, drawn over whatever the entity is otherwise drawn as.** `None`,
`Selected`, `All`; off by default, because it answers a question — why did my click miss, why did
Focus fly me in there — and an answer permanently on screen is noise. An entity already drawn as
exactly that box gets no second copy of it in a second colour, which is the part of the rule worth a
test rather than a comment.

**The collision half is the bounding sphere, and that is not a workaround.** CNA's collision, like
XNA's, *is* `BoundingBox`, `BoundingSphere` and the intersection tests on them; there is no collider
component, and `plans/phase-26-physics-nav.md` already says Studio integrates with a physics system
rather than implementing one. Inventing a `CNA.BoxCollider` built-in here would be Studio deciding
the runtime's physics schema — exactly what that phase exists to prevent. So what an editor can
honestly show is the volume a CNA game actually tests, and the sphere is the half a user cannot
guess from the box: `BoundingSphere::CreateFromBoundingBox` centres on the box and reaches its
furthest corner, so around anything long and thin it swallows the empty space beside the object. The
plank in the test is twenty times taller as a sphere than as a box. `STUDIO-26001` builds on this.

**Three rings, where the light gizmo draws one and says why.** A light's ring is a boundary the user
aims, and two of three collapsing edge-on would leave two lines through the middle of its badge. A
sphere is a volume being inspected, and its collapsed rings are its silhouette from that angle —
which is the truth about a sphere and reads as one.

**Three copies of `toWorldMatrix` became one.** The wireframe, the model batch and now the bounds
all compose scale-rotate-translate, and three private copies of an order-dependent product is three
chances for one to be written in a different order. The same for `findEntityMesh`, which three
modules were asking; `buildSceneModelBatch` keeps its own two-step form because it alone has to tell
"no `ModelRenderer`" from "a `ModelRenderer` whose mesh has not landed", and that is the distinction
the shared function collapses.

**Verification.** `tests/SceneTests.cpp`: a model measures as its mesh and not as the fallback box,
and a ray at its edge hits the first and misses the second; a rotated model's box contains every one
of its vertices and grows on the two axes it should and not on the third; the overlay is off by
default, adds twelve edges to a model and none to a sprite, and adds rather than replaces; the
sphere is three rings of twenty-four and is many times the box's extent on a plank.
`tests/StudioViewportPanelTests.cpp`: the options carry the mesh provider — the assertion the
second defect was invisible without — and the commands are exclusive, checkable, 3D-only, with the
sphere toggle refusing while nothing is overlaid. Checked by causing all five: two-corner bounds, a
model whose mesh is ignored, options with the provider dropped, a second box on the path that is
already the bounds, and a sphere sized from the smallest half-extent. Each fails by name.

### `STUDIO-11009` — Icons and billboards for entities with no geometry

**Acceptance.** An entity that draws nothing reads as a place rather than as a small object.

**The gap was the plainest case, not the exotic one.** Cameras, lights, audio sources and model
references already got badges. What had no answer was an entity with a transform and *nothing else*
— a spawn point, a trigger, an empty grouping node. It classified as `None`, so the only thing that
showed it was the small bounds box every entity gets, and a bounds box around nothing is a tiny
wireframe cube. A level with ten spawn points in it read as ten identical cubes, none of which was
an object and none of which could be told from another. The comment in the code already said what
the intent was — "an entity that draws nothing gets a badge rather than a box" — and the case where
an entity draws *nothing at all* was the one it did not cover.

**A marker is a place, so it is drawn as a cross and not as an outline.** Every other badge is a
closed shape, because every other badge stands for a thing: a camera body, a lamp, a cube. A shape
with an interior says something occupies that space, which is the opposite of what a spawn point
means. The diagonals are not decoration either — the grid's lines are axis-aligned, and an upright
cross sitting on a grid line disappears into it.

**Sprites, animated sprites and tilemaps keep their `None`.** They are geometry: the viewport
already draws them and already boxes them, and a marker on top would be a second mark on one object
rather than a way to find an invisible one. The new answer is the *fallback*, reached only after
every kind above it has declined, which is also why the enumerator is added last — the order of
`StudioIconKind` is the match order.

**It is the same answer in both viewports.** `getStudioIconKind` feeds the 2D icon pass and the 3D
badge alike, so a marker is drawn, hit-tested and clickable in 2D for the same reason it is visible
in 3D. That is the whole of the change on the 2D side; nothing there needed a new path.

**Verification.** `tests/ViewportTests.cpp` asserts the classification directly — transform-only is
`Empty`, all three drawing components stay `None`, a camera is still a `Camera` so the fallback has
not swallowed the kinds above it, and `collectStudioIcons` returns exactly the marker and the camera
out of five entities. `tests/SceneTests.cpp` asserts the drawing: both badges present, of different
sizes so one cannot be mistaken for the other, and the whole frame costing exactly the two badges
and no box. Checked by causing both halves: returning `None` again fails four assertions across two
cases by name, and a marker that draws no lines fails three more.

**One case had to be repointed rather than relaxed.** `TheWireframeBoxesEveryEntityAndMarksTheSelection`
pinned transform-only entities being boxed — which is the behaviour this task deliberately changes.
Its entities now carry sprites, via a `makeSpriteEntity` helper, so a case about *boxes* is built
out of entities that have something to box. Every one of its assertions is unchanged.

**A plan defect found and fixed in passing.** `plans/phase-11-viewport.md` carried a byte-identical
second copy of the `STUDIO-11003`, `11004`, `11005`, `11007` and `11010` entries — 212 lines of it,
accumulated one entry at a time across the four commits that added them. Removed. The file's own
convention of an acceptance stub *and* a write-up for the same task (`STUDIO-11013`, `STUDIO-11015`)
is left alone: those two differ, and duplicating a full entry is not that.

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

### `STUDIO-11015` — Maya and Blender navigation schemes

**Acceptance.** Setting the navigation preference changes what a drag does in both viewports, and
the three schemes genuinely differ.

**The defect.** Three schemes were stored, loaded, given a row in the Preferences panel — and read
by nothing. The viewport's gestures were hard-coded to Studio's own, so a user who chose Maya got
Studio's bindings and no indication that anything had failed. A preference that changes nothing is
worse than no preference: a user who sets it and finds the viewport unchanged concludes the editor
is broken, which is a fair reading.

**A pure function**, and that is most of the value. `studioViewportGestureFor` takes six booleans
and an enumeration and returns an enumeration. It is the whole of what the three schemes disagree
about, so every combination that matters is a test rather than something performed with a mouse. The
viewport keeps the arithmetic; this keeps the vocabulary.

**The schemes are what their tools do, not an interpretation.** Maya puts every camera gesture
behind Alt so an unmodified drag is *always* a selection — a scheme that let one unmodified button
navigate is the thing a Maya user finds by moving the camera when they meant to pick something.
Blender puts them on the middle button with Shift and Control, leaving left free for the same
reason, and Control wins over Shift because Shift+Control+middle is a zoom there. A user who asks
for Maya and gets nearly-Maya is worse served than one who was told the scheme is not implemented.

**The gesture is resolved once, at the press, and kept for the drag.** Asked every frame, a user who
released Shift halfway through a pan would find the camera orbiting from wherever the pan had got
to. The gesture a drag *started* as is the one the user is still making.

**Two details that only appear when you use it.** Flying on W/A/S/D stays on Studio's scheme alone:
Maya puts a dolly on Alt with the right button and Blender puts nothing there, so binding the keys
to a right-drag under either would be this editor's habit leaking into somebody else's vocabulary.
And the 2D viewport follows the scheme too, resolving an Orbit to a pan — a 2D view has no orbit,
and a user who set the scheme for the 3D view and found the 2D one unchanged would have half a
preference, which is the defect this task exists to close.

**Verification.** `StudiosOwnSchemeIsUnchanged`, `MayaPutsEveryCameraGestureBehindAltAndNothingElse`,
`BlenderPutsEveryCameraGestureOnTheMiddleButton`, and — the one that makes the others worth having —
`TheSchemesDisagreeAboutSomethingOrTheyWouldNotBeThreeSchemes`. Three enumerators that resolved to
one mapping would pass every other case and would be this same defect one level further in.

### `STUDIO-11014` — A new CNA-native project opens directly into a 3D world viewport

**Acceptance.** A project created from Empty 3D opens showing its 3D world, without the user
pressing anything.

**Done, and the mechanism is a property of the project rather than a preference.** A `.cnaproject`
carries `defaultView`, set from the template that created it, and a project that names one opens in
it. That is a fact about what kind of game it is: a 3D world opened in the 2D view is a grid with
the level somewhere off the edge of it, and telling every new user to press 3 is a first five
minutes nobody should have.

**Not a lock**, deliberately. It decides the view a project *opens* in and nothing else; switching
is still `2` and `3`, and the value is never written back from the viewport — so a project's answer
does not drift because somebody glanced at the other view.

**Applied by invoking `studio.view.3d`, not by assigning the viewport's state**, and that is the
whole reason it is four lines rather than one. The command also frames the camera on the scene,
ends any gesture in flight and says in the log which view is on. Assigning the state would have
opened a 3D view looking straight down an axis at nothing — which is exactly the picture this task
exists to prevent.

**Both routes, not one.** A project opened from the Hub and one opened with `--project` make the
same switch, because a project's answer must not depend on which of the two opened it.

**The key is additive** and written only when set, like `language` and `gridSnap`: an existing
project does not gain a diff the first time Studio touches it, and an absent key means Studio's
default.
