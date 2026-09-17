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

> **CNA is the framework. CNA Studio is the professional authoring environment. A project authored
> by Studio remains a normal project for CNA or one of its supported bindings, and builds and runs
> without CNA Studio.**

For the one language implemented today, that reads:

```
C++ project ──▶ the project's own CMake ──▶ CNA
```

**This used to say "a CNA Studio project remains a CNA *C++* project"**, and the change is a
generalisation rather than a widening of scope. CNA has several language bindings, and a project
authored for one of them is still a CNA project; the old wording made C++ a property of Studio's
*model* rather than of a project, which is the assumption §13 exists to keep out of the Project
Hub, project creation, build orchestration, source opening, component metadata and packaging. C++
remains the only required and fully implemented gameplay-language workflow.

Every decision below is tested against that sentence. Four consequences follow, and they are not
negotiable:

1. **Studio is never a runtime dependency.** A game authored in Studio builds and runs with Studio
   uninstalled — and preferably rebuilds from source with Studio uninstalled too. There is no
   Studio-only runtime container, no proprietary project format that hides the source, and no
   mandatory Studio-generated build step that cannot be reproduced by hand.
2. **Studio drives the project's own build system, and never replaces it.** For a C++ project that
   is the project's own CMake. Studio may *create* and *maintain* a sensible default build file for
   a project it created, but that file stays editable, and the exact configure and build commands
   Studio runs are always visible.
3. **Hand-written source is never overwritten.** Code generation writes to a clearly separated
   generated subtree — `Generated/` for C++, and whatever the language's adapter names — with
   deterministic, reviewable output.
4. **Game and user code runs outside the Studio process.** Play mode is a separate `cna-player`
   process (§6), and a packaged game has no Studio in it at all.

This is enforced by test, not by good intentions: `STUDIO-02051` exports a project into an empty
directory, configures it with nothing but CMake and a CNA checkout, compiles it and runs it, with
Studio not consulted after the export; `STUDIO-08011` does the same for every project template,
from creation rather than from export.

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

**Implemented.** `CNA/Studio/Project/StudioHostRequirements.hpp` holds the contract and the whole
of its evaluation, in a module that links no CNA: requirements name CNA's `RendererFeature` and
`RendererLimit` entries by the stable English identifiers `CNA::GetRendererFeatureName` returns,
so the contract is expressed in exactly the terms the device answers in while every branch of the
decision — including ones only a renderer nobody owns could reach — is tested in CI with no GPU.

`CNA/Studio/Viewport/CnaCapabilityBridge.hpp` is the only place in Studio that touches a real
`RendererCapabilityProfile`. It copies every declared feature and limit across, converts nothing
and decides nothing. That division is deliberate: anything it judged would be testable only on
hardware.

Requirements carry a **severity**. A missing *required* capability stops Studio and produces the
diagnostic; a missing *recommended* one disables the panel that needs it and is reported. Each also
states whether a `Restricted` answer is enough for what Studio does with it.
`cna-studio --host-capabilities` prints the current contract.

### 4.1 The modern API is a requirement, not a field on the report

The contract's headline — that hosting Studio needs a renderer capable of CNA's modern graphics
API — was, until `STUDIO-02070`, carried through the evaluation as a *reported fact* and consulted
by nothing. A renderer with no modern API at all passed `canHostStudio` on the strength of
`ThreeDimensionalPipeline`, `DepthStencilBuffer` and a 2048-pixel texture limit, which is precisely
the classic XNA capability set a renderer that cannot execute a shader has.

It is a requirement now, in the same table as the others, so it produces the same named diagnostic
and is tested by the same evaluation. Three things have to hold together:

| Signal | Source | Failure it catches |
|--------|--------|--------------------|
| The engine layer is compiled in | `CNA_CNAEXT` / `CNA::Graphics::getEngineLayerVersion()` | Studio built against a CNA with `-DCNA_CNAEXT=OFF` |
| The renderer classifies `ShaderEffects` | `RendererCapabilityProfile` | A renderer that has the headers and cannot compile a shader |
| The renderer classifies `ShaderEffectSourceExecution` | `RendererCapabilityProfile` | A renderer that accepts source and ignores it |

