# Phase 2 — Architecture refresh

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-02001` … `STUDIO-02999` and are never reused.

**Purpose.** Replace the prototype's stale architectural assumptions with a model that matches current CNA, and make the invariants enforceable by test rather than by review.

**Exit criteria.** The Studio/runtime boundary, the renderer/platform model and the host capability contract are written down, and each one has a guard test that fails when it is violated.

**Progress:** 41 of 47 complete `████████░░░░`

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
| `STUDIO-02042` | Authored numbers are written as the shortest text that reads back unchanged | ✅ | — |
| `STUDIO-02050` | Define the service decomposition of the application shell | ✅ | — |
| `STUDIO-02054` | Extract `StudioPlayService` from `StudioShellPanels` | ✅ | `STUDIO-02050` |
| `STUDIO-02055` | Extract `StudioBuildService` from `StudioShellPanels` | ✅ | `STUDIO-02050` |
| `STUDIO-02056` | Extract `StudioComparisonService` | ✅ | `STUDIO-02050` |
| `STUDIO-02057` | Extract `StudioPreferencesService` | ✅ | `STUDIO-02050` |
| `STUDIO-02058` | Move panel binding out of `StudioShellPanels` into per-panel binders | ⬜ | `STUDIO-02050` |
| `STUDIO-02059` | Guard test: no service reaches another through a locator or a singleton | ✅ | `STUDIO-02055` |
| `STUDIO-02060` | Restore the CNA-backed build against current CNA | ✅ | `STUDIO-02001` |
| `STUDIO-02061` | Screenshot success is reported honestly | ✅ | `STUDIO-02060` |
| `STUDIO-02051` | Early guard: an exported project configures and builds with Studio unavailable | ✅ | `STUDIO-02040` |
| `STUDIO-02052` | Export a project as a standalone CNA game | ✅ | `STUDIO-02040` |
| `STUDIO-02053` | The exported game's runtime travels with the project | ✅ | `STUDIO-02052` |
| `STUDIO-02070` | `modernApiAvailable` must decide host eligibility, not decorate the report | ✅ | `STUDIO-02021` |
| `STUDIO-02071` | Read modern-API availability from the build instead of asserting it | ✅ | `STUDIO-02070` |
| `STUDIO-02072` | Choose a UI render backend from the two profile verdicts, and announce it | ✅ | `STUDIO-02070` |
| `STUDIO-02073` | Guard test: failing the Studio host contract never disqualifies a game target | ✅ | `STUDIO-02070` |
| `STUDIO-02074` | Retire the compatibility host profile once the modern renderer is the default | ✅ | `STUDIO-04026` |
| `STUDIO-02080` | Generalise the invariant: a Studio project is a project for CNA or one of its bindings | ✅ | `STUDIO-02001` |
| `STUDIO-02081` | Separate running a build from deciding what a build is | ✅ | `STUDIO-02080` |
| `STUDIO-02082` | Separate the vocabulary of packaging from the language that does it | ✅ | `STUDIO-02080` |
| `STUDIO-02083` | The C++ language adapter, reproducing current behaviour exactly | ✅ | `STUDIO-02081`, `STUDIO-02082` |
| `STUDIO-02084` | A project declares its language, and the registry resolves it | ✅ | `STUDIO-02083` |
| `STUDIO-02085` | Guard test: only the C++ adapter knows how a C++ project is built | ✅ | `STUDIO-02083` |
| `STUDIO-02086` | Prove the seam changed no behaviour | ✅ | `STUDIO-02083` |
| `STUDIO-02087` | The toolchain-path preference becomes per-language when a second language exists | ⛔ | `STUDIO-02084` |

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

### `STUDIO-02042` — Authored numbers are written as the shortest text that reads back unchanged

**Acceptance.** A scene, project or asset file holds the number that was authored: `0.6`, not
`0.600000024`. The text read back produces the identical float, and a value that is genuinely a
double rather than a float widened on the way in keeps full precision. Whole numbers stay whole,
and a plain decimal is preferred to shorter scientific notation.

**Verification.** `NumberTextTests.cpp`: round-trip and shortest-form cases over a table including
the values that exposed this, an end-to-end assertion on a serialised scene, and
`JsonAndTheFieldsAgreeAboutEveryFloat`, which holds the writer to the same rule the property grid
uses. The two are separate implementations because `src/core/Json.cpp` is embedded verbatim into
exported games and may include nothing but the standard library.

**Found by**, rather than planned: putting a volume of `0.6` on an audio source while verifying
`STUDIO-07044` and looking at the panel. `%.9g` is the precision that round-trips every binary32,
which is why it was chosen — and nine significant digits of a float are nine digits of its binary
representation.

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


### `STUDIO-02070` — `modernApiAvailable` must decide host eligibility, not decorate the report

**Acceptance.** A renderer without the modern CNAEXT graphics API does not pass `canHostStudio`,
and the diagnostic names the missing modern requirement rather than implying it.

**The defect, exactly.** `StudioHostEvaluation` carried `modernApiAvailable` from the snapshot into
its report and **no requirement consulted it**. `canHostStudio` was `unmetRequired().empty()` over a
list holding `ThreeDimensionalPipeline`, `DepthStencilBuffer` and a 2048-pixel texture minimum —
which is exactly the classic XNA capability set a renderer that cannot execute a shader has. The
contract's headline was in its report and nowhere in its decision.

**Two profiles rather than one stricter list**, because Studio genuinely has two UI GPU backends
while `docs/UI-RENDER-PATH.md`'s migration runs. `StudioHostProfile::Modern` requires the engine
layer, `ShaderEffects` and `ShaderEffectSourceExecution`; `StudioHostProfile::Compatibility` keeps
the last two as recommendations. **`Modern` is the default argument**, because the bug was a
permissive default and the fix has to be the other one.

**The compatibility profile is an allowance with a written reason, not a loophole.** CNA's
`SOFTWARE` renderer — the only one this project's CI can build (gap G-10) — reports both shader
capabilities unsupported. A single modern-only contract would make Studio refuse to start in every
automated configuration it has. So the fallback exists, is named `compatibility`, is announced in
the report, the log, the Diagnostics panel and the status bar, and `--ui-renderer=modern` turns it
off. `STUDIO-02074` deletes it.

**It also reconciled two things that had been disagreeing.** `RendererCatalog` has classified
`SOFTWARE` as preview-only since it was written, while the runtime contract said it could host
Studio. They agree now, and `CaseDARendererThatCannotHostStudioIsStillAValidGameTarget` checks it in
both directions.

**Verification.** `tests/StudioHostCapabilityTests.cpp`, four named cases plus the resolver's:
Case A — a classic-only device fails and the diagnostic names `ModernGraphicsApi` and `CNAEXT`;
Case A′ — the engine layer present is necessary and not sufficient, because a type Studio can name
is not a shader the device will run; Case B — each required capability removed in turn fails with
itself named and nothing else; Case C — the full profile passes with an empty diagnostic; Case D —
the same device that fails the host contract validates as a game target, and no problem on any axis
cites hosting Studio as the reason

### `STUDIO-02071` — Read modern-API availability from the build instead of asserting it

**Acceptance.** No literal is passed for a capability. The answer comes from the build state, and
where CNA cannot yet be asked, exactly one adapter says so.

**What was there.** `/*modernApiAvailable=*/true`, at both host call sites. The field reported what
the call site asserted, so a Studio configured against a CNA with `-DCNA_CNAEXT=OFF` would have
claimed the modern API and then failed to find a type for it.

**What it is now.** `captureStudioModernApiState()`, the one adapter, reading `CNA_CNAEXT` — which
reaches that translation unit through CNA's own `cna_build_config` interface target, so it is CNA's
statement about itself rather than Studio's guess about CNA — plus
`CNA::Graphics::getEngineLayerVersion()` for the revision. **No CNA gap was needed**: the question
is answerable from CNA's public API today.

**And it catches something the literal could not.** CNA publishes the engine-layer revision twice on
purpose: a macro for what a translation unit compiled against and a function for what it linked to.
When those disagree, something was rebuilt and something else was not — which surfaces much later
as a call resolving to the wrong shape. `versionMismatch` reports it as a warning rather than as
unavailability, because a mixed build *has* the layer, at an unknown revision, which is a different
and more alarming thing than not having it

### `STUDIO-02072` — Choose a UI render backend from the two profile verdicts, and announce it

**Acceptance.** One place decides which backend draws; the decision carries a sentence naming what
decided it; and the sentence reaches the report, the log and the user.

**Why one place.** The ImGui host and the native shell host asked the same three questions in the
same order, and the day they stopped agreeing would have been a day one of them ran on a different
renderer than the other with nothing saying so. `assessStudioHost()` asks the device **once** and
evaluates the one snapshot twice — asking twice would be two chances for a device to answer
differently, and a report whose halves disagreed about one renderer is worse than either half.

**The reason is populated even when the modern path is chosen.** "Why is this host on the classic
renderer" and "why is this host on the modern one" are the same question asked by somebody reading a
bug report, and an empty string answers neither.

**`--ui-renderer=modern`** makes the modern profile a hard requirement. That is what turns "Studio
requires the modern API" from a sentence in a document into something a script can check.

**Verification, as it stood at the time.** `AHostMeetingTheModernProfileGetsTheModernRenderer`,
`AClassicOnlyHostFallsBackAndTheReasonNamesWhatIsMissing` — which asserted the reason names both
missing capabilities, because "the modern renderer is unavailable" without them sends a reader to
the renderer's documentation rather than to the one line that answers them —
`TheFallbackCanBeRefusedAndThenAClassicOnlyHostGetsNothing`,
`AHostMeetingNeitherProfileGetsNothingAndSaysSo`, and `CnaStudioRejectsUnknownUiRenderer`.

**Superseded by `STUDIO-02074`.** There is no fallback left to verify: the three tests naming one
(`TheFallbackCanBeRefusedAndThenAClassicOnlyHostGetsNothing`,
`AHostMeetingNeitherProfileGetsNothingAndSaysSo`, and the two-profile half of
`AnEvaluationIsDeterministicAndOrderedAsDeclared`) are deleted, and
`AClassicOnlyHostFallsBackAndTheReasonNamesWhatIsMissing` is renamed to
`AClassicOnlyHostGetsNothingAndTheReasonNamesWhatIsMissing` — same assertion about the reason
naming both missing capabilities, but asserting refusal rather than a fallback choice.
`AHostMeetingTheModernProfileGetsTheModernRenderer` and `CnaStudioRejectsUnknownUiRenderer` still
hold unchanged.

### `STUDIO-02074` — Retire the compatibility host profile once the modern renderer is the default

**Acceptance.** `StudioHostProfile` has one member, `--ui-renderer` is gone or is a no-op kept for
scripts, and a host that cannot run the modern UI renderer refuses to start.

**Deliberately not now.** It cannot be done before `STUDIO-04026`, and it should not be done before
CI can build a renderer that meets the modern profile (gap G-10). Doing it earlier would mean
deleting the only configuration this project has automated coverage in.

**Both conditions are met, and this is still a decision rather than a mechanical cleanup.**
`STUDIO-04026` is done. Gap G-10 is narrowed and `STUDIO-04029` put `OPENGL4` under Xvfb — a
renderer that meets the modern profile — into `.github/workflows/build.yml`'s `cna` job as a real,
running CI leg, verified there rather than only reproduced locally. Neither reason this task gave
for waiting still holds.

What was not free was the other half of that same CI job: the `SOFTWARE` leg, same workflow, same
matrix, asserted `expect_backend: compatibility` and passed. That leg exists because `SOFTWARE`
needs no display and no GPU, which is what let this project have automated host coverage before
`OPENGL4`-under-Xvfb existed at all (gap G-10, as it stood). Making a host that cannot run the
modern renderer refuse to start, as this task's acceptance asks, turns that leg's assertion from
"runs Studio on the compatibility renderer" to "refuses to start" — a decision about what CI still
covers and how, not a consequence that falls out of deleting `CnaUiRenderer`, so it was put to the
project rather than assumed: **repurpose the leg as build-only**, keeping `cna-studio` and
`cna-player-software` compiled and tested on `SOFTWARE` (it remains a valid *game*-target renderer,
capability contract Case D), with the leg's assertion changed from hosting Studio to Studio's
refusal diagnostic.

**Done.** `StudioHostProfile` has one member (`Modern`); `StudioUiBackendChoice` has two
(`None`, `Modern`); `resolveStudioUiBackend` takes one evaluation and returns `Modern` or refuses,
with no fallback argument left to take. `CnaStudioShellHost::LoadContent` no longer has a
`--ui-renderer=compat` override block or a runtime fallback that constructs `CnaUiRenderer` — a host
that cannot run the modern renderer sets `StudioUiBackendChoice::None` and the existing
`canHostStudio()` refusal path takes it from there. `--ui-renderer` is still parsed and validated
(`auto`/`modern`/`compat` all still accepted) but read by nothing, kept only so a script that already
passes it does not get an unknown-flag error.

`.github/workflows/build.yml`'s `SOFTWARE` leg now sets `expect_backend: none` and asserts
`cna-studio --host-capabilities` reports it, instead of `compatibility`. Two new CTest cases —
`CnaStudioNativeShellRefusesAHostThatCannotMeetTheModernProfile` (the same `--host-capabilities`
check, at the CTest level) and `CnaStudioNativeShellRefusesToOpenAWindowHere` (`--ui=studio` itself,
`WILL_FAIL`) — cover the same refusal on every renderer that does not meet the modern profile.
Every real-window CTest that used to run on `SOFTWARE` through the classic backend
(`CnaStudioNativeShellWindowSmoke` and its siblings, `CnaStudioGridPlaneChangesTheView`, …) is now
declared only behind `CNA_STUDIO_HOST_MEETS_MODERN_PROFILE`, computed the same way
`CNA_STUDIO_TEST_SURFACE` already was. `CnaStudioUiRenderBackendsAgree` and
`cmake/UiRendererAbTest.cmake` are deleted with it: the byte-equality A/B they ran needed two
backends to compare, and after this task there is only one.

**This unblocks `STUDIO-04027`.** `CnaUiRenderer` is constructed nowhere in `src/` or `include/` any
more — `makeUiRenderBackend()`'s `Compatibility` case and the runtime shader-rejection fallback were
its only two call sites, and both are gone. The classic backend has a live, tested caller no longer;
it has no caller at all.

### `STUDIO-02050` — Define the service decomposition of the application shell

**Acceptance.** The boundaries are written down with a stated rule for what earns its own type, and
the first of them exists so that the shape is checked against real code rather than proposed.

**The rule.** A service earns its own type by owning **state with a lifetime**, **operations with
rules**, and a **failure mode of its own**. All three, not one. That rule is what keeps this from
becoming twenty wrapper classes: a responsibility with no state is a free function, and a
responsibility with no failures of its own belongs to whatever owns its failures.

**Three things it explicitly forbids.** No service locator and no registry — dependencies are
constructor arguments, so the set of things a service can reach is the set visible in its
constructor. No forwarder-per-operation — `StudioShellPanels` hands out `play()` and `builds()`
rather than growing six methods that do nothing; the few forwarders that stayed are the ones with a
*second* responsibility here, and `setPlayerBuilds` is the example: it also feeds the Diagnostics
panel. And no splitting a rule across two services — building and packaging are one service because
both answer "turn this project into something that runs elsewhere" and both carry the invariant
this product is held to, and a rule in two places is a rule that gets weakened in one of them.

**What `StudioShellPanels` had become.** Play control, build control, packaging, preferences,
recovery, the renderer comparison, plugin polling, notifications, viewport state and the binding of
every panel — 1421 lines, and growing by one reasonable addition at a time. That is the shape an
application object takes just before it stops being reviewable.

**The decomposition, in the order it is being done:**

| Service | Owns | Status |
|---------|------|--------|
| `StudioPlayService` | The player process, play state, installed builds, the session override, input forwarding | `STUDIO-02054` ✅ |
| `StudioBuildService` | The build process, the finish transition, standalone packaging | `STUDIO-02055` ✅ |
| `StudioComparisonService` | The renderer comparison run, its request and its report | `STUDIO-02056` ✅ |
| `StudioPreferencesService` | The preferences model, its sink, and applying it to a live shell | `STUDIO-02057` ✅ |
| per-panel binders | The 440-line `bind()` | `STUDIO-02058` |

`StudioRecoverySession` already exists and already has this shape, which is part of why the shape
is the one chosen.

**Deliberately not services.** Selection, documents and the asset database are `StudioContext`'s and
already are. A `WorkspaceService` would own nothing `StudioDockTree` and `StudioWorkspaceStore` do
not already own between them. An `ActionService` would be a second name for `StudioActionRegistry`.
Each of those would be a wrapper, which is the failure mode this task exists to avoid.

### `STUDIO-02054` — Extract `StudioPlayService` from `StudioShellPanels`

**Acceptance.** Play's rules can be exercised without a shell, without panels and without a device;
no caller of the old spelling breaks.

**What it proves, and it is the point of the whole decomposition.** Every rule in
`ThePlayServiceRunsWithNoShellAndNoPanels`,
`TheSessionOverrideOutranksTheProjectAndIsDroppedWhenItsBuildGoesAway` and
`ThePlayServiceRaisesItsCrashNotificationThroughASinkRatherThanAShell` used to need a
`StudioShell`, a `StudioShellPanels` and therefore a binding of every panel in Studio to reach.
They need a context, a log and a lambda now. A decomposition whose parts still cannot be used apart
has moved code rather than separated concerns.

**The notification sink is what made it possible.** The crash notification was
`shell.notifications().raise(...)`, so testing play's one user-visible failure needed a shell. It is
a `std::function` argument now, and the test reads what was raised.

**Verification.** The four cases above, plus `TheShellStillSpeaksForThePlayServiceItOwns`, which
checks the forwarding half — including that `playerBuilds()` returns the *service's* vector rather
than a copy, because a forwarder returning a snapshot would let the panel and the service drift
apart within one frame.

### `STUDIO-02055` — Extract `StudioBuildService` from `StudioShellPanels`

**Acceptance.** The build's finish transition and the standalone package are one type with one
owner; `StudioShellPanels::build()` still names the process every panel and test already spells.

**Building and packaging together**, for the reason on the type: they are the same question asked
twice, and both carry "CNA Studio produces CNA games, not CNA Studio games". `poll()` and
`packageProject()` return whether they did anything, which they previously did not — a package that
failed and a package that was written looked identical to the caller.

### `STUDIO-02056` — Extract `StudioComparisonService`

**Acceptance.** The comparison's rules can be exercised without a shell, without panels and
without a device; the panel reads the service rather than a copy of it.

**A state machine, a report and failures of its own**, which is the three-part rule on
`STUDIO-02050`: a run that outlives many frames, six operations with real preconditions, and ways
to go wrong — a scene that was never saved, captures it cannot read back, renderers that finished
and drew different pictures — that are nothing like a build failing or a player crashing.

**Why it is not part of `StudioPlayService`, which also launches players.** Play starts one game
because a user wants to play it and is looking at it. A comparison starts several, in sequence,
over something closer to half an hour, to answer a question about renderers — and answers it with
images rather than with a window. Folding them together would make one type "the things that start
processes", which is a category rather than a responsibility.

**The one interesting dependency is the installed builds**, and it arrives as a provider rather
than as a `StudioPlayService&`. What the comparison needs from play is the list of renderers this
Studio has a player for, which must be the same list Play chooses from or the panel would offer a
renderer Play will not use. Taking the play service would hand this type the player process, the
session override and the input bridge as well, all of which would then be reachable by anybody who
noticed they were there. `STUDIO-02059` does not catch that — a constructor argument is a legal
dependency however large it is — so it stays a judgement, and this is the judgement.
`TheComparisonAsksForTheBuildsRatherThanRememberingThem` covers the other half: a snapshot taken at
construction would go stale the moment Studio rescanned.

**Verification.** `TheComparisonServiceRunsWithNoShellAndNoPanels`,
`TheComparisonAsksForTheBuildsRatherThanRememberingThem`,
`TheComparisonRaisesItsOutcomeThroughASinkRatherThanAShell`, and
`TheShellStillSpeaksForTheComparisonServiceItOwns`, which checks the forwarding half — including
that `comparisons()` returns the *service* rather than a copy, and that `comparisons()` hands out
the whole service rather than growing a method per operation.

**One behaviour changed on purpose.** The outcome used to be dropped entirely when there was no
shell to raise it on. It goes through the sink unconditionally now, like the play service's crash
notification, so a headless caller gets it in the log rather than not at all.

### `STUDIO-02057` — Extract `StudioPreferencesService`

**Acceptance.** The ordering rule — applied first, persisted second — can be stated without a shell,
and the Preferences panel edits the service's model rather than a copy of it.

**Why a handful of settings earns a type.** The third part of the rule on `STUDIO-02050`: a failure
mode of its own, and one Studio gets wrong by default. A preference is applied and *then* persisted,
so a write that fails still leaves the user looking at what they chose — they can see it worked and
decide what to do about the file. The opposite order makes a full disk look like a control that does
nothing. That is a rule with a failure and a message, and it is exactly the kind that gets quietly
reversed by somebody tidying up a function that does two things.

**The theme goes out through a sink**, like every other service's outcome. Applying preferences
means giving the shell a theme, and taking a `StudioShell&` would put this back where it started:
untestable without one, and holding the whole shell to call one method on it. A test now reads the
theme that was applied, which is how `APreferenceIsAppliedBeforeItIsPersistedEvenWhenTheWriteFails`
can assert the *order* of two side effects rather than only their results.

**`reset()` rather than assigning a default-constructed model**, so the reset cannot be done without
the apply. A reset that changed the record and not the screen is the one a user reports as "Reset
did nothing".

**Not here, deliberately:** reading and writing the file. That is `StudioPreferencesStore`'s, it is
CNA-free and already tested on its own, and a service that also knew the file format would own two
things that change for different reasons. This one owns the decision; the store owns the bytes.

**Verification.** `ThePreferencesServiceRunsWithNoShellAndNoPanels`,
`APreferenceIsAppliedBeforeItIsPersistedEvenWhenTheWriteFails`,
`AResetAppliesAndPersistsRatherThanOnlyChangingTheRecord`, and
`TheShellStillSpeaksForThePreferencesServiceItOwns` — which checks that `preferences()` returns the
service's own model rather than a copy, because a panel editing one while the theme read another
would be a preference that appears to do nothing every other frame.

### `STUDIO-02059` — Guard test: no service reaches another through a locator or a singleton

**Acceptance.** A test fails the build on a static instance accessor or a global registry lookup in
any service, naming the file and line. The rule is only worth stating if it is enforced: a locator
added later would look exactly like the code around it.

**Done, and written *before* `02056`–`02058` rather than after.** A guard added last is a guard
that has to be made to pass; a guard added first is a rule the next three services are written
against. `tests/ServiceBoundaryGuardTests.cpp`, four cases, documented in `docs/ARCHITECTURE.md`
§10.1.

**It matches structures, not names.** `instance()` renamed to `shared()` evades a name check and
nothing else, so the scan looks for the four things every locator is actually built from: a mutable
`static` holding a Studio type, a `static` function handing out a reference or pointer to one, a
mutable namespace-scope variable of one, and `static T& get()` — plus `typeid`/`std::type_index`,
which is how a heterogeneous registry is keyed and which Studio otherwise has no use for. Returning
**by value** is a factory and is allowed: `Uuid::generate` and `Project::createDefault` hand the
caller a thing rather than *the* thing.

**What it does not ban, deliberately.** A mutable `static` holding no Studio type — the CRC table
in `UiSoftwareRasterizer`, the thread-local engine in `Uuid` — is a cache of a pure function rather
than a dependency anybody has. Failing those would make the guard painful enough to switch off. The
type vocabulary is read from Studio's own headers rather than listed, so a service added tomorrow is
covered without anyone remembering; types declared inside a single `.cpp` are excluded because
nothing outside that file can name them, which is also what stopped `MessageChannel`'s Winsock
initialisation guard being a false positive.

**Proved against fixtures, and against the real tree.** `TheGuardSeesEveryShapeOfLocatorItClaimsTo`
feeds the scanner each prohibited shape and requires it flagged;
`TheGuardDoesNotFireOnTheThingsThatLookLikeOne` feeds it the by-value factories, constants, members,
parameters and out-of-line `operator=` that surround the real code and requires silence. Both run
the same function the tree is scanned with, because a guard whose test path differs from its
production path proves nothing about the production path. It was additionally verified end to end by
putting a real singleton into `StudioBuildService.cpp` and watching the suite name the file and the
line.

**One exception is recorded**, as a site rather than a pattern: the Dear ImGui prototype's clipboard
adapter, whose hooks have nowhere instance-shaped to live because ImGui's clipboard callbacks are C
function pointers reached through a global context. `EveryRecordedExceptionIsStillARealViolation`
requires it to still match something, so the entry fails the suite the day `STUDIO-07030` deletes
the file — an allowlist that can outlive what it excused is a licence.

### `STUDIO-02080` — Generalise the invariant: a Studio project is a project for CNA or one of its bindings

**Acceptance.** `docs/ARCHITECTURE.md` §1 states the invariant in a form that does not make C++ a
property of Studio's model, records the C++ form beneath it, and §13 records the decision with the
alternatives that were rejected.

**Why this had to change before Phase 8 rather than after.** The old invariant read *"a CNA Studio
project remains a CNA **C++** project"*. That is right about what is being built first and wrong as
a permanent architectural statement, for a reason that has nothing to do with ambition: CNA has
several language bindings, and a project authored for one of them is still a CNA project. The
wording put C++ in the *model* rather than in the project, and a model with that in it grows

```
if (language == Cpp) … else if (language == …) …
```

through the Project Hub, project creation, build orchestration, source opening, component metadata
and packaging, one individually reasonable addition at a time. Phase 8 is the tranche that would
have written most of those, which is why this is in front of it rather than behind it.

**C++ remains the only required and fully implemented workflow.** Nothing here is a commitment to a
second language, and `STUDIO-02084`'s registry ships exactly one adapter.

### `STUDIO-02081` — Separate running a build from deciding what a build is

**Acceptance.** `BuildProcess` runs a planned job and contains no reference to any build system.
`BuildRequest` and the CMake command construction move to the C++ adapter's own directory.

**What it cost.** `BuildProcess::start` used to take a `BuildRequest` and call `describeBuildProblem`,
`planBuild` and `getDefaultBuildDirectory` itself — so the runner, the log, the notifications and
the Build panel all knew what CMake was. It now takes a `StudioBuildJob`: the steps, the directory
and a description for the log's first line. `src/project/BuildRunner.cpp` kept the process
supervision and lost 169 lines to `src/project/cpp/CppToolchain.cpp`, which is the whole of what
Studio knows about driving CMake.

`studioTargetProfileCMakeArguments` moved with it, out of `TargetProfile.cpp`. The profile is a
*project* fact every language shares — os, architecture, renderer, platform, configuration,
features — and `-DCMAKE_BUILD_TYPE=` is not: a binding with a different build system answers the
same profile with different words.

### `STUDIO-02082` — Separate the vocabulary of packaging from the language that does it

**Acceptance.** `StudioExportRequest`, `StudioExportResult` and `studioExportTargetName` are
language-neutral; `exportStandaloneProject` is the C++ adapter's.

A caller fills in a request and reads a result without learning which build system wrote the tree.
Target-name reduction stays generic because every build system this is likely to meet wants an
identifier, and a project called `2048` should get the same executable name in any language.

### `STUDIO-02083` — The C++ language adapter, reproducing current behaviour exactly

**Acceptance.** One `StudioLanguageAdapter` implementation, under
`include/CNA/Studio/Project/Cpp/` and `src/project/cpp/`, that answers every boundary in
`docs/ARCHITECTURE.md` §13.3 by delegating to code that already existed.

**Deliberately a facade, with no behaviour change in the same commit.** An abstraction introduced
together with a behaviour change is one whose regressions cannot be attributed. `STUDIO-02086` is
the mechanical proof that nothing moved.

**One generator was genuinely extended**, and it was extended rather than duplicated:
`cmakeListsFor` now takes the directories to stage rather than assuming `Content/`, so the same
generator serves `--export` (one `Content` tree) and a project created in place (`Scenes/` and
`Assets/` where the editor writes them). Writing them twice would mean an export and a new project
that drift, and only one of the two has a build test.

### `STUDIO-02084` — A project declares its language, and the registry resolves it

**Acceptance.** `.cnaproject` carries `"language"`; `StudioLanguageRegistry` resolves it to an
adapter; `StudioContext` holds the registry as it holds the component registry.

**Two resolution rules, and each has a wrong answer that nothing would report.** An *absent* key
resolves to the registry's default, because every project file written before the key existed is
C++ and refusing them would be adding a required field to a format people have files in. An
*unknown* language resolves to **null**, because falling back to the default would build a project
as something it is not, and the failure would appear a long way from its cause.

The key is written only when set, for the reason `gridSnap` is: an additive key that appeared in
every file the moment Studio touched it would make the first save of every existing project a diff
nobody asked for.

**Not a locator.** `studioBuiltInLanguages()` returns a registry **by value** — a factory, not an
accessor — and `StudioContext` holds one as a member. `STUDIO-02059`'s guard refuses a `static`
function handing out a reference to a Studio type, and would have refused the obvious shape.

### `STUDIO-02085` — Guard test: only the C++ adapter knows how a C++ project is built

**Acceptance.** Three tests, each failing on a different way the boundary erodes.

| Guard | What it refuses |
|-------|-----------------|
| `OnlyTheCppLanguageAdapterKnowsHowACppProjectIsBuilt` | A file outside the adapter's two directories including one of its headers or naming one of its symbols |
| `NoStudioCodeBranchesOnWhichLanguageAProjectIsWrittenIn` | `== "cpp"`, `!= "cpp"` and the `kCppLanguageId` comparisons, in the generic model |
| `TheLanguageSeamStillCarriesEveryBoundaryItWasIntroducedFor` | §13.3's table and the interface drifting apart, in either direction |

**The rule is a directory, not a list of exempt files.** A list has to be edited when a file is
added, which means it gets edited by whoever is adding the file that breaks it. There is exactly
one named exemption — `src/project/StudioBuiltInLanguages.cpp` — and it is a whole file holding one
function, so the exemption cannot quietly come to cover something else. Somewhere has to name the
adapters a build ships, or nothing is registered.

**The include check reads raw text and the symbol check reads stripped code**, because an
`#include` path is a string literal and the scanner blanks string contents — a check written
against stripped code would have silently matched nothing.

### `STUDIO-02086` — Prove the seam changed no behaviour

**Acceptance.** `TheCppAdapterPlansExactlyTheBuildTheToolchainFunctionsDo` compares the adapter's
`planBuild` against `planBuild(makeBuildRequestFromActiveProfile(project))` command line by command
line, its build directory against `getDefaultBuildDirectory`, and its problem sentence against
`describeBuildProblem` — so a change to either side without the other fails here rather than in
somebody's build.

Around it, the properties the seam is supposed to buy, each asserted rather than asserted *about*:
generated and hand-written source live in different, non-nested directories and a newly created
project writes nothing into the generated one; generating a project twice produces the same bytes;
a new project carries its own runtime as ordinary source; the standalone build instructions name
the directory they apply to.

### `STUDIO-02087` — The toolchain-path preference becomes per-language when a second language exists

**⛔ Deferred, and recorded rather than done.** `StudioPreferences::cmakePath` is a persisted key
with one language behind it. Generalising it to a map keyed by language id today would buy nothing
— there is one entry — and cost a preferences format migration, which is a real cost paid against
an imagined requirement. It is the one known non-generic remainder of the seam, and it is written
down here so that the second adapter finds it rather than trips over it.
