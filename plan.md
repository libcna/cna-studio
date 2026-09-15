# CNA Studio — Master Development Plan

> This is the authoritative roadmap for CNA Studio and the source of truth for what is done.
> Per-phase task detail lives in [`plans/`](plans/); this file holds the index, the id space, the
> phase status and the global progress.
>
> Architecture: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
> Where this repository came from: [`docs/ORIGIN.md`](docs/ORIGIN.md).
> The prototype's task history: [`docs/LEGACY-EDITOR-TASK-MAP.md`](docs/LEGACY-EDITOR-TASK-MAP.md).
> CNA deficiencies Studio has found: [`docs/CNA-GAPS.md`](docs/CNA-GAPS.md).

## What this plan is

CNA Studio is a multi-thousand-hour engineering programme, not a project with an end date this
year. This plan is written to survive that: stable ids, explicit dependencies, honest status, and a
decomposition that grows as work approaches rather than one invented up front.

**The rule that shapes every task below:**

> **CNA Studio produces CNA games, not CNA Studio games.**

A game authored in Studio is an ordinary CNA C++ project that builds, runs and ships with Studio
uninstalled. Any task that would compromise that is wrong, however convenient it is.

## Status legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Complete and verified |
| 🔄 | In progress |
| ⬜ | Not started |
| ⛔ | Deliberately deferred — the reasoning is recorded on the task |
| 🔬 | Blocked on research or an architectural decision |

## Id scheme

Task ids are `STUDIO-PPNNN`, where `PP` is the phase number (`00`–`35`) and `NNN` is the
sequence within that phase. Phase 2's twentieth task is `STUDIO-02020`.

- Ids are **stable** and **never reused**. A task that is cancelled keeps its id and is marked ⛔.
- Adding a task appends within its phase; it never renumbers an existing one.
- Sequence numbers are deliberately sparse, so related work can be inserted near what it belongs to.
- The scheme has room for 999 tasks per phase — 36,000 in total — which is well beyond what this
  programme will need.

Legacy `ED-*` ids from the CNA Editor prototype are **retired** and are never issued again. The
mapping is in [`docs/LEGACY-EDITOR-TASK-MAP.md`](docs/LEGACY-EDITOR-TASK-MAP.md).

## Global progress

**163 of 488 tasks complete** `████████░░░░░░░░░░░░░░░░░`  33.4%

| Status | Count |
|--------|------:|
| ✅ Complete | 162 |
| 🔄 In progress | 10 |
| ⬜ Not started | 310 |
| ⛔ Deferred | 2 |
| 🔬 Blocked | 4 |
| **Total** | **488** |

> **On the task count.** 488 tasks are decomposed today. That is not the final number: the
> programme is expected to reach the low thousands as the later phases are broken down on approach.
> Tasks are added when the work is understood well enough to state a completion condition — never
> to reach a number. A phase whose detail is still coarse says so by having few rows, which is
> honest; padding it would make this document worthless as a planning instrument.

## Phases