The second and third were already in the table as **recommended**, which is what let the first be
absent without consequence. Compiling in the layer is now required; executing a shader from source
is required; and §3's separation still holds completely — a renderer that fails all three remains a
perfectly valid **game target** for a project whose game does not need them. `STUDIO-02073` is the
test that says so in as many words.

**Availability is read, never asserted.** `STUDIO-02071` replaced the literal
`/*modernApiAvailable=*/true` at both host call sites with one adapter,
`captureStudioModernApiState()`, which reports what the build has. Where CNA cannot yet be asked
something, the adapter is the single place that says so and `docs/CNA-GAPS.md` records the
limitation — Studio never claims a capability was detected when it was assumed.

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

### 5.1 Four layers, four different questions

`ARCHITECTURE.md` used "the Studio renderer" for four distinct things until `STUDIO-04021` traced
what actually reaches the GPU. They are separated here because the host capability contract (§4) is
a statement about the second asking something of the third, and it cannot be enforced while the two
are one word.

| # | Layer | Question it answers | Module |
|---|-------|---------------------|--------|
| 1 | UI framework | What should the frame contain? | `cna-studio-ui-core` — links no CNA |
| 2 | UI GPU renderer | Which graphics calls draw it? | `cna-studio-ui-renderer` |
| 3 | CNA host renderer | Which CNA renderer executes those calls? | chosen at Studio's configure time |
| 4 | Game target renderer | Which CNA renderer does the *user's game* ship on? | the project's target profile |

Layer 1 emits `UiDrawData` and owns no device resource. Layer 2 owns every texture, buffer and
effect the UI has. Layer 3 is what layer 2 is compiled against, and is the subject of the contract
in §4. Layer 4 is independent of all three, for the reasons in §3.

**The audit's finding.** Layer 2 was, until the staged migration recorded in
[`UI-RENDER-PATH.md`](UI-RENDER-PATH.md), implemented entirely on the classic XNA 4.0 surface —
`BasicEffect` and `DrawUserIndexedPrimitives` — inherited from the Dear ImGui prototype and correct
for what it drew. It is not the modern CNAEXT path this phase is named after. That document traces
the call chain, lists every graphics call the UI makes, says how the mis-statement survived six
phases, and holds the migration's stages.

### Planned UI modules

| Module | Responsibility |
|--------|----------------|
| `cna-studio-ui-core` | Widget tree and state, ids, events, focus, command routing, input model, accessibility metadata, headless test renderer |
| `cna-studio-ui-layout` | Row/column, flex, grids, splitters, dock layout, scrolling, virtualised lists, sizing constraints |
| `cna-studio-ui-widgets` | The widget library |
| `cna-studio-ui-docking` | Panel docking, tab stacks, floating panels, saved workspaces |
| `cna-studio-ui-renderer` | Layer 2: the UI GPU renderer. Holds `StudioModernUiRenderer` behind `StudioUiRenderBackend`, plus texture ownership, batching, clipping and DPI. Held the classic `CnaUiRenderer` alongside it until `STUDIO-04027` deleted it |

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

The migration was a strangler, not a flag day:

```
existing panels ──▶ compatibility adapter ──▶ new CNA Studio UI
```

**It is finished.** ImGui remained as a migration fallback while panels were ported one at a time
with tests green throughout, and was then removed — code (`STUDIO-07030`), CMake option and
vendored source (`STUDIO-07031`) — after feature, input and docking parity and visual acceptance.
`STUDIO-07099` is the architecture guard test that fails if production Studio UI regains a
dependency on it, and it checks three routes: a `#include` or symbol in any source, the option or
vendor-target tokens in either `CMakeLists.txt`, and the vendored directory itself.

