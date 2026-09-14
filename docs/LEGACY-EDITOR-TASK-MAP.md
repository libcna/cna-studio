# Legacy CNA Editor task map (`ED-*` → `STUDIO-*`)

The CNA Editor prototype tracked its work as `ED-001` … `ED-908` in its own `plan.md`. CNA
Studio uses a new `STUDIO-NNNNN` id space, and the old ids are **not** reused. This document is the
bridge, so that implementation history is not lost when someone asks "was this ever done, and by
whom, and does it still work?".

## How to read this

There are 117 legacy tasks. Their status **in the prototype** at the import commit
(`3bce82dd74e9a201a21e31308d43d2ee7761d641`) is recorded verbatim below.

> **A legacy ✅ does not mean the feature is complete in CNA Studio.**

This is the important rule. A prototype task marked complete means *that code was written and
tested against the prototype's Dear ImGui UI*. CNA Studio replaces that UI, and a feature has not
survived the migration until it works, and is tested, through the Studio UI. The roadmap in
`plan.md` therefore re-states the surviving capabilities as Studio tasks with their own acceptance
criteria, and `plan.md` — not this document — is the source of truth for what is done.

What a legacy ✅ *does* mean, and it is worth a lot: there is working, tested code in this
repository to migrate rather than to write from scratch.

## Status legend (as recorded in the prototype)

| Symbol | Meaning in the prototype |
|--------|--------------------------|
| ✅ | Done and verified against the prototype's UI |
| 🔄 | In progress |
| ⬜ | Not started |
| ⛔ | Deferred by explicit decision |
| 🔬 | Blocked on a spike or a decision |

## Summary

| Prototype status | Count | Disposition in CNA Studio |
|------------------|------:|---------------------------|
| ✅ Done | 98 | Carried forward. Implementation survives the Studio migration and its tests pass; re-verification against the Studio UI is tracked by the successor task. |
| 🔄 In progress | 2 | Carried forward, incomplete. Resumed under its successor task. |
| ⬜ Not started | 12 | Not started in the prototype. Re-scoped into the Studio roadmap. |
| ⛔ Deferred | 5 | Deliberately deferred by the prototype. The Studio roadmap re-decides rather than inheriting the deferral. |
| **Total** | **117** | |


## Phase −1 — Foundation

*Core document model, JSON, undo, assets, project*

| Legacy id | Task | Prototype status |
|-----------|------|:----------------:|
| `ED-001` | Repository, CMake, C++23, MS-PL SPDX headers, Doxygen conventions | ✅ |
| `ED-002` | `Uuid` — v4 generation, canonical parse and format, nil sentinel | ✅ |
| `ED-003` | Dependency-free JSON reader and writer, order-preserving, diff-stable (D-12) | ✅ |
| `ED-004` | `StudioMath` — the editor's own POD vector/quaternion/colour/rectangle types (D-13) | ✅ |
| `ED-005` | `PropertyValue` — 13-alternative tagged union with JSON round-trip (D-05) | ✅ |
| `ED-006` | `ComponentDescriptor`, `PropertyDescriptor`, `ComponentRegistry` (D-05) | ✅ |
| `ED-007` | `StudioCommand` and `CommandHistory` — undo, redo, merging, dirty tracking, bounded retention (D-06) | ✅ |
| `ED-008` | `StudioEntity`, `StudioComponent`, editor-only state (D-04, D-07) | ✅ |
| `ED-009` | `SceneDocument` — hierarchy, cycle rejection, recursive delete, JSON round-trip | ✅ |
| `ED-010` | Seven scene commands, all undoable, `SetProperty` merging on a stable key | ✅ |
| `ED-011` | Six built-in component descriptors | ✅ |
| `ED-012` | `AssetDatabase` — stable ids, `.cnaasset` sidecars, move and missing detection (D-08) | ✅ |
| `ED-013` | `Project` — `.cnaproject`, `ProjectKind`, the 14-backend capability table (D-10, F-02) | ✅ |
| `ED-014` | `StudioUi` abstraction and `NullStudioUi` (D-02) | ✅ |
| `ED-015` | `StudioViewport` abstraction and `NullStudioViewport` | ✅ |
| `ED-016` | `StudioProtocol` and `MessageStreamDecoder` (D-09) | ✅ |
| `ED-017` | `PluginManifest` and `PluginHost` discovery and validation (D-11) | ✅ |
| `ED-018` | `StudioContext` — the composition layer | ✅ |
| `ED-019` | `StudioApplication`, argument parsing, six panels, headless frame loop | ✅ |
| `ED-020` | Test harness and 69 tests; CTest registration | ✅ |
| `ED-021` | `examples/HelloSprites` — a real project the editor opens end to end | ✅ |
| `ED-022` | `UiDrawData` and `UiInputState` — the toolkit boundary (D-14) | ✅ |
| `ED-023` | `MessageChannel` — non-blocking loopback TCP transport, POSIX and Winsock | ✅ |

