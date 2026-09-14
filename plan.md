# CNA Editor — Implementation Plan

> The reasoning behind this plan is in [`ANALYSIS.md`](ANALYSIS.md). Read that first: it records
> what was verified against the CNA codebase, where the original architecture discussion was wrong,
> and the fifteen decisions (D-01 … D-15) every task below rests on.
>
> **Task ids** use an `ED-NNN` scheme, banded by phase: `ED-0xx` Phase 0, `ED-1xx`/`ED-2xx` Phase 1,
> `ED-3xx` Phase 2, `ED-4xx` Phase 3, `ED-5xx` Phase 4, `ED-9xx` cross-cutting. Ids are stable and
> never reused.

## Status legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Done and verified |
| 🔄 | In progress |
| ⬜ | Not started |
| ⛔ | Deferred by explicit decision (reasoning recorded on the task) |
| 🔬 | Blocked on a spike or a decision |

---

## Current state

**Phases 0, 1 and 3 are complete, Phase 2 is eleven rows of twelve, and every priority the owner
named is closed** — robustness and data safety, live editing into the running player, the production 2D
tools, and the backend comparison mode. The editor opens a project, edits it, plays it *and sees the
game drawn* (ED-246), compares how two graphics backends render it (ED-510/511/513), and a shipped
game can load what it produced. The repository still builds and passes its full suite with no CNA
checkout, no GPU and no window:

- 12 modules, three executables, and **424 passing tests across 12 CTest suites** (17 with CNA)
- clean at `-Wall -Wextra -Wpedantic -Werror`
- the **real Dear ImGui UI** draws every editor panel headless, and its geometry is validated
  command-by-command in CI
- the editor renders **identically on two CNA backends** (SOFTWARE and EASYGL), verified by
  screenshot
- the scene draws with an adaptive grid, sprites ordered by layer depth, selection outlines,
  **translate, rotate and scale gizmos** — each drag one undo entry, each grabbable in world or in
  the entity's own axes — and **icons** for entities that have no geometry to draw
- the **3D viewport draws imported models as solid, lit geometry** (ED-402), depth-tested, with
  the editor's wireframe overlay laid over the top, and lit by the scene's own `CNA.Light`
  entities where it has any -- reduced to the three directional lights and the ambient term XNA's
  fixed-function rig can be told, with point and spot lights approximated and the approximation
  written down
- a **3D viewport** with the same three manipulators (ED-408/409), each acting on one entity or on
  a whole selection about its shared pivot, a grid that can sit on the scene's plane or on a floor,
  orbit and fly navigation, and picking through the 3D projection — all of it CNA-free geometry, so
  what a user can grab is tested in CI and only the pixels need a GPU
- a **Build panel** configures and builds the open project by driving its own CMake, shows the
  commands before running them, and tails the build log
- **audio preview**: an artist can hear a clip from the inspector, either as the component will
  play it or as the file was imported
- a **Diagnostics panel** reports what this build actually is: its UI toolkit, its viewport
  backend, that backend's real capability set queried from the device, and the player builds
  found beside it
- **sprite animation**: a clip of sheet frame indices, an inspector preview that plays, pauses
  and steps, and the viewport drawing the same frame — with playback deliberately outside the
  document
- **tilemaps**: a `CNA.Tilemap` component, viewport brush, eraser, eyedropper and rectangle fill
  with one undo entry per stroke, and rendering in the content pass so tiles and sprites sort
  against each other
- **prefabs**: Create Prefab from a hierarchy row writes a `.cnaprefab` and makes the original
  an instance; dropping one from the browser instantiates it; the inspector reports what an
  instance has changed and offers Revert and Apply. Every step undoes, file writes included
- **layers and tags**: layers are a project-level ordered list that drives the `CNA.Layer`
  choices, editable and undoable from the Inspector; tags are a `List<String>` component
- **list properties** exist end to end: declared element type, JSON array encoding, and an
  inspector block that adds, removes and reorders — each press its own undo entry
- **edits reach the running game**: a property change, its undo, and a texture changed on disk
  are all pushed to the live `cna-player` over the bridge, verified against a real process
- **play mode works end to end**: the Play toolbar launches a real `cna-player` on a backend chosen
  from the binaries actually installed, drives it through Pause, Step and Stop over a real loopback
  socket, and routes the game's own log into the console — and the player **draws the scene**, in
  its own window, through its own camera, so what the game shows can be looked at and captured
- the **asset browser** shows a derived folder tree with thumbnails and a filter; renaming or
  moving an asset keeps its id, so no scene is touched (D-08), and both are undoable
- a **header-only loader** lets a shipped game read a `.cnascene` without the editor
  ([`docs/DESIGN-SCENE-LOADER.md`](docs/DESIGN-SCENE-LOADER.md)), verified by `scene-loader-demo`
  against the real example project
- a **History panel** shows the undo stack and jumps to any point in it, undone entries included
- **a file from an older build can be upgraded**: the migration chain runs on every load of a
  `.cnascene`, `.cnaproject` and `.cnaasset`, and is empty because nothing has changed yet
- **unsaved work survives a crash**: a `.cnarecovery` snapshot is written every thirty seconds
  while the document differs from its file, and offered — never silently applied — on reopen
  ([`docs/FORMATS.md`](docs/FORMATS.md))
- a **Validation panel** reports what a scene is doing wrong before a build does: broken asset
  references, two cameras both claiming to be primary, a zero scale, an entity that does nothing.
  Every rule describes a legal state, so nothing is refused and nothing is repaired automatically
- **CI** builds and tests on Linux under GCC Debug and Clang Release, both at `-Werror`
- `./build/cna-editor --headless --project=examples/HelloSprites/HelloSprites.cnaproject` opens a
  project, scans its assets, loads a three-entity scene and draws a frame

**Built and run against a real CNA checkout.** With `-DCNA_EDITOR_WITH_CNA=ON` and
`CNA_GRAPHICS_BACKEND=SOFTWARE`, `cna-editor` opens a window, docks its nine panels, and draws them
entirely through CNA's *public* API — no `CNA::Internal::*`, no authored shader, no per-backend
renderer:

```
cna-editor: backend SOFTWARE, 56 frames, 1600x900 display, 14 draw calls, 1858 triangles,
            1 textures created, 0 texture updates, 0 commands clipped away
```

`--screenshot=PATH` writes a PNG of the final frame through `GraphicsDevice::GetBackBufferData` and
`Texture2D::SaveAsPng`, so the CI smoke test asserts on an image rather than on a clean exit — a
window that opens blank and one that works look identical from the outside otherwise.

Report: [`docs/SPIKE-IMGUI-CNA.md`](docs/SPIKE-IMGUI-CNA.md), including the one real rendering bug
this turned up and how it was tracked down (ED-119).

---

## Module map

| Module | Depends on | CNA? | Purpose |
|--------|-----------|:----:|---------|
| `cna-editor-core` | — | no | Uuid, JSON, `PropertyValue`, `ComponentDescriptor`, `EditorCommand`, `CommandHistory` |
| `cna-editor-scene` | core, assets, project, ui | no | `SceneDocument`, `EditorEntity`, scene commands, built-in components |
| `cna-editor-assets` | core | no | `AssetDatabase`, `.cnaasset` sidecars, importers |
| `cna-editor-project` | core | no | `.cnaproject`, the backend capability table |
| `cna-editor-ui` | core | no | `EditorUi` abstraction, `NullEditorUi`, `UiDrawData`, `UiInputState` |
| `cna-editor-ui-imgui` | ui | no | `ImGuiEditorUi` — the Dear ImGui implementation (D-14) |
| `cna-editor-runtime-bridge` | core | no | `EditorProtocol`, `MessageChannel` (TCP), `PlayerProcess` |
| `cna-editor-player` | scene, assets, project, bridge | no | `PlayerHost` — the player's protocol and state machine (D-15) |
| `cna-editor-plugins` | core | no | Manifest discovery, validation, dynamic loading |
| `cna-editor-context` | scene, assets, project, ui | no | `EditorContext` — the composition layer |
| `cna-editor-viewport` | scene, ui | **yes** | The only module that links CNA: scene viewport, `CnaUiRenderer`, `CnaUiPlatform` (D-01, D-03) |
| `cna-editor` | context, ui-imgui, plugins, bridge, viewport | via viewport | The editor application, and one class per panel under `src/panels/` |
| `cna-player` | player, viewport | via viewport | The player executable; one build per backend (F-01, D-15) |

The layering is enforced by the build graph, not by review: a stray `#include <Microsoft/Xna/...>`
outside `cna-editor-viewport` fails to compile.

---

## Phase −1 — Foundation ✅