**The temporary adapter and the permanent seam are not the same thing, and only one of them is
still here.** `STUDIO-07001` was the compatibility adapter: the arrangement under which both
presentations were alive at once. With nothing left to be compatible *with*, its requirement no
longer applies and it is ⊘ superseded rather than complete — see `plans/phase-07-panel-migration.md`
for why back-dating it to ✅ would have been a lie about what happened.

What outlived it is `STUDIO-07016`, `StudioShell::setPanelContent`, and it outlived it because
hosting a panel's content was never a migration concern:

```
StudioShell::registerPanel(descriptor)   ── the panel exists, has a tab, has a Window menu row
StudioShell::setPanelContent(id, fn)     ── and this is what draws in it
```

Two calls rather than one, because the shell knows its panel *list* when it is assembled and a
panel's content arrives with whoever binds it. A panel with nothing bound draws as an empty
surface. That was the unported case during the migration; it is the not-yet-populated case now,
which is what a plugin-contributed panel needs before its plugin has initialised — so
`STUDIO-28005` depends on this seam rather than on the adapter that is gone.

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
| Only `cna-studio-viewport` and `cna-studio-ui-renderer` include CNA headers | Losing the headless-testable core |
| No direct Vulkan/D3D/OpenGL/Metal/WebGPU calls | Renderer-specific code leaking into Studio |
| Production UI has no Dear ImGui dependency *after* migration | Silent regression of the UI migration |
| An unclassified CNA renderer identity fails a test | A hard-coded renderer list rotting silently |
| Generated game builds with Studio unavailable | Studio becoming a runtime or build dependency |
| Shipping game output contains no editor-only code | Editor code shipping in a game |
| Every document mutation goes through a command | Undo silently not working |
| Unknown components survive save/load | Losing a user's data when a plugin is missing |
| Files are byte-deterministic across saves | Version-control churn |
| `plan.md`'s status breakdown matches the phase files | A ledger that is quoted and wrong |
| No service reaches another through a locator or a singleton | Services that were separated on paper and still talk through globals |

### 10.1 Dependencies are constructor arguments

The rule the shell decomposition (§5, `STUDIO-02050`) is built on:

> **The set of things a service can reach is the set visible in its constructor.**

There is no service locator and no registry. `StudioPlayService` takes a context, a log and a
notification sink; that is the whole of what it can touch, which is why its rules can be exercised
with two doubles and a lambda rather than by constructing the object that binds every panel in
Studio.

A locator would undo that one `get<T>()` at a time, and it would arrive the way
`StudioShellPanels` reached 1421 lines: by a sequence of individually reasonable additions, each
one saving a constructor argument in a hurry. Review does not catch it, because a locator added
later looks exactly like the code around it. So `STUDIO-02059` catches it instead, by scanning for
the four structures every locator and every singleton is actually built from — renaming
`instance()` to `shared()` evades none of them:

1. a mutable `static` holding a Studio type — the storage, whether function-local, a class static
   member, or file-scope;
2. a `static` function handing out a reference or pointer to a Studio type — the accessor.
   Returning **by value** is a factory (`Uuid::generate`, `Project::createDefault`) and is fine:
   the caller gets a thing rather than *the* thing;
3. a mutable namespace-scope variable of a Studio type — the same storage without the keyword;
4. `static T& get()` — the generic locator, which rules 1–3 miss because `T` names nothing.

`std::type_index` and `typeid` are refused too: a registry keyed on runtime type identity is a
locator with an extra step, and Studio has no other use for runtime type identity.

**What it deliberately does not ban.** A mutable `static` that holds no Studio type — a CRC lookup
table, a thread-local random engine — is a cache of a pure function, not a dependency anybody has.
Failing those would make the guard painful enough to be switched off, which is the only way a guard
actually dies. The discriminator is whether the static holds one of Studio's own *published* types:
a type declared inside a single `.cpp` cannot be what one part of Studio reaches another through,
because nothing outside that file can name it.

The guard is proved against fixtures rather than against a committed violation: it is fed each
prohibited shape and required to flag it, and fed the by-value factories, constants, members and
parameters that surround the real code and required to stay silent. An absence assertion that has
never been shown to fail is an assertion about nothing.