| Phase | Title | Ids | Status | Tasks | Done | Progress |
|------:|-------|-----|:------:|------:|-----:|----------|
| 0 | [Audit and baseline](plans/phase-00-audit-baseline.md) | `STUDIO-00NNN` | 🔄 | 15 | 13 | `█████████░` |
| 1 | [Product rename](plans/phase-01-product-rename.md) | `STUDIO-01NNN` | 🔄 | 16 | 13 | `████████░░` |
| 2 | [Architecture refresh](plans/phase-02-architecture-refresh.md) | `STUDIO-02NNN` | 🔄 | 27 | 22 | `████████░░` |
| 3 | [Studio UI core](plans/phase-03-ui-core.md) | `STUDIO-03NNN` | 🔄 | 33 | 29 | `█████████░` |
| 4 | [CNAEXT UI renderer](plans/phase-04-ui-renderer.md) | `STUDIO-04NNN` | 🔄 | 19 | 11 | `██████░░░░` |
| 5 | [Docking and workspace](plans/phase-05-docking.md) | `STUDIO-05NNN` | 🔄 | 15 | 14 | `█████████░` |
| 6 | [Studio shell](plans/phase-06-studio-shell.md) | `STUDIO-06NNN` | 🔄 | 24 | 22 | `█████████░` |
| 7 | [Existing-panel migration](plans/phase-07-panel-migration.md) | `STUDIO-07NNN` | 🔄 | 27 | 15 | `██████░░░░` |
| 8 | [Project Hub](plans/phase-08-project-hub.md) | `STUDIO-08NNN` | ⬜ | 12 | 0 | `░░░░░░░░░░` |
| 9 | [Content Browser 2](plans/phase-09-content-browser.md) | `STUDIO-09NNN` | ⬜ | 16 | 0 | `░░░░░░░░░░` |
| 10 | [Asset pipeline and importing](plans/phase-10-asset-pipeline.md) | `STUDIO-10NNN` | ⬜ | 13 | 0 | `░░░░░░░░░░` |
| 11 | [3D viewport 2](plans/phase-11-viewport.md) | `STUDIO-11NNN` | ⬜ | 14 | 0 | `░░░░░░░░░░` |
| 12 | [Selection and gizmos 2](plans/phase-12-gizmos.md) | `STUDIO-12NNN` | ⬜ | 11 | 0 | `░░░░░░░░░░` |
| 13 | [World Outliner 2](plans/phase-13-outliner.md) | `STUDIO-13NNN` | ⬜ | 12 | 0 | `░░░░░░░░░░` |
| 14 | [Details Inspector 2](plans/phase-14-inspector.md) | `STUDIO-14NNN` | ⬜ | 18 | 0 | `░░░░░░░░░░` |
| 15 | [C++ gameplay component workflow](plans/phase-15-cpp-gameplay.md) | `STUDIO-15NNN` | ⬜ | 12 | 0 | `░░░░░░░░░░` |
| 16 | [Play In Editor 2](plans/phase-16-play-in-editor.md) | `STUDIO-16NNN` | 🔄 | 18 | 3 | `██░░░░░░░░` |
| 17 | [Build profiles](plans/phase-17-build-profiles.md) | `STUDIO-17NNN` | 🔄 | 12 | 5 | `████░░░░░░` |
| 18 | [Cook, package and export](plans/phase-18-package-export.md) | `STUDIO-18NNN` | ⬜ | 11 | 0 | `░░░░░░░░░░` |
| 19 | [Materials](plans/phase-19-materials.md) | `STUDIO-19NNN` | ⬜ | 9 | 0 | `░░░░░░░░░░` |
| 20 | [Lighting](plans/phase-20-lighting.md) | `STUDIO-20NNN` | ⬜ | 8 | 0 | `░░░░░░░░░░` |
| 21 | [Animation](plans/phase-21-animation.md) | `STUDIO-21NNN` | ⬜ | 9 | 0 | `░░░░░░░░░░` |
| 22 | [Material and shader graph](plans/phase-22-shader-graph.md) | `STUDIO-22NNN` | ⬜ | 10 | 0 | `░░░░░░░░░░` |
| 23 | [Particles and VFX](plans/phase-23-particles.md) | `STUDIO-23NNN` | ⬜ | 4 | 0 | `░░░░░░░░░░` |
| 24 | [Audio](plans/phase-24-audio.md) | `STUDIO-24NNN` | ⬜ | 8 | 0 | `░░░░░░░░░░` |
| 25 | [Terrain and world tools](plans/phase-25-terrain.md) | `STUDIO-25NNN` | ⬜ | 7 | 0 | `░░░░░░░░░░` |
| 26 | [Physics and navigation tooling](plans/phase-26-physics-nav.md) | `STUDIO-26NNN` | ⬜ | 8 | 0 | `░░░░░░░░░░` |
| 27 | [Profiling and diagnostics](plans/phase-27-profiling.md) | `STUDIO-27NNN` | ⬜ | 14 | 0 | `░░░░░░░░░░` |
| 28 | [Plugins and SDK](plans/phase-28-plugins.md) | `STUDIO-28NNN` | ⬜ | 11 | 0 | `░░░░░░░░░░` |
| 29 | [Renderer and platform matrix](plans/phase-29-renderer-matrix.md) | `STUDIO-29NNN` | 🔄 | 7 | 5 | `███████░░░` |
| 30 | [Large-project performance](plans/phase-30-performance.md) | `STUDIO-30NNN` | ⬜ | 12 | 0 | `░░░░░░░░░░` |
| 31 | [Reliability](plans/phase-31-reliability.md) | `STUDIO-31NNN` | 🔄 | 13 | 1 | `█░░░░░░░░░` |
| 32 | [Accessibility and localisation groundwork](plans/phase-32-accessibility.md) | `STUDIO-32NNN` | ⬜ | 6 | 0 | `░░░░░░░░░░` |
| 33 | [Documentation, templates and CI](plans/phase-33-docs-ci.md) | `STUDIO-33NNN` | 🔄 | 21 | 10 | `█████░░░░░` |
| 34 | [Release engineering](plans/phase-34-release.md) | `STUDIO-34NNN` | ⬜ | 6 | 0 | `░░░░░░░░░░` |
| 35 | [Production polish](plans/phase-35-polish.md) | `STUDIO-35NNN` | ⬜ | 10 | 0 | `░░░░░░░░░░` |

