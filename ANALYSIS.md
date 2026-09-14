# CNA Editor — Architecture Analysis

> **Purpose.** This document is the *reasoning* behind `plan.md`. It takes the original editor
> architecture discussion, checks every assumption in it against the CNA codebase as it actually
> exists, records where the two disagree, and states the decisions the design is built on and why
> each alternative was rejected.
>
> **Method.** Every claim about CNA below was verified against the `openeggbert/cna` repository at
> revision `ac3aaae` (2026-08-03), not inferred from documentation. File and line references point
> into that repository.
>
> **Status legend.** ✅ verified against the codebase · ⚠️ corrects the original discussion ·
> 🔬 needs a spike before it can be settled.
>
> **Update, 2026-08-03.** Questions Q-01 (can Dear ImGui render through CNA's public API?) and
> Q-04 (where does `cna-player` live?) are both resolved — see §4. Two new decisions, **D-14** and
> **D-15**, record what came out of them. The Q-01 spike has its own report:
> [`docs/SPIKE-IMGUI-CNA.md`](docs/SPIKE-IMGUI-CNA.md).
>
> **Second update, same day.** The editor now opens in a window and renders, built and *linked*
> against a real CNA checkout rather than merely compile-checked. Decision **D-16** records how the
> window host is structured. Two further CNA observations came out of running it for real — F-07
> below.

---

## 0. Summary

The original discussion is architecturally sound and most of it survives review intact. Its core
thesis — *the editor is a set of tools built on top of CNA's public API, not a new mandatory
engine* — is right, and this repository is built on it.

Six things changed after checking the code:

| # | Finding | Effect |
|---|---------|--------|
| **F-01** | CNA's graphics backend is a **compile-time** choice, not a runtime one | ⚠️ Large. Kills `cna-editor --graphics=vulkan`; makes multi-process play mode structurally mandatory rather than merely prudent |
| **F-02** | CNA has **14** graphics backends, not 30–35 | ⚠️ Small. The support-tier table is concrete and testable rather than aspirational |
| **F-03** | `GraphicsDevice` lives in `Microsoft::Xna::Framework::Graphics`, not `CNA::Graphics` | ⚠️ Small, but every API sketch in the discussion had the namespace wrong |
| **F-04** | CNA is a **standalone repository** with sibling-checkout dependencies, and has no install/export rules | ⚠️ Medium. The proposed `cna/tools/cna-editor/` monorepo path does not match reality |
| **F-05** | CNA is C++23 with a `System::` runtime layer from `sharp-runtime` | ✅ Confirms the technology baseline; constrains what the editor may reuse |
| **F-06** | CNA vendors no UI toolkit, and no ImGui anywhere | ⚠️ Small. Dear ImGui is a *new* dependency owned by this repository, not a shared one |

Everything else in the discussion — the document model, undo-by-command, hand-written reflection,
asset ids, the plugin manifest, the phase ordering, and the explicit "do not build this yet" list —
is adopted essentially as proposed.

---

## 1. Findings: where the discussion and the codebase disagree

### F-01 — CNA's graphics backend is selected at compile time ⚠️

This is the single most consequential finding, and it invalidates a recurring assumption.

The discussion proposed launching the editor with a runtime switch:

```
cna-editor --graphics=vulkan
cna-editor --graphics=opengl
cna-editor --graphics=software
```

CNA cannot do this. `cmake/BackendSelection.cmake` resolves `CNA_GRAPHICS_BACKEND` to exactly one
`CNA_BACKEND_*` preprocessor definition per build, and `include/CNA/GraphicsBackendType.hpp` reads
it in a `constexpr` function:

```cpp
constexpr GraphicsBackendType getCurrentGraphicsBackendType()
{
#if defined(CNA_BACKEND_SDL_RENDERER)
    return GraphicsBackendType::SdlRenderer;
#elif defined(CNA_BACKEND_EASYGL)
    return GraphicsBackendType::EasyGL;
// ... one branch per backend ...
#else
#error "CNA: no CNA_BACKEND_* compile definition set"
#endif
}
```

The header's own comment is explicit: *"Resolved entirely from the `CNA_BACKEND_*` compile
definition … so this is a compile-time constant — usable in a constant expression."* One CNA build
contains one backend. There is no backend registry, no runtime dispatch, and no way to load a
second one.

**Consequences, in order of severity:**

1. **`--graphics` cannot be an editor option.** One editor binary is permanently bound to the
   backend it was compiled against. The skeleton therefore *rejects* the flag with an explanatory
   message rather than ignoring it (`src/app/EditorApplication.cpp`), and a test asserts the
   rejection. Silently accepting it would teach users a mental model the framework does not have.

2. **Multi-process play mode stops being a robustness nicety and becomes structural.** The
   discussion argued for a separate `cna-player` process on the grounds that a game crash should
   not take the editor down. That argument is good but secondary. The real reason is that the
   editor and the previewed game are linked against *different CNA builds* and cannot share an
   address space at all. In-process play mode is not a simpler alternative here — it is
   unimplementable except for the editor's own single backend.

3. **Backend comparison mode becomes cheaper than it looked, and more valuable.** The discussion
   presented it as an ambitious Phase 4 feature. In fact it falls out of the architecture: if
   preview already means "spawn a player process", then spawning four of them from four
   per-backend builds is the same mechanism run four times. What it needs is not new architecture
   but a build matrix — one `cna-player-<backend>` binary per configuration — plus screenshot
   capture and image diffing.

4. **The editor's own UI has a backend requirement independent of the game's.** Which motivates
   the support tiers below.

### F-02 — There are 14 backends, and they are named ⚠️

The discussion spoke of "approximately 30–35 graphics backends". `cmake/BackendSelection.cmake`
lists 14:

`SDL_RENDERER`, `EASYGL`, `BGFX`, `VULKAN`, `WEBGPU`, `HEADLESS`, `SOFTWARE`, `D3D11`, `D3D12`,
`CANVAS`, `ASCII`, `DX3`, `D3D9`, `SDL_GPU`.

This is good news, not bad: the discussion's three-tier support model (*Editor Supported* /
*Preview Only* / *Runtime Only*) can be a concrete table rather than a policy. It is implemented as
data in `include/CNA/Editor/Project/Project.hpp` and asserted by
`tests/ProjectAndAssetTests.cpp`, so that adding a backend to CNA produces a failing test here as
the reminder to classify it:

| Tier | Backends | Rationale |
|------|----------|-----------|
| **Editor Supported** | EASYGL, VULKAN, SDL_RENDERER, BGFX, SDL_GPU, D3D11, D3D12 | Can host a docked editor UI at an arbitrary window size |
| **Preview Only** | WEBGPU, D3D9, SOFTWARE | Correct enough to render a game, unsuitable as an interactive UI host — SOFTWARE is too slow, D3D9 too limited, WEBGPU too experimental |
| **Runtime Only** | CANVAS, ASCII, DX3, HEADLESS | No desktop process to host (CANVAS is Emscripten-only), no window at all (HEADLESS), or a deliberately lossy presentation (ASCII, DX3) |

The discussion's own example — "a historical DirectX 1 backend need not render the whole editor UI;
run the editor on Vulkan and show the scene through DX1 in a preview process" — is exactly right,
and DX3 is the real backend it applies to.

### F-03 — The namespace is `Microsoft::Xna::Framework`, not `CNA` ⚠️

Every API sketch in the discussion used `CNA::Graphics::GraphicsDevice`. CNA's actual public layout
is two-tier:

- `include/Microsoft/Xna/Framework/**` — the XNA 4.0-compatible surface: `Game`, `GraphicsDevice`,
  `SpriteBatch`, `Texture2D`, `Color`, `Matrix`, `Vector3`, `Rectangle`, `Content`, `Audio`,
  `Input`, `Media`, `Net`, `GamerServices`.
- `include/CNA/**` — CNA's own extensions: `Logger`, `Platform`, `GraphicsBackendType`,
  `GraphicsCapability`, `RenderPipelineSettings`, `PbrMaterial`, `Devices`.

Method naming follows the property convention seen throughout the examples —
`getGraphicsDeviceProperty()`, `setTitleProperty()`. The `System::` namespace (`System::TimeSpan`,
`System::Exception`) comes from the `sharp-runtime` sibling repository.

This is cosmetic for the architecture but matters for every signature the editor writes, and it is
corrected throughout this repository.

### F-04 — CNA is a standalone repository, not a monorepo with a `tools/` tree ⚠️

The discussion proposed:

```
cna/
├── runtime/
├── modules/
├── tools/
│   ├── cna-editor/
│   ├── cna-content-builder/
│   └── cna-player/
└── editor-sdk/
```

The real `openeggbert/cna` has a `tools/` directory, but it holds development utilities
(`cna-reference`, `xna-oracle`, `fna-reference`, `gltf_to_cnj`, `avatar_builder`), not shipped
applications. More importantly, `cna-editor` already exists as its **own repository**, alongside
`cna-gltf-viewer`, `cna-examples`, `cna-craft` and others — the project is already organised as a
family of sibling repositories.