Complete. Everything below is implemented, tested and building.

| Id | Task | Status |
|----|------|:------:|
| ED-001 | Repository, CMake, C++23, MS-PL SPDX headers, Doxygen conventions | ✅ |
| ED-002 | `Uuid` — v4 generation, canonical parse and format, nil sentinel | ✅ |
| ED-003 | Dependency-free JSON reader and writer, order-preserving, diff-stable (D-12) | ✅ |
| ED-004 | `EditorMath` — the editor's own POD vector/quaternion/colour/rectangle types (D-13) | ✅ |
| ED-005 | `PropertyValue` — 13-alternative tagged union with JSON round-trip (D-05) | ✅ |
| ED-006 | `ComponentDescriptor`, `PropertyDescriptor`, `ComponentRegistry` (D-05) | ✅ |
| ED-007 | `EditorCommand` and `CommandHistory` — undo, redo, merging, dirty tracking, bounded retention (D-06) | ✅ |
| ED-008 | `EditorEntity`, `EditorComponent`, editor-only state (D-04, D-07) | ✅ |
| ED-009 | `SceneDocument` — hierarchy, cycle rejection, recursive delete, JSON round-trip | ✅ |
| ED-010 | Seven scene commands, all undoable, `SetProperty` merging on a stable key | ✅ |
| ED-011 | Six built-in component descriptors | ✅ |
| ED-012 | `AssetDatabase` — stable ids, `.cnaasset` sidecars, move and missing detection (D-08) | ✅ |
| ED-013 | `Project` — `.cnaproject`, `ProjectKind`, the 14-backend capability table (D-10, F-02) | ✅ |
| ED-014 | `EditorUi` abstraction and `NullEditorUi` (D-02) | ✅ |
| ED-015 | `EditorViewport` abstraction and `NullEditorViewport` | ✅ |
| ED-016 | `EditorProtocol` and `MessageStreamDecoder` (D-09) | ✅ |
| ED-017 | `PluginManifest` and `PluginHost` discovery and validation (D-11) | ✅ |
| ED-018 | `EditorContext` — the composition layer | ✅ |
| ED-019 | `EditorApplication`, argument parsing, six panels, headless frame loop | ✅ |
| ED-020 | Test harness and 69 tests; CTest registration | ✅ |
| ED-021 | `examples/HelloSprites` — a real project the editor opens end to end | ✅ |
| ED-022 | `UiDrawData` and `UiInputState` — the toolkit boundary (D-14) | ✅ |
| ED-023 | `MessageChannel` — non-blocking loopback TCP transport, POSIX and Winsock | ✅ |

**Two real bugs were found and fixed during this phase**, both recorded in ANALYSIS.md:

- The asset modification stamp used the filesystem clock's native tick count (~4.6 × 10¹⁸ ns),
  which is outside `double`'s exact-integer range. Since JSON numbers are doubles, it round-tripped
  through the sidecar *changed*, and every asset would have looked modified on every scan. Now
  stored in seconds; guarded by `AssetSidecarStampSurvivesAJsonRoundTrip`.
- `EditorContext` sat in `cna-editor-core` while depending on scene, assets and project — a
  dependency cycle. It now lives in its own `cna-editor-context` module above them.

---

## Phase 0 — Technical prototype ✅

**Goal.** Prove CNA can host an editor UI. One window, docked panels, one CNA-rendered viewport.

**Status.** Done except the viewport's own scene drawing (ED-120…ED-122), which is Phase 1 work.
The editor opens, docks and renders through CNA's public API, verified by screenshot.

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-100 | **Spike: can Dear ImGui render through CNA's *public* API?** | ✅ | **Yes, completely.** Every capability is present, including proper text input via `TextInputEXT` — the one I expected to be missing. One implementation serves every backend; no per-backend renderer, no authored shader. Report: [`docs/SPIKE-IMGUI-CNA.md`](docs/SPIKE-IMGUI-CNA.md) |
| ED-101 | Vendor Dear ImGui (docking branch) into `third_party/` | ✅ | 1.92.9b, core only — none of ImGui's own backends, because we supply our own |
| ED-110 | `ImGuiEditorUi` — implements every `EditorUi` method | ✅ | Zero panel changes were needed: the D-02 payoff, collected |
| ED-113 | `propertyField` widgets for all 13 property types | ✅ | Sliders, combo boxes, colour picker, drag fields. Quaternion-as-Euler is deferred — see note below |
| ED-116 | `CnaUiRenderer` — draws `UiDrawData` through CNA's public graphics API | ✅ | Compiles against real CNA headers |
| ED-117 | `CnaUiPlatform` — fills `UiInputState` from CNA's public input API | ✅ | Text via `TextInputEXT`, never synthesised from key codes |
| ED-130 | Report the CNA public-API gaps the spike uncovered | ✅ | Two, both minor: `Color` is not default-constructible (G-01); clipboard needs `CNA_DEVICES` (G-02) |
| ED-102 | `CNA_EDITOR_WITH_CNA=ON` build verified against real `../cna` + `../sharp-runtime` checkouts | ✅ | Fully built and linked, not just compile-checked. Needed SDL3's X11 dev packages and FFmpeg dev headers; CNA's `FATAL_ERROR` guidance for the sibling checkouts proved accurate |
| ED-111 | **Window, graphics device, event loop; `--ui=imgui` becomes real** | ✅ | `runEditorInWindow` hosts the editor in a `Microsoft::Xna::Framework::Game`. The `Game` subclass stays inside the `.cpp` so CNA remains a *private* link dependency |
| ED-112 | Default dock layout on first run; user's saved layout respected thereafter | ✅ | ImGui does not place windows into a dock space by itself — without this every panel floated stacked at the same position |
| ED-114 | Console panel: severity filter, scroll-lock, copy | ✅ | Copy takes what the filter is showing, not everything — copying hidden messages would be a surprise. Auto-scroll only follows the tail when already at it, so scrolling up to read an error is not undone by the next frame's logging |
| ED-119 | **Leading glyph missing from docked tab labels** | ✅ | Fixed. Texture uploads happened in the draw phase, but Dear ImGui marks a request satisfied the instant it is issued and a fixed-timestep loop runs many update frames without a draw — so glyphs first needed on such a frame were acknowledged and never uploaded. Uploads moved into the update phase; guarded by `ImGuiUiRequestsAnUpdateWhenNewGlyphsAppear`. Full write-up: docs/SPIKE-IMGUI-CNA.md §8 |
| ED-124 | Editor verified on a second backend (EASYGL, real OpenGL ES 3.2 under Xvfb) | ✅ | Pixel-identical output to SOFTWARE — confirmed by hand then, and checked automatically now: ED-510's comparison mode reports the two backends drawing the *game* differ on 0.05% of pixels, all of them on anti-aliased sprite outlines |
| ED-123 | `--screenshot=PATH` and the `CnaEditorWindowSmoke` CTest | ✅ | The mechanism ED-510's backend comparison mode captures through, now also on `cna-player` (ED-246) |
| ED-115 | Persistent `DynamicVertexBuffer`/`DynamicIndexBuffer` in `CnaUiRenderer` | ⛔ | Deferred: `DrawUserIndexedPrimitives` re-uploads per call, which is fine at 20–60 commands a frame. Profiling should ask for this before anyone does it |
| ED-118 | Quaternion inspector as Euler angles | ✅ | XNA's own `CreateFromYawPitchRoll` convention, so the angles shown are the ones the game produces. The angles the user typed are cached against the quaternion they produced, so the two fields beside the one being edited cannot jump to an equivalent spelling mid-edit — and the cache stops applying the instant an undo, a gizmo drag or a reload writes a value it did not produce |
| ED-120 | `CnaSceneRenderer` draws sprites through `SpriteBatch` | ✅ | Layer depth honoured via `SpriteSortMode::BackToFront`, parent transforms composed, tint/origin/flip applied. A sprite whose texture will not load draws a placeholder at the bounds the picker uses, so what you click and what you see agree |
| ED-121 | Viewport renders into an offscreen target composited into the dock | ✅ | `RenderTarget2D` shared with the UI renderer as a borrowed texture, so the panel draws it with no per-frame blit. Turned up gap G-03 |
| ED-122 | Editor camera: pan, zoom, frame-selection | ✅ | `EditorCamera2D` implemented and tested (pan tracks the cursor at any zoom, wheel-zoom anchors under the pointer, framing respects margins). Wheel and drag are wired to the viewport panel, and `F` frames the selection — including entities with no drawable geometry, which centre rather than doing nothing |
| ED-125 | Grid with adaptive 1-2-5 spacing, major lines and axes | ✅ | Fixed world spacing is unusable: solid at low zoom, invisible at high |
| ED-126 | Click-to-select in the viewport, via ray-cast picking | ✅ | ED-206 in substance; `pickEntityAt` honours layer depth, parent transforms and disabled entities |