## Phase purposes

**Phase 0 — Audit and baseline.** Establish exactly what was inherited, prove it works, and record the measurements everything later is compared against.

**Phase 1 — Product rename.** Turn the imported prototype into CNA Studio at the level of identity: build targets, executable, public API, user-visible text and documentation.

**Phase 2 — Architecture refresh.** Replace the prototype's stale architectural assumptions with a model that matches current CNA, and make the invariants enforceable by test rather than by review.

**Phase 3 — Studio UI core.** Build the widget, state, layout, input and styling foundations of an original editor UI, CNA-free and headless-testable so that everything except the pixels is decided in CI.

**Phase 4 — CNAEXT UI renderer.** Draw the Studio UI through CNA's public modern graphics API: text, icons, batching, clipping, render resources and DPI, with no renderer-specific code.

**Phase 5 — Docking and workspace.** A first-class panel shell: docking, tab groups, splitters, floating panels and persistent layouts.

**Phase 6 — Studio shell.** The application frame: menus, toolbar, status bar, the command registry that everything routes through, and preferences.

**Phase 7 — Existing-panel migration.** Port every prototype panel onto the Studio UI and retire the Dear ImGui presentation.

**Phase 8 — Project Hub.** A professional entry point: create, open, recent projects and a small set of well-maintained templates.

**Phase 9 — Content Browser 2.** Turn the prototype asset browser into a professional content workflow that scales to a real project.

**Phase 10 — Asset pipeline and importing.** Import the content a real game needs, incrementally, without absorbing large third-party parsers into Studio itself.

**Phase 11 — 3D viewport 2.** A professional 3D authoring viewport: navigation, visualisation modes and correctness.

**Phase 12 — Selection and gizmos 2.** Production-quality transform manipulation.

**Phase 13 — World Outliner 2.** Large-hierarchy editing that stays responsive and never loses a mutation.

**Phase 14 — Details Inspector 2.** A first-class production property editor driven by the descriptor model.

**Phase 15 — C++ gameplay component workflow.** Make project-defined C++ behaviour a first-class authoring concept — without an embedded IDE and without parsing all of C++.

**Phase 16 — Play In Editor 2.** Expand the separate-player architecture into a complete play-test workflow.

**Phase 17 — Build profiles.** Model the target matrix properly: platform, architecture, renderer, configuration and feature profile.

**Phase 18 — Cook, package and export.** Produce a standalone CNA game that does not know CNA Studio exists.

**Phase 19 — Materials.** Make Studio the primary authoring environment for CNA's modern rendering.

**Phase 20 — Lighting.** Lighting authoring that matches what the runtime can actually execute.

**Phase 21 — Animation.** Skeletal and sprite animation authoring and preview.

**Phase 22 — Material and shader graph.** A node-based authoring surface for CNA's modern graphics API.

**Phase 23 — Particles and VFX.** Author whatever the CNA runtime can execute — without building a new particle runtime inside Studio.

**Phase 24 — Audio.** Grow audio preview into an authoring workflow.

**Phase 25 — Terrain and world tools.** Large environment authoring, well after core scene editing is robust.

