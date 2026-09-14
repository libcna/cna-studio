# Phase 2 — Architecture refresh

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-02001` … `STUDIO-02999` and are never reused.

**Purpose.** Replace the prototype's stale architectural assumptions with a model that matches current CNA, and make the invariants enforceable by test rather than by review.

**Exit criteria.** The Studio/runtime boundary, the renderer/platform model and the host capability contract are written down, and each one has a guard test that fails when it is violated.

**Progress:** 22 of 27 complete `█████████░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-02001` | Write `docs/ARCHITECTURE.md` as the current architectural record | ✅ | `STUDIO-00011` |
| `STUDIO-02002` | Write `docs/CNA-GAPS.md` as the CNA deficiency register | ✅ | `STUDIO-00012` |
| `STUDIO-02003` | Write `docs/LEGACY-EDITOR-TASK-MAP.md` | ✅ | `STUDIO-00003` |
| `STUDIO-02004` | Rewrite `plan.md` as the CNA Studio master roadmap | ✅ | `STUDIO-02001` |
| `STUDIO-02010` | Re-measure CNA gap G-03 across the current renderer set | ⬜ | `STUDIO-02002` |
| `STUDIO-02011` | Re-measure CNA gap G-05 (`PbrEffect` draws nothing) against the current renderer set | ⬜ | `STUDIO-02002` |
| `STUDIO-02020` | Define the Studio host renderer capability contract as data | ✅ | `STUDIO-02001` |
| `STUDIO-02021` | Evaluate the contract against the live device at start-up | ✅ | `STUDIO-02020` |
| `STUDIO-02022` | Fail cleanly and precisely when the compiled renderer cannot host Studio | ✅ | `STUDIO-02021` |
| `STUDIO-02030` | Guard test: Studio must classify every renderer identity CNA registers | ✅ | `STUDIO-02020` |
| `STUDIO-02031` | Guard test: Studio must classify every platform identity CNA implements | ✅ | `STUDIO-02030` |
| `STUDIO-02032` | Guard test: no `CNA::Internal::*` anywhere in Studio | ✅ | — |
| `STUDIO-02033` | Guard test: only `cna-studio-viewport` includes CNA headers | ✅ | — |
| `STUDIO-02034` | Guard test: no direct Vulkan/D3D/OpenGL/Metal/WebGPU calls in Studio modules | ✅ | — |
| `STUDIO-02035` | Guard test: every document mutation goes through a command | ⬜ | — |
| `STUDIO-02036` | Guard test: unknown plugin components survive a save/load round trip | ✅ | — |
| `STUDIO-02037` | Guard test: authored files are byte-deterministic across repeated saves | ⬜ | — |
| `STUDIO-02038` | Legacy renderer-name migration for projects written by the prototype | ✅ | `STUDIO-02030` |
| `STUDIO-02039` | Guard test: no two public headers define the same type in one namespace | ✅ | — |
| `STUDIO-02040` | Define the target-profile model: OS, platform, architecture, renderer, configuration, features | ✅ | `STUDIO-02020` |
| `STUDIO-02041` | Separate the Studio host renderer from the game target renderer throughout | ✅ | `STUDIO-02040` |
| `STUDIO-02050` | Define the service decomposition of the application shell | ⬜ | — |
| `STUDIO-02060` | Restore the CNA-backed build against current CNA | ✅ | `STUDIO-02001` |
| `STUDIO-02061` | Screenshot success is reported honestly | ✅ | `STUDIO-02060` |
| `STUDIO-02051` | Early guard: an exported project configures and builds with Studio unavailable | ✅ | `STUDIO-02040` |
| `STUDIO-02052` | Export a project as a standalone CNA game | ✅ | `STUDIO-02040` |
| `STUDIO-02053` | The exported game's runtime travels with the project | ✅ | `STUDIO-02052` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-02001` — Write `docs/ARCHITECTURE.md` as the current architectural record

**Acceptance.** Supersedes `ANALYSIS.md` on everything concerning current CNA; states the invariant, the renderer/platform split, the capability model, the module architecture and the guard tests

### `STUDIO-02002` — Write `docs/CNA-GAPS.md` as the CNA deficiency register

**Acceptance.** Every gap carries its affected API, current and expected behaviour, Studio impact, workaround, suggested fix and the test CNA would need. Six at the time this task was written; the register grows as Studio finds more