**Exit criterion.** `cna-editor --project=examples/HelloSprites/HelloSprites.cnaproject` opens a
real window, shows five docked panels, and renders the three-entity scene in the viewport.

---

## Phase 1 — Usable 2D MVP ✅

**Goal.** The minimum milestone from the original discussion, in full.

**Status.** Complete. Every exit criterion below is met and covered by a test: a project opens, the
panels dock, a JSON scene loads, an object is selected in the viewport or the hierarchy, its
position changes in the inspector, undo returns it, the scene saves, and it runs in a separate
`cna-player` process. Q-02 is answered, so a shipped game can consume a scene too
([`docs/DESIGN-SCENE-LOADER.md`](docs/DESIGN-SCENE-LOADER.md)).

### Editing

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-200 | Hierarchy panel: rename in place, drag-to-reparent, context menu, multi-select | ✅ | Every operation through a command (D-06). Structural changes are deferred to the end of the frame — a reparent reorders the child lists the recursion is walking. An empty rename is treated as a slip and keeps the old name; dropping a parent onto its own descendant is refused with a reason rather than pushing a command that does nothing. Deleting or duplicating a whole selection is **one** undo entry, through a `CompositeCommand` that undoes its parts in reverse — one per entity would take N presses of Ctrl+Z and would walk the document back through arrangements it was never in. A multi-selection is now also *manipulable*: the gizmo sits on the selection's shared pivot, drags move, turn and scale every member about it, and the whole drag is one `TransformEntitiesCommand` -- one undo entry, since a command per entity would take N presses of Ctrl+Z and would pass through arrangements the scene was never in. Descendants of selected entities are left alone: a child is carried by its parent, and transforming both moves it twice |
| ED-201 | Sprite rendering resolves textures through `AssetDatabase`, ordered by layer depth | ✅ | Done as part of ED-121. `SpriteSortMode::BackToFront` honours `layerDepth` on XNA's own convention (0 front, 1 back), so the editor's ordering is the one the game will see. A texture that fails to load is remembered as failed, costing one attempt rather than one per frame |
| ED-202 | Grid with adaptive spacing | ✅ | Done as ED-125 |
| ED-203 | Selection outline as an overlay pass | ✅ | Drawn in a third `SpriteBatch` pass after the content, never as scene geometry |
| ED-204 | Icons for entities the viewport cannot draw | ✅ | Cameras, lights, audio sources — and, until ED-402, model renderers. Sized in screen pixels so they survive any zoom, and the picker tests them *after* sprites so an icon over the level art still wins its own badge. The badge outline doubles as the selection feedback, which is the only kind an entity with no bounds can get |
| ED-205 | Translate gizmo, with merged undo across the drag | ✅ | Geometry, hit-testing and the drag are CNA-free and unit-tested; the drag measures from the grab point rather than accumulating, so it cannot drift. One drag is one undo entry — the first edit opens it and the rest merge in, which is also what keeps two separate drags of the same entity from collapsing together |
| ED-206 | Ray-cast picking against entity bounds | ✅ | Done as ED-126. CNA-free and unit-tested, so "clicking selects the wrong thing" is caught in CI rather than by hand |
| ED-207 | Inspector: add and remove components, respecting `unique` and `required` | ✅ | The picker lists only what can actually be added, so a `unique` component already present is never on offer. A `required` component gets no Remove button rather than a dead one. Removal is by index, not by type — clicking Remove on the second audio source has to delete that one, and the two are indistinguishable afterwards if it does not |
| ED-208 | Asset drag-and-drop from the browser onto a sprite slot | ✅ | A slot declares the asset kind it takes and refuses anything else, naming both kinds. Accepting a sound into a texture slot would give a scene that loads and a sprite that never appears, with nothing to explain it. The kind is a string on `PropertyDescriptor`, not an `AssetType`, so the descriptor header stays below the asset database and a plugin can name a kind the editor never saw |
| ED-209 | Keyboard shortcuts: Ctrl+Z/Y/S/N/D, Delete, F to frame, W/E/R for gizmo modes, X for the gizmo space | ✅ | Each shortcut and its menu item call the same method, so the two cannot drift apart. Modifiers are matched exactly, so Ctrl+Shift+Z does not also fire Ctrl+Z's undo, and every shortcut is suppressed while a text field has the keyboard. `X` toggles rather than selecting — there are two spaces, so a toggle needs no second binding to get back — and says which one it landed on, since the two look identical on an unrotated entity |
| ED-210 | Split the panels out of `EditorApplication` into their own classes | ✅ | Six panel classes, each owning its own view state. `EditorApplication.cpp` went from 1207 lines to 523 and keeps only what no single panel owns. A panel reaches shared operations through `EditorActions`, a deliberately narrow interface — everything on it is something the menu bar, a shortcut and at least one panel all trigger, so they cannot drift apart |

### Assets

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-220 | Asset browser: folder tree, thumbnails, filtering, rename, move | ✅ | The tree is *derived* from the source paths rather than stored, so moving an asset changes one string and nothing has to be kept in sync. A move keeps the asset's id and is verified not to change a scene file by a single byte (D-08); the source and its sidecar move together or the move is rolled back, because an orphaned file gets a fresh id on the next scan and silently breaks every reference to it |
| ED-221 | Texture importer: dimensions, mipmaps, premultiplied alpha, thumbnails | ✅ | Settings declared and editable; dimensions read from the file's header (PNG and BMP state theirs at fixed offsets, so a few dozen bytes answer it — JPEG needs segment walking and belongs with the importer that will decode it for real). Thumbnails reuse the scene renderer's texture cache, so a preview costs nothing once the sprite using it has been drawn, and are fitted to their real aspect using the recorded pixel size |
| ED-222 | Importer settings surfaced in the inspector, reusing the descriptor system | ✅ | An importer's settings are a named list of typed, defaulted fields — which is what `ComponentDescriptor` already describes, so the inspector needed no new code and a plugin's importer is editable on the same terms as a built-in one. Edits are commands on the same history as the scene's (D-06): an editor where some edits undo and others quietly do not is worse than one where none do |
| ED-223 | Filesystem watcher; reimport on external change | ✅ | Polling, deliberately: inotify, kqueue and ReadDirectoryChangesW are three implementations with three failure modes, and each still needs a polling fallback for the network and container mounts where a team's assets often live. The clock is passed in, so the tests advance time exactly and never sleep. A change drops the viewport's cached texture — without that the editor reports the edit and goes on drawing the art from before it |
| ED-224 | "Missing references" report, with a relink dialog | ✅ | Answers the question `AssetDatabase::getMissingAssets()` does not: which *entities* point at something that will not load. The two overlap but neither contains the other — an id deleted from the database is invisible to that call, and an unused asset whose file vanished is harmless here. Grouped by asset id, because the breakage is almost always one asset that many entities share; relinking is one command, so it undoes in one press. A row is a drop target for the browser |

### Play mode

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-240 | `cna-player` host: loads a project and a scene, speaks the protocol | ✅ | Lives in this repository (Q-04 resolved, decision D-15). `PlayerHost` is CNA-free, so the whole message surface is unit-tested headless |
| ED-241 | Process spawn and lifetime; a player crash is reported, never fatal to the editor | ✅ | `PlayerProcess`, POSIX and Windows. `MSG_NOSIGNAL` so a dead player cannot `SIGPIPE` the editor; children are reaped so a long session cannot leak zombies |
| ED-242 | Socket transport over `MessageStreamDecoder` | ✅ | `MessageChannel`: non-blocking loopback TCP, ephemeral port, listen-before-spawn so there is no connect race |
| ED-243 | Play / Pause / Step / Stop, with the player's log routed into the console | ✅ | The toolbar sits above the viewport image; the player's own log comes back through `ReportLog` and lands in the console with its severity preserved |
| ED-244 | Player discovery: find installed `cna-player-<backend>` binaries and offer only those | ✅ | `discoverPlayerBuilds()`. Direct consequence of F-01: the editor offers only backends that actually exist |
| ED-245 | Editor-side Play toolbar wired to `PlayerProcess` | ✅ | Backend chosen from the player binaries actually installed beside the editor, never from the fourteen CNA knows about. A dirty scene is saved first and the console says so — the player is a separate process reading from disk, so playing what is on screen means writing it there. `TheEditorDrivesARealPlayerThroughPlayPauseStepAndStop` covers it against a real process and a real socket |
| ED-246 | `cna-player` draws the scene it loaded | ✅ | Added because its absence made three finished rows only half true: play mode ran the game with nothing on screen, and a hot-reload (ED-306) or a live edit (ED-307) was observable in a log rather than in a picture. The window host is a `Game` subclass hidden behind a free function, exactly like the editor's, so CNA stays a private link dependency of one module. It draws through the *same* `CnaSceneRenderer` the editor's viewport uses — `renderGameView` runs the content pass and skips the grid and overlay passes — because a second renderer would be a second answer to "what does this look like when it runs", which is the one property the editor exists to guarantee. The view comes from the scene's own primary camera, `orthographicSize` read as the visible height, and the clear colour from that camera too. A screenshot is queued by the CNA-free half and taken by the frame loop that owns the device, so `screenshotReady` is sent once pixels are on disk rather than when the request arrived. No window to be had — a headless container, no X server — degrades to running with nothing drawn and says so, rather than taking the editor's play session down with it |
| ED-250 | **How a game consumes a compiled scene** | ✅ | **Q-02 resolved: a header-only loader shipped from this repository**, so CNA never has to know what a `.cnascene` is and moving it into CNA later stays a relocation rather than a rewrite. Design and the options rejected: [`docs/DESIGN-SCENE-LOADER.md`](docs/DESIGN-SCENE-LOADER.md). `examples/SceneLoaderDemo` is both the documentation and the integration test — it opens a real device, loads `HelloSprites`, and checks the composed world transforms against what the editor's own code produces |