CNA also has **no install or export rules**: no `install()` calls, no package config, no
`find_package(CNA)`. Its own dependency on `sharp-runtime` is handled by `add_subdirectory` against
a sibling checkout, with a `FATAL_ERROR` that spells out the fix.

The editor therefore follows the same pattern the framework already uses on itself, which is
decision D-03 below.

### F-05 — Technology baseline ✅

Confirmed from `CMakeLists.txt` and the source tree:

| Aspect | Value |
|--------|-------|
| Language | C++23 (`CMAKE_CXX_STANDARD 23`, extensions off) |
| Build | CMake ≥ 3.20, with CMakePresets |
| Licence | `MS-PL`, SPDX header on every file (`header.txt`) |
| Documentation | Doxygen throughout, `@brief`/`@param`/`@return` |
| Runtime | `sharp-runtime` sibling checkout supplies the `System::` layer |
| Third-party | SDL3, SDL_image, SDL_mixer, cgltf, ENet, stb — all vendored |
| Exceptions | Used pervasively; explicitly enabled even under Emscripten |
| Tests | CTest, with per-backend suites in `cmake/Tests/` |

This repository matches all of it: C++23, CMake ≥ 3.20, MS-PL SPDX headers, Doxygen comments,
CTest.

### F-07 — Two things only a real run could find ✅

Compiling against CNA's headers proved the API exists. Actually *running* the editor found two
things a compile could not.

**A backend can render without a window.** The host first took its drawing size from
`GameWindow::getClientBoundsProperty()`, which returns 0×0 on the SOFTWARE backend — that backend
creates no SDL window at all. The editor rendered a zero-sized UI and issued zero draw calls while
exiting perfectly cleanly. The fix is to take the size from
`GraphicsDevice::getViewportProperty()`, the surface actually being drawn into. That is more
correct in general: on a scaled or letterboxed presentation the window and the viewport differ, and
the viewport is what a renderer needs.

Worth noting the tier table (F-02) predicted this: SOFTWARE is classified *Preview Only* precisely
because it cannot host an editor UI in the ordinary sense.

**A clean exit proves nothing.** A window that opens, loops and closes having drawn nothing is
indistinguishable from a working editor unless you look at the pixels. That is why
`--screenshot=PATH` exists and why `CnaEditorWindowSmoke` asserts on a written PNG rather than on
an exit code. The same mechanism — `GraphicsDevice::GetBackBufferData` plus
`Texture2D::SaveAsPng`, both public API — is what plan.md ED-510's backend comparison mode will
capture through, so building it now costs nothing later.

### F-06 — No UI toolkit is vendored ⚠️

`third_party/` contains SDL, SDL_image, SDL_mixer, cgltf, enet and stb. There is no Dear ImGui, no
Qt, no widget library of any kind anywhere in CNA.

Dear ImGui is therefore a **new dependency, owned by this repository**, not something inherited.
That raises the cost of the choice slightly and is one more reason for the abstraction in D-02:
the dependency is contained in one module, and its removal would be a contained change.

---

## 2. Design decisions

Each decision states what was chosen, why, what was rejected, and — where it exists — where it is
implemented in this repository.

### D-01 — The editor uses CNA's public API only

**Decision.** `cna-editor-viewport` links CNA and calls `Microsoft::Xna::Framework::*`. It never
includes `CNA::Internal::*`.

**Why.** The discussion's original argument stands and is worth restating: if the editor cannot
draw a scene using only the API a game has, then neither can a game. Every place the editor would
*want* internal access is a gap in CNA's public surface, and finding those gaps is a benefit of
building the editor, not an obstacle to it.

**Rejected.** Privileged access "just for the viewport". It would hide exactly the information the
exercise is meant to produce, and it would couple the editor to CNA's internal refactors.

### D-02 — All UI goes through `cna-editor-ui`; Dear ImGui is one implementation

**Decision.** No panel, command or plugin may call a UI toolkit directly. They call the abstract
`EditorUi` (`include/CNA/Editor/Ui/EditorUi.hpp`). Dear ImGui becomes the first real
implementation in Phase 0; `NullEditorUi` already exists and is a first-class one.

**Why.** Three reasons, in order of weight:

1. **The toolkit choice stays reversible.** F-06 makes ImGui a dependency this project owns and
   would have to maintain. Committing to it irreversibly, before the editor has any real panels,
   is a bet placed at the moment of least information.
2. **The panel layer becomes testable with no display.** `tests/ApplicationTests.cpp` runs the
   *real* `EditorApplication` — same code path a user gets — over `NullEditorUi` and
   `NullEditorViewport`, on a build machine with no GPU. This is why `NullEditorUi` reports panels
   as *visible* and tree nodes as *expanded*: a headless frame walks every panel body and the whole
   hierarchy, so `--headless` is a genuine smoke test rather than a no-op.