### `STUDIO-02003` — Write `docs/LEGACY-EDITOR-TASK-MAP.md`

**Acceptance.** All 117 legacy ids mapped with their prototype status, and the rule that a legacy complete does not mean complete in Studio stated explicitly

### `STUDIO-02004` — Rewrite `plan.md` as the CNA Studio master roadmap

**Acceptance.** 36 phases, stable `STUDIO-PPNNN` ids, per-task status/dependencies/acceptance/verification, global progress, and a `plans/` hierarchy for phase detail

### `STUDIO-02010` — Re-measure CNA gap G-03 across the current renderer set

**Acceptance.** Named list of which renderer identities present a sampled render target flipped, replacing the prototype's two-renderer observation

**Verification.** A render-to-target-and-sample test run per available renderer

### `STUDIO-02011` — Re-measure CNA gap G-05 (`PbrEffect` draws nothing) against the current renderer set

**Acceptance.** Either the gap is confirmed with the renderer named, or it is closed with evidence

### `STUDIO-02020` — Define the Studio host renderer capability contract as data

**Acceptance.** One module owns a required-feature set expressed in `CNA::RendererFeature` terms. No renderer names appear in it. `Unknown` counts as unmet for a required feature

**How it was met.** `StudioHostRequirements` names CNA's features and limits by the stable English
identifiers `CNA::GetRendererFeatureName` returns, which keeps the contract and its whole evaluation
in a module that links no CNA and needs no GPU while still being expressed in the terms the device
answers in. Requirements carry a severity — a missing recommended capability disables a panel and
says so, rather than refusing to start — and each says per requirement whether a `Restricted`
answer is good enough, which is the difference between a contract that is accurate and one that is
merely strict. Two required capabilities, one required limit, six recommended: the set is small
because a padded contract refuses to start on renderers Studio works on, and the pressure that
creates is to ignore the contract rather than to fix it

**Verification.** `tests/StudioHostCapabilityTests.cpp`, over synthetic devices: satisfied,
unsupported, restricted-and-enough, restricted-and-not-enough, unclassified, a limit below its
minimum, and a limit the renderer never reported — which is recorded as absent rather than as zero,
because "reports 0" sends a reader looking for hardware that does not exist

### `STUDIO-02021` — Evaluate the contract against the live device at start-up

**How it was met.** `CnaCapabilityBridge` is the only place in Studio that touches
`RendererCapabilityProfile`, and it copies **every** declared feature and limit across — walking
CNA's own `AllRendererFeatures()` span rather than a list written in Studio, which would need
updating when CNA adds a feature and would fail silently when it was not. It converts nothing and
decides nothing: if it made judgements, those judgements would only be testable on hardware

**Verification.** The report is produced on every windowed run and is returned in
`CnaStudioHostResult`; `cna-studio --host-capabilities` prints the contract, and on a CNA build
evaluates it against the live device and exits

### `STUDIO-02022` — Fail cleanly and precisely when the compiled renderer cannot host Studio

**Acceptance.** A diagnostic naming each missing requirement and whether it is unsupported or unclassified; no window is opened

**How it was met, and the honest limitation.** CNA creates the graphics device with the window, so
the device cannot be interrogated before a window exists. Studio evaluates at the first moment one
does, prints the diagnostic, and exits before drawing a frame — which is as close to "no window is
opened" as an honest implementation gets, and saying so beats pretending. The exit code is distinct
(`6`), so a build matrix can tell "this renderer cannot host Studio" from "Studio crashed" without
parsing text

**Verification.** `tests/StudioHostCapabilityTests.cpp`: a synthetic profile missing one required
feature, with the diagnostic asserted to name the capability, the reason, the renderer's own
qualification and the renderer itself — and asserted **not** to mention the recommended
capabilities the same device also lacks, because a message that exists to say why Studio will not
start buries its own point when padded with things that are not the reason

### `STUDIO-02030` — Guard test: Studio must classify every renderer identity CNA registers

**Acceptance.** A test enumerates CNA's renderer identities and fails, naming the identity, when Studio has no classification for one. Adding a renderer to CNA breaks this test on the same commit

### `STUDIO-02031` — Guard test: Studio must classify every platform identity CNA implements

**Acceptance.** As above for `CNA_PLATFORM`, including the reserved-but-unimplemented identifiers

