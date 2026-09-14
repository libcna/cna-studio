# NEXT — continuity notes

> Short-term state for whoever picks this up next, human or otherwise. The long-lived plan is
> [`plan.md`](plan.md); the reasoning behind the architecture is [`ANALYSIS.md`](ANALYSIS.md).
> This file records what is true *right now*: what just landed, what is half-done, what is known
> broken, and where to start.

**Branch:** `claude/cna-editor-architecture-plan-l4jza7` — push only, no pull request (owner's call).

---

## Status at a glance

| | |
|---|---|
| Build (standalone, no CNA) | ✅ clean at `-Wall -Wextra -Wpedantic -Werror` |
| Build (`-DCNA_EDITOR_WITH_CNA=ON`) | ✅ clean |
| Unit tests | ✅ 442 / 442 (also under Clang Release) |
| CTest (standalone) | ✅ 12 / 12 |
| CTest (CNA config) | ✅ 17 / 17 |
| CI | ✅ Linux, GCC Debug + Clang Release, `-Werror` |
| **Phase 1** | ✅ **complete** — all 23 tasks |
| **Phase 2** | 🔄 11 of 12 done — **ED-311 closed** when ED-410 gave `NestedStructure` its first real consumer. Only ED-302 remains, blocked on CNA gap G-04 |
| **Phase 3** | ✅ **complete** — 13 of 13 — ED-400, ED-401, ED-408, ED-409, ED-405, **ED-402** (solid lit models), **ED-404** (lights, read and drawn) **ED-413** (sprites in the 3D view) **ED-407** (environment and fog) **ED-406** (mesh thumbnails) **ED-403** (materials as assets) **ED-410** (per-part material lists) **ED-411** (plugins really load) and **ED-412** (panels, menus, component types and importers from a plugin) |
| **Phase 5** | 🔄 ED-510, ED-511 and ED-513 done — the backend comparison mode, end to end |
| **Owner priorities** | ✅ **all four closed**: robustness and data safety; live editing into the player; production 2D tools; backend comparison |

---

## Start here

You are picking up a branch with nothing half-finished on it: the working tree is clean, every
commit is pushed, and all 432 tests pass in three configurations. There is no rescue work to do
first.

Phase 3 is eleven rows of thirteen. What is left is **ED-410** (per-mesh material lists, which is
finally designable because ED-403 gave it a material asset to list) and the **two plugin rows**.

1. Read the rest of this file, then `plan.md`'s *Current state*. `ANALYSIS.md` only when a
   *why* is unclear — its decisions (D-01 … D-15, findings F-01/F-02) are cited by id everywhere.
2. Rebuild and run the checks in *Validation commands* below before changing anything, so a later
   failure is yours rather than inherited. The standalone build needs no CNA checkout at all.
3. Pick from *Where to start next* at the bottom. Nothing there is blocked on anything in this
   repository except where it says so.

**Standing constraints** (owner's, still in force): develop and push only on
`claude/cna-editor-architecture-plan-l4jza7`; **no pull request**; everything in this repository is
written in English; do not bump a `formatVersion` without the owner's say-so; do not open issues or
pull requests against `openeggbert/cna` — the CNA gaps G-01…G-04 stay documented here.

---

## What landed in this session

**ED-402 is closed: the 3D view draws solid, lit models instead of wire boxes.** That is the thing
the owner reacted to after the wireframe screenshots, and it is verified by screenshot on EASYGL —
the example crate is a shaded box where it was a cage.

**1. The split follows the wireframe's, and it is what made the task checkable.**
`SceneModels.hpp` in `cna-editor-scene` decides *what* to draw, *where*, and *lit by what*; it is
CNA-free and tested in CI with no device. `CnaModelPass` in the viewport uploads and draws it. The
things most likely to be wrong in a model pass — the world matrix, which mirror is applied, whether
an entity is lit from the right side — are all arithmetic, and a screenshot is a poor instrument
for checking arithmetic.

**2. Four things were found by running it rather than by writing it**, and each one cost a picture
of an empty viewport. They are worth reading before touching this code:

- **The offscreen target had no depth buffer, and `Clear(color)` does not clear depth anyway.**
  The 2D view never needed one — sprites sort by draw order — and models sort per pixel. The
  failure mode is the one worth naming: every draw call issued, nothing rejected loudly, nothing
  on screen.
- **`CullCounterClockwiseFace` is right *because* of the Y mirror, not despite it.** The importer
  winds outward faces counter-clockwise in world space, the mirror reverses apparent winding, and
  the two cancel. That was reasoned out and then *measured*: the triangle count drops by 1113 on
  the example scene when culling is on, which is the back faces going away.
- **`PbrEffect` draws nothing on EASYGL** — gap **G-05**, below. It constructs, accepts every
  parameter, issues its draw calls and puts no pixels anywhere, while `BasicEffect` renders the
  same geometry, matrices and lights correctly in the same frame. So the PBR path is written,
  complete, and **off behind one constant** (`kPreferPbrEffect` in `CnaModelPass.cpp`). Flipping
  it is how PBR is re-tested when CNA changes; nothing else has to move.
- **A second CNA bug found on the way**, also in G-05: `PbrEffect::FillGpuDrawParams` sets
  `textureEnabled = true` unconditionally while binding `texture0` only when a texture exists, so
  a material with a base-colour *factor* and no map — most hand-authored glTF, including this
  repository's own crate — samples an unbound texture. Worked around by binding a white texel,
  which is also the arithmetically correct answer: glTF multiplies factor by map, and the identity
  for that multiply is 1.

**3. ED-404's lighting half landed with it.** `CNA.Light` had been a component descriptor since
Phase 1 with nothing reading it. `SceneLighting.hpp` reads it and performs the reduction the API
forces: `IEffectLights` is XNA's fixed-function rig — an ambient colour and exactly three
directional lights, no point light, no spot light, no cone. So a point or spot light becomes a
**directional light aimed at whatever is being drawn**, dimmed by distance. That is genuinely
position-dependent, which is what a point light *is* — two objects either side of a lamp are lit
from opposite directions, and a test fails for any implementation that uses the light's own axis
instead. Its limit is equally real and stated in the header: one large model is lit as though it
sat at its own origin.

Three smaller decisions inside it, each with a plausible alternative:
- **Falloff reaches zero at the range** rather than following inverse square, which never does.
  Physical is the wrong answer here: it leaves a lamp faintly tinting the far side of the level and
  the range control in the inspector doing nothing anyone can see.
- **The three brightest lights at that point win**, not the nearest and not the first three.
  Document order is not something a user arranges deliberately, and losing the sun because a dim
  lamp was added earlier is a picture nothing on screen accounts for.
- **No lights, or none in range, asks for XNA's `EnableDefaultLighting()`** rather than applying
  zero lights. Same arithmetic, completely different screen: black models read as a broken
  renderer, and the commonest scene of all is one a model has just been dropped into.

**4. The importer carries both descriptions of a material now.** The metallic-roughness factors and
the normal, ORM and emissive maps were being read and discarded; they are kept, *beside* the
Blinn-Phong fields rather than instead of them. Which gets drawn is a property of the build, and a
material holding only the PBR half would render as untextured grey on the fallback — a bug that
appears on one machine and not another. The Blinn-Phong pair is derived from the PBR pair in
`convertMaterial`, so the two cannot describe different materials. An occlusion map in its own
image is reported rather than half-applied.

**5. What the UI now says, because a decision came with an obligation.** The owner chose "editor
viewport only", which means a `ModelRenderer` is visible while authoring and absent when the game
runs. That is exactly the silent difference this editor exists to prevent, so the 3D toolbar says
it in words beside the models, along with how many meshes are still loading. The Diagnostics panel
reports which effect the model pass actually got — with PBR being a CNA extension and fourteen
backends across three support tiers, "why does it look different on that machine" deserves an
answer that is not a screenshot comparison.

**6. Sprites in the 3D view (ED-413), which the owner picked as what follows ED-402.** The view
showed *no* sprites at all before, and the reason was real rather than an oversight: `SpriteBatch`
cannot draw the trapezoid a sprite becomes seen from an angle. ED-402 removed the obstacle rather
than the reason — through a `VertexBuffer` a sprite is an ordinary textured quad. Three decisions:

- **A sprite lies in the scene's XY plane and does not billboard.** Billboarding is the more
  flattering choice and wrong twice: the picker, `computeEntityBounds3D` and the gizmos already
  treat a sprite as a flat box in that plane, so a billboarded one is drawn where it cannot be
  clicked — and the *game* draws through `SpriteBatch`, so it would be a picture the game never
  produces. A sprite seen edge-on is a line; that is the truth about a flat thing.
- **Only the Z rotation is honoured**, because that is all `SpriteBatch` applies. The 3D view is
  allowed to show more than the 2D one and never more than the game.
- **Depth-tested, depth writes off, culling off, unlit.** Writes off so a sprite's transparent
  corners do not punch a hole later sprites cannot draw through; culling off because half of them
  would vanish the moment the camera orbited past their plane; unlit with the tint as the emissive
  colour, because a sprite's art has its lighting painted into it and the models' directional rig
  would darken every sprite by wherever the sun happens to be.

**7. ED-404 is now closed, both halves.** The visualisation is an arrow along the light's direction
and one ring at its range. The arrow is sized in **pixels** converted at the light's own depth, as
the manipulators size their arms — the first attempt used a fixed two world units and was invisible,
because this editor's scenes are laid out in pixel-like hundreds. One ring in the scene's plane
rather than three about the axes, since two of three would be edge-on in the view this editor opens
in; a directional light gets none at all, because a boundary that means nothing is worse than no
boundary.

The example scene gained a **Key Light**, on the same argument the Crate was added on: a feature
demonstrated only by its own tests is one nobody can look at. Its effect shows immediately — the
crate is dimmer under a real point light at distance than under the default rig. That cost the
scene-loader demo its hardcoded count of four, updated to five with a check that a light carries no
sprite: it is the third shape of entity that loader has to survive.

**8. ED-407 — the scene's own ambient and fog.** A field on the *scene*, not a component on a
designated entity: a cave is dark and a moor is foggy, and neither is a property of anything
standing in one. The alternative is how a scene comes to have a mandatory entity that must not be
deleted. **Additive in both directions**, and the second direction is the one with a test: a scene
written before ED-407 has no `environment` object, reads as the defaults, and is written back out
*without* one — a loader that read a default and then serialised it would turn every existing scene
into a modified file the first time it was opened. What fog can do is bounded by `IEffectFog`: a
colour and a linear band. Sprites are fogged too, or one would look like it was floating in front of
the fog rather than standing in it.

**9. ED-406 — a model asset's thumbnail is *drawn*, not decoded.** The ED-402 pass into its own
square target, framed on the model's own bounds from three-quarters on, lit by the default rig
rather than the open scene's — a thumbnail is a picture of the asset, and one that changed because
somebody moved a lamp in the level they have open would be a picture of something else. Its own
target rather than the viewport's, because the browser draws thumbnails in the same frame the
viewport draws the scene.

This needed **`--panel=TITLE`** to be verifiable at all, on the same argument that put `--view=3d`
and `--orbit` here. Two attempts were wrong first, and the reason is worth keeping: a focus set
before a panel's own `Begin`, or during the frame the dock layout is built, is undone when
`DockBuilderFinish` commits the arrangement and the last window docked into a node takes its tab. It
has to be applied **by name, after that**.

**10. ED-403 — materials are assets now, and the override the inspector promised works.**
`CNA.ModelRenderer` has declared a material reference since Phase 1 with nothing able to satisfy it.
A `.cnamaterial` satisfies it: a new format at version 1, fields deliberately identical to
`MeshMaterial` so the pass stays one code path, textures by `Uuid` rather than by path because an id
survives a rename (D-08). Editing and creating both go through commands that write the file and undo
by replaying the previous bytes — the shape ED-300's prefab Apply established.

**The mistake worth recording**: the first version of the document carried a `materialId` of its
own. An asset's identity is the `Uuid` in its `.cnaasset` sidecar, so that was a second answer to
"which material is this", free to disagree with the first. It was found by pointing the example's
crate at the document's id and getting back an unchanged brown crate. The example now has a
`PaintedRed.cnamaterial` and the crate is red, which is the whole feature in one picture.

**11. ED-410, and the `NestedStructure` that waited three phases for a consumer.** ED-311 declared
`List` and deliberately left the nested half unbuilt, on the grounds that a schema designed against
no consumer is a schema designed wrong. This is the consumer: a per-part material list whose element
is a part *name* and a material reference — two fields of different types, which no list of a single
type can carry. So the type was built now, shaped by what actually needed it. **Waiting was right**:
a schema designed in Phase 2 would have been designed for prefab overrides, which turned out to be
computed rather than stored and never needed one.

**Keyed by part name, not by index**, and the row turns on that. Indices are the obvious choice and
fail *silently* — a model reimported with a mesh added in the middle shifts every index after it,
the override then points at a different part, and nothing anywhere can notice. A name that matches
nothing is detectable, so entries matching nothing are **kept** rather than dropped and
`validateModelPartMaterials` reports them.

**Two bugs found by running it, both worth inheriting:**

- The scene loader was still calling the schema-blind reader, so the list decoded as empty and the
  crate stayed the wrong colour. `EntityJson` now uses `propertyValueFromJson`.
- **`PropertyValue::get<T>` returns a *copy*.** So `value.get<StructureValue>().find("part")` leaves
  a pointer into a temporary that dies at the end of the statement — and the read that follows
  returned a silently empty part name rather than crashing. `SceneModels` happened to bind a
  reference and was fine; `SceneValidation` did not. The trap is documented on `find()` itself,
  because the next person will write the inline form first.

**12. ED-411 and ED-412 — plugins load, and can add to the editor.** Discovery and validation were
built in Phase 1 *deliberately first*, on the grounds that an ABI mismatch reaching `dlopen` is a
crash rather than an error message (D-11). This is what went behind that gate.

- **`loaded` and `active` are two flags now.** A manifest can pass every check that runs without
  executing the plugin's code and still fail to open. A manifest a user can fix and a build a user
  can fix are different problems.
- **Unload order is the thing that crashes when reversed**, so it is stated where it happens:
  `shutdown()` while the code is still mapped, destroy through the plugin's *own* function so
  allocation and deallocation land in one runtime, then close the library. `EditorApplication`
  gained its only destructor because a plugin's `shutdown` is handed the context.
- **A plugin must not link the editor's static libraries.** It would get its own copy of every
  registry, so the component it registered would go into a `ComponentRegistry` the editor has never
  heard of — a failure that reads as "initialize ran and nothing happened". It leaves the symbols
  undefined and the host resolves them, which is why `cna-editor` is built with `ENABLE_EXPORTS`.
  Found by running it: a perfectly good plugin failing with "undefined symbol" naming a function
  the editor plainly contains.
- **ED-412's six extension points needed three answers.** Component types and importer settings
  needed *nothing new*. Panels and menu commands needed a registry. Exporters are a menu command
  that writes a file. **Gizmos are deliberately not built** — a gizmo draws, hit-tests, owns a drag
  across frames and turns it into undoable commands, so a registry for one would have to expose the
  viewport's interaction loop. When a plugin needs one, that plugin is what it should be designed
  against — the argument that kept `NestedStructure` unbuilt until ED-410 asked, and was right.
- **The host removes a plugin's registrations as a backstop**, though `shutdown` is still the
  contract. A panel outliving its library is a `std::function` pointing into unmapped code, and it
  fails on the next frame drawn rather than at the moment of the mistake.
- Found by a test that **passed from the repository root and failed under ctest**: with no
  executable path, "plugins beside the editor" resolved relative to the *working directory*. The
  editor now declines to guess rather than loading a stranger's plugins.

Verified against a real `.so` built beside the suite — nothing here can be checked against a double
— and by screenshot: a **Tools** menu and a **Test Plugin Panel** in the running editor.

---

## What landed in the session before this one

**ED-409 is closed and the 3D view is a complete editor**, plus two of the smaller items that were
queued behind it. Five commits, each validated in the standalone, CNA and Clang-Release
configurations before it was pushed.

**1. A bug in last session's rotate gizmo, found by writing the selection-wide half.**
`RotateGizmo3DDrag::update` composed the turn as `start * delta`, which applies the angle about the
entity's *own* axes; the header, the comment beside the line and the 2D gizmo it was written to
mirror all say the turn happens in world space. The two orders differ by exactly the rotation the
entity already has, so the existing test -- which turns an entity at identity -- could not see it.
Anything already lying on its side turned about a ring nobody had drawn. The new test pins the
*property* rather than an angle: a turn about world Z leaves anything pointing along world Z where
it is, whatever angle the drag came out as.

**2. ED-409's scale gizmo**, the last of the three manipulators and the one whose problem the other
two do not have. The resolution is that it does not need the world at all. Translate needs a *line*
to slide along and refuses when the arm points at the camera; rotate needs a *plane* to measure an
angle in and drops a ring seen edge-on; a scale factor is a **ratio of screen distances**, and the
screen always has one.

So an arm here is never dropped and never refuses. Its *direction* is the true projection of the
axis, clipped against the near plane through the wireframe's own `projectSegment`; its *length* was
a chosen constant to begin with, so it is bounded at both ends -- floored at thirty pixels so an
edge-on handle stays clear of the centre one, capped so an arm pointing nearly at the eye does not
throw its handle off the panel. Foreshortening fades toward a floor rather than to nothing, which is
the honest thing to say about an axis that is still there and has little room left to be precise in.
The one case an arm *is* dropped is an axis pointing exactly through the eye, where it projects onto
its own origin; the centre handle (`GizmoAxis3D::All`, which scales all three axes) covers those
pixels, and it is what a press in the middle of a gizmo means anyway.

The panel's 3D interaction was turned inside out on the way: hover and grab now ask one question of
one layout in `beginGizmo3DDrag`, instead of one layout per manipulator computed in parallel and two
near-identical begin-a-drag blocks.

**Verified by screenshot on EASYGL**: red, green and blue arms with square handles, the white centre
square, and the near-edge-on Z arm sitting at its floor instead of vanishing under the middle.

**3. All three 3D manipulators act on a whole selection**, about its shared pivot. `MultiTranslate3D`
is now `MultiTransform3D` and answers all three gestures, as `MultiTransformDrag` has in 2D: the
single-entity drags compute the gesture and this turns one gesture into the edits a selection needs.
Rotate and scale carry their members *around* the pivot as well as changing them. Scale does that in
the **gizmo's** frame rather than in world components, because the offsets have to grow along the
same arms the sizes do.

That needed the gizmo to move too, so the pivot is an argument to the three layout functions rather
than something the drawing does afterwards -- where a gizmo is drawn is where it must be grabbed.
While a drag runs the pivot is the one captured at the press, not a fresh average, which would chase
the entities and bend a steady drag into a spiral.

**4. A grid the 3D view can put on the floor.** `WireframeOptions::gridPlane`, offered from the View
menu and only in the 3D view. The default is unchanged and stays the scene's own XY plane, because
that is where everything this editor can currently place lives; the floor is what ED-402 will want.
One function either way -- the two planes differ by which pair of axes is in the plane and nothing
else, so a second function would be the same loop twice, free to drift.

**5. A key for the 2D/3D view**, on the digits that name it: `2` and `3`, two keys rather than one
toggle, because a key named for the view it selects is safe to press when the user is already there.
Digits are also free while flying, which W, E and R are not. `UiKey` gained `Digit2` and `Digit3`,
appended rather than inserted.

---

## The session before that

**Both of the owner's choices were done, and three more things with them.**

**1. ED-400, the 3D viewport.**

- **`EditorMatrix` and `EditorCamera3D`.** The repository had no matrix type and not one vector
  operation before this -- an orthographic view of a plane gets by on scalars. Both mirror XNA's
  conventions exactly (row-major, row vectors on the left, right-handed, depth to [0, 1]), because
  a camera that agreed with itself but not with the runtime would show the scene mirrored the
  moment anyone pressed Play. Orbit and fly are two ways of moving one state rather than two modes.
- **`SceneWireframe.hpp`**: which lines the 3D view draws, decided CNA-free and tested in CI, with
  the renderer reduced to stroking segments. Near-plane clipping shortens a segment rather than
  dropping it, the grid spacing is the 2D viewport's own `chooseGridSpacing`, and a truncated
  wireframe says so. 3D picking (ray against bounds, nearest wins) came with it.
- **The view, wired in**: a 2D/3D button in the viewport toolbar, a View menu item, `--view=3d`,
  navigation (middle-drag orbits, right-drag turns in place, Shift pans, wheel dollies, WASD+QE fly
  while the right button is held), picking through the 3D projection, and Frame Selected following
  whichever camera is on screen.

**Verified by screenshot on EASYGL**: perspective ground grid to a horizon, red X and blue Z axes,
and boxes around all three entities of HelloSprites.

**Two bugs the screenshot found**, neither of which any test would have:

1. `setViewport()` carried the 2D camera across a viewport swap and dropped the 3D one. The
   windowed host installs its real viewport *after* `initialize()`, so `--view=3d` aimed the camera
   at the scene and then threw the aim away. Three screenshots of an empty grid before it was
   understood.
2. Entering the 3D view left the camera at its constructed default, which shows empty grid for any
   scene laid out away from the origin. It now frames the scene on first entry -- only the first,
   so a later toggle does not throw away an orbit.

**One convention to hold on to**, recorded in `plan.md` too, and *changed late in the session at
the owner's instruction* ("ano, ať 3D odpovídá 2D vizuálně"): **the 3D camera is Y-down**, like the
2D one and like `SpriteBatch`, and its grid is the scene's own XY plane rather than a ground plane
under it. So an entity below another in the 2D viewport is below it in the 3D one, and a 3D camera
at yaw and pitch zero shows exactly what the 2D camera shows -- which is also why the default pitch
is now zero: the user orbits away from a picture they recognise.

The arithmetic stays Y-up and right-handed; the *projection's* Y is mirrored, which is the actual
conversion between the two frames. Mirroring the view's up vector instead would have been a
180-degree roll -- vertical fixed, world +X on the left. The mirror is inside the one
view-projection that `worldToScreen` and `screenToRay` both use, so picking, the gizmo and the
wireframe cannot disagree with what is drawn; `TheThreeDimensionalViewAgreesWithTheTwoDimensionalOneAboutWhichWayIsDown`
pins all of it.

**This is a bill ED-402 will have to pay.** XNA's 3D side is Y-up -- `CreateLookAt`, `BasicEffect`,
every loaded model -- so the model pass must apply the same mirror or models will disagree with
everything around them. A ground-plane grid probably becomes the right one then; `buildSceneGrid`
is where that choice lives.

**2. ED-408, a 3D translate gizmo** — added because a view you can select in but not move things in
is a viewer rather than an editor. `TransformGizmos3D.hpp`, separate from the 2D manipulators
because almost nothing is shared: those lay out in screen space, this one lays out in the world and
asks *where along this world line is the cursor pointing*. Arms are sized in pixels and converted at
the entity's own depth; an arm pointing at the camera is refused rather than answered; a child
stores a parent-relative position (`worldDeltaToLocal3D`). A whole selection drags as one undo
entry, through `MultiTranslate3D` beside the single-entity drag, exactly as the 2D viewport does it.
**Rotate followed it** (ED-409, half): three rings, *sampled and projected* rather than described,
because a circle in the world is an ellipse on screen and the grab has to answer in pixels — one
polyline serves both the drawing and the hit-test, so what a user sees is what they can take hold
of. A ring seen edge-on is dropped: it would project to a line through the centre, overlap the
other two, and have no plane a drag could measure an angle in. The drag turns in world space and
stores in the parent's frame, wraps its delta into (-pi, pi], and snaps the *turn* rather than the
absolute angle. Scale and the selection-wide halves landed this session, along with a fix to the
order that turn was composed in.

**Icons, so the 3D view is not ten identical cubes.** A camera, a light and an audio source all
draw nothing and were all the same wire box. Each has a badge now — camera body and lens, point
with four rays, cone with a wavefront, flat-drawn cube for a model — sized in *pixels*, like the 2D
viewport's icons and for the same reason, and keyed off the same `getEditorIconKind` so the two
views cannot disagree about what an entity is.

**3. Input forwarding into the running player** (the owner's second choice). State, not events,
because an XNA game polls and because a lost snapshot is corrected by the next one while a lost
key-down leaves a key stuck forever. The pointer travels with the surface it was measured against;
the *player* maps it into its own window, and the reply carries the player's own view, which the
Diagnostics panel shows. What the player does with it is deliberately nothing: `PlayerHost::getInput()`
is where a game would read it once there is a way to attach game code to an entity, and that
question stays closed.

**4. A project-settable snap step.** `gridSnap` in `.cnaproject`, additive exactly as `layers` was,
with `SetProjectGridSnapCommand` beside `SetProjectLayersCommand` and the field in the idle
Inspector. Zero means "use the visible grid", which is what an older project means.

**5. Documentation, smoke tests and two loose ends.** `docs/FORMATS.md` gained `gridSnap`; the README gained a
command-line options table (it had none, despite using the flags in its own examples).
`EditorMatrix::transpose` was removed as dead code, and `PlayerInputSnapshot::middleButton` -- on
the wire, compared, printed, and never set -- is now filled from the interaction. ctest gained
three cases for the 3D path: headless, against a real device, and `--view=isometric`, which must
fail rather than start quietly in 2D.

---

## Before that

Fourteen commits, each validated in three configurations before it was pushed:

`ED-401` rotate and scale gizmos with a local/world toggle · `ED-246` **`cna-player` draws the scene**
· `ED-510` backend comparison · `ED-511` the same from the command line, with an exit code ·
`ED-513` per-backend player builds from one configure · gizmo snapping and Ctrl+click selection ·
an interaction boundary on `CommandHistory` · an audio preview that can actually be stopped · a
gizmo that manipulates a whole selection · JPEG dimensions · delete and duplicate as one undo entry
· the gizmo mode and space shown in the viewport toolbar · two validation rules for states that
otherwise fail in silence.

The two that matter most to anyone picking this up: **the player draws now**, so play mode and hot
reload are things you can look at rather than read about in a log; and the **backend comparison
works against two real backends**, which is what finding F-01 was always supposed to buy.

Nothing in that list changed a file format. Every format is still at version 1.

---

## Validation commands

```bash
# Standalone: no CNA checkout, no GPU, no window.
cmake -S . -B build && cmake --build build -j8
./build/tests/cna-editor-tests
ctest --test-dir build

# With CNA. Needs ../cna, ../sharp-runtime, ../easy-gl, ../meta-gl and CNA's SDL submodules
# (git -C ../cna submodule update --init --depth 1).
cmake -S . -B build-cna -DCNA_EDITOR_WITH_CNA=ON -DCNA_DEVICES=ON -GNinja
cmake --build build-cna -j8 --target cna-editor cna-editor-tests cna-player
ctest --test-dir build-cna -R "CnaEditor|CnaPlayer|CnaScene"

# The CNA-config ctest has one test labelled needs-display. Configure the display it should use:
#   cmake -S . -B build-cna -DCNA_EDITOR_TEST_DISPLAY=:99
# and have a server there:  Xvfb :99 -screen 0 1600x900x24 &
# `xvfb-run -a` works for one-off runs but picks its own display, which that test will not see.

# Two backends, and the comparison between them. The second player is a full CNA build, so this
# is minutes rather than seconds.
cmake -S . -B build-cna -DCNA_EDITOR_WITH_CNA=ON -DCNA_EDITOR_PLAYER_BACKENDS="SOFTWARE" -GNinja
cmake --build build-cna -j8 --target cna-player-backends
DISPLAY=:99 ./build-cna/cna-editor --compare-backends \
    --project=examples/HelloSprites/HelloSprites.cnaproject
# Exit 5 when they differ, 0 when they agree. --tolerance=N sets how close counts as the same.

# The player, drawing the game on its own.
DISPLAY=:99 ./build-cna/cna-player-easygl --project=examples/HelloSprites/HelloSprites.cnaproject \
    --frames=20 --screenshot=/tmp/player.png

# Headless smoke, and a screenshot of the real window.
./build/cna-editor --headless --project=examples/HelloSprites/HelloSprites.cnaproject
DISPLAY=:99 ./build-cna/cna-editor --project=examples/HelloSprites/HelloSprites.cnaproject \
    --frames=40 --screenshot=/tmp/editor.png

# A docked panel that shares a tab bar cannot be photographed unless it is brought forward:
#   --panel=Assets   (exact title; ED-406's model thumbnails are only visible this way)
#
# The 3D viewport (ED-400). --view=3d exists so this picture can be taken at all, and
# --orbit=YAW,PITCH (ED-405) so it can be taken from an angle -- head-on is the one pose in which
# a real mesh and a flat sprite look alike, so it cannot show whether models arrived.
DISPLAY=:99 ./build-cna/cna-editor --project=examples/HelloSprites/HelloSprites.cnaproject \
    --view=3d --orbit=35,25 --frames=40 --screenshot=/tmp/editor3d.png
# Since ED-402 the Crate is a *solid, lit* box rather than a cage -- that is what this picture is
# for. A crate that comes back as a wireframe outline with nothing inside means the model pass
# drew nothing; check the Diagnostics panel's "Model effect" line first, then the depth clear.
#
# To re-test CNA's PBR path when it changes (gap G-05): flip `kPreferPbrEffect` to true in
# src/viewport/CnaModelPass.cpp, rebuild, and take this same picture. Nothing else moves.
```

Do **not** run `cmake --build build-cna` without a target list: CNA's own examples fail to compile
in this environment (`examples/common/PixelTestGame.hpp` cannot find `SDL3/SDL.h`). That is a CNA
build issue, unrelated to the editor, and naming the editor targets sidesteps it entirely.

---

## Decisions taken by the owner (2026-08-04, later — after seeing the 3D screenshots)

The owner's reaction to the wireframe screenshots was "a v tomto mám vyvíjet 3D AAA hry???". The
answer given, and not softened: no. CNA is XNA-class, the reference titles are Terraria and
Stardew, and no amount of work here turns it into Unreal. What *was* conceded is that the 3D view
showing wireframe boxes is a consequence of the model pipeline being unbuilt, not of the framework's
ceiling. The decisions below follow from that.

| Question | Answer |
|----------|--------|
| Target | **Still XNA class, but make the 3D view look like a 3D view.** The priority is that it draws real models instead of wire boxes. |
| Next task | **ED-402 — model rendering.** |
| Where the meshes come from | **ED-405 first: the glTF importer**, built on the `cgltf` CNA already integrates. The longer road, and the one that does not depend on the content pipeline. So the order is ED-405, then ED-402. |
| Branch, CI, CNA gaps, additive fields | All unchanged: same branch and no pull request; CI standalone only; no issues against `openeggbert/cna`; additive fields in the persisted formats need no permission. |

**The one precondition to get right on day one**, already in `plan.md`: the model pass must apply
the same Y mirror the rest of the 3D view uses. XNA's 3D side is Y-up, this camera is Y-down, and a
model pass that skipped the mirror would put models upside down relative to the grid, the gizmos
and every sprite around them.

**Where to start**, concretely: ED-405 is an *importer*, so it lands in `cna-editor-assets` beside
the existing ones and produces an asset record plus a sidecar, exactly as textures do. Read
`AssetImporters.hpp` first. The mesh data it produces then needs a home the CNA-linking viewport
can read — that is the seam to design before writing the parser, because getting it wrong means
writing ED-402 twice.

---

## Decisions taken by the owner (2026-08-04, for the next session)

| Question | Answer |
|----------|--------|
| Phase 3, given that CNA ships `BasicEffect`, `VertexBuffer` and the rest | **Start ED-400**: perspective/orthographic viewport camera with orbit and fly navigation. Do the maths CNA-free first, as `EditorCamera2D` was done — it needs no model pipeline. ED-402/ED-404 still wait. |
| Additive fields in `.cnaproject` / `.cnascene` / `.cnaasset` | **Allowed without asking**, exactly as `layers` was added: no `formatVersion` bump, old files still open, and a round-trip test proving it. Changing or removing an existing field, or bumping a version, still waits for the owner. |
| How far play mode goes | **Add input forwarding**: keyboard and mouse reach the running player and the editor reports what it sees. No scripting or behaviour hook — that question shapes the document model and stays closed. |
| Branch and pull request | Unchanged: `claude/cna-editor-architecture-plan-l4jza7`, **no pull request**. |
| CI | Unchanged: standalone configuration only — no sibling checkouts, no Xvfb, no backend-comparison job. |
| CNA gaps G-01…G-04 | Unchanged: documented here, **no issues or pull requests against `openeggbert/cna`**. |
| Verification | Keep screenshot-verifying UI work on EASYGL under Xvfb. |

---

## Decisions taken by the owner (2026-08-03)

| Question | Answer |
|----------|--------|
| **ED-250 / Q-02** — how a game consumes a compiled scene | **Header-only loader shipped from this repository.** The game includes a header that reads `.cnascene` through CNA's public API. CNA stays untouched, which keeps the D-01/D-03 boundary intact, and the choice is reversible: if a CNA module turns out to be better later, the loader moves into it without the format changing. |
| Priorities after Phase 1 | In order: robustness and data safety → live editing into the running player → production 2D tools → backend comparison mode. |
| CI | GitHub Actions, Linux, standalone configuration only. No sibling checkouts, no submodules. |
| Pull request | None. Push to the branch. |
| CNA gaps G-01/G-02/G-03 | Leave documented here. Do not open issues or pull requests against `openeggbert/cna`. |

---

## Standing assumptions

- **Everything that changes a document is a command** (ANALYSIS.md D-06). This now includes the
  asset database, not only the scene: `SetImporterSettingCommand` goes through the same history.
  An editor where some edits undo and others quietly do not is worse than one where nothing does.
- **Asset identity is a Uuid, never a path** (D-08). Moving or renaming an asset must not touch a
  single scene file. Anything in ED-220 that moves an asset has to keep that true.
- **File formats stay backward compatible.** `formatVersion` is not bumped without the owner's
  say-so; ED-902's migration work reads old versions rather than writing new ones. If a breaking
  change ever looks necessary, it goes here and in `plan.md` prominently, and waits.
- **`cna-editor-viewport` is the only module that may link CNA** (D-03). The build graph enforces
  it: a stray `#include <Microsoft/Xna/...>` anywhere else fails to compile.

---

## Recently completed

Newest first. Each is a single commit on the branch.

- **A key for the 2D/3D view.** `2` and `3`, two keys rather than one toggle: a key named for the
  view it selects is safe to press when the user is already there, and digits stay free while flying
  where W, E and R do not. `UiKey` gained `Digit2` and `Digit3`, appended rather than inserted.
- **A grid the 3D view can put on the floor.** `WireframeOptions::gridPlane`, on the View menu and
  only while the 3D view is on -- the 2D viewport has one plane and no decision to make about it.
  The default is unchanged. The test pins it through the property that made that default worth
  having: a camera with no pitch lies *in* the ground plane and sees one line where the scene's own
  plane fills the view, and tipping it over the floor swaps the two round.
- **All three 3D manipulators act on a whole selection**, about its shared pivot. `MultiTranslate3D`
  became `MultiTransform3D`; rotate and scale carry their members *around* the pivot as well as
  changing them, and scale does that in the gizmo's frame rather than in world components, since the
  offsets have to grow along the same arms the sizes do. The pivot is an argument to the layout
  functions rather than something the drawing does afterwards: where a gizmo is drawn is where it
  must be grabbed.
- **ED-409's scale gizmo**, the last of the three. An arm is never dropped and never refuses,
  because a scale is a ratio of screen distances and the screen always has one; it shortens to a
  thirty-pixel floor and fades instead. Verified by screenshot on EASYGL.
- **The 3D rotate gizmo turns in the world, as it always said it did.** `start * delta` applies the
  angle about the entity's own axes; the two orders differ by exactly the rotation the entity
  already has, which is why a test at identity could not see it.

- **Two validation rules for states that fail silently.** A tilemap with a tile size of zero draws
  nothing *and* swallows every brush stroke, saying neither; an animation with no sheet leaves the
  sprite beside it drawing a placeholder, which looks exactly like a broken asset reference and is a
  different problem with a different fix. Both are warnings, since a component added a moment ago
  and not yet configured is a normal state -- what is not normal is finding out by wondering why
  painting does nothing.
- **The viewport toolbar shows the gizmo mode and space.** `W`/`E`/`R` and `X` could change them
  and the menu could set them, but nothing *displayed* them -- a user who could not see which
  manipulator was active had to press a key to find out what it had been. The space button is
  labelled with the space it is in rather than the one pressing it selects, because a toolbar
  reports state; the menu still says "Use Local Space", because a menu item is an instruction.
- **Delete and duplicate a selection as one undo entry**, plus JPEG dimensions. Both already acted
  on the whole selection; what they did not do was undo as one action. `CompositeCommand` (new, in
  core) holds several commands and undoes them in reverse, which is also what makes it safe to build
  out of commands that depend on each other's effects. Delete takes the selection's *roots* -- the
  same helper the gizmo uses -- since a delete carries the subtree with it and asking again for a
  child would push a command that finds nothing.
  Fixed on the way: `TwoRealPlayersAreComparedAgainstEachOther` was flaky. It told the two captures
  apart by looking for "-2" anywhere in the path, and the scratch directory's name contains a Uuid
  -- so the test passed or failed depending on random hex. It matches the filename stem now.
- **A gizmo on a multi-selection.** With Ctrl+click able to build one, the gizmo being stuck on the
  primary selection had become the obvious gap. It now sits on the selection's **shared pivot** --
  the average of the members' world positions, not the bounding box's centre, which would move when
  an entity was merely rotated -- and a drag moves, turns or scales all of them about it. Rotation
  and scale carry the members *around* the pivot as well as changing them, which is the difference
  between rotating an arrangement and spinning each of its parts in place.
  Two things make it correct rather than merely working. The whole drag is one
  `TransformEntitiesCommand`, so one Ctrl+Z undoes it: a command per entity would undo them one at a
  time, through arrangements the scene was never in. And descendants of selected entities are
  excluded (`findSelectionRoots`), because a child is already carried by its parent and transforming
  both moves it twice.
  The single-entity drags still compute the *gesture* -- how far, what angle, what factor -- and the
  new `MultiTransformDrag` turns one gesture into the edits a whole selection needs, so twenty
  entities cannot disagree about how far the cursor went.
- **An interaction boundary on `CommandHistory`**, which closes the oldest known problem in this
  file: two separate drags of one inspector slider collapsed into a single undo entry. The merge key
  answers "is this the same *edit*" -- entity, component, property -- and cannot answer "is this the
  same *gesture*", because two drags of one field are identical by everything it can see and differ
  only in that the user let go in between.
  `endInteraction()` marks that boundary, and the application calls it once per frame on every frame
  where no widget is active. The signal is new: `EditorUi::isAnyItemActive()`, answered by ImGui's
  own `IsAnyItemActive()` -- which already covers viewport drags, since the viewport image is an
  ImGui item and stays active for the whole of one. The gizmos have always opened a new entry on the
  first edit of a drag; this is the same rule for every other continuous control, including ones
  nobody has written yet.
- **Gizmo snapping and Ctrl+click selection**, both of which were waiting on the same missing piece:
  `UiImageInteraction` carried no modifier state. It now carries `control` and `shift`, filled by the
  ImGui backend from `ImGuiIO` and settable by tests.
  Holding Ctrl rounds a drag -- translation to the **visible** grid (the same `chooseGridSpacing` the
  renderer draws with, moved into the scene module so there is one answer to "how far apart are the
  lines"), rotation to 15 degrees, scale to tenths. A modifier rather than a mode, because "line this
  up with that, now nudge it" happens seconds apart. The *result* is snapped rather than the movement
  -- except for rotation, where the *turn* is, since snapping the absolute angle would straighten
  whatever it touched -- and only on the axes the handle allows. That last one was a real bug the
  test caught: an X-constrained drag was snapping Y as well, moving the entity along the one axis the
  user had just excluded, where they were not looking.
  Ctrl+click on the scene adds and removes from the selection; Ctrl on empty space does nothing,
  since clearing a half-assembled selection is the one thing that cannot have been meant.
- **ED-513** per-backend player builds from one configure. `CNA_EDITOR_PLAYER_BACKENDS="SOFTWARE"`
  now builds the second player itself instead of leaving it to a hand-run CMake in a scratch
  directory. A *nested* build per backend, because `CNA_GRAPHICS_BACKEND` is a cache variable of
  CNA's own build: one value per build tree, and no arrangement of targets in this one can change
  that. Each child is told to build nothing but the player -- no ImGui, no tests, and an empty
  backend list so it cannot spawn children of its own. Empty by default and minutes per entry when
  it is not, since each one compiles CNA again; but without it ED-510 has nothing to compare and
  play mode's backend picker has exactly one entry.
- **ED-511** the conformance harness. `cna-editor --compare-backends` runs *exactly* what the
  Backends panel runs -- `ComparisonPanel::startComparison`, called directly, not a second path that
  could pass while the panel was broken -- prints one line per backend, and **exits non-zero when
  they disagree**. The exit code is the assertion; a build server reads none of the output.
  `--tolerance=N` is on the command line because the threshold is a policy a project sets, not a
  constant this editor should choose for it. A headless run is refused up front: comparing means
  decoding captures, decoding needs a device, and "every capture was unreadable" is a confusing way
  to say "wrong configuration". Verified against the two real player builds: exit 5 with the diff
  reported, and exit 0 at `--tolerance=64`, which is exactly the largest difference the two backends
  actually produce.
- **ED-510** backend comparison, the last item on the owner's priority list. `BackendComparison`
  launches one player per installed build, waits for each handshake, asks all of them for the same
  frame over the bridge and compares what comes back against the first to answer. It needed no new
  architecture, exactly as `plan.md` predicted: play mode already spawns and supervises a player, so
  this is that, several times over.
  The pixel arithmetic is `ImageDiff` in `cna-editor-core` -- CNA-free and tested against images the
  test builds itself. Decoding a capture is *injected* (`ImageReader`/`ImageWriter`, supplied by the
  viewport), because turning a PNG back into pixels needs a graphics API and one module may have one.
  **The tolerance is the load-bearing detail.** Two backends drawing the same scene are not required
  to be bit-identical and never will be -- different rasterisation rules, different filtering
  precision -- so a comparison with no tolerance reports every backend as different from every other,
  which is true and useless. What is reported is how many pixels differ, by how much, and *where*:
  the bounding box is usually the whole diagnosis, and a difference image is written beside the
  captures with the matching picture dimmed and the differing pixels in magenta.
  **Verified against two real backends**, not just at the seams: a `cna-player-software` built into
  the scratch directory and installed beside `cna-player-easygl`. They disagree on 496 of 921600
  pixels (0.05%), largest channel difference 64, inside a 103x64 box -- and the difference image
  shows why at a glance: it is the anti-aliased outline of the two sprites and nothing else. That is
  a real, explainable difference between CNA's backends, found by pressing a button.
- **ED-246** `cna-player` draws. It has spoken the whole protocol since ED-240 and shown nothing
  the whole time, which quietly made three finished rows half true -- play mode ran a game with a
  blank window, and a live edit or a hot reload was observable in a log rather than in a picture.
  The window host mirrors the editor's exactly: a `Game` subclass hidden behind a free function, so
  CNA stays a private link dependency of one module. It draws through the **same**
  `CnaSceneRenderer`, via a new `renderGameView` that runs the content pass and skips the grid and
  overlay ones -- the separation was already structural, so this is not running two passes rather
  than filtering anything. A second renderer would have been a second answer to "what does this
  look like when it runs", which is the property the editor exists to guarantee.
  The view is the scene's own: `computeGameView` finds the primary camera, reads `orthographicSize`
  as the visible **height** (so a wider window shows more world instead of stretching it), takes the
  camera's world transform so a camera on a rig works, and clears to that camera's `clearColor`.
  Screenshots changed shape: `PlayerHost` **queues** them and the frame loop that owns the device
  takes them, so `screenshotReady` now means pixels are on disk. A failed or impossible capture
  answers `written: false` with a reason, because an editor waiting for a reply that never arrives
  is worse off than one told no. No window to be had degrades to running with nothing drawn and says
  so -- which is exactly what the bridge tests, which have no display, now exercise.
  This is also ED-510's missing half: comparing backends means capturing frames from N players, and
  until now there were no frames.
- **ED-401** rotate and scale gizmos, and the local/world space toggle (`X`). Built out of phase
  order because it is not a 3D task in a 2D viewport: `E` and `R` had been selecting a manipulator
  that did not exist since Phase 1. `TranslateGizmo.hpp` became `TransformGizmos.hpp` and now holds
  all three -- one layout, one hit-test, one drag each, all CNA-free, so what a user can grab is
  tested in CI and only the pixels need a GPU.
  Rotate turns in **world** space and stores the result in the parent's frame, or a child of a
  rotated parent would turn by a rotated fraction of the angle the cursor described; the delta is
  wrapped into (-pi, pi] so dragging across the seam does not spin it. Scale is a **ratio in screen
  space**, since scale is unitless and only a ratio is zoom-independent; dragging through the pivot
  flips the entity (negative scale is legitimate and `SpriteBatch` honours it) but never lands on
  zero, which would make it invisible *and* unclickable.
  **Scale deliberately has no space toggle**: a non-uniform scale in world space needs a shear,
  which a position/rotation/scale transform cannot express, so its arms are always the entity's own
  axes. The translate gizmo's arms are now stored directions rather than assumed ones, which is what
  lets the drawing, the hit-test and the drag read the same two vectors in either space; the
  renderer grew a rotated-quad `drawLine`, since SpriteBatch has no line primitive and an arm that
  is not axis-aligned cannot be a rectangle. Verified by screenshot on EASYGL: ring, rotated arms
  and arrowheads, and the scale gizmo's square handles.
- **ED-308** the build panel. Shells out to `cmake` rather than writing a script, because that
  gives the editor the exit code and the output; drives the *project's own* `CMakeLists`,
  contributing only the backend and the output directory; and reports a missing toolchain before
  offering the button, since CMake's own message for that says nothing a user can act on. The
  commands are shown before they run. `planBuild` is pure, so the part that carries the knowledge
  is tested without a compiler; the process half is exercised against a real `cmake` over a
  `project(... NONE)` probe, which needs no compiler either.
  `findCMake()` is resolved once by the panel and cached -- it walks every directory on the PATH,
  and the panel draws every frame.
- **ED-304** audio. The preview plays through CNA's *public* `SoundEffect(path)` constructor, so
  unlike the sprite-font preview there was nothing forbidden in the way -- worth checking before
  assuming a second G-04. `EditorAudio` sits beside `EditorViewport` in the one CNA-linking module.
  The component preview uses the component's own volume, pitch and pan; the asset preview uses
  neutral ones, because that is the file as imported rather than as some entity plays it.
  `CNA.AudioListener` takes its position from the Transform rather than repeating it, and a second
  enabled listener is an error for the same reason a second primary camera is.
  The preview now holds a **`SoundEffectInstance`** rather than calling the fire-and-forget
  `SoundEffect::Play()`, so Stop stops the clip and `isPlaying()` asks the device instead of
  reporting a remembered flag. **Not exercised here:** this container has no audio device, so the
  change is verified by compiling and by reading CNA's API, not by hearing it.
- **ED-309** backend diagnostics. Capabilities are asked of the *device* rather than derived from
  the backend's name -- several vary by driver within one backend. EASYGL correctly reports no
  wireframe fill mode (OpenGL ES has no `glPolygonMode`), which a name-keyed table would have got
  wrong. They cross the module boundary as strings, so nothing outside the CNA-linking module knows
  `GraphicsCapability` exists.
- **Tilemap eyedropper and rectangle fill**, and the `ViewportPanel::handleInteraction` split
  that had been marked due. The split came first, deliberately: the *ordering* between interaction
  modes is load-bearing -- a drag in progress outranks everything, a tool outranks the gizmo, the
  gizmo outranks the picker -- and adding two more modes to one long function would have buried
  three real rules. Each is now one line with its reason beside it. The eyedropper hands the brush
  back to the paint tool, because picking a tile is never the goal; an empty cell is refused rather
  than taken as -1, which would silently turn it into an eraser. A fill applies on release, so the
  rectangle can be adjusted before it lands, and is one undo entry however large.
- **ED-303** sprite animation, complete. A frame is an index into a sheet, not a rectangle -- the
  same arithmetic the tilemap already does, and far smaller to author. The frame list is an
  ordinary `List<Integer>`, so reordering and adding frames needed no new widget. Playback is a
  plain value the inspector owns and throws away, never the document's (D-07), and the clock is
  passed in so the test steps it exactly. `EditorUi::imageRegion` is new: it draws one sub-rectangle
  of a sheet, in texels, because everything on this side of the boundary already speaks texels.
  The viewport draws the previewed frame too: the inspector publishes the *result* through
  `EditorActions` while keeping the playback itself, which is the same shape the selection already
  uses and for the same reason. An animated sprite is sized by its frame rather than by its sheet,
  or a sixteen-frame walk cycle would be sixteen times too wide to click and Frame Selected would
  zoom out to fit a strip nobody is looking at.
  Per-frame durations close the row: an **optional** parallel list, ignored unless it is exactly
  as long as the frame list, so a scene written before it existed plays identically. Playback walks
  one frame at a time now rather than dividing by a single rate, bounded so a list of near-zeroes
  cannot spin it.
- **ED-302 (part)** sprite fonts. The `.spritefont` description is read and reported in the
  inspector, all of it **read-only** -- the file is the content pipeline's own input, so an
  editable copy in the sidecar would be a second answer to a question the build asks the file.
  Read by a targeted tag scan, not an XML parser, and guarded by a structural check so an ordinary
  XML file with a `<Size>` element is not read as a font. **The glyph preview is not done** and
  ED-302 is 🔄: it needs a built `SpriteFont`, and CNA exposes no public way to make one from a
  `.spritefont` -- recorded as gap G-04.
- **ED-301** tilemaps. Flat `List<Integer>` grid on an ordinary component, so no new serialised
  structure and nothing else had to learn a new type. A stroke is one undo entry and the merge key
  carries the *stroke id* -- entity + property alone cannot tell two drags apart. Painting is an
  `EditorTool`, not a `GizmoMode`, and while a brush is active it suppresses both the gizmo and
  click-to-select: a tilemap's gizmo sits over its own first tiles, so the first stroke would
  otherwise drag the map instead of painting it. That was a real bug the test caught.
  Rendering shares the sprite pass, with viewport culling -- a 200x200 map is forty thousand draw
  calls a frame otherwise. Verified by screenshot on EASYGL.
- **ED-300** prefabs, complete. Create Prefab from a hierarchy row, drop one from the browser to
  instantiate, and an inspector block that reports what an instance has changed with Revert and
  Apply. Every step undoes, file writes included.
  Two things worth knowing about Apply, because both were bugs before they were tests: it maps the
  instance's entity ids *back* through the links before writing (an instantiated instance has fresh
  ids, and writing those verbatim would leave every link naming an entity the file no longer has),
  and it strips the instance bookkeeping from what it writes (or every future instance would be
  born claiming to be an instance of something else). `findPrefabOverrides` compares through the
  *descriptor* on both sides, because "unset" and "set to the default" are deliberately
  indistinguishable in the document model and a round trip through a file turns one into the other.
- **ED-300 (part)** prefabs: the document model and the scene operations. `.cnaprefab` reuses the
  scene's entity encoding through the newly extracted `EntityJson.hpp` -- one codec, because an
  instantiated prefab and a hand-authored entity must be indistinguishable once they are in a scene.
  **Overrides are computed, not stored**, so prefabs added no field to the scene format at all.
  `InstantiatePrefabCommand`, `RevertPrefabInstanceCommand`, `findPrefabOverrides` and the link keys
  are in and tested. **The UI is not**: nothing yet creates a prefab from a selection or drops one
  into a scene, so ED-300 is 🔄. See *Where to start next*.
- **ED-305** layers and tags. Layers live in the `.cnaproject` (a property of the game, not of one
  level) and drive `CNA.Layer`'s choices by re-registering the descriptor. Renaming a layer leaves
  entities holding the old name deliberately -- which of the remaining layers they meant is the
  user's decision -- and the new `unknown-enum-value` validation rule reports it. Tags are their own
  component holding a `List<String>`. `SetProjectLayersCommand` lives in `cna-editor-context`
  because it has to touch both the project and the registry, and neither of those modules may
  depend on the other. The idle Inspector now edits project settings instead of saying "Nothing
  selected". No `formatVersion` bump: `layers` is an additive field, and there is a test that a
  project file written before it existed still opens.
- **ED-311 (part)** `PropertyType::List`. Element type declared on the descriptor, never
  inferred. `"list"` appended to the type-name table, not inserted, because those names are on the
  wire. The inspector draws a collapsible block; every change returns the whole new list, so add,
  remove, move and edit are all plain `SetPropertyCommand`s, and structural ones take their own
  undo entry. **`NestedStructure` is not done** and the plan row is 🔄, not ✅.
  Fixed on the way: an array of non-numbers on an *unregistered* component used to be inferred as a
  vector, so `["ground", "solid"]` became `(0, 0)` and was written back that way -- silently
  emptying a field in the one case the descriptor system promises to survive. Inference now only
  calls a short array a vector when every element is a number.
- **ED-306 / ED-307** live editing into the running player. One hook does the property half:
  every document change goes through a command (D-06), so `EditorContext`'s new command observer
  sees all of them and the application mirrors the `SetPropertyCommand`s. Undo and redo mirror too,
  and the value is read from the *document* rather than from the command, because after an undo the
  live value is the old one. Assets are sent by id, and the player rescans before looking the id up.
  Verified end to end against a real `cna-player` process, not just at the seams.
  `PlayerHost::takeReloadedAssets()` is the seam the graphics half drains, and since ED-246 it does:
  a texture changed on disk is visible in the running game rather than only in its log.
- **ED-902** format migration. A chain of single-version steps, run on every load of a
  `.cnascene`, `.cnaproject` and `.cnaasset`. The gate and the upgrade are one piece of code,
  because refusing a file from the future and upgrading one from the past both answer "what version
  is this?". **No `formatVersion` was bumped** and every chain is empty -- that is the intended
  state, and registering a step is not a licence to bump one. `loadFromJson` takes an optional
  migrator so a test can prove the loader reads the *upgraded* document, not the original.
- **ED-903** crash recovery. No signal handler, on purpose -- one serialising a document from
  inside `SIGSEGV` calls `malloc` and the filesystem with a corrupted heap. A `.cnarecovery`
  snapshot instead, written by ordinary code every `--autosave=SECONDS` while the document differs
  from its file, atomically by rename, in the user's state directory. Offered on reopen, never
  applied: while the offer stands, autosave for that scene is suspended, because the snapshot file
  is keyed by scene id and the current session's unsaved seconds are worth less than the previous
  session's unsaved hours. Format documented in `docs/FORMATS.md`.
- **ED-905** undo history panel. Rows are *positions*, not entries -- one more row than there are
  commands, and the extra one is the document as opened, which is what someone asking to put it
  back is aiming at. Clicking navigates through `EditorApplication::undo/redo`, so a jump prunes
  the selection the way Ctrl+Z does.
- **ED-310** scene validation. `SceneValidation.hpp` holds the structural rules -- duplicate
  primary cameras, inverted camera planes, zero scale, empty entities, a missing Transform, a
  second copy of a unique component, an unregistered component type, a sprite with no texture.
  Missing references stayed where they were, in `MissingReferences.hpp`, because they need the
  asset database and the rules do not; both now report into one **Validation** panel, since a user
  whose scene misbehaves does not know in advance which of the two is at fault. Every rule
  describes a *legal* state, so nothing refuses to save and nothing is repaired automatically, and
  each rule's test asserts both halves: the offending scene is reported and the nearest legitimate
  scene is not.

- **ED-221 / ED-220 thumbnails.** Reuse the scene renderer's texture cache, so a preview costs
  nothing once the sprite using it has been drawn. Needed a *keyed* borrow in the UI renderer —
  the existing one owns a single slot, right for the render target and wrong for anything there
  can be many of. Invalidating an asset now releases the UI's borrowed entry first, because the
  watcher makes a dangling pointer there a routine event rather than a theoretical one.
- **ED-250** header-only scene loader, and `docs/DESIGN-SCENE-LOADER.md`. Q-02 resolved.
  `examples/SceneLoaderDemo` is both the documentation and the integration test.
- **ED-220** asset browser: derived folder tree, filter, rename, move. A move keeps the asset's id
  and is asserted not to change a scene file by a single byte.
- **ED-900 (partial)** Linux CI, and this file.

- **ED-223** asset watcher. Polling rather than a native watcher, deliberately — three platform
  implementations with three failure modes, each still needing a polling fallback for the network
  and container mounts where a team's assets often live. The clock is passed in, so tests advance
  time exactly and never sleep. A change drops the viewport's cached texture via
  `EditorViewport::invalidateAsset`, which also clears the failed-load memory so a file that comes
  back gets another attempt.
- **ED-222 / ED-221 (part)** importer settings. An importer's settings are declared as a
  `ComponentDescriptor`, so the inspector needed no new code. Texture dimensions are read from the
  file header (PNG and BMP state theirs at fixed offsets); an unmeasurable format reports unknown
  rather than zero.
- **ED-224** missing-reference report and relink. Answers a different question from
  `AssetDatabase::getMissingAssets()`: which *entities* point at something that will not load.
  `RelinkAssetCommand` rewrites every reference as one undo entry.
- **ED-210** panel split. `EditorApplication.cpp` went from 1207 lines to 523; six panel classes
  under `src/panels/`, reaching the editor through the narrow `EditorActions` interface.
- **ED-200 / ED-208 / ED-114** hierarchy editing, asset drag-and-drop onto slots, console filter.
- **ED-118** rotations as Euler degrees, in XNA's own `CreateFromYawPitchRoll` convention.
- **ED-204** icons for entities the viewport cannot draw.
- **ED-209 / ED-122** keyboard shortcuts and Frame Selected.
- **ED-205** translate gizmo.

---

## In progress / immediately next

Phase 1 closed. Working through the owner's priority order:

1. ~~**Robustness and data safety**~~ ✅ — ED-310, ED-905, ED-903 and ED-902 are all done. No
   `formatVersion` was bumped; `.cnarecovery` is a *new* format at version 1, not a change to an
   existing one.
2. ~~**Live editing into the running player**~~ ✅ — ED-306 and ED-307 are done.
3. ~~**Production 2D tools**~~ ✅ — ED-300, ED-301, ED-303, ED-304, ED-305, ED-308 and ED-309 are
   done; ED-302 and ED-311 🔄 and both blocked, see below.
   Note that `NestedStructure` (the open half of ED-311) turned out **not** to be needed by
   prefabs: ED-300 computes overrides rather than storing them, so nothing needs a nested schema
   yet. Leave it unbuilt until something real asks for one.
4. ~~**ED-510** backend comparison mode~~ ✅ — with ED-511 and ED-513 beside it.

Everything on that list is closed, and so is the 3D work the owner opened afterwards — including
ED-405, the model pipeline the owner named after seeing the wireframe screenshots. **ED-402 is done**, and with it the
lighting half of ED-404. The 3D view draws solid, lit models. See *Where to start next* — the
owner picked sprites in the 3D view as what follows.

---

## Known problems and limitations

- **The backend comparison needs two player builds, and a default build produces one.** ED-513 makes
  the second one a configure option (`CNA_EDITOR_PLAYER_BACKENDS`) rather than a hand-run CMake, but
  it is off by default because each entry compiles CNA again -- minutes, not seconds. Anyone who
  presses Compare on a default build will be told it needs another build, which is correct and still
  worth knowing before it happens.
- **A red `needs-display` ctest usually means Xvfb died, not that the editor did.** Twice in one
  session `CnaEditorWindowSmoke` and `CnaSceneLoaderDemo` failed together with "Subprocess aborted"
  in hundredths of a second; both times the X server was simply gone (`pgrep Xvfb`), and restarting
  it made all twelve pass. Check that before reading the diff.
- **Image dimensions are read for PNG, BMP and JPEG only.** Anything else reports unknown, and the
  inspector shows 0×0 with a tooltip saying why. A format that needs a decoder to measure is one
  the importer should measure when it loads the file for real.
- **`--headless` writes to the project.** Opening a project applies importer facts and may rewrite
  a sidecar. This is intended (it corrected a stale stamp in the example) but worth knowing before
  running the editor against a repository you want left alone.

### Gaps found in CNA — documented only, not filed upstream

| Id | Gap | Effect here |
|----|-----|-------------|
| G-01 | `Microsoft::Xna::Framework::Color` has no default constructor | `std::vector<Color>::resize(n)` does not compile. XNA's `Color` *is* default-constructible, so this is a real behavioural difference. Worked around with `assign`. |
| G-02 | `CNA::Devices::Clipboard` sits inside `#ifdef CNA_DEVICES`, default OFF | An editor built against a default CNA has no clipboard. Degrades cleanly and is reported. |
| G-03 | `RenderTarget2D` sampled as a texture is not origin-normalised across backends | EASYGL renders it flipped, SOFTWARE does not. Worked around by `EditorViewport::isRenderTextureFlippedVertically()`, which is a compile-time constant per backend. |
| G-05 | `PbrEffect` draws nothing on EASYGL, silently | The 3D model pass cannot use PBR (ED-402). It constructs, accepts every parameter, issues its draw calls and puts no pixels on screen; `BasicEffect` renders the same geometry, matrices and lights correctly in the same frame. Culling and an unbound sampler were both ruled out first. Second bug in the same place: `FillGpuDrawParams` sets `textureEnabled` unconditionally while binding `texture0` only when a texture exists, so a base-colour factor with no map samples nothing — worked around with a white texel. The PBR path here is complete and off behind `kPreferPbrEffect` |
| G-04 | No public way to build a `SpriteFont` | `SpriteFont`'s constructor takes an already-built glyph atlas, and the only reader that produces one is `CNA::Internal::Xnb::SpriteFontReader` -- which D-01 forbids the editor from touching. So the editor can describe a `.spritefont` but not preview its glyphs (ED-302). A public `ContentManager::Load<SpriteFont>` specialisation, or a public font builder, would close it. |

---

## ED-402: decisions taken by the owner (2026-08-04)

Asked and answered. These are decisions, not guesses -- the earlier draft of this section recorded
them as assumptions because the questions had gone out unanswered, and that draft is gone.

| Question | Answer |
|----------|--------|
| Which effect draws a model? | **`PbrEffect`, falling back to `BasicEffect`** where the backend or build has none. Needs additive `MeshMaterial` fields -- metallic, roughness, and paths for the normal, ORM and emissive maps -- which the glTF importer currently reads and throws away because ED-405 assumed `BasicEffect`. Additive fields are pre-approved, so no `formatVersion` bump and a test that an older `.cnaasset` still opens. |
| Where does light come from? | **Scene `CNA.Light` entities, with XNA's three-point default when the scene has no enabled light.** This pulls ED-404 forward into ED-402. A scene with no lights must not be black: that reads as a broken renderer rather than an unlit scene. |
| Does the game render 3D, or only the editor? | **Editor viewport only**, for now. `CNA.Camera` and `cna-player` stay 2D. The consequence has to be *visible in the UI*: a `ModelRenderer` that is there while authoring and gone when the game runs is exactly the silent difference this editor exists to prevent, so the viewport says so rather than leaving it to be discovered. |
| What follows ED-402? | **Sprites drawn correctly in the 3D view.** Not a plan row yet; add one. Today the 3D view shows no sprites at all, because `SpriteBatch` cannot draw the trapezoid a sprite becomes when seen from an angle -- but once a `VertexBuffer` path exists for models, a sprite is a textured quad through the same path. Without it the 3D view shows the models rather than the scene. |

**Not a decision, and not negotiable:** the model pass must apply the *same* Y mirror as the rest of
the 3D view (`EditorCamera3D`'s projection flip). XNA's 3D side is Y-up and this camera is Y-down,
so a pass that skips the mirror renders models upside down relative to the grid, the gizmos and
every sprite around them. It is already stated in `plan.md`; it is repeated here because it is the
first thing ED-402 can get wrong and the last thing anyone would suspect.

---

## Where to start next

Read this file, then `plan.md`'s *Current state*.

**Phase 3 is complete and Phase 2 is eleven rows of twelve.** The one thing left in either is
blocked on CNA. So the next session picks its own direction, and these are the candidates in the
order I would take them:

1. **ED-900 — CI on Windows and macOS.** Linux has been green all along and the other two have
   never been built. This is the highest-value unglamorous row left: every platform-specific thing
   written this session (`dlopen`/`LoadLibrary` in `PluginHost`, `ENABLE_EXPORTS`, the shared-module
   suffix in the test plugin's manifest) has only ever run on Linux, and the Windows half is
   written from documentation rather than from a passing build.
2. **ED-904 — editor preferences, persisted separately from any project.** Small, self-contained,
   and the editor has accumulated several settings that currently live nowhere: the grid plane, the
   gizmo space, which view it opens in.
3. **ED-901 — Doxygen matching CNA's.** The headers are written for it already.
4. **Phase 4** proper, if the owner wants features over foundations: ED-500 (animation timeline),
   ED-502 (particles), ED-505 (frame debugger over the bridge, which the bridge already supports).

**Blocked, not forgotten:**

- **ED-302's glyph preview** needs a public way to build a `SpriteFont` from a `.spritefont`. CNA
  has none (gap **G-04**). It is the only thing keeping Phase 2 from closing.
- **`PbrEffect` (G-05)** draws nothing on EASYGL. The PBR path here is written and one constant
  away — `kPreferPbrEffect` in `CnaModelPass.cpp`.
- **Animation.** `MeshData` still has no node hierarchy, on purpose. CNA ships `SkinnedEffect`,
  `SkinnedModelEXT` and `AnimationPlayer`, so the framework side is further along than this
  repository is.
- **Plugin gizmos**, above: waiting for a plugin that needs one.

**Smaller, unblocked:**

1. **Hear the audio preview on a machine with a sound device.** Still only reasoned about.
2. **Sprite picking uses the axis-aligned box, not the rotated quad**, so a rotated sprite has a
   slightly generous hit area in the 3D view.
3. **A second look at `findSelectionRoots`** — move it to the scene module when a *fourth*,
   non-gizmo caller appears.
4. **The example project carries a material per demonstrated feature.** If it grows a third,
   consider whether it is still an example or has become a test fixture.

The one behaviour to preserve throughout: every `formatVersion` is at 1, and ED-902's migration
chains are empty on purpose. This session added `.cnamaterial` as a *new* format at version 1, and
`environment` and `materials` as *additive* fields — the permission the owner already gave. Keep it
that way, and do not bump a `formatVersion` without asking.