**Exit criterion.** The original minimum milestone, verbatim: *open a project, show docked panels,
load a JSON scene with three sprites, select an object in the viewport or hierarchy, change its
position in the inspector, undo, save the scene, and run it in a separate CNA Player process.*

---

## Phase 2 — Production 2D editor 🔄

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-300 | Prefabs: create, instantiate, override, apply | ✅ | `.cnaprefab` reuses the scene's entity encoding through the extracted `EntityJson.hpp`, because an instantiated prefab and a hand-authored entity must be indistinguishable once they are in a scene. **Overrides are computed, not stored**: the scene holds the instance's real values and "what changed?" is a comparison. A stored list would be a second description of the same fact, free to disagree — and that disagreement surfaces as a property reverting to a value the user never chose. It also meant prefabs added *no* field to the scene format. Revert removes user-added entities on purpose: a revert that kept some changes is not a revert, and whoever wanted a partial one has undo. Apply maps the instance's ids *back* through the links before writing, or every link would name an entity the file no longer has; it strips the instance bookkeeping, or every future instance would be born claiming to be an instance of something else |
| ED-301 | Tilemap component and tile-painting tool | ✅ | The grid is a flat `List<Integer>` on an ordinary component — no new serialised structure, so a scene holding a tilemap is readable by anything that could already read a scene. A paint stroke is **one** undo entry however many cells it crosses, and the merge key carries the *stroke* as well as the property: entity + property alone cannot tell two drags apart, and one Ctrl+Z would lose both. Painting is an `EditorTool`, deliberately not a `GizmoMode` — a gizmo mode picks which manipulator acts on the selection, a tool decides whether a press manipulates anything at all. While a brush is active it also suppresses the gizmo, because a tilemap's gizmo sits over its own first tiles and the first stroke would otherwise drag the map. Five tools: paint, erase, an eyedropper that hands the brush back to the paint tool (picking a tile is never the goal, painting with it is), and a rectangle fill applied on release as one undo entry |
| ED-302 | `SpriteFont` preview and importer settings | 🔄 | **The description is read and reported; the glyph preview is blocked on CNA (gap G-04).** Every field the importer declares is *read-only*, and that is the design: a `.spritefont` is the content pipeline's own input, so an editable copy in the sidecar would be a second answer to a question the build asks the file. What the editor adds is visibility — "what font is this and does it cover the characters I need" is otherwise an XML file away. Read by a targeted tag scan rather than an XML parser: the schema is fixed, tiny and machine-written, and a dependency to read six fields would be the larger risk. Rendering actual glyphs needs a built `SpriteFont`, and CNA exposes no public way to make one from a `.spritefont` |
| ED-303 | Sprite animation editor with a timeline | ✅ | A frame is an *index into a sheet*, not a rectangle: a sheet is a uniform grid in every case anyone authors by hand, an index is far smaller to author, and it is the same arithmetic the tilemap already does. The frame list is an ordinary `List<Integer>`, so reordering and adding frames needed no new widget. **Playback is not in the document** (D-07) — it is a plain value the panel owns and throws away, because a scene recording the frame an artist happened to be paused on would carry it into every save and every diff. The clock is passed in, so tests step it exactly and never sleep. The viewport draws the previewed frame because the *result* is published through `EditorActions` while the playback stays in the panel — the same shape the selection uses, and for the same reason. An animated sprite is also sized by its frame rather than by its sheet, or a sixteen-frame walk cycle would be sixteen times too wide to click. Per-frame durations are an **optional** parallel list, ignored unless it is exactly as long as the frame list: a clip that never needed a hold on one frame should not carry a list of identical numbers, and no existing scene has one at all |
| ED-304 | Audio source and listener editing, with preview playback | ✅ | The preview plays through CNA's **public** `SoundEffect(path)` constructor — no content pipeline, so unlike the sprite-font preview (G-04) there was nothing forbidden in the way. `EditorAudio` sits beside `EditorViewport` in the one CNA-linking module: the name is narrower than the job, but inventing a second CNA-linking module to widen it would trade an awkward name for the one rule the build graph enforces. The component preview uses the component's **own** volume, pitch and pan, because a preview at some other level is a preview of a different sound; the asset preview uses neutral ones, since that is the file as imported. `CNA.AudioListener` takes its position from the entity's Transform rather than repeating it, and a second enabled listener is an error for the same reason a second primary camera is. The preview holds a **`SoundEffectInstance`** rather than calling the fire-and-forget `SoundEffect::Play()`: that call hands back no handle, so Stop could only ever change the editor's belief about what was audible and a finished clip went on being reported as playing. `isPlaying()` now asks the device |
| ED-305 | Layers and tags | ✅ | Layers live in the `.cnaproject`, because one named in one level and missing from the next would make moving an entity between them silently change what it is; the order is the meaning, so it is a list. The list drives `CNA.Layer`'s choices by re-registering the descriptor — exactly the escape hatch `ComponentRegistry` documents. Renaming a layer leaves entities holding the old name **on purpose**: which of the remaining layers they meant is the user's decision, and the Validation panel reports it rather than the editor guessing. Tags are their own component holding a `List<String>`, not fields on `EditorEntity` — a tag is a game concept and the entity type deliberately is not one (D-04). The idle Inspector now edits project settings instead of saying "Nothing selected", and the layer edit is undoable and written through |
| ED-306 | Asset hot-reload into a running player over the bridge | ✅ | The watcher already noticed the file and the editor already dropped its own cached texture; what was missing was telling the player. Sent **by id**, like everything else on this wire (D-08), so a reload survives the file being renamed between the change and the message, and both sides resolve it through the database they each scanned. The player rescans before looking the id up — a record still carrying the old stamp would make its next scan think the asset changed again. `PlayerHost::takeReloadedAssets()` is the seam the CNA-linked half drains to drop its caches, which ED-246 now does every frame — so a texture changed on disk is visible in the running game rather than only in its log |
| ED-307 | Live property editing into a running player | ✅ | One hook, because every document change goes through a command (D-06) — there is no second path an edit could take. `EditorContext` gained a command observer; the application mirrors a `SetPropertyCommand` and leaves everything else alone rather than guessing, since a partially applied scene in the player is worse than a stale one. Undo and redo mirror too: a player that saw the edit but not its reversal would be showing a state that exists nowhere. The value is read from the **document**, not from the command, because after an undo the live value is the old one |
| ED-308 | Build and publish dialog driving CNA's own CMake targets | ✅ | Three decisions. The editor **shells out to `cmake`** rather than writing a script, because that gives it the exit code to report and the output to show — and the machinery already existed, since play mode supervises a child process. It drives the **project's own** `CMakeLists`, contributing only what it actually knows: the backend and the output directory. And a **missing toolchain is reported before the button is offered**, because anyone who installed an editor and not a compiler is the common case, and CMake's own message for it says nothing a user can act on. The exact commands are shown before they run, so someone who needs an option the editor does not model can take the command away. `planBuild` is pure, which is what makes the interesting half testable with no compiler on the machine |
| ED-309 | Backend diagnostics: report the current build's `GraphicsCapability` set | ✅ | Asked of the **device**, not derived from the backend's name: several of these vary by driver within one backend, so a table keyed on the backend would confidently report what the machine cannot do. Verified on EASYGL, which correctly reports no wireframe fill mode — OpenGL ES has no `glPolygonMode`, and a name-keyed table would have claimed otherwise. The panel also lists the player builds discovery found and the whole backend table, because with a compile-time backend (F-01) "which of these can I actually run" is a real question with a real answer. Capabilities cross the module boundary as strings, so nothing outside the CNA-linking module knows `GraphicsCapability` exists and the list keeps working when CNA adds an entry |
| ED-310 | Scene validation: missing references, duplicate primary cameras, zero scale, empty entities | ✅ | `SceneValidation.hpp` holds the structural rules — including two for states that otherwise fail in silence: a tilemap whose tile size is zero (draws nothing *and* swallows every brush stroke) and an animation with no sheet or no frames (the sprite beside it draws a placeholder, which looks exactly like a broken asset reference and is a different problem); missing references stay in `MissingReferences.hpp` because they need the asset database and the rules do not. Both report into one **Validation** panel: a user whose scene misbehaves does not know in advance which of the two is at fault. Every rule describes a *legal* state, so nothing refuses to save and nothing is repaired automatically — a rule that fired on a scene the user meant to write would be worse than no rule. Clicking an issue selects the entity |
| ED-311 | `PropertyType::List` and `NestedStructure`, with inspector support | ✅ | **Both, at last.** `List` landed in Phase 2; `NestedStructure` stayed deliberately unbuilt for three phases because nothing needed it -- prefab overrides turned out to be computed rather than stored (ED-300), so the schema it was expected to serve never existed. ED-410 is the consumer it was waiting for, and the shape it took is the shape that consumer asked for: named fields of declared types, one level deep, encoded as a plain JSON object. Waiting was right. A schema designed in Phase 2 against no consumer would have been designed for prefab overrides, which never needed one |
| ED-320 | GPU picking through an id render target | ⛔ |