## Phase 0 — Technical prototype

*CNA hosting spike, UI abstraction, window host*

| Legacy id | Task | Prototype status |
|-----------|------|:----------------:|
| `ED-100` | **Spike: can Dear ImGui render through CNA's *public* API?** | ✅ |
| `ED-101` | Vendor Dear ImGui (docking branch) into `third_party/` | ✅ |
| `ED-110` | `ImGuiStudioUi` — implements every `StudioUi` method | ✅ |
| `ED-113` | `propertyField` widgets for all 13 property types | ✅ |
| `ED-116` | `CnaUiRenderer` — draws `UiDrawData` through CNA's public graphics API | ✅ |
| `ED-117` | `CnaUiPlatform` — fills `UiInputState` from CNA's public input API | ✅ |
| `ED-130` | Report the CNA public-API gaps the spike uncovered | ✅ |
| `ED-102` | `CNA_STUDIO_WITH_CNA=ON` build verified against real `../cna` + `../sharp-runtime` checkouts | ✅ |
| `ED-111` | **Window, graphics device, event loop; `--ui=imgui` becomes real** | ✅ |
| `ED-112` | Default dock layout on first run; user's saved layout respected thereafter | ✅ |
| `ED-114` | Console panel: severity filter, scroll-lock, copy | ✅ |
| `ED-119` | **Leading glyph missing from docked tab labels** | ✅ |
| `ED-124` | Studio verified on a second backend (EASYGL, real OpenGL ES 3.2 under Xvfb) | ✅ |
| `ED-123` | `--screenshot=PATH` and the `CnaStudioWindowSmoke` CTest | ✅ |
| `ED-115` | Persistent `DynamicVertexBuffer`/`DynamicIndexBuffer` in `CnaUiRenderer` | ⛔ |
| `ED-118` | Quaternion inspector as Euler angles | ✅ |
| `ED-120` | `CnaSceneRenderer` draws sprites through `SpriteBatch` | ✅ |
| `ED-121` | Viewport renders into an offscreen target composited into the dock | ✅ |
| `ED-122` | Studio camera: pan, zoom, frame-selection | ✅ |
| `ED-125` | Grid with adaptive 1-2-5 spacing, major lines and axes | ✅ |
| `ED-126` | Click-to-select in the viewport, via ray-cast picking | ✅ |

## Phase 1 — Usable 2D MVP

*Panels, editing, assets, play mode*

| Legacy id | Task | Prototype status |
|-----------|------|:----------------:|
| `ED-200` | Hierarchy panel: rename in place, drag-to-reparent, context menu, multi-select | ✅ |
| `ED-201` | Sprite rendering resolves textures through `AssetDatabase`, ordered by layer depth | ✅ |
| `ED-202` | Grid with adaptive spacing | ✅ |
| `ED-203` | Selection outline as an overlay pass | ✅ |
| `ED-204` | Icons for entities the viewport cannot draw | ✅ |
| `ED-205` | Translate gizmo, with merged undo across the drag | ✅ |
| `ED-206` | Ray-cast picking against entity bounds | ✅ |
| `ED-207` | Inspector: add and remove components, respecting `unique` and `required` | ✅ |
| `ED-208` | Asset drag-and-drop from the browser onto a sprite slot | ✅ |
| `ED-209` | Keyboard shortcuts: Ctrl+Z/Y/S/N/D, Delete, F to frame, W/E/R for gizmo modes, X for the gizmo space | ✅ |
| `ED-210` | Split the panels out of `StudioApplication` into their own classes | ✅ |
| `ED-220` | Asset browser: folder tree, thumbnails, filtering, rename, move | ✅ |
| `ED-221` | Texture importer: dimensions, mipmaps, premultiplied alpha, thumbnails | ✅ |
| `ED-222` | Importer settings surfaced in the inspector, reusing the descriptor system | ✅ |
| `ED-223` | Filesystem watcher; reimport on external change | ✅ |
| `ED-224` | "Missing references" report, with a relink dialog | ✅ |
| `ED-240` | `cna-player` host: loads a project and a scene, speaks the protocol | ✅ |
| `ED-241` | Process spawn and lifetime; a player crash is reported, never fatal to the editor | ✅ |
| `ED-242` | Socket transport over `MessageStreamDecoder` | ✅ |
| `ED-243` | Play / Pause / Step / Stop, with the player's log routed into the console | ✅ |
| `ED-244` | Player discovery: find installed `cna-player-<backend>` binaries and offer only those | ✅ |
| `ED-245` | Studio-side Play toolbar wired to `PlayerProcess` | ✅ |
| `ED-246` | `cna-player` draws the scene it loaded | ✅ |
| `ED-250` | **How a game consumes a compiled scene** | ✅ |