### `STUDIO-02032` — Guard test: no `CNA::Internal::*` anywhere in Studio

**Acceptance.** A source scan over every Studio module fails on any such include or qualified name

### `STUDIO-02033` — Guard test: only `cna-studio-viewport` includes CNA headers

**Acceptance.** Enforced by the build graph today; made explicit as a test so the property is stated rather than inferred from a link error

### `STUDIO-02034` — Guard test: no direct Vulkan/D3D/OpenGL/Metal/WebGPU calls in Studio modules

**Acceptance.** A source scan for the backend API symbol prefixes fails the build

### `STUDIO-02035` — Guard test: every document mutation goes through a command

**Acceptance.** Mutating `SceneDocument` outside a `StudioCommand` is detectable and tested for

### `STUDIO-02036` — Guard test: unknown plugin components survive a save/load round trip

**Acceptance.** Inherited from the prototype and still passing; restated here as an architecture guard

### `STUDIO-02037` — Guard test: authored files are byte-deterministic across repeated saves

**Acceptance.** Saving the same document twice produces identical bytes; ordering is stable and no timestamps leak

### `STUDIO-02038` — Legacy renderer-name migration for projects written by the prototype

**Acceptance.** A `.cnaproject` naming `easygl`, `d3d11`, `d3d12`, `d3d9` or `dx3` opens and is migrated to the current identity, with a warning saying what changed and why. `ascii` is reported as removed with no substitute chosen for the user

**Verification.** Round-trip tests for a migrated name, a removed name, and alias-table consistency

### `STUDIO-02039` — Guard test: no two public headers define the same type in one namespace

**Acceptance.** A duplicate type name in CNA::Studio fails the suite, naming both headers. Names are qualified by enclosing namespace, so CNA::Studio::SceneLoadResult and CNA::Studio::Runtime::SceneLoadResult are correctly not a collision

**Verification.** Verified against a deliberately injected duplicate; reported both files exactly

### `STUDIO-02040` — Define the target-profile model: OS, platform, architecture, renderer, configuration, features

**Acceptance.** A project carries named profiles rather than one global renderer string. Validity is decided by capability and build metadata, not by scattered name comparisons

### `STUDIO-02041` — Separate the Studio host renderer from the game target renderer throughout

**Acceptance.** No code path treats the two as one value. A project using only classic functionality is not denied a renderer merely because it cannot host Studio's UI

### `STUDIO-02040` — Define the target-profile model

**Acceptance.** Six axes — operating system, architecture, platform, renderer, configuration and
features — replacing the prototype's single "backend" string, which could express none of "SDL3
windowing with a Vulkan renderer", a 32-bit Windows build, or a game that does not want the
networking layer compiled in

**How it was met.** `StudioTargetProfile` is a value, so a project holds as many as it ships on and
none of them is privileged. It validates against what CNA declares — registered renderer identities,
implemented platforms, legacy alias migration — and against the per-renderer operating-system gates
CNA's own `cmake/RendererSelection.cmake` enforces as hard configure errors, so Studio says
"CNA cannot build DirectX 11 for Linux" before the build rather than after it. It translates to the
exact CMake arguments Studio would run, with every feature passed explicitly on *or* off: passing
only the enabled ones lets a stale cache keep a feature the profile turned off, which is a build
that works for whoever configured it and for nobody else.

A project written before profiles existed — which is every project the prototype wrote — has its one
renderer string migrated into one profile, and is told so. `defaultGraphicsBackend` stays on disk
and follows the active profile, because it is a serialized contract the player's build discovery
depends on

**Verification.** `tests/StudioTargetProfileTests.cpp`, and `STUDIO-29007` for the transcription

### `STUDIO-02041` — Separate the Studio host renderer from the game target renderer throughout

**Acceptance.** A renderer that cannot host Studio's UI is still offered as a game target

**How it was met.** Two models that share no code: `StudioHostRequirements` answers "can this device
draw Studio" and `StudioTargetProfile` answers "what does this game build for". A test walks every
renderer Studio classifies as unable to host it, picks an operating system CNA will build it for,
and asserts the profile validates — because the day those decisions share a path is the day a user
is told they cannot ship for a platform because the tool could not run on it

**Verification.** `tests/StudioTargetProfileTests.cpp`: every non-hosting renderer is a valid game
target, and the profile validator never consults the host contract

