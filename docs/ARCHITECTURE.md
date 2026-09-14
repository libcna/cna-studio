# CNA Studio — Architecture

> This document supersedes `ANALYSIS.md` for anything concerning CNA's current shape.
> `ANALYSIS.md` is retained as the historical record of the CNA Editor prototype: its fifteen
> decisions explain why the imported code looks the way it does, and most of them still hold. But
> it audited an **older CNA revision** and its renderer table, backend counts and capability claims
> are stale. Where the two disagree, this document is correct.

**Audit basis:** `libcna/cna` branch `next` at `e05b3d0f026e0926741f89459daf02579240399d`,
`libcna/sharp-runtime` branch `next` at `0c82d9b888bdf5f7d5663c77942f339bcb2a7445`, both read on
2026-09-14.

---

## 1. The invariant

> **CNA is the framework. CNA Studio is the professional authoring environment. A CNA Studio
> project remains a CNA C++ project.**

Every decision below is tested against that sentence. Three consequences follow, and they are not
negotiable:

1. **Studio is never a runtime dependency.** A game authored in Studio builds and runs with Studio
   uninstalled — and preferably rebuilds from source with Studio uninstalled too. There is no
   Studio-only runtime container, no proprietary project format that hides the source, and no
   mandatory Studio-generated build step that cannot be reproduced by hand.
2. **Studio drives the project's own CMake.** It does not replace it. Studio may *create* and
   *maintain* a sensible default `CMakeLists.txt` for a project it created, but that file stays
   editable, and the exact configure and build commands Studio runs are always visible.
3. **Handwritten C++ is never overwritten.** Code generation writes to a clearly separated
   `Generated/` subtree with deterministic, reviewable output.

This is enforced by test, not by good intentions: `STUDIO-18020`'s standalone-export test exports a
project, copies it to a clean directory, makes Studio unavailable, configures it with its own
CMake, builds it and runs a smoke test.

---

## 2. What changed in CNA since the prototype

The prototype's analysis is out of date in ways that matter to the architecture. The four
significant ones:

### 2.1 Renderer and platform are separate axes

The prototype modelled one flat "backend" concept. Current CNA does not:

| Axis | CMake variable | Chooses |
|------|----------------|---------|
| Renderer | `CNA_GRAPHICS_RENDERER` | How pixels are produced |
| Platform | `CNA_PLATFORM` | Where the window, events and input come from |

`cmake/PlatformSelection.cmake` says so explicitly, and rejects invalid combinations rather than
tolerating them until something dereferences null. Studio models these separately everywhere:
target profiles, the Build panel, diagnostics and the capability contract.

**Platforms implemented today:** `SDL3` (default), `SDL2`, `HEADLESS`, and `TERMINAL` (POSIX only).
**Reserved but unimplemented, and a hard configure error:** `SDL12`, `WIN32`, `EMSCRIPTEN`.

### 2.2 There are 50 renderer identities, not 14

`cmake/RendererRegistry.cmake` maps 50 renderer identities to implementing families. The
prototype's table of 14 is historical, and not merely incomplete — it is **wrong**:

- `EASYGL`, the prototype's *default* renderer, is no longer a renderer identity at all. EasyGL
  became a renderer **family** serving five GL profiles (`OPENGLES2`, `OPENGLES3`, `OPENGL33`,
  `WEBGL1`, `WEBGL2`), with the GL profile a runtime value. A project naming `easygl` names
  something that cannot be built.
- `D3D11`, `D3D12`, `D3D9` and `DX3` were renamed to the `DIRECTXnn` form.
- `ASCII` is no longer a renderer; the equivalent presentation is a CNAEXT post-process effect
  applied on top of an ordinary renderer.

Studio's catalogue (`CNA/Studio/Project/RendererCatalog.hpp`) classifies all 50, models platforms
as their own axis, and carries a **legacy alias table** so a `.cnaproject` written by the prototype
migrates rather than failing — and is told what changed, because silently substituting a renderer
changes what a user's game ships on.

Studio must never hard-code this list. `STUDIO-02030` adds a test that fails loudly when CNA
registers an identity Studio has not classified, and a guard test rejects scattered
`name == "vulkan"` comparisons anywhere outside the catalogue itself.

### 2.3 Capability reporting is now a first-class runtime model

This is the most important change, and it replaces the prototype's hand-maintained capability
table outright.

`GraphicsDevice::GetRendererCapabilityProfileEXT()` returns a `CNA::RendererCapabilityProfile`:
an immutable snapshot built and cached after native renderer creation, carrying

- **32 atomic `RendererFeature` entries**, each answered as `Supported`, `Restricted`,
  `Unsupported` or `Unknown` — with an optional English qualification on each;
- **22 numeric `RendererLimit` values**, each with an explicit known/unknown flag;
- **per-`SurfaceFormat` usage masks** (`TextureStorage`, `Sampled`, `Filterable`, `RenderTarget`,
  `Blendable`, `StorageRead`/`Write`/`Atomic`, transfer, mip, multisample);
- a generated English capability report.