**Phase 26 — Physics and navigation tooling.** Editor integration for runtime physics and navigation — Studio does not become the owner of a physics engine.

**Phase 27 — Profiling and diagnostics.** Professional performance and debugging tools.

**Phase 28 — Plugins and SDK.** Third-party extensibility that fails safely.

**Phase 29 — Renderer and platform matrix.** Keep Studio correct as CNA's renderer and platform sets grow.

**Phase 30 — Large-project performance.** Scale to real projects, not to the example.

**Phase 31 — Reliability.** This is a content-authoring application. Losing work is unacceptable.

**Phase 32 — Accessibility and localisation groundwork.** Laid after the UI stabilises, on the metadata the UI core carries from the start.

**Phase 33 — Documentation, templates and CI.** Real developer documentation, and the test infrastructure that keeps all of it true.

**Phase 34 — Release engineering.** Ship Studio itself.

**Phase 35 — Production polish.** The long-running quality campaign that separates a tool that works from a tool people choose.
---

## Current state

The repository is at the end of the **first implementation tranche**. What exists today:

- The CNA Editor prototype is imported at the repository root, with its provenance recorded
  (`docs/ORIGIN.md`) and its baseline verified: **442 unit assertions, 12 CTest suites, 0 warnings**
  in the default dependency-free configuration.
- The product rename is complete: `cna-studio` executable, 12 `cna-studio-*` targets, the
  `CNA::Studio` namespace, `include/CNA/Studio/`, `CNA_STUDIO_*` options, and user-visible text that
  no longer calls the product an editor. 94 files moved with `git mv`, so per-file history survived.
- The architecture is re-stated against **current** CNA (`next`), which has changed substantially
  since the prototype's analysis: renderer and platform are separate axes, there are 50 renderer
  identities rather than 14, and `RendererCapabilityProfile` provides a genuine runtime capability
  model. See `docs/ARCHITECTURE.md`.
- Six CNA gaps are registered, two of them re-verified as fixed upstream since the prototype filed
  them. See `docs/CNA-GAPS.md`.
- The **first foundations of the native Studio UI** exist and are tested headless: the design token
  model with both shipped themes (`STUDIO-03004`/`03005`/`03006`), widget identity with per-frame
  collision detection (`STUDIO-03002`), and retained widget state with reclamation
  (`STUDIO-03003`), in a `cna-studio-ui-core` module that links no CNA at all.

- A **running Studio shell**: menu bar, grouped toolbar, three docks with tab strips, a gridded
  viewport and a status bar, laid out from theme metrics so the whole frame scales with DPI
  (`STUDIO-06003`/`06006`/`06007`, `STUDIO-05002`). It emits `UiDrawData` — the same seam
  `CnaUiRenderer` already draws through CNA's public API — so the native UI inherits a working CNA
  renderer rather than needing one written for it.
- The **first screenshot tests**, running with no GPU at all: a CPU rasteriser turns that geometry
  into an image in-process, so the shell has golden-image coverage at six resolution and DPI
  combinations long before the graphical CI of `STUDIO-33010` exists.

`cna-studio --shell-preview=shell.png` renders it from the real binary with no GPU and no
display.

- A **renderer and platform catalogue** replacing the prototype's stale 14-entry backend table:
  all 50 of CNA's renderer identities classified, platforms modelled as their own axis, and a
  legacy alias table that migrates a `.cnaproject` written by the prototype rather than failing on
  it (`STUDIO-02030`/`02031`/`29001`/`29002`).
- **Ten architecture guard tests** (`STUDIO-02032`/`02033`/`02034`) that fail the build on a
  `CNA::Internal` reference, a direct backend call, a CNA header outside the viewport module, a
  Dear ImGui dependency in the native UI, a missing SPDX header or a hard-coded renderer-name
  comparison — each naming the file, the line and the rule.

- The **CNA-backed build restored against current CNA**. The prototype's viewport did not compile
  against it, its CMake read a variable CNA no longer defines (so the player built under a name
  discovery could never find), and both hosts took a graphics profile under which every screenshot
  throws. All three are fixed (`STUDIO-02060`), and Studio now builds, runs and draws its full UI
  through real CNA on the SOFTWARE renderer.