**ED-320 deferred.** Ray-cast picking (ED-206) is correct and needs no render target or GPU
read-back. This is an optimisation to reach for when profiling says so, not before — and it would
have to be written per backend, which is exactly the kind of cost F-01 makes worth avoiding.

---

## Phase 3 — Basic 3D ✅

**Precondition.** CNA's 3D API is stable and the glTF conformance work is done. This phase comes
*after* that, as the original discussion argued — not before. **The owner opened ED-400 on
2026-08-04**, on the grounds that CNA does ship `BasicEffect`, `VertexBuffer`/`DynamicVertexBuffer`
and the rest of the Graphics surface a camera needs. **ED-401 was the other exception and was built early**, because it is not a 3D task at all in a 2D viewport: `E` and `R` had been selecting
a manipulator that did not exist since Phase 1, which is a promise the editor was making and not
keeping. **ED-408 and ED-409 followed ED-400** for the same kind of reason: a view you can select
in but not move things in is a viewer rather than an editor. **ED-405 broke the deadlock the other
eight were in**: they waited on a model pipeline, and it was the importer that had to exist first,
because nothing can draw a mesh before something has read one. Five rows of thirteen are done, and
**ED-402 then closed**, and with it the lighting half of ED-404: the 3D view draws solid, lit
models instead of wire boxes, which is what the owner asked for after seeing the wireframe
screenshots. Seven rows of thirteen are done or part-done, and nothing left in this phase is
blocked on anything in this repository.

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-400 | Perspective and orthographic viewport camera, orbit and fly navigation | ✅ | `EditorCamera3D` in `cna-editor-scene`, CNA-free like the 2D one and tested the same way; `EditorMatrix` in core is the arithmetic under it, mirroring XNA's `Matrix` field for field. Orbit and fly are not modes but two ways of moving one state -- pivot, distance, yaw, pitch -- so a user can orbit, fly and orbit again without the camera turning about a point it left minutes ago. What the view draws is `SceneWireframe.hpp`, also CNA-free and also tested; the renderer only strokes the segments it is handed. `--view=2d` or `3d` starts in either, which is what lets the result be photographed |
| ED-401 | Rotate and scale gizmos; local/world space toggle | ✅ | |
| ED-402 | `ModelRenderer` rendering | ✅ | **The 3D view draws solid, lit geometry**, split the way ED-400 split the wireframe: `SceneModels.hpp` decides what to draw, where, and lit by what -- CNA-free and tested in CI with no device -- and `CnaModelPass` uploads and draws it. That split is what makes the parts most likely to be wrong checkable at all: the world matrix, which mirror is applied, and whether an entity is lit from the right side are arithmetic, and a screenshot is a poor way to check arithmetic. **Four things were found by running it**, each costing a picture of an empty viewport: the offscreen target had no depth buffer (the 2D one needs none -- sprites sort by draw order, models sort per pixel) and `Clear(color)` does not clear depth in any case; `CullCounterClockwiseFace` is right *because* of the Y mirror rather than despite it, and was measured as well as reasoned -- the triangle count drops by 1113 on the example scene when it is on, which is the back faces going away; **`PbrEffect` draws nothing on EASYGL** (gap G-05), so the PBR path is written, complete and off behind one constant while `BasicEffect` renders correctly; and `PbrEffect::FillGpuDrawParams` sets `textureEnabled` unconditionally while binding `texture0` only when a texture exists, so a material with a base-colour factor and no map -- most hand-authored glTF -- samples nothing, worked around with a white texel, which is also the arithmetically correct answer since glTF multiplies factor by map |
| ED-403 | Material editing and preview | ✅ | `CNA.ModelRenderer` has declared a material reference since Phase 1 and nothing could satisfy it -- the inspector offered a picker with nothing to pick and the pass drew the mesh's own materials regardless. A **`.cnamaterial`** is what it points at now: a new format at version 1, so nothing already written gains or loses a field and every migration chain still has nothing to do. **The fields are `MeshMaterial`'s, deliberately** -- a material asset able to express more than an imported one would be one the pass handles twice, and the extra expressiveness is bounded by the same effect pair either way. What it adds is what an *asset* needs: a name a person chose, and textures by `Uuid` rather than by a path relative to some model file, since an id survives a rename (D-08). **It carries no id of its own**, and the first version did: an asset's identity is the `Uuid` in its sidecar, and a second id inside the file is a second answer to "which material is this" -- found by pointing the example's crate at the document's id and getting the unchanged crate back. An override replaces **every** part's material, which is the only thing one material can mean for a model of several; ED-410's per-mesh list is the other answer. Editing writes the file through a command that undoes by replaying the previous bytes (D-06, the shape ED-300's Apply established), merging per field so a dragged colour is one entry. Created from the browser, numbered until the name is free, followed by a rescan -- until that runs the file exists on disk and not in the database, which is a state nothing else is written to expect |
| ED-404 | Light components with viewport visualisation | ✅ | **Both halves.** `SceneLighting.hpp` reads `CNA.Light` -- a descriptor since Phase 1 that nothing had ever read -- and performs the reduction `IEffectLights` forces, since XNA's fixed-function rig is an ambient colour and exactly three directional lights with no point light, no spot and no cone. A point or spot becomes a directional light **aimed at whatever is being drawn** and dimmed by distance: position-dependent, which is what a point light *is*, with the honest limit that one large model is lit as though it sat at its own origin. Falloff reaches zero at the range rather than following inverse square, which never does -- physical is the wrong answer here, leaving a lamp tinting the far side of the level and the range control doing nothing visible. Where more than three apply the **three brightest at that point** win, not the nearest and not the first three. No lights, or none in range, asks for XNA's `EnableDefaultLighting()` rather than applying zero: black models read as a broken renderer, and the commonest scene is one a model was just dropped into. The **visualisation** is an arrow along the direction plus one ring at the range, the arrow sized in *pixels* converted at the light's depth as the manipulators are -- a fixed world length is invisible in a scene laid out in pixel-like hundreds, which the first attempt demonstrated. One ring in the scene's plane rather than three about the axes, since two of three would be edge-on in the view this editor opens in; a directional light gets none, because a boundary that means nothing is worse than no boundary |
| ED-405 | glTF importer built on CNA's own `cgltf` integration | ✅ | **The seam came first, as the row demanded**, and it is `MeshData.hpp` in `cna-editor-core` -- not beside the importer that produces it or the viewport that will draw it, because those two modules cannot see each other and must not need to. Three decisions are recorded there and both consumers inherit them: geometry arrives *already in the editor's world convention* (Y mirrored once, in the data, so no consumer has to remember), a vertex matches `VertexPositionNormalTexture` field for field so ED-402's upload is a copy rather than a translation, and there is deliberately **no node hierarchy** -- transforms are baked, because a schema with no consumer is the ED-311 mistake again. **cgltf is vendored** in `third_party/cgltf/` rather than reached for across the sibling checkout: the default build has no CNA in it (D-03), and CNA's own reader is `CNA::Internal::GltfImport`, which D-01 forbids. Its symbols are prefixed (`cgltf_prefixed.h`) because CNA vendors the same library inside `extern "C"`, and a build linking both fails outright -- a clash the standalone configuration and CI cannot see. **The seam is proved by a consumer, not by assertion**: `SceneWireframe` draws a `ModelRenderer`'s real mesh through `MeshProvider`, CNA-free and checked in CI with no GPU, so ED-402 inherits a `MeshData` already known to survive the trip |
| ED-406 | Mesh preview in the asset browser | ✅ | A model asset is *drawn* rather than decoded: `renderModelThumbnail` runs the ED-402 pass into its own square target with a camera framing the model's own bounds from three-quarters on -- head-on is the one angle at which a cube and a flat square are the same picture, which is exactly what a thumbnail exists to tell apart. Its **own** target rather than the viewport's, because the browser draws thumbnails in the same frame the viewport draws the scene and one target would have each overwriting the other's pixels between the two `image()` calls. Lit by the **default** rig rather than the open scene's: a thumbnail is a picture of the asset, and one that changed because somebody moved a lamp in the level they happen to have open would be a picture of something else. The geometry comes from the same `MeshCache` the 3D view uses, passed *in* rather than looked up -- the cache lives in `EditorContext` because a `MeshData` needs no CNA, and a viewport reaching for it would be the CNA-linking module depending on the context. Needed `--panel=TITLE` to be photographable at all, on the same argument as `--view=3d`: a panel sharing a tab bar with five others cannot be seen from a script otherwise, and the focus has to be applied *after* `DockBuilderFinish` or the last window docked into the node takes the tab back |
| ED-407 | Environment and fog settings | ✅ | Ambient light and linear fog, as a field on the **scene** rather than a component on some designated entity: a cave is dark and a moor is foggy, and neither is a property of anything standing in one. The alternative -- an "Environment" entity the scene must contain -- is how a scene comes to have a mandatory entity that must not be deleted, and how one missing it renders in a way nothing in the hierarchy explains. **Additive, in both directions**: a scene written before this has no `environment` object, reads as the defaults, and is written back out *without* one. That second half is the half that is easy to lose, and it has its own test -- a loader that read a default and then serialised it would turn every existing scene into a modified file the first time it was opened. What fog can do is bounded by `IEffectFog`: a colour and a linear band, no exponential and no height fog, because both effects implement that one interface. Sprites are fogged too, or a sprite would look like it was floating in front of the fog rather than standing in it. Edited from the idle Inspector beside the project settings, through a command like every other document change (D-06) that merges per *field*, so dragging a distance is one undo entry and changing the colour afterwards is a second. Fog switched on with an empty band is a Validation warning -- nothing is broken, both effects treat it as no fog, but a user who checked the box will otherwise keep checking it |
| ED-408 | 3D translate gizmo | ✅ | Added during the ED-400 session, because a view you can select in but not move things in is a viewer rather than an editor. `TransformGizmos3D.hpp`: a separate file from the 2D manipulators because almost nothing is shared -- those lay out in screen space against `EditorCamera2D`, this one lays out in the world and asks a different question each frame, *where along this world line is the cursor pointing*. Arms are sized in pixels and converted to world units at the entity's own depth, so the manipulator is the same size on screen wherever the entity is; an arm pointing at the camera is refused rather than answered, since a pixel of movement there would fling the entity across the level |
| ED-409 | 3D rotate and scale gizmos | ✅ | **Rotate** is three rings, *sampled and projected* rather than described, because a circle in the world is an ellipse on screen and the grab has to answer in pixels -- one polyline serves both the drawing and the hit-test, so what a user sees is exactly what they can take hold of. A ring seen edge-on is dropped: it would project to a line through the centre, overlap the other two, and have no plane a drag could measure an angle in. **Scale** is the one whose problem the other two do not have, and the resolution is that it does not need the world at all: a factor is a ratio of screen distances, and the screen always has one. So an arm is never dropped and never refuses -- its direction is the true projection of the axis, near-plane clipped through the wireframe's own `projectSegment`, and its length is floored at thirty pixels so an edge-on handle stays clear of the centre one and capped so an arm pointing at the eye does not throw its handle off the panel. Foreshortening fades toward a floor rather than to nothing. The centre handle (`GizmoAxis3D::All`) scales all three axes; scale takes no `GizmoSpace`, for the reason it takes none in 2D. All three manipulators act on a **whole selection** through `MultiTransform3D`, about the shared pivot the gizmo is drawn on |
| ED-410 | Per-mesh material lists (needs ED-311) | ✅ | And it is what finally built ED-311's `NestedStructure`, three phases after that row was opened -- which is the vindication of leaving it unbuilt rather than an argument against having waited. The element is a part name and a material reference, two fields of different types, which is precisely what no list of a single type can carry. **Keyed by part name, not by index**, and that is the decision the row turns on: indices are the obvious choice and fail *silently*, because a model reimported with a mesh added in the middle shifts every index after it and the override then points at a different part with nothing anywhere to notice. A name that matches nothing is detectable -- so entries matching nothing are **kept** rather than dropped, and `validateModelPartMaterials` reports them. A wrong answer that can be reported beats one that cannot. The single override and the list compose as "this whole model, except these parts". Reading goes through `propertyValueFromJson` rather than `PropertyValue::fromJson`, because decoding a structure needs the field schema and that lives on a descriptor; the read is driven by the **schema** and not by the file, so an omitted field takes its declared default and an undeclared one is dropped rather than written back in a shape nothing can read |
| ED-413 | Sprites drawn in the 3D view | ✅ | Not in the original plan; **the owner asked for it after ED-402**, and it is what turns the 3D view from a model viewer into a view of the scene. Until now it drew no sprites at all, and the reason was real -- `SpriteBatch` cannot draw the trapezoid a sprite becomes seen from an angle. ED-402 removed the obstacle rather than the reason: through a `VertexBuffer` a sprite is an ordinary textured quad. Three decisions, each with a plausible alternative. **A sprite lies in the scene's XY plane and does not billboard**: the picker, `computeEntityBounds3D` and the gizmos all treat it as a flat box there, so a billboarded sprite is drawn where it cannot be clicked -- and the *game* draws through `SpriteBatch`, so billboarding would show a picture the game never produces. A sprite seen edge-on is a line; that is the truth about a flat thing. **Only the Z rotation is honoured**, because that is all `SpriteBatch` applies: the 3D view may show more than the 2D one, never more than the game. **Depth-tested with depth writes off, culling off, unlit with the tint carried as emissive** -- a sprite's art has its lighting painted in, and running it through the models' directional rig would darken every sprite by wherever the sun happens to be. Drawn one quad at a time in back-to-front order rather than batched by texture, because batching reorders and order is the whole reason a transparent pass looks right |
| ED-411 | **Plugin dynamic loading**: `dlopen`/`LoadLibrary`, `extern "C"` entry, unload, hot-reload | ✅ | Behind the gate ED-017 built first, which was the point of building it first: an ABI mismatch that reaches `dlopen` is a crash rather than an error message (D-11). `loaded` and `active` are now two flags, because a manifest that passes every check can still fail to open -- a dependency of the plugin's own, a symbol it forgot to export -- and a manifest a user can fix and a build a user can fix are different problems. **Unload order is the thing that crashes when reversed**: `shutdown()` while the code is still mapped, then destroy through the plugin's *own* destroy function so allocation and deallocation happen in one runtime, then close the library. `EditorApplication` gained its only destructor for the same reason -- a plugin's `shutdown` is handed the context, so member destruction alone would release the handles without ever calling it. **A plugin must not link the editor's static libraries**, and that is the decision worth carrying: it would get its own copy of every registry, so the component it registered would go into a `ComponentRegistry` the editor has never heard of, and the failure reads as "initialize ran and nothing happened". It leaves the symbols undefined and the host resolves them, which is why `cna-editor` is built with `ENABLE_EXPORTS` -- without it a perfectly good plugin fails with "undefined symbol" naming a function the editor plainly contains. Tested against a **real** `.so` built beside the suite, because nothing that actually breaks here can be checked against a double |
| ED-412 | Plugin extension points: importers, component types, panels, menu commands, gizmos, exporters | ✅ | Six points, and the work was mostly deciding which needed anything. **Component types and importer settings needed nothing new** -- a plugin holds an `EditorContext` and `registerComponent` is already the supported way in, a path ED-305 relies on for the layer list rather than a door left open. **Panels and menu commands** needed `PluginExtensionRegistry`, because the editor draws its own from a fixed list and there was nowhere for a stranger to be added. **Exporters are a menu command that writes a file**; a registry of their own would invent a concept the editor does not have, and ED-520 wants "Export as MC3", which is a menu item. **Gizmos are deliberately not built**, and not for effort: a gizmo draws, hit-tests, owns a drag across frames and turns it into undoable commands -- a registry for one would have to expose the viewport's interaction loop, the editor's least settled surface. When a plugin needs one, *that* plugin is what it should be designed against, which is the argument that kept `NestedStructure` unbuilt until ED-410 asked and was right that time. Everything registered is owned by a plugin id and the host removes it on unload as a **backstop**, because a panel outliving its library is a `std::function` pointing into unmapped code that fails on the next frame drawn rather than at the moment of the mistake. Both loops iterate a *copy*: invoking a plugin command can unload the plugin that registered it |