3. **Plugin ABI stability.** A plugin compiled against `EditorUi` survives a toolkit change.

**Cost.** One virtual call per widget. At editor frame rates this is not measurable.

**Why immediate-mode.** The abstraction is immediate-mode because that is the cheaper direction to
adapt: wrapping a retained toolkit (Qt) in an immediate-mode facade is routine; the reverse
requires inventing state the immediate-mode toolkit never had.

**Rejected.** *Writing a custom GUI toolkit* — the discussion is right that this would sink the
first version. *Qt for Phase 1* — heavier, and the LGPL/commercial question against MS-PL is a
licensing conversation this project does not need yet. It stays a documented, costed Phase 2+
option.

### D-03 — CNA is an optional sibling checkout, off by default

**Decision.** `CNA_EDITOR_WITH_CNA=OFF` by default. When ON, `add_subdirectory(../cna)`, matching
CNA's own `../sharp-runtime` pattern (F-04). Only `cna-editor-viewport` links CNA.

**Why.** The document model, the undo stack, the asset database, the project format and the wire
protocol have nothing to do with graphics. Keeping the default build CNA-free means this repository
clones and builds in seconds with no `../cna`, no `../sharp-runtime`, no GPU and no window — which
is what makes the test suite run in CI, and what makes a contributor's first build succeed.

The dependency edge is enforced by the build graph rather than by review: nothing but
`cna-editor-viewport` links `CNA`, so a stray `#include <Microsoft/Xna/...>` in the document model
fails to compile.

**Rejected.** *Required checkout* — would make the editor unbuildable alone for no gain.
*FetchContent* — slow configure, and awkward when CNA is being developed in parallel, which it is.
*Fully decoupled plugin viewport* — the cleanest end state and a plausible Phase 3 refactor, but it
front-loads a dynamic-loading problem before there is anything to load.

### D-04 — An entity/component **document** model, not an ECS, and no ECS in CNA

**Decision.** The editor has entities with components. CNA does not, and is not asked to.
`EditorEntity`/`EditorComponent` are *document* types (`include/CNA/Editor/Scene/EditorEntity.hpp`).

**Why.** This is the decision that keeps CNA an XNA-compatible framework. An entity graph is a good
way to *author* a scene and a poor thing to force on a game that already has its own object model.
The editor compiles its document into whatever the game wants at load time: CNA objects, plain C++
classes, a factory callback.

**Consequence.** Components store their fields in a name-keyed map rather than in real C++ members.
The cost is a map lookup per field access — irrelevant at editor rates. The payoff is that a
plugin-supplied component type needs no compiled code in the editor, and that a scene referencing a
component whose plugin failed to load still round-trips through save and load with nothing lost.
`ScenePreservesUnknownComponentTypes` in `tests/SceneTests.cpp` asserts exactly this, because
"the plugin is missing" turning into "the plugin's data is gone" is unacceptable.

### D-05 — Hand-written reflection metadata

**Decision.** `ComponentDescriptor` + `PropertyDescriptor` + `PropertyValue`
(`include/CNA/Editor/Core/`), registered at runtime into a `ComponentRegistry`.

**Why.** C++ has no usable reflection, and the discussion is right that this is the most
load-bearing part of the editor SDK. Everything downstream is generic over it: the inspector builds
its widgets by walking a descriptor; one `SetPropertyCommand` undoes a change to any property of
any component; the serialiser reads and writes component JSON with no per-type code path. A plugin
registering a descriptor at runtime gets all three for free.

**Implemented.** 13 property types (bool, integer, float, string, enum, colour, Vector2/3/4,
quaternion, rectangle, asset reference, entity reference). Asset and entity references are
*distinct* alternatives rather than plain UUIDs, because the inspector needs the distinction to
pick a widget and the asset database needs it to walk a scene's outbound references.

**Deferred.** `List` and `NestedStructure`, both named in the discussion. They need inspector
support that does not exist yet; `ModelRenderer` carries a single material override until then.

**Rejected.** *Macro-based registration* as the only mechanism — the explicit descriptor form is
what a plugin uses across an ABI boundary. A `CNA_EDITOR_COMPONENT` macro can be sugar over it
later.

### D-06 — Undo is a hard rule from day one

**Decision.** Every document mutation is an `EditorCommand` pushed through `CommandHistory`. Not a
convention — an invariant.

**Why.** The discussion's warning is exactly right, and it is the one architectural mistake that
cannot be repaired incrementally: by the time you notice undo is missing, every call site is a
place where undo silently does not work.