**Exceptions are sites, not patterns.** One construct is recorded as accepted — the Dear ImGui
prototype's clipboard adapter, whose hooks have nowhere instance-shaped to live because ImGui's
clipboard callbacks are C function pointers reached through a global context. It is named by file
and declaration with the task that deletes it (`STUDIO-07030`), and a second test requires every
recorded exception to still match something, so an exception cannot outlive the thing it excused.

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

---

## 13. The language and toolchain seam

**Decision.** Studio addresses a project's language through one registered adapter
(`CNA/Studio/Project/LanguageAdapter.hpp`). C++ is the only implementation and the only language
whose workflow is required to work; nothing in the generic model branches on which language a
project is written in. Recorded here, with what was rejected, because the decision is not
reversible cheaply once it has been made the other way.

### 13.1 Why the invariant had to be generalised

The invariant this whole product is held to used to read:

> CNA is the framework. CNA Studio is the professional authoring environment. A CNA Studio project
> remains a CNA **C++** project.

That is right about what is being built first and wrong as a permanent architectural statement, for
a reason that has nothing to do with ambition: **CNA has several language bindings**, and a project
authored for one of them is still a CNA project. The sentence as written makes C++ a property of
*Studio's model* rather than of a project, and a model with that in it grows the shape below without
anybody deciding to:

```
if (language == Cpp)  { … }
else if (language == …) { … }
```

in the Project Hub, in project creation, in build orchestration, in opening a source file, in
component metadata, in packaging. Each addition is individually reasonable; the sum is a rewrite of
every panel that grew one. So the invariant is now:

> **CNA is the framework. CNA Studio is the authoring environment. A project authored by Studio
> remains a normal project for CNA or one of its supported bindings, and builds and runs without
> CNA Studio.**

and for C++ specifically:

```
C++ project ──▶ the project's own CMake ──▶ CNA
```

Studio drives the native build system of the selected project language. It never replaces it. The
three consequences in §1 are unchanged and apply to every language: Studio is never a runtime
dependency, hand-written source is never overwritten, and authored data stays human-readable,
deterministic and version-control friendly. Game and user code continues to execute outside the
Studio process.

### 13.2 The shape

```
CNA Studio
    |
    +-- StudioLanguageRegistry          an ordinary object, constructed and handed to what needs it
            |
            +-- C++ adapter             implemented; drives CMake
            +-- binding adapter         future; none written on speculation
            +-- ...
```

`StudioContext` holds the registry, next to the component registry and for the same reason: it is
shared, non-UI state that every panel needs and none should construct for itself. It is not a
service locator — it is a member of an object that is already a constructor argument everywhere,
which is the distinction §10.1 draws.

A `.cnaproject` carries `"language": "cpp"`. The key is written only when set, so an existing
project file does not gain a diff the first time Studio touches it; an absent key means "this
build's default", which the registry resolves, and which is C++ because C++ is the only language
Studio has ever authored.

### 13.3 What the adapter owns

Each row is a boundary that was already present in the code or in the plan. None was invented for a
language that does not exist yet — the seam is the size of the boundaries that are real today, and
`STUDIO-02085`'s guard test checks that this table and the interface still agree.

| Boundary | Where it lives now |
|----------|--------------------|
| project language/toolchain identity | `StudioLanguageDescriptor::id`, `displayName`, `toolchainName` |
| project/template compatibility with a language | `supportsProjectKind`, and a template's `languages` list |
| toolchain availability | `probeToolchain` — asked before anything is offered, never after a failure |
| configure/build commands | `planBuild`, returning a `StudioBuildJob` the language-neutral `BuildProcess` runs |
| creation of source/project files | `projectFiles`, given a `StudioProjectScaffold` |
| run/debug/Play integration | the built executable is where `planBuild` put it; `cna-player` is language-neutral and stays so |
| opening the project and source files in an external IDE | `StudioLanguageDescriptor::sourceFileExtensions` and `sourceDirectory` locate them (`STUDIO-15011`) |
| gameplay-component metadata | the files it is read from are `sourceDirectory` + `sourceFileExtensions`; the mechanism is `STUDIO-15001`, still open |
| generated code ownership | `generatedDirectory`, a subtree the tool owns outright |
| hand-written source ownership | `sourceDirectory`, which generation never writes into |
| package/export workflow | `exportStandalone` |
| standalone build verification | `standaloneBuildInstructions`, which is what `--export` prints and what CI runs |