### `STUDIO-02050` — Define the service decomposition of the application shell

**Acceptance.** Explicit services with explicit dependencies — Project, Document, Selection, Command, Asset, Import, Play, Build, Package, Workspace, Preferences, Job — and no service locator. `StudioApplication` stops being the place new subsystems are added

### `STUDIO-02060` — Restore the CNA-backed build against current CNA

**Acceptance.** cna-studio and cna-player build and run against libcna/cna branch next. The viewport uses GraphicsRendererType rather than the removed GraphicsBackendType; the CMake reads CNA_GRAPHICS_RENDERER rather than the removed CNA_GRAPHICS_BACKEND, and fails loudly when it is empty rather than producing an undiscoverable cna-player-; and both hosts request the HiDef graphics profile, without which GetBackBufferData throws and every screenshot is lost

**Verification.** 22 CTest suites green against a real CNA checkout, SOFTWARE renderer, SDL3 platform

### `STUDIO-02061` — Screenshot success is reported honestly

**Acceptance.** A failed capture no longer reports success. `screenshotAttempted` latches the retry; `screenshotWritten` means a file exists. The failure also reaches stderr, because the run that most needs to hear it is the scripted one with nobody watching

**Verification.** CnaStudioWindowSmoke and CnaPlayerWindowSmoke now fail when no file is produced

### `STUDIO-02051` — Early guard: an exported project configures and builds with Studio unavailable

**Acceptance.** A reduced form of the Phase 18 standalone test, run in CI from the start so the invariant cannot rot while the packaging workstream is still ahead

**How it was met.** `CnaStudioStandaloneExport` exports the example project into an empty directory,
configures it with nothing but CMake and a CNA checkout, compiles it, and runs it. Studio is not
consulted after the export. The assertion is the line the game prints — how many entities came out
of the scene the editor wrote and how many sprites it drew — because a game that built and loaded
nothing would pass every earlier step

**Why it builds rather than inspects.** Both failures it has found so far were invisible in the
exported tree. `CNA_ENABLE_VIDEO` is tri-state and Studio passed the boolean `ON`, which made an
exported game *require* FFmpeg (`STUDIO-17012`). And CNA builds its own tests and examples by
default when consumed as a subdirectory, where neither can succeed — the tests want an initialised
googletest submodule and the examples resolve a helper through `CMAKE_SOURCE_DIR`, which from a
subdirectory is the consuming project's root (CNA gap G-09). Both looked correct on paper

**Note on the dependency.** Originally listed as depending on `STUDIO-02050`, the service
decomposition. It does not: export reads a `Project` and writes files, and waiting for a
refactor of the application shell would have left the invariant unguarded for no reason

**Verification.** `CnaStudioStandaloneExport`, labelled `slow` and not excluded in CI; roughly four
minutes, most of it CNA

### `STUDIO-02052` — Export a project as a standalone CNA game

**Acceptance.** `cna-studio --project=P --export=DIR` writes a tree containing the game's `main`,
its build file, the project-owned runtime and the content — and nothing that refers to Studio. It
refuses a directory that already has files in it unless asked twice, because export writes a whole
tree and a tree written over the wrong directory is not something an undo can help with

**On the command line, not only in the GUI.** The invariant this serves has to be *provable by a
script*: a GUI-only export is a claim nobody can test

**Verification.** `tests/ProjectExportTests.cpp` — the files written, no absolute path from the
exporting machine anywhere in the tree, every quoted `#include` resolving inside the export or in
CNA, no Studio target named in the generated build, the refusal and the override, the target-name
rule, and a project with no startup scene exporting with a warning rather than failing

### `STUDIO-02053` — The exported game's runtime travels with the project

**Acceptance.** The scene runtime is written into the exported project as ordinary source it
compiles itself. No library, SDK or editor has to be present on the machine that builds it

**Embedded, not read from disk.** A shipped Studio has no source tree beside it, so export must work
from the binary alone — the runtime is embedded at build time the same way the typefaces are.
Embedding from the files Studio itself compiles is also what stops the exported runtime drifting
from the writer that produced the scene: a committed second copy would be free to

**Verification.** `TheExportedRuntimeIsTheSameCodeStudioItselfCompiles` compares every embedded file
against its original byte for byte