**ED-400's boundary was worth stating, and ED-405 moved it.** The 3D view drew a ground grid and a
box per entity, and that was the honest whole of it while there was no mesh to read. There is one
now: a `ModelRenderer` whose asset has been imported is drawn as its own triangle edges, each edge
once, sampled at a stride when a dense model would not fit the segment budget so that what appears
is the whole shape drawn sparsely rather than one corner of it drawn completely. What has *not*
moved is sprites, and for the original reason: `SpriteBatch` cannot draw the trapezoid a sprite
becomes when seen from an angle. What the camera *does* answer is the question a 3D view exists for
-- where is everything, in relation to everything else -- and picking, framing and navigation all
work against it. Manipulating followed as its own tasks rather than as parameters on the 2D ones:
ED-408 for translate and ED-409 for rotate and scale, all three now acting on a whole selection
about its shared pivot. Entities that draw nothing get a screen-space badge per icon kind rather
than a wire box, keyed off the same `getEditorIconKind` the 2D viewport asks, so the two views
cannot disagree about what an entity is.

**What each manipulator needs from the world is what makes them three tasks and not one.** Translate
needs a *line* to slide along and refuses when the arm points at the camera; rotate needs a *plane*
to measure an angle in and drops a ring seen edge-on; scale needs neither, because a factor is a
ratio of screen distances. That is why the edge-on case, which the other two answer by refusing, is
survivable for scale -- and why its arms fade and shorten instead of disappearing.