- The **input and action layer** that turns the shell from a picture into a UI: hover, click,
  mouse capture that survives leaving a widget's bounds, clip-aware hit testing, modal input
  blocking, keyboard focus and Tab navigation (`STUDIO-03007`…`03012`), and a central action
  registry through which menus, toolbars and shortcuts all invoke the same object
  (`STUDIO-06001`/`06002`).

The suite is **566 assertions across 17 CTest suites**, green and warning-free in both GCC Debug
and GCC Release at `-Werror`, plus **22 CTest suites green against a real CNA checkout**. Two latent defects inherited from the prototype were found by
building at `-O3 -Werror`, which the prototype's CI did not do, and both are fixed: an ignored
`freopen` result that would have sent a build's output nowhere while leaving an empty log, and a
dangling reference to a member of a by-value `std::optional` temporary in a recovery test.

What the prototype already provides, and what Phase 7 must carry across rather than rewrite:
scene and prefab documents with undo on every mutation; a UUID-stable asset database; a 2D and a 3D
viewport with translate/rotate/scale gizmos; glTF model import; sprite animation; tilemaps; layers
and tags; a validation panel; autosave and crash recovery; format migration; a plugin host; a
separate `cna-player` process that draws the game and takes live edits; and a Build panel that
drives the project's own CMake.

## Rules this plan is held to

1. **A task is done when its acceptance condition holds and its verification passes** — not when
   scaffolding for it exists. A phase is never marked complete because its structure is in place.
2. **Every new behaviour comes with a test**, at the cheapest layer that can actually catch the
   failure. A deterministic unit test beats a screenshot test; a screenshot test beats nothing for
   graphical behaviour.
3. **Nothing working is removed without a replacement.** The prototype's capabilities are migrated,
   not discarded, and a legacy ✅ does not count as done here until it works through the Studio UI.
4. **The repository stays buildable and green after every tranche.** Incomplete work is marked, not
   hidden.
5. **Architectural invariants are enforced by test**, not by comment. The guard tests are listed in
   `docs/ARCHITECTURE.md` §10 and tracked as tasks in Phase 2.
6. **Blocked means blocked on authority or information**, not on difficulty. A blocked task records
   what decision is needed and who can make it, and work continues elsewhere.

## Deliberately not built

Recorded so that the absence is a decision rather than an oversight.

| Not building | Why |
|--------------|-----|
| An embedded C++ IDE | Studio opens the user's real IDE. Building a worse one before basic game production is excellent would be a large subsystem serving nobody |
| Visual scripting | C++ is the primary gameplay language. Visual scripting ahead of an excellent C++ workflow would be solving the wrong problem |
| A new physics engine | Studio provides editor integration for a runtime's physics. Wanting collider widgets is not a reason to own a physics engine |
| A new particle runtime inside Studio | Studio authors what the CNA runtime can execute. Runtime ownership stays with CNA or a project plugin |
| A proprietary build system | The project's own CMake is the build. Studio drives it and never replaces it |
| A mandatory binary project database | Authored data stays human-readable, diffable and version-control friendly |
| World partitioning and streaming | Deferred (`STUDIO-25007`) until a real project demonstrates the need |
| Remote profiling | Deferred (`STUDIO-27013`) until local profiling is good |

## Open questions

| Id | Question | Blocked on |
|----|----------|------------|
| `STUDIO-15001` | Which reflection mechanism for project-defined C++ components? | An architectural decision, recorded before implementation |
| `STUDIO-16020` | How does player output reach a Studio viewport without compromising process separation? | Research |
| `STUDIO-16021` | Which native code reload strategy earns its reliability cost? | Research, after the build workflow exists |
| `STUDIO-02010` | Which renderer identities actually exhibit CNA gap G-03? | Access to the renderer set in CI |
| `STUDIO-02011` | Is CNA gap G-05 still present on the current renderer set? | Access to the renderer set in CI |

## How to work this plan

1. Pick an unblocked ⬜ task whose dependencies are all ✅.
2. Mark it 🔄 in its phase file.
3. Implement it, with the test its verification column names.
4. Mark it ✅ only when the acceptance condition actually holds.
5. Regenerate the progress figures and update `HANDOFF.md` before ending a session.