## Phase 2 — Production 2D editor

*Prefabs, tilemaps, animation, validation, recovery*

| Legacy id | Task | Prototype status |
|-----------|------|:----------------:|
| `ED-300` | Prefabs: create, instantiate, override, apply | ✅ |
| `ED-301` | Tilemap component and tile-painting tool | ✅ |
| `ED-302` | `SpriteFont` preview and importer settings | 🔄 |
| `ED-303` | Sprite animation editor with a timeline | ✅ |
| `ED-304` | Audio source and listener editing, with preview playback | ✅ |
| `ED-305` | Layers and tags | ✅ |
| `ED-306` | Asset hot-reload into a running player over the bridge | ✅ |
| `ED-307` | Live property editing into a running player | ✅ |
| `ED-308` | Build and publish dialog driving CNA's own CMake targets | ✅ |
| `ED-309` | Backend diagnostics: report the current build's `GraphicsCapability` set | ✅ |
| `ED-310` | Scene validation: missing references, duplicate primary cameras, zero scale, empty entities | ✅ |
| `ED-311` | `PropertyType::List` and `NestedStructure`, with inspector support | ✅ |
| `ED-320` | GPU picking through an id render target | ⛔ |

## Phase 3 — Basic 3D

*3D viewport, gizmos, model import, lighting*

| Legacy id | Task | Prototype status |
|-----------|------|:----------------:|
| `ED-400` | Perspective and orthographic viewport camera, orbit and fly navigation | ✅ |
| `ED-401` | Rotate and scale gizmos; local/world space toggle | ✅ |
| `ED-402` | `ModelRenderer` rendering | ✅ |
| `ED-403` | Material editing and preview | ✅ |
| `ED-404` | Light components with viewport visualisation | ✅ |
| `ED-405` | glTF importer built on CNA's own `cgltf` integration | ✅ |
| `ED-406` | Mesh preview in the asset browser | ✅ |
| `ED-407` | Environment and fog settings | ✅ |
| `ED-408` | 3D translate gizmo | ✅ |
| `ED-409` | 3D rotate and scale gizmos | ✅ |
| `ED-410` | Per-mesh material lists (needs ED-311) | ✅ |
| `ED-413` | Sprites drawn in the 3D view | ✅ |
| `ED-411` | **Plugin dynamic loading**: `dlopen`/`LoadLibrary`, `extern "C"` entry, unload, hot-reload | ✅ |
| `ED-412` | Plugin extension points: importers, component types, panels, menu commands, gizmos, exporters | ✅ |

## Phase 4 — Advanced tooling

*Backend comparison, plugins, build*

| Legacy id | Task | Prototype status |
|-----------|------|:----------------:|
| `ED-500` | Animation timeline and curve editor | ⬜ |
| `ED-501` | Skeletal animation preview | ⬜ |
| `ED-502` | Particle system editor | ⬜ |
| `ED-503` | Material node graph | ⬜ |
| `ED-504` | Shader editor with live compilation | ⬜ |
| `ED-505` | Frame debugger over the bridge | ⬜ |
| `ED-506` | Profiler panel fed by `ReportFrameStats` | ⬜ |
| `ED-510` | **Backend comparison mode** | ✅ |
| `ED-511` | Backend conformance harness built on ED-510 | ✅ |
| `ED-513` | Per-backend player builds from one configure | ✅ |
| `ED-512` | Remote device preview (Android, browser) | ⬜ |
| `ED-520` | MC3 / Mesh-Craft plugin: import/export, primitive editor, CSG preview, glTF export | ⬜ |
| `ED-521` | Terrain and world tools | ⬜ |

## Cross-cutting

*CI, docs, history, deferred items*

| Legacy id | Task | Prototype status |
|-----------|------|:----------------:|
| `ED-900` | CI: build and test on Linux, Windows and macOS, warnings as errors | 🔄 |
| `ED-901` | Doxygen configuration matching CNA's | ⬜ |
| `ED-902` | Format migration framework for `.cnaproject`, `.cnascene`, `.cnaasset` | ✅ |
| `ED-903` | Crash handling: never lose an unsaved document | ✅ |
| `ED-904` | Studio preferences, persisted separately from any project | ⬜ |
| `ED-905` | Undo history panel | ✅ |
| `ED-906` | Localisation of the UI strings | ⛔ |
| `ED-907` | Adopt GoogleTest if the suite needs fixtures or parameterised cases | ⛔ |
| `ED-908` | Optional content-hash change detection for assets | ⛔ |


## Id policy

- Legacy `ED-*` ids are **retired**. They are never issued again and never renumbered.
- CNA Studio ids are `STUDIO-NNNNN`, allocated from a range large enough for the whole programme.
- A Studio task that migrates a legacy capability names the `ED-*` id it descends from in its
  architectural notes, so the trail runs in both directions.