**Implemented.** `CreateEntity`, `DeleteEntity`, `RenameEntity`, `ReparentEntity`, `SetProperty`,
`AddComponent`, `RemoveComponent`, plus merging via `MergePolicy::MergeWithPrevious`. The suite
covers the cases editors habitually get wrong:

- **Merging collapses a drag into one entry that undoes to where the drag started** — not to the
  penultimate mouse position. `SetPropertyCommandMergesAcrossADrag` drives 50 changes and asserts
  one history entry and a full return to origin.
- **Merging is keyed on entity + component + property**, so alternating between two objects stays
  two entries.
- **`isDirty()` moves in both directions.** Undoing back past a save marks the document dirty
  again: the file on disk no longer matches memory, even though the change count went down. Editors
  routinely get this wrong.
- **Undo restores *absence*, not just value.** A component loaded from a scene file that omitted an
  optional field must be able to go back to omitting it, or an undone edit silently starts writing
  a field the file never had.
- **Deleting an entity restores its whole subtree with the hierarchy intact**, not as loose roots.

### D-07 — Editor state is separated from runtime data in the same file

**Decision.** `.cnascene` carries an `editorState` object per entity, distinct from `components`.

**Why.** Tree expansion, layer colours, notes and icon overrides are cosmetic. Keeping them in a
named sub-object means the runtime scene compiler drops them wholesale rather than having to know
which of a component's fields are cosmetic.

**Deliberately not done.** A separate `.cnascene.user` sidecar. It would double the file count and
guarantee the two drift apart under version control.

### D-08 — Assets are referenced by id, never by path

**Decision.** Every asset gets a UUID in a `.cnaasset` sidecar next to its source file. Scenes
store the id. `AssetDatabase` maps id to current path.

**Why.** Moving `Assets/player.png` into `Assets/Characters/` then touches nothing: no scene
changes, no reference breaks, and the move is a one-line diff in a sidecar instead of a
hundred-line diff across every scene that used the texture.
`AssetDatabaseKeepsIdentityAcrossAMove` asserts it.

**Two consequences worth stating.**

*A missing source file keeps its record.* A file that is gone today may be one `git checkout` away
from returning, and deleting the record would permanently break every reference to it. The database
reports it as missing instead (`AssetDatabaseReportsMissingSourcesRatherThanDroppingThem`).

*Change detection uses size and modification time, not a content hash.* Hashing every asset on
every project open is how an editor comes to take thirty seconds to start. A hash can be added
later as an opt-in for correctness-critical pipelines. **A real bug was found here during
implementation**: the modification stamp was originally the filesystem clock's native tick count —
around 4.6 × 10¹⁸ nanoseconds — which is outside the range a `double` represents exactly. JSON
numbers are doubles, so it round-tripped through the sidecar *changed*, and every asset would have
looked modified on every scan. The stamp is now stored in seconds, and
`AssetSidecarStampSurvivesAJsonRoundTrip` guards it.

### D-09 — Play mode is a separate process, over line-delimited JSON

**Decision.** `cna-player` is its own binary, built once per backend. The editor spawns it and
talks over a stream socket, one JSON object per line
(`include/CNA/Editor/RuntimeBridge/EditorProtocol.hpp`).

**Why separate process.** F-01 makes it mandatory, not merely wise. The discussion's own reasons —
a crash cannot take the editor down, historical and 32-bit builds become testable, reload is
simpler, the simulation is more faithful — all still apply on top.

**Why line-delimited JSON.** The traffic is a handful of messages per second. Being able to watch a
session with `nc` during bring-up is worth far more than the bytes a binary format would save. It
can be replaced later without changing the message model.

**Two details that matter.** The framing is the newline, so a message body must contain exactly one
and it must be last. And a `MessageStreamDecoder` reassembles arbitrary chunks: a reader that
assumes one `recv()` equals one message works right up until a message straddles a packet boundary,
and then fails in a way that is very hard to reproduce. `MessageDecoderReassemblesSplitMessages`
feeds the stream in 7-byte slices to prove it.

`SetProperty` carries its own value type on the wire, unlike a scene file, because the player's
component registry may not match the editor's after a plugin reload.

### D-10 — Two project kinds, so CNA never becomes a mandatory engine

**Decision.** `.cnaproject` declares `kind`: `CnaNative` or `XnaCompatible`
(`include/CNA/Editor/Project/Project.hpp`).

**Why.** This is the concrete mechanism behind the discussion's most important architectural point.
A `CnaNative` project opts into scenes, entities, the inspector, gizmos and the runtime bridge. An
`XnaCompatible` project does not: the editor is an asset and content-pipeline tool plus a launcher,
and the game keeps its own hand-written `Initialize`/`LoadContent`/`Update`/`Draw` with no editor
concepts in it. A pure XNA port must never be forced through the entity model to use the tooling.

