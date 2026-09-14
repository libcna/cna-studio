# Phase 2 — Architecture refresh

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-02001` … `STUDIO-02999` and are never reused.

**Purpose.** Replace the prototype's stale architectural assumptions with a model that matches current CNA, and make the invariants enforceable by test rather than by review.

**Exit criteria.** The Studio/runtime boundary, the renderer/platform model and the host capability contract are written down, and each one has a guard test that fails when it is violated.

**Progress:** 5 of 21 complete `███░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-02001` | Write `docs/ARCHITECTURE.md` as the current architectural record | ✅ | `STUDIO-00011` |
| `STUDIO-02002` | Write `docs/CNA-GAPS.md` as the CNA deficiency register | ✅ | `STUDIO-00012` |
| `STUDIO-02003` | Write `docs/LEGACY-EDITOR-TASK-MAP.md` | ✅ | `STUDIO-00003` |
| `STUDIO-02004` | Rewrite `plan.md` as the CNA Studio master roadmap | ✅ | `STUDIO-02001` |
| `STUDIO-02010` | Re-measure CNA gap G-03 across the current renderer set | ⬜ | `STUDIO-02002` |
| `STUDIO-02011` | Re-measure CNA gap G-05 (`PbrEffect` draws nothing) against the current renderer set | ⬜ | `STUDIO-02002` |
| `STUDIO-02020` | Define the Studio host renderer capability contract as data | ⬜ | `STUDIO-02001` |
| `STUDIO-02021` | Evaluate the contract against the live device at start-up | ⬜ | `STUDIO-02020` |
| `STUDIO-02022` | Fail cleanly and precisely when the compiled renderer cannot host Studio | ⬜ | `STUDIO-02021` |
| `STUDIO-02030` | Guard test: Studio must classify every renderer identity CNA registers | ⬜ | `STUDIO-02020` |
| `STUDIO-02031` | Guard test: Studio must classify every platform identity CNA implements | ⬜ | `STUDIO-02030` |
| `STUDIO-02032` | Guard test: no `CNA::Internal::*` anywhere in Studio | ⬜ | — |
| `STUDIO-02033` | Guard test: only `cna-studio-viewport` includes CNA headers | ⬜ | — |
| `STUDIO-02034` | Guard test: no direct Vulkan/D3D/OpenGL/Metal/WebGPU calls in Studio modules | ⬜ | — |
| `STUDIO-02035` | Guard test: every document mutation goes through a command | ⬜ | — |
| `STUDIO-02036` | Guard test: unknown plugin components survive a save/load round trip | ✅ | — |
| `STUDIO-02037` | Guard test: authored files are byte-deterministic across repeated saves | ⬜ | — |
| `STUDIO-02040` | Define the target-profile model: OS, platform, architecture, renderer, configuration, features | ⬜ | `STUDIO-02020` |
| `STUDIO-02041` | Separate the Studio host renderer from the game target renderer throughout | ⬜ | `STUDIO-02040` |
| `STUDIO-02050` | Define the service decomposition of the application shell | ⬜ | — |
| `STUDIO-02051` | Early guard: an exported project configures and builds with Studio unavailable | ⬜ | `STUDIO-02050` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-02001` — Write `docs/ARCHITECTURE.md` as the current architectural record

**Acceptance.** Supersedes `ANALYSIS.md` on everything concerning current CNA; states the invariant, the renderer/platform split, the capability model, the module architecture and the guard tests

### `STUDIO-02002` — Write `docs/CNA-GAPS.md` as the CNA deficiency register

**Acceptance.** Six gaps with affected API, current and expected behaviour, Studio impact, workaround, suggested fix and the test CNA would need

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

**Verification.** Unit tests over synthetic capability profiles: satisfied, unsupported, unclassified

### `STUDIO-02021` — Evaluate the contract against the live device at start-up

**Acceptance.** Studio reports platform, renderer, CNAEXT availability, classified capabilities, and the unmet requirements by name

### `STUDIO-02022` — Fail cleanly and precisely when the compiled renderer cannot host Studio

**Acceptance.** A diagnostic naming each missing requirement and whether it is unsupported or unclassified; no window is opened

**Verification.** Test with a synthetic profile missing one required feature

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

### `STUDIO-02040` — Define the target-profile model: OS, platform, architecture, renderer, configuration, features

**Acceptance.** A project carries named profiles rather than one global renderer string. Validity is decided by capability and build metadata, not by scattered name comparisons

### `STUDIO-02041` — Separate the Studio host renderer from the game target renderer throughout

**Acceptance.** No code path treats the two as one value. A project using only classic functionality is not denied a renderer merely because it cannot host Studio's UI

### `STUDIO-02050` — Define the service decomposition of the application shell

**Acceptance.** Explicit services with explicit dependencies — Project, Document, Selection, Command, Asset, Import, Play, Build, Package, Workspace, Preferences, Job — and no service locator. `StudioApplication` stops being the place new subsystems are added

### `STUDIO-02051` — Early guard: an exported project configures and builds with Studio unavailable

**Acceptance.** A reduced form of the Phase 18 standalone test, run in CI from the start so the invariant cannot rot while the packaging workstream is still ahead