The four-state answer matters. `Unknown` means *the renderer has not classified this*, which is
not the same as *no*. Studio treats `Unknown` as not-satisfied for a **required** feature and says
so — a tool that starts and then fails to draw is worse than one that refuses with a reason.

The older, coarser `CNA::GraphicsCapability` enum still exists and is still queryable through
`GraphicsDevice::SupportsCapability()`. Studio prefers `RendererFeature`.

### 2.4 CNAEXT is a substantial modern rendering layer

`modules/graphics-ext` publishes 98 public headers: PBR materials, clustered forward shading,
cascaded and cube shadow maps, image-based lighting, a post-process chain (bloom, tonemapping,
SSAO, SSR, DoF, motion blur, FXAA, colour grading), compute shaders, storage buffers, GPU timers,
render-target pooling and a render pipeline. It also publishes `ShaderEffect` for source-based
shaders across six shader dialects.

This is what makes a *professional* Studio viewport possible rather than a wireframe preview, and
it is what the material, lighting and shader-graph workstreams target.

---

## 3. Studio host renderer versus game target renderer

These are two independent concepts and conflating them is the single easiest way to get this
product's architecture wrong.

|  | Studio host renderer | Game target renderer |
|--|----------------------|----------------------|
| Draws | CNA Studio's own UI and viewport | The user's game |
| Chosen by | What Studio was compiled against | The project's target profile |
| Must satisfy | The Studio host capability contract (§4) | Whatever the *game* needs |
| Lives in | The `cna-studio` process | The `cna-player` process, or the packaged build |

Studio may run on a modern Vulkan-capable renderer while the user previews and packages their game
on an entirely different one. A project that uses only classic XNA-compatible functionality must
**not** be denied a renderer merely because that renderer cannot host Studio's UI. The separate
player process (§6) is what makes this natural rather than exotic.

---

## 4. The Studio host capability contract

Studio's own UI has real requirements. They are stated once, as data, in one module —
`StudioHostRequirements` — and evaluated against the runtime `RendererCapabilityProfile`. They are
**not** a list of renderer names.

The contract is capability-driven for a reason: adding a renderer to CNA must not require editing
Studio, and a renderer that gains a capability must become eligible automatically.

At start-up Studio can always report:

- the current CNA platform implementation;
- the current graphics renderer;
- whether the modern CNAEXT graphics API is available;
- the renderer's classified capabilities;
- **which Studio requirements are unmet, by name, and whether each is unsupported or merely
  unclassified.**

If the compiled renderer cannot host Studio, Studio fails cleanly with that diagnostic rather than
opening a window it cannot draw into.

Requirements are introduced as the UI needs them, not speculatively. The *shape* is fixed now; the
membership is a living list owned by `STUDIO-02020`.

---

## 5. Module architecture

The prototype's layering is good and is kept. The important property — **only one module links
CNA** — is enforced by the build graph rather than by review: a stray `#include
<Microsoft/Xna/...>` anywhere else fails to compile.

```
                        cna-studio (application)
                                 │
        ┌────────────────────────┼────────────────────────┐
        ▼                        ▼                        ▼
 cna-studio-ui-*          cna-studio-context       cna-studio-plugins
 (the new Studio UI)      (composition)            (manifest, loading)
        │                        │
        │   UiDrawData ▼  ▲ UiInputState
        └────────────┬───┴───────┐
                     ▼           │
           cna-studio-viewport ──┘        ← the ONLY module that links CNA
                     │
   ┌─────────────────┼─────────────────┐
   ▼                 ▼                 ▼
cna-studio-scene  cna-studio-assets  cna-studio-project
   └─────────────────┼─────────────────┘
                     ▼
              cna-studio-core
```

The UI is joined to CNA by two plain data types rather than by an interface: `UiDrawData` carries
geometry out, `UiInputState` carries input in. That is why the CNA-side renderer contains no UI
toolkit header at all, and why "the Studio UI renders through CNA's public API" is a property of
the build graph rather than a claim to re-check by hand. The new UI keeps this seam exactly.

### Planned UI modules

| Module | Responsibility |
|--------|----------------|
| `cna-studio-ui-core` | Widget tree and state, ids, events, focus, command routing, input model, accessibility metadata, headless test renderer |
| `cna-studio-ui-layout` | Row/column, flex, grids, splitters, dock layout, scrolling, virtualised lists, sizing constraints |
| `cna-studio-ui-widgets` | The widget library |
| `cna-studio-ui-docking` | Panel docking, tab stacks, floating panels, saved workspaces |
| `cna-studio-ui-renderer` | The CNAEXT-backed renderer: font atlas, icon atlas, batching, clipping, DPI |

`cna-studio-ui-core` and `cna-studio-ui-layout` are **CNA-free and headless-testable**. That is the
property that keeps the UI workstream honest: layout, focus, hit-testing and command routing are
all decided by code that runs in CI with no GPU, and only the pixels need a device.

---

## 6. Process architecture

Studio and the game run in **separate processes**, and this is structural rather than merely
prudent:

- CNA's renderer is selected at compile time, so Studio and the game may be linked against
  different CNA builds and cannot share an address space;
- a game crash must not take Studio down;
- user gameplay C++ must not be able to corrupt Studio's document state;
- several renderer/player configurations can coexist.

Game C++ executes in the game process. Studio does not load arbitrary user gameplay code into
itself for convenience when the process boundary is available and safer.

---

## 7. The UI migration

The imported Dear ImGui UI proved CNA could host an editor. It is **not** the production UI of CNA
Studio, and it is not the answer to "make it look professional" either.

The migration is a strangler, not a flag day:

```
existing panels ──▶ compatibility adapter ──▶ new CNA Studio UI
```

ImGui remains as a migration fallback and debug UI while panels are ported one at a time with tests
green throughout. It is removed — code, CMake option and vendored source — only after feature
parity, input parity, docking parity, visual acceptance via screenshot tests, and a period of
stable daily use. `STUDIO-07099` is the architecture guard test that fails if production Studio UI
regains a dependency on it after that point.

---

## 8. Two project kinds

A project declares what it is, and Studio respects that:

| Kind | Studio offers |
|------|---------------|
| `CnaNative` | The full authoring environment: scenes, entities, components, inspector, gizmos, prefabs, play mode, viewports |
| `XnaCompatible` | Asset browser, importer settings, content preview, renderer configuration, Play — and nothing else |

A hand-written XNA-style CNA game is never forced through Studio's entity/component model to use
Studio's tooling. CNA remains usable with no Studio at all; this distinction is how that stays
true in practice rather than in principle.

---

## 9. Data architecture

**Authoring data and runtime data are separate concerns in the same file.** Editor-only state —
panel layout, outliner expansion, selection, gizmo settings, viewport camera, notes — lives in a
named sub-object the runtime scene compiler drops wholesale. It never pollutes shipped gameplay
state.

**Assets are referenced by id, never by path.** Every asset carries a UUID in a `.cnaasset`
sidecar. Moving a file touches no scene and breaks no reference.

**Every document mutation goes through a command.** Undo is retrofitted at nobody's convenience:
by the time you notice it is missing, every call site is a place undo silently does not work.

**Formats are deterministic, versioned and migratable.** Stable ordering, no timestamp churn, a
migration chain that runs on every load, and unknown plugin components that survive a save/load
round trip untouched.

**File formats are CNA formats.** `.cnaproject`, `.cnascene`, `.cnaasset` and `.cnaprefab` did not
change when the product was renamed, and will not change without a migration path and tests. Two
JSON keys still read `editorState` and `editorApiVersion` because existing files and built plugins
depend on them; they are pinned deliberately and documented in `docs/FORMATS.md`.

---

## 10. Architecture guard tests

Important boundaries are enforced by machinery, not by comments. The guards, each a real test:

| Guard | What it prevents |
|-------|------------------|
| No `CNA::Internal::*` in Studio | Reaching into CNA's internals instead of reporting a gap |
| Only `cna-studio-viewport` includes CNA headers | Losing the headless-testable core |
| No direct Vulkan/D3D/OpenGL/Metal/WebGPU calls | Renderer-specific code leaking into Studio |
| Production UI has no Dear ImGui dependency *after* migration | Silent regression of the UI migration |
| An unclassified CNA renderer identity fails a test | A hard-coded renderer list rotting silently |
| Generated game builds with Studio unavailable | Studio becoming a runtime or build dependency |
| Shipping game output contains no editor-only code | Editor code shipping in a game |
| Every document mutation goes through a command | Undo silently not working |
| Unknown components survive save/load | Losing a user's data when a plugin is missing |
| Files are byte-deterministic across saves | Version-control churn |

---

## 11. Decisions inherited from the prototype

These are recorded in `ANALYSIS.md` as D-01 … D-16 and are **kept**: CNA public API only; UI behind
an abstraction; CNA optional and off by default; a document model rather than an ECS; hand-written
reflection metadata; undo as a hard rule; editor state separated from runtime data; assets by id;
play mode as a separate process; two project kinds; manifest-first plugins; zero external
dependencies in the core; the toolkit boundary as a data type; `cna-player` living in this
repository; the window host as a free function.

One is **superseded**: the prototype decided that a custom professional UI system was "not for v1".
CNA Studio is the long-term product, and a substantial original UI architecture is now the target
(§5, §7).

---

## 12. Open architectural questions

Recorded rather than guessed at. Each has a task id in `plan.md`.

| Id | Question |
|----|----------|
| `STUDIO-15001` | Which C++ reflection mechanism for project-defined gameplay components — registration functions, descriptor files, lightweight macros, or a small project-side generator? Decided on maintainability and ABI safety, not elegance |
| `STUDIO-16020` | How does player output reach a Studio viewport efficiently without compromising the separate-process architecture? |
| `STUDIO-16021` | Does native code hot reload beyond "rebuild and restart the player with state restored" earn its reliability cost? |
| `STUDIO-02010` | Which of the 50 renderer identities actually exhibit G-03's render-target flip? |