One convention was chosen and must not drift: **the 3D camera is Y-down, like the 2D one and like
`SpriteBatch`**, and its grid is the scene's own XY plane rather than a ground plane under it. An
entity below another in the 2D viewport is below it in the 3D one, and a 3D camera at yaw and pitch
zero shows exactly what the 2D camera shows -- the user orbits away from a picture they recognise.

The arithmetic underneath stays Y-up and right-handed; what is mirrored is the *projection's* Y,
which is precisely the conversion between the two frames. That choice is deliberate and the
alternative is worth recording: mirroring the view's up vector instead is a 180-degree roll, which
fixes the vertical and puts world +X on the left. Because the mirror lives in the one
view-projection that `worldToScreen` and `screenToRay` both go through, picking, the gizmo and the
wireframe cannot disagree with what is drawn.

**The cost was billed to ED-402 and ED-405 paid it.** XNA's 3D side is Y-up: `CreateLookAt`,
`BasicEffect` and every loaded model assume it. The mirror is now applied **at import, into the
data**, rather than at draw time -- which is the choice worth recording, because the alternative
looked equivalent and is not. Mirroring in the model pass means the wireframe path and the CNA path
each have to remember, and the day one of them forgets is the day models are upside down in one
view and not the other; mirroring once in the importer means every consumer, present and future,
inherits a mesh that already stands the right way up.

The consequence that is easy to miss came with it: **mirroring one axis reverses the handedness of
every triangle in the file**, so the importer reverses the winding back. That is invisible in
wireframe and becomes visible the moment ED-402 turns backface culling on -- which is exactly why
it is pinned by a test now (`TriangleWindingIsReversedToSurviveTheYMirror`) rather than found then.
A node that is *already* mirrored -- a negative scale, which authoring tools produce routinely --
has flipped once already and the two cancel, so the test is on the sign of the whole transform
rather than on the mirror alone.

A ground-plane grid becomes the useful one now that models can stand on it, and it is built and
waiting: `WireframeOptions::gridPlane`, offered from the View menu while the 3D view is on. The
default stays the scene's own plane, because that is still where everything else this editor places
lives.

**ED-401** ships all three manipulators as one CNA-free module — layout, hit-test, drag — so what
a user can grab is tested in CI and only the pixels need a GPU. Three decisions inside it are worth
recording, because each had a plausible alternative:

- **Rotate turns in world space and stores the result in the parent's frame.** The cursor is
  describing a world angle; a child of a rotated parent that applied that angle locally would turn
  by a rotated fraction of it and visibly lag its own cursor.
- **Scale is a ratio in screen space, not a difference.** Scale is unitless, and only a ratio is
  independent of the zoom the drag happens to be at. Dragging through the pivot flips the entity
  rather than collapsing it — negative scale is a legitimate edit that XNA's own `SpriteBatch`
  honours — but never lands exactly on zero, which would make the entity invisible *and*
  unclickable.
- **Scale has no local/world toggle.** A non-uniform scale applied in world space needs a shear,
  which a position/rotation/scale transform cannot express. A "world scale" could only be a lie in
  the one place a user is entitled to exact numbers, so the arms are always the entity's own axes.

The space toggle itself (`X`) is one setting shared by every manipulator rather than one per gizmo:
it is a way of working, not a property of a tool.

**Snapping** arrived with the modifier keys the UI abstraction was missing. Holding Ctrl over the
viewport rounds a drag: translation to the *visible* grid — the same `chooseGridSpacing` the renderer
draws with, so a snapped entity always lands on a line that exists — rotation to 15 degrees, scale to
tenths. Three details are load-bearing. It is a **modifier, not a mode**, because "line this up with
that, now nudge it" happens seconds apart. The **result** is snapped rather than the movement, or an
entity that started at 3.7 would land on 13.7 instead of on 10 — except for rotation, where the
**turn** is snapped instead, since snapping the absolute angle would silently straighten whatever it
touched. And only the axes the handle allows: snapping one the drag deliberately constrained out
moves the entity along an axis the user just said not to touch, invisibly, because they are watching
the other arm. That last one was a bug the test caught.

The same modifier work gave the picker **Ctrl+click to add or remove**, which is how a multi-selection
is built everywhere else. Ctrl on empty space does nothing at all: clearing a selection somebody is
halfway through assembling is the one outcome they cannot have meant.

**ED-411** is where discovery-and-validation (ED-017, done) becomes real loading. Validation was
built first deliberately: an ABI mismatch that reaches `dlopen` is a crash, not an error message
(D-11).

---

## Phase 4 — Advanced tooling ⬜

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-500 | Animation timeline and curve editor | ⬜ | |
| ED-501 | Skeletal animation preview | ⬜ | |
| ED-502 | Particle system editor | ⬜ | |
| ED-503 | Material node graph | ⬜ | |
| ED-504 | Shader editor with live compilation | ⬜ | |
| ED-505 | Frame debugger over the bridge | ⬜ | |
| ED-506 | Profiler panel fed by `ReportFrameStats` | ⬜ | |
| ED-510 | **Backend comparison mode** | ✅ | Built early, and cheaply, exactly as this section predicted: it is play mode run several times over. `BackendComparison` launches one player per installed build, waits for each handshake, asks all of them for the same frame over the bridge and compares what comes back against the first to answer. The pixel arithmetic is `ImageDiff` in `cna-editor-core` — CNA-free and tested against images built in the test — and decoding a capture is injected, because it needs a graphics API and only one module may have one (D-03). The tolerance is the load-bearing detail: two backends are not required to be bit-identical and never will be, so a comparison with none reports every backend as different from every other, which is true and useless. What is reported is how many pixels differ, by how much, **where** — the bounding box is usually the whole diagnosis — and a difference image written beside the captures |
| ED-511 | Backend conformance harness built on ED-510 | ✅ | `cna-editor --compare-backends` runs exactly what the Backends panel runs — the same call, not a parallel path that could pass while the panel was broken — prints one line per backend and **exits non-zero when they disagree**. The exit code is the assertion: a build server needs to read none of the output. `--tolerance=N` is on the command line because the threshold is a policy a project sets, not a constant this editor is entitled to choose for it. Refused up front on a headless run: comparing means decoding the captures, decoding needs a device, and reporting every capture as unreadable is a confusing way to say 'wrong configuration' |
| ED-513 | Per-backend player builds from one configure | ✅ | `CNA_EDITOR_PLAYER_BACKENDS="SOFTWARE;EASYGL"` builds one extra `cna-player-<backend>` per entry and puts it beside the editor, which is where discovery looks. A *nested* build per backend rather than more targets in this one, because `CNA_GRAPHICS_BACKEND` is a cache variable of CNA's own build: there is exactly one value per build tree and no arrangement of targets can change that. Empty by default and expensive when it is not — each entry compiles CNA again — but without it ED-510 has nothing to compare and play mode's backend picker has one entry |
| ED-512 | Remote device preview (Android, browser) | ⬜ | |
| ED-520 | MC3 / Mesh-Craft plugin: import/export, primitive editor, CSG preview, glTF export | ⬜ | Lives outside the editor core, by design (D-11) |
| ED-521 | Terrain and world tools | ⬜ | |