### 13.4 What the C++ adapter owns, concretely

Everything under `include/CNA/Studio/Project/Cpp/` and `src/project/cpp/`: CMake discovery, the
configure and build command lines, the translation of a `StudioTargetProfile` into `-D` cache
entries, the generated `CMakeLists.txt` and `Source/Main.cpp`, the project-owned scene runtime that
travels inside an exported tree, and the standalone export itself. It reproduces exactly what
Studio did before the seam existed; introducing an abstraction and a behaviour change together is
how a regression becomes impossible to attribute.

### 13.5 How the boundary is enforced

Three guard tests, not prose:

| Guard | What it prevents |
|-------|------------------|
| `OnlyTheCppLanguageAdapterKnowsHowACppProjectIsBuilt` | Any file outside the adapter's two directories including one of its headers or naming one of its symbols |
| `NoStudioCodeBranchesOnWhichLanguageAProjectIsWrittenIn` | The `if (language == …)` chain, in the generic model |
| `TheLanguageSeamStillCarriesEveryBoundaryItWasIntroducedFor` | §13.3 and the interface drifting apart |

The rule is a **directory**, not a list of exempt files: a list has to be edited when a file is
added, which means it gets edited by whoever is adding the file that breaks it. There is exactly one
named exemption, `src/project/StudioBuiltInLanguages.cpp` — somewhere has to name the adapters a
build ships, and that file holds that one function and nothing else, so the exemption cannot quietly
come to cover something more.

### 13.6 Rejected alternatives

| Rejected | Why |
|----------|-----|
| **Leave it. C++ is the only language and may be the only one** | The cost of the seam is one interface with one implementation. The cost of not having it is discovering, when a binding is finally wanted, that the answer is spread across the Hub, the Build panel, the export command and the packaging workflow. The two costs are not comparable, and only one of them is paid later |
| **A plugin framework: discovery, manifests, dynamic registration** | Every part of that is machinery for a problem nobody has. There is one adapter, compiled in, listed in one function. A registration protocol before there is anything to register is a protocol designed against imagined requirements |
| **Speculative adapters for CNA's other bindings** | Writing an adapter for a binding whose actual contract has not been inspected produces a guess that later has to be *unpicked* rather than extended — and it would be the only implementation nobody could test. The seam's value is that adding one is an addition |
| **A `ProjectLanguage` enum in the generic model** | An enum is a branch waiting to be written, and the compiler encourages it: every `switch` over it with a `default` is a place a new language silently does the wrong thing. A string id resolved once to an adapter has no such site |
| **Put the seam at the build step only** | Building is the loudest boundary but not the only one, and the others are the ones that would have rotted quietly: creating a project's files, packaging it, locating its source. A seam around the build alone would have let the Project Hub learn what a `CMakeLists.txt` is, which is the failure this exists to prevent |
| **Generalise the toolchain path preference to a per-language map now** | Not earned. `StudioPreferences::cmakePath` is a persisted key with one language behind it; a map with one entry buys nothing and costs a format migration. Recorded as a known non-generic remainder on `STUDIO-02087` rather than done on speculation |
| **A visual scripting language as the second "binding"** | Out of scope by an existing decision (`plan.md`, *Deliberately not built*). C++ first, and excellent, before anything else |

### 13.7 What this deliberately does not do

It does not make Studio multi-language. The language chooser in the Project Hub offers what the
registry holds, which today is C++ alone. It does not build an embedded IDE, add visual scripting,
or move any gameplay or runtime responsibility out of CNA or a binding and into Studio. It buys one
property and no others: **adding a CNA binding later is an addition, not a rewrite.**