`EditorContext::openProject` honours this — it does not load a startup scene for an
`XnaCompatible` project — and warns when such a project declares one, because that is a real
mismatch worth surfacing rather than silently honouring.

### D-11 — Plugins: manifest first, `extern "C"` entry point, validate before loading

**Decision.** A plugin is `plugin.json` plus a shared library. The manifest is read and validated
*before* the library is opened. The entry point is `extern "C"`.

**Why validate first.** An ABI mismatch that reaches `dlopen` is a crash, not an error message. The
Phase 1 implementation deliberately stops short of loading: it discovers manifests, checks API
versions, orders dependencies and reports what *would* load. Getting rejection right before loading
exists means the eventual dynamic loading has nothing left to get wrong except the loading itself.

**Why `extern "C"`.** A C++ symbol would bind the plugin to the exact compiler and standard library
the editor was built with — something no plugin author can be expected to match. Allocation and
deallocation both happen inside the plugin's own runtime, via paired create/destroy exports;
mixing allocators across a shared-library boundary is the classic way to make a plugin system crash
only in release builds.

### D-12 — Zero external dependencies in the editor core

**Decision.** `cna-editor-core` links nothing but the C++ standard library. That includes the JSON
implementation (`src/core/Json.cpp`, ~400 lines) and the test harness
(`tests/TestHarness.hpp`, ~80 lines).

**Why.** It is what makes D-03 real. A dependency in core would propagate to every module and undo
the "clones and builds in seconds" property. A test framework would be the first dependency to
sneak back in, which is why the harness is hand-written.

**Two editor-friendly relaxations** in the JSON parser: `//` line comments and trailing commas.
These files are hand-edited often enough to justify it.

**When to revisit.** If the suite needs fixtures, parameterised cases or death tests, adopting
GoogleTest is a contained change — nothing outside `TestHarness.hpp` knows how assertions are
spelled. If profiling ever shows JSON parsing dominating project-open time, a vendored fast parser
is likewise contained behind `Json::parse`.

### D-14 — The toolkit boundary is a data type, not an interface

**Decision.** Dear ImGui produces a `UiDrawData` (geometry) and consumes a `UiInputState` (input).
`CnaUiRenderer` and `CnaUiPlatform` sit on the other side of those two types. Neither side includes
the other's headers, and in particular **`CnaUiRenderer.cpp` contains no ImGui header**.

**Why.** The obvious design is for the renderer to implement an abstract `ImGuiRenderer` interface
and receive `ImDrawData*`. That works, and it was rejected for three reasons:

1. **It makes the D-01 claim uncheckable.** "The editor UI renders through CNA's public API" should
   be a property of the build graph, not something a reviewer re-verifies by hand each time. With
   the data seam, a renderer that reached for ImGui internals would not compile.
2. **It costs nothing.** ImGui's vertex layout and CNA's `VertexPositionColorTexture` differ, so a
   per-vertex repack is required whichever way this is structured. Doing it while filling
   `UiDrawData` means it happens exactly once, in the place that was going to pay for it anyway.
3. **It makes the whole UI testable headless.** `tests/UiTests.cpp` runs the real
   `EditorApplication` over the real Dear ImGui, drives frames of synthetic input, and validates
   every draw command's index ranges, vertex offsets and clip rectangles — with no window, no GPU
   and no CNA checkout. That is why `CNA_EDITOR_WITH_IMGUI` defaults to **ON** while
   `CNA_EDITOR_WITH_CNA` defaults to OFF: ImGui is portable C++ with no system dependencies, so
   building it costs only compile time and buys real CI coverage.

**Also decided here.** `ImGuiEditorUi` owns the `UiTextureId` namespace rather than the renderer.
ImGui 1.92 asserts the moment a draw command references a texture whose id is unset, and draw
commands are read in the same pass that collects texture requests — so "renderer assigns ids and
reports them back" cannot work without a two-phase frame. Details in docs/SPIKE-IMGUI-CNA.md §6.

### D-15 — `cna-player` lives in this repository

**Decision.** The player binary is built here, from `src/player/`, not in `openeggbert/cna`.

**Why.** It was a genuine toss-up (recorded as Q-04) and the project owner chose this repository.
The reasoning that supports it: the player exists *because the editor needs it*, it speaks the
editor's protocol, and its message-handling half (`PlayerHost`) is pure editor-side logic that
shares the scene document, the asset database and the project format with the editor itself.
Splitting those across repositories would mean versioning the protocol *and* the document model
across a repository boundary for no gain.