**ED-510 was cheaper than it looked, and it is done.** F-01 turned out to be an enabler exactly as
predicted: once preview means "spawn a player process", spawning four of them from four per-backend
builds is the same mechanism run four times. It needed no new architecture — a sequence over
`PlayerProcess`, one new protocol reply that actually waits for pixels, and arithmetic over two
buffers. **ED-511**, the conformance harness, is that sequence run from the command line: same call, one
line per backend, and an exit code a build server can assert on.

---

## Cross-cutting

| Id | Task | Status | Notes |
|----|------|:------:|-------|
| ED-900 | CI: build and test on Linux, Windows and macOS, warnings as errors | 🔄 | Linux is live: GCC Debug and Clang Release, both at `-Werror`, plus the full suite, CTest, a headless smoke run and a guard that opening the example does not modify it. Windows and macOS remain. The standalone configuration only — a CNA job needs four sibling checkouts, CNA's SDL submodules and an X server, and is worth adding when the editor has a reason to regress there |
| ED-901 | Doxygen configuration matching CNA's | ⬜ | |
| ED-902 | Format migration framework for `.cnaproject`, `.cnascene`, `.cnaasset` | ✅ | A chain of **single-version steps** — 3 to 4, then 4 to 5 — because one function per `(from, to)` pair grows quadratically and is where migration frameworks go to die. Steps run on the parsed JSON, before any of it reaches a document type, since by then the fields the old file used are gone. The gate and the upgrade are the same code: refusing a file from the future and upgrading one from the past both answer "what version is this?". Every chain is **empty** — no `formatVersion` was bumped — so the first real change is a small tested addition to a path that already runs on every load. A sidecar that cannot be upgraded keeps its id and loses only its settings (D-08) |
| ED-903 | Crash handling: never lose an unsaved document | ✅ | Deliberately **not** a signal handler: one serialising a document from inside `SIGSEGV` calls `malloc` and the filesystem with a corrupted heap, so the situation it is least likely to survive is the one it exists for. Instead a `.cnarecovery` snapshot written by ordinary code every `--autosave=SECONDS`, atomically by rename so a crash mid-snapshot leaves the previous one intact, in the user's *state* directory and never beside a project. On reopen the work is **offered**, not applied, and autosave for that scene is suspended until the offer is answered — the current session's unsaved seconds are worth less than the previous session's unsaved hours, and they share a file name |
| ED-904 | Editor preferences, persisted separately from any project | ⬜ | |
| ED-905 | Undo history panel | ✅ | Rows are *positions*, not entries, so the state the document was opened in is reachable — it is the one a user asking to "put it back how it was" is aiming at. Undone entries stay on the list rather than disappearing, because they are exactly what a redo is trying to reach. Clicking navigates to that position through the application's own undo, not straight into `CommandHistory`, so a jump prunes the selection like any Ctrl+Z. It does **not** remove one entry from the middle: a command's undo is only valid against the state its `execute()` left behind |
| ED-906 | Localisation of the UI strings | ⛔ | Deferred: English-only until the panel set stops changing shape |
| ED-907 | Adopt GoogleTest if the suite needs fixtures or parameterised cases | ⛔ | Deferred: nothing outside `TestHarness.hpp` knows how assertions are spelled, so this stays a contained change (D-12) |
| ED-908 | Optional content-hash change detection for assets | ⛔ | Deferred: hashing every asset on project open is how an editor comes to take thirty seconds to start (D-08). Add as opt-in when a pipeline needs it |

---

## Deliberately not built

Taken from the original discussion, which was right about all of it. Each of these would have sunk
a first version:

| Not doing | Instead |
|-----------|---------|
| A custom GUI toolkit | Dear ImGui behind `EditorUi` (D-02); a custom toolkit can be its own project later |
| A full ECS in CNA runtime | An editor *document* model that compiles down to whatever the game wants (D-04) |
| Visual scripting | — |
| A material node graph | Phase 4 at the earliest (ED-503) |
| An embedded C++ IDE | — |
| Editor and game in one process | Separate `cna-player` — mandatory given F-01, not merely wise (D-09) |
| Editor UI on all 14 backends | Three support tiers, classified per backend (F-02) |
| Multi-user collaboration | — |
| Unity-style prefab variants | Plain prefabs first (ED-300) |
| A physics engine | — |
| A complete 3D editor | Phase 3 is *basic* 3D, and only after glTF conformance |

---

## Open questions

These block specific tasks and are stated in full in ANALYSIS.md §4.

| Id | Question | Blocks |
|----|----------|--------|
| ~~Q-01~~ | ~~Can Dear ImGui render through CNA's public API alone?~~ | ✅ **Resolved: yes.** [`docs/SPIKE-IMGUI-CNA.md`](docs/SPIKE-IMGUI-CNA.md) |
| ~~Q-02~~ | ~~How does a game consume a compiled scene?~~ | ✅ **Resolved: a header-only loader shipped from `cna-editor`.** [`docs/DESIGN-SCENE-LOADER.md`](docs/DESIGN-SCENE-LOADER.md) |
| Q-03 | Which process owns the window in play mode? | Answered by ED-245: the player owns its own top-level window. Revisit when it becomes annoying |
| ~~Q-04~~ | ~~Does `cna-player` live here or in `openeggbert/cna`?~~ | ✅ **Resolved: here.** Decision D-15 |

---

## Gaps found in CNA

Reporting these is a deliverable of decision D-01, not a side effect. Both come from the ED-100
spike; details in [`docs/SPIKE-IMGUI-CNA.md`](docs/SPIKE-IMGUI-CNA.md) §3.

| Id | Gap | Impact | Suggested fix |
|----|-----|--------|---------------|
| G-01 | `Microsoft::Xna::Framework::Color` has no default constructor | `std::vector<Color>::resize(n)` does not compile. XNA's `Color` is a struct and *is* default-constructible, so this is a real behavioural difference that will bite any port resizing a colour buffer | Add `Color() = default;` with zero-initialised members |
| G-02 | `CNA::Devices::Clipboard` is entirely inside `#ifdef CNA_DEVICES`, which defaults to OFF | An editor built against a default CNA has no clipboard; copy/paste in text fields silently does nothing. The editor degrades cleanly and reports it | Document that tooling expects `-DCNA_DEVICES=ON` |
| G-03 | A `RenderTarget2D` sampled as a texture is not origin-normalised across backends | EASYGL presents it flipped and SOFTWARE does not, so the viewport panel would show the scene upside down on one of them. Worked around by `EditorViewport::isRenderTextureFlippedVertically()`, a compile-time constant per backend | Normalise the sampling origin in the backends, or state the convention in the public API so a consumer can rely on it |
| G-05 | `PbrEffect` draws nothing on EASYGL, and reports no error doing it | The editor's model pass (ED-402) cannot use PBR. It constructs without throwing, accepts every parameter, issues its draw calls and puts no pixels on screen, while `BasicEffect` renders the same geometry, matrices and lights correctly in the same frame. Backface culling and an unbound base-colour sampler were both ruled out first. A second, separate bug found on the way: `PbrEffect::FillGpuDrawParams` sets `p.textureEnabled = true` unconditionally while binding `p.texture0` only when a texture exists, so a material with a base-colour *factor* and no map samples an unbound texture | Bind no texture *and* clear `textureEnabled` when there is none; and check the EASYGL PBR path draws at all |
| G-04 | No public way to build a `SpriteFont` | `SpriteFont`'s constructor takes an already-built glyph atlas, and the only thing that produces one is `CNA::Internal::Xnb::SpriteFontReader` — which D-01 forbids the editor from touching. The editor can therefore describe a `.spritefont` but not preview its glyphs (ED-302) | A public `ContentManager::Load<SpriteFont>` specialisation, or a public font builder |