**Consequence — the build matrix.** Because CNA fixes its backend at compile time (F-01), a real
installation ships one player per backend: `cna-player-easygl`, `cna-player-software`,
`cna-player-d3d11`. The CMake here names the binary after `CNA_GRAPHICS_BACKEND` when built with
CNA, and `discoverPlayerBuilds()` finds whichever are installed — so the editor offers the user
only the backends that actually exist, rather than a menu of fourteen of which two work.

**What this bought immediately.** `PlayerHost` is deliberately CNA-free: it decides what a message
means and tracks play/pause/step, while drawing belongs to the CNA-linked main loop. So the whole
message surface is unit-tested headless, and a `cna-player` built *without* CNA still speaks the
entire protocol — which is what the editor's own end-to-end bridge test runs against.

### D-16 — The window host is a free function, not an exported `Game` subclass

**Decision.** `runEditorInWindow(options, application)` is the public interface. The
`Microsoft::Xna::Framework::Game` subclass that implements it lives entirely inside
`src/viewport/CnaEditorHost.cpp`.

**Why a `Game` subclass at all.** The editor is structurally a CNA application: it wants a window,
a graphics device, a frame loop and input, which is exactly what `Game` provides. Building a
parallel window/loop abstraction beside it would mean maintaining a path the games being edited
never exercise — and would quietly weaken D-01, since the editor would stop eating its own dog food
at the layer most worth testing.

**Why the class is not exported.** A base class cannot be hidden behind a pimpl. Exposing the class
publicly would put `Game` in the public interface, which forces `cna-editor-viewport` to link CNA
`PUBLIC` — and then every target linking the viewport can reach CNA headers, and the layering rule
stops being enforced by the build graph. The free function keeps CNA a *private* link dependency.
This was found the honest way: the first attempt exported the class and the build broke, which is
the build system correctly refusing a design that would have weakened D-03.

**Same reasoning applied to `cna-player`.** It needs the compiled-in backend name, which it now
asks for through `CnaUiRenderer::getBackendName()` rather than by including `CNA/GraphicsBackendType.hpp`
itself. One module includes CNA; everything else asks that module.

### D-13 — The editor's own math types, duplicated from CNA's

**Decision.** `EditorVector2/3/4`, `EditorQuaternion`, `EditorColor`, `EditorRectangle` in
`include/CNA/Editor/Core/EditorMath.hpp`, laid out to match their CNA counterparts field for field.

**Why.** A direct consequence of D-03: using `Microsoft::Xna::Framework::Vector3` in the document
model would drag CNA into `cna-editor-core` and make the default build require a CNA checkout.
Conversion happens in exactly one place — `cna-editor-viewport` — where the dependency exists
anyway, and matching layout keeps it a field copy rather than a reinterpretation.

**Cost, stated honestly.** Two parallel type families that must be kept in sync by hand. This is a
real maintenance tax, accepted because the alternative — a CNA-dependent core — costs more.

---

## 3. Adopted from the discussion without material change

- The module split, and the principle that a 2D project must not link 3D import, skeletal
  animation, glTF or CSG.
- JSON for `.cnaproject`, `.cnascene` and `.cnaasset`: readable, diffable, mergeable, migratable.
  A binary runtime form is the content builder's job, not the editor's.
- Editor overlay as a separate render pass, never as objects in the game's scene. Selection
  outlines, grids, gizmos and icons are editor artefacts; putting them in the scene graph means a
  build eventually ships with them.
- Ray-cast picking before GPU picking. GPU picking is an optimisation to reach for when profiling
  asks, not before.
- The phase ordering, including 3D arriving after the glTF conformance work rather than before.
- **The entire "do not build in v1" list**, which is the most valuable paragraph in the original
  discussion: no custom GUI toolkit, no ECS in CNA, no visual scripting, no material graph, no
  embedded IDE, no shared process, no editor UI on all backends, no multi-user collaboration, no
  prefab variants, no physics engine, no full 3D editor.

---

## 4. Open questions

✅ **Q-01 — Does Dear ImGui integrate cleanly with every Editor Supported backend? — RESOLVED: yes.**
The spike ran and passed; the renderer and platform layer are implemented and compile against real
CNA headers. Everything an immediate-mode UI needs is in CNA's public API — including, against
expectation, proper text input via `TextInputEXT` rather than synthesised key codes. One
implementation serves every backend; no per-backend renderer, no authored shader. Two minor CNA
gaps were found and are worth filing upstream: `Color` is not default-constructible (so
`std::vector<Color>::resize` does not compile), and the clipboard sits behind the optional
`CNA_DEVICES` feature. Full report and capability table: **docs/SPIKE-IMGUI-CNA.md**.

✅ **Q-02 — How does a game consume a compiled scene? Resolved.**
A **header-only loader shipped from this repository**: `include/CNA/Editor/Runtime/SceneLoader.hpp`.
CNA never has to know what a `.cnascene` is, which keeps D-01 and D-03 intact, and the choice is
reversible — moving the loader into CNA later is a relocation rather than a rewrite, while a format
that has been part of CNA's public surface for a release cannot be taken back out of it. The
options rejected and what the decision costs: **docs/DESIGN-SCENE-LOADER.md**. Delivered as ED-250.

🔬 **Q-03 — Which process owns the window in play mode?**
Embedding the player's output in the editor's viewport panel needs either a shared surface, an
embedded child window, or a copied framebuffer over the socket. The cheapest first answer is a
separate top-level window, which is what Phase 1 does. Revisit when it becomes annoying, not
before.

✅ **Q-04 — Should `cna-player` live here or in `openeggbert/cna`? — RESOLVED: here.**
Decided by the project owner. See decision **D-15** for the reasoning and for the build-matrix
consequence. Implemented: `src/player/`, plus `MessageChannel` (loopback TCP transport) and
`PlayerProcess` (spawn, supervise, discover installed builds) on the editor side. Exercised
end-to-end by `tests/PlayerTests.cpp`, which starts the real binary over a real socket.

---

## 5. What "done" means for the first milestone

Restating the discussion's own minimum milestone, unchanged, because it is a good one:

> CNA Editor opens a project, shows docked panels, loads a JSON scene with three sprites, lets the
> user select an object in the viewport or the hierarchy, change its position in the inspector,
> undo, save the scene, and run it in a separate CNA Player process.

Of that, the current implementation already does: open a project, load and save a scene, select,
change a property through a command, undo, redo, track the saved/dirty state, draw every panel
through the real Dear ImGui, and **run the scene in a separate `cna-player` process over a real
socket** — verified by `FullProjectRoundTripThroughTheApplication`,
`ImGuiUiProducesValidDrawDataForTheWholeEditor` and
`EditorLaunchesARealPlayerProcessAndTalksToIt` respectively.

The window exists too: built against a real CNA checkout, the editor opens, docks its five panels
and renders them through CNA's public API, verified by a screenshot rather than by an exit code.

What remains of the original milestone is the *scene* viewport's own drawing — sprites, grid,
selection outline, gizmo — and the editor-side Play toolbar. Those are `plan.md` Phase 1.

---

## 6. Verification record

| Claim | Source |
|-------|--------|
| Compile-time backend selection | `cna/cmake/BackendSelection.cmake`, `cna/include/CNA/GraphicsBackendType.hpp` |
| 14 backends | `cna/cmake/BackendSelection.cmake` `CNA_GRAPHICS_BACKEND` property strings |
| C++23, CMake ≥ 3.20 | `cna/CMakeLists.txt` lines 1–7 |
| `sharp-runtime` sibling requirement | `cna/CMakeLists.txt`, the `FATAL_ERROR` block |
| No install/export rules | absence of `install(` in `cna/CMakeLists.txt`, `cna/cmake/CnaLibrary.cmake` |
| MS-PL SPDX header | `cna/header.txt`, `cna/LICENSE` |
| Namespace layout | `cna/include/Microsoft/Xna/Framework/`, `cna/include/CNA/` |
| Property-style method naming | `cna/examples/demo_sound/src/SoundDemo.cpp` |
| Vendored third-party set | `cna/third_party/` |
| `tools/` holds dev utilities, not apps | `cna/tools/` |
| Sibling repository family | `openeggbert` repository listing |
| Scissor test implemented in every backend | `cna/src/CNA/Internal/Backends/*/`*GraphicsBackend.cpp* |
| `DrawUserIndexedPrimitives` overload for `VertexPositionColorTexture` + `uint16_t` | `cna/include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` |
| `TextInputEXT` exists and delivers UTF-16 | `cna/include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp` |
| `Color` has no default constructor (gap G-01) | `cna/include/Microsoft/Xna/Framework/Color.hpp`; confirmed by compiling |
| Clipboard gated on `CNA_DEVICES` (gap G-02) | `cna/include/CNA/Devices/Clipboard.hpp` |

CNA revision inspected: `ac3aaaeb2a5ba27dbd9e22e782c7041e6e40947c`, 2026-08-03.
sharp-runtime revision: `b797928f81c8542d13856fc98e812a04b20d5f3a`.

The CNA-linked sources (`src/viewport/*.cpp`) were compiled against those real headers with
`-std=c++23 -Wall -Wextra`, not merely written against the documentation.
