# CNA Studio — Handoff

State of the work in progress, for whoever continues it. Updated at the end of each long session.

**Last updated:** 2026-09-17

---

## What this session did

**It corrected one architectural assumption before it hardened, closed out the two Phase 7 rows that
had become zombies, and shipped the Project Hub — including the first CI proof that a project a
*user creates* builds and runs with Studio uninstalled.**

| Piece | Where it stood | Where it stands |
|-------|----------------|-----------------|
| **The invariant** | "A CNA Studio project remains a CNA **C++** project" — C++ inside Studio's model | Generalised. One `StudioLanguageAdapter`, one implementation, and three guard tests that refuse the `if (language == …)` chain before it can be written |
| **Phase 7** | 44 of 46, two rows open, one of them impossible | 45 of 46 and closed. `STUDIO-07003` proved and ticked; `STUDIO-07001` ⊘ superseded rather than back-dated |
| **Phase 8** | 0 of 12 | 12 of 12, with four templates and a CTest case per template that creates, reopens, configures, compiles and runs |

**The language seam is the piece with the longest shadow, and it had to go first.** CNA has several
language bindings, and the old invariant put C++ in *Studio's model* rather than in a project.
Phase 8 is the tranche that would have written most of the consequences — the Project Hub, project
creation, build orchestration, packaging — one individually reasonable `if` at a time. Doing it
afterwards would have been a rewrite of every panel that grew one. Doing it first cost one
interface, one implementation and a header split; the Hub that followed cannot name a compiler,
and that is checked rather than intended.

**`STUDIO-07003` was true and the plan did not know.** Its last open row was the prototype's 2D/3D
toolbar control, waiting on Phase 11 — which shipped it, and nothing edited the phase file to say
so. It is closed on a check now, at the level its acceptance is written at: *reachable*, not
registered. A command that exists and is on no menu, no toolbar and no viewport strip is one a user
cannot press, and the pre-existing command check passed it happily.

**`STUDIO-07001` could not be made true and was not pretended into one.** It was the temporary
compatibility adapter for the Dear ImGui migration; the migration finished and `STUDIO-07030`/`07031`
deleted Dear ImGui, so the condition it waited on — both UIs in one running Studio — has nothing on
the other side of it. The plan gained a ⊘ status for exactly this case, and the guard that adds up
its status column gained the row. Resurrecting a deleted UI to earn a tick would have been the wrong
answer to a bookkeeping problem.

**`STUDIO-08011` passed on all four templates**, on a CNA-backed `SOFTWARE` build: `basic-sample`
327 s, `empty-2d` 263 s, `empty-3d` 244 s, `xna-compatible` 215 s — each created, reopened in
Studio, configured with nothing but CMake and a CNA checkout, compiled, and run to the line it had
to print. **It is the result that matters most.** `STUDIO-02051` has proved since Phase 2 that an
*exported* project builds with no Studio. Every project a user actually makes comes out of the
Project Hub instead, and until this session that path had never been compiled by anything — a
template producing a tree that does not build would have been found by the first person to press
New Project. There is now one CTest case per template directory, discovered by globbing
`templates/`, that creates a project, reopens it in Studio, configures it with nothing but CMake and
a CNA checkout, compiles it and requires the game to *say* what it did.

**And two defects the new tests found.** An XNA-compatible project was created already reporting a
missing startup scene, because `Project::createDefault` fills in a conventional scene path that is a
dangling reference for a project kind with no scenes. And `studioBuiltInLanguages().all()` binds a
reference into a temporary — a factory returning by value is the right shape and it is a shape that
is easy to use wrongly, which `-Wdangling-reference` caught on the first build.

## Where things are

| | |
|---|---|
| Repository | <https://github.com/libcna/cna-studio> |
| Branch | `claude/studio-baseline-audit-51dyxr` |
| HEAD | commit **109** — `docs: the handoff after the service extraction and the renderer measurement` |
| Working tree | Clean (everything below is committed and pushed) |
| Commits on this branch | 109, all authored `Robert Vokac <robertvokac@robertvokac.com>` |
| Added this session | 11 |

> **Why HEAD is recorded as a count and a subject rather than a hash.** The previous handoff named
> `8fe23bf` and was two commits stale within the same session, because a file cannot contain the
> hash of the commit that adds it: whatever hash is written is necessarily the *previous* one, and
> the next commit makes it wrong. A commit count and a subject line are both knowable before the
> commit is made, so they are correct the moment it lands. Check with
> `git rev-list --count HEAD` and `git log -1 --format=%s`.

### The eleven commits of this session

| Commit | What |
|--------|------|
| `studio: refuse a service locator by test rather than by rule` | `STUDIO-02059`, written **before** the services it governs |
| `studio: the renderer comparison is a service, not a corner of the shell` | `STUDIO-02056` |
| `studio: preferences own their ordering rule rather than a shell method` | `STUDIO-02057` |
| `studio-renderer: measure the two UI backends instead of assuming` | `STUDIO-04028` and `STUDIO-04029` |
| `studio-renderer: which CNA renderer this build uses is not the UI backend's to say` | Layer 3 separated from layer 2; the player stops linking a UI renderer |
| `docs: STUDIO-04027's blocker expired and a different one replaced it` | The deletion attempted, and what it found |
| `studio-ui: the native Details panel shows the asset you selected` | `STUDIO-07045` |
| `studio-scene: the World Outliner stops being quadratic in scene size` | `STUDIO-30013` |
| `docs: the Content Browser stat-s every asset on every frame` | `STUDIO-30014`, and `STUDIO-30015` filed |
| `docs: review the shell by eye again, and file the paint-order trap as a task` | `STUDIO-03041` filed |
| `docs: the handoff after the service extraction and the renderer measurement` | This file |

Read in this order to pick the work up:

1. [`plan.md`](plan.md) — the master roadmap and the source of truth for what is done
2. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the current architecture; supersedes `ANALYSIS.md`
3. [`docs/UI-RENDER-PATH.md`](docs/UI-RENDER-PATH.md) — what actually reaches the GPU, the four
   layers the phrase "the Studio renderer" was used for, and the migration's stages. **"What the two
   backends actually cost" is new** and is what `STUDIO-04027` decides on
4. [`docs/ORIGIN.md`](docs/ORIGIN.md) — where this repository came from and its verified baseline
5. [`docs/CNA-GAPS.md`](docs/CNA-GAPS.md) — CNA deficiencies Studio has found; **G-11 is new**: a
   UI vertex costs 56 bytes to carry 20 bytes of data
6. [`plans/phase-35-polish.md`](plans/phase-35-polish.md) — **new milestone**: CNA Studio Visual
   Quality 1.0, and why it was brought forward from the end of the programme

---

## Verification commands

**Last run, all five legs green:**

| Leg | Result |
|-----|--------|
| Debug | 1 335 assertions, 61 CTest suites |
| Release + `-Werror` | clean build, 61 CTest suites |
| ASan + UBSan | 1 335 assertions, no sanitiser report |
| CNA `SOFTWARE` | 67 CTest suites, the standalone export among them |
| CNA `SOFTWARE`, template builds | 4 of 4: 327 s, 263 s, 244 s, 215 s |
| CNA `OPENGL4` under Xvfb | 79 CTest suites, 16 of them windowed |

The default build has no external dependencies — no CNA checkout, no GPU, no window:

```bash
cmake -S . -B build -G Ninja
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The whole Project Hub story, from a build directory, with no GPU and no display:

```bash
./build/cna-studio --list-templates
./build/cna-studio --new-project=/tmp/MyGame --template=empty-3d --project-name="My Game"
./build/cna-studio --project="/tmp/MyGame/My Game.cnaproject" --headless --frames=2

# And the half that matters: the same directory, built with no Studio anywhere.
cmake -S /tmp/MyGame -B /tmp/MyGame/build -DCNA_ROOT=/path/to/cna -DCNA_GRAPHICS_RENDERER=SOFTWARE
cmake --build /tmp/MyGame/build -j4
cd /tmp/MyGame/build && SDL_VIDEODRIVER=dummy ./My_Game --frames=10
# -> My_Game: loaded 2 entities, drew 0 sprites
```

`ctest -R CnaStudioTemplateBuilds` is that sequence for every template, on a CNA-backed build. Both
it and `CnaStudioStandaloneExport` are labelled `slow` and hold a `RESOURCE_LOCK`, because each
compiles CNA into its own tree and running four at once is how a CI job runs out of memory.

CI runs all five configurations below, `OPENGL4` included as of `STUDIO-04029`. Run them all
locally before a push anyway: the CNA jobs
take the best part of an hour each, and these are where the latent defects have actually been
found — an ignored `freopen` result, a dangling reference to a subobject of a temporary, an ODR
violation that no compiler diagnosed, and a renderer that submitted every triangle correctly and
drew a blank window:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j4 && ./build-asan/tests/cna-studio-tests
```


```bash
cmake -S . -B build-werror -G Ninja -DCMAKE_BUILD_TYPE=Release -DCNA_STUDIO_WARNINGS_AS_ERRORS=ON
cmake --build build-werror -j4
ctest --test-dir build-werror
```

Build against a **shader-capable** CNA renderer, which is what the modern UI renderer needs and what
`SOFTWARE` cannot provide. This needs no GPU — Mesa's llvmpipe under Xvfb is enough:

```bash
apt-get install -y libgl1-mesa-dev xvfb
cmake -S . -B build-gl -G Ninja -DCNA_STUDIO_WITH_CNA=ON \
      -DCNA_STUDIO_CNA_ROOT=/path/to/cna -DCNA_SHARP_RUNTIME_ROOT=/path/to/sharp-runtime \
      -DCNA_GRAPHICS_RENDERER=OPENGL4 -DCNA_PLATFORM=SDL3 \
      -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF \
      -DCNA_ENABLE_NET=OFF -DCNA_ENABLE_DRACO=OFF -DCNA_CNAEXT=ON \
      -DCNA_STUDIO_TEST_DISPLAY=:99
cmake --build build-gl -j4

# xvfb-run rather than a backgrounded Xvfb: --server-num must agree with
# CNA_STUDIO_TEST_DISPLAY above, and this is what CI does.
xvfb-run --server-num=99 --server-args="-screen 0 1920x1080x24" ctest --test-dir build-gl
```

**`libgl1-mesa-dri` as well as `libgl1-mesa-dev` on a minimal image.** The `-dev` package is what
the configure needs; llvmpipe, which is what actually answers at run time, is in `-dri`. Without it
the build succeeds and the context creation fails.

See which UI render backend a host gets, and force either:

```bash
DISPLAY=:99 ./build-gl/cna-studio --host-capabilities          # prints the profile and the choice
DISPLAY=:99 ./build-gl/cna-studio --ui-renderer=modern ...     # refuse rather than fall back
DISPLAY=:99 ./build-gl/cna-studio --ui-renderer=compat ...     # the classic path, for an A/B
```

Run the native Studio UI in a real window, on a real CNA device — needs the CNA-backed build:

```bash
./build-cna/cna-studio --ui=studio --project=examples/HelloSprites/HelloSprites.cnaproject
```

Add `--select=Player` to start with something in the Details panel, `--panel=output` to raise the
Output Log, and `--workspace=none` to leave your saved layout alone. On a machine with no display,
`SDL_VIDEODRIVER=dummy` plus `--frames=N --screenshot=PATH` captures it instead.

Render the same shell headless, with no CNA, no GPU and no window:

```bash
./build/cna-studio --shell-preview=shell.png --shell-size=1280x720
./build/cna-studio --shell-preview=light.png --shell-theme=light --shell-scale=2.0
```

See it with a menu open and the pointer over a row, which is where the hover, highlight and popup
tokens are actually exercised:

```bash
./build/cna-studio --shell-preview=menu.png --shell-open-menu=File --shell-pointer=40,90
./build/cna-studio --shell-preview=pressed.png --shell-pointer=20,40 --shell-mouse-down
./build/cna-studio --shell-preview=tip.png --shell-pointer=20,40 --shell-tooltip
./build/cna-studio --shell-preview=sub.png "--shell-open-menu=Window>Panels" --shell-pointer=1000,110
./build/cna-studio --shell-preview=ctx.png --shell-pointer=133,533 --shell-right-click
```

**With the ported panels holding real content**, which is what most of the shell actually is. The
preview binds the same panels the editor does, so this needs no CNA and no display:

```bash
./build/cna-studio --shell-preview=panels.png --shell-size=1400x900 \
    --project=examples/HelloSprites/HelloSprites.cnaproject
./build/cna-studio --shell-preview=build.png --panel=build --shell-size=1400x900 \
    --project=examples/HelloSprites/HelloSprites.cnaproject
```

Export a project as a standalone CNA game, and build it with Studio nowhere in sight:

```bash
./build/cna-studio --project=examples/HelloSprites/HelloSprites.cnaproject --export=/tmp/game
cmake -S /tmp/game -B /tmp/game/build -DCNA_ROOT=/path/to/cna
cmake --build /tmp/game/build && (cd /tmp/game/build && ./HelloSprites --frames=10)
```

See what Studio requires of a host renderer, and — on a CNA build — whether this one meets it:

```bash
./build/cna-studio --host-capabilities
```

See the Visual Quality 1.0 surfaces the regression matrix captures — the World Outliner with icons
and visibility toggles, the Content Browser card grid, the Details panel's axis colours and the
viewport toolbar:

```bash
./build/cna-studio --shell-preview=outliner.png --shell-size=1920x1080 --panel=outliner \
    --project=examples/HelloSprites/HelloSprites.cnaproject --select=Player
./build/cna-studio --shell-preview=content.png --shell-size=1920x1080 --panel=content \
    --project=examples/HelloSprites/HelloSprites.cnaproject
```

Measure what a frame of UI costs each render backend (`STUDIO-04028`). Needs no CNA, no GPU and no
window; `=NAME` selects scenarios by substring:

```bash
./build/cna-studio --ui-benchmark --ui-benchmark-frames=60 \
    --project=examples/HelloSprites/HelloSprites.cnaproject
./build/cna-studio --ui-benchmark=outliner --ui-benchmark-frames=11
```

Put an asset in the Details panel, which is the only way a still capture reaches the asset inspector
— it is otherwise opened by clicking a Content Browser row:

```bash
./build/cna-studio --shell-preview=asset.png --shell-size=900x420 --shell-panel-only=details \
    --project=examples/HelloSprites/HelloSprites.cnaproject \
    --select-asset=Assets/Textures/player.png
```

Collect the visual-test captures as CI artefacts:

```bash
CNA_STUDIO_TEST_ARTIFACTS=./artifacts ./build/tests/cna-studio-tests
```

Every capture the suite takes is reviewable this way. The matrix is five resolutions
(1280×720, 1600×900, 1920×1080, 2560×1440, 3840×2160) × two themes, plus four scenarios that put
the shell in a state a bare launch does not reach — a menu open, a context menu, a panel with a
project loaded, and the pointer on a row.

### Last measured result

Measured on a quiet tree at commit 108, before this file was written.

| Configuration | Result |
|---------------|--------|
| GCC 13.3 Debug, no CNA | **1311 test cases, 60 CTest suites, 0 failures, 0 warnings** |
| GCC 13.3 Release `-Werror`, no CNA | **1311 test cases, 60 CTest suites, 0 failures, 0 warnings** |
| GCC 13.3 Debug + ASan + UBSan, no CNA | **1311 test cases, 0 failures, no sanitizer reports** |
| GCC 13.3 Debug, **against real CNA** (`next`, `SOFTWARE`, SDL3) | **1320 test cases, 78 CTest suites, 0 failures** |
| GCC 13.3 Debug, **against real CNA** (`next`, `OPENGL4`, SDL3, under Xvfb) | **1320 test cases, 79 CTest suites, 0 failures** |

**Thirty cases added this session**: five for the service-locator guard, nine for the UI benchmark's
cost model, five for the asset inspector, two for the hierarchy grouping, four for the comparison
service, four for the preferences service, and one guarding the player against a UI-backend
dependency.

**The `OPENGL4` row is `STUDIO-04029`, and it is in CI now** rather than only on this machine.
`OPENGL4` is the first renderer this project has automated that can execute a shader, which makes it
the first on which the modern UI renderer runs at all. Its extra CTest suite over `SOFTWARE` is
`CnaStudioUiRenderBackendsAgree`, which cannot be declared on a renderer that can only run one of
the two backends — and the *set* difference is exactly that one suite, measured with `comm` over
`ctest -N`, which is the evidence `STUDIO-04027` turns on.

The nine extra *cases* in a CNA-backed run are `STUDIO-29007` (which reads CNA's own
`RendererSelection.cmake`), `STUDIO-04020` (the host key map), and the seven in
`StudioUiRendererTests.cpp` — all of which need a CNA checkout to exist at all. The eighteen extra
CTest *suites* are the window, screenshot, play-mode and standalone-export runs, which need a real
device.

The CNA-backed suite now includes the native shell on a real device in both themes and at 2x, the
shell with a project open, the workspace surviving a real process exit, and
`CnaStudioStandaloneExport` — which exports the example project, configures it with nothing but
CMake and a CNA checkout, compiles it and runs it (about four minutes, most of it CNA).

Every capturing case now passes `--screenshot-min-colors`, so a run whose geometry rendered to
nothing fails instead of writing a blank PNG and exiting zero (`STUDIO-04015`). The visual matrix
asks for **256** distinct colours rather than 16, which is what caught the modern renderer drawing a
blank frame three separate times — a shell capture that is genuinely drawn has thousands, and 16 is
a threshold a single filled rectangle can clear. The counts those
cases already asserted separate "submitted no geometry" from "submitted some"; this separates
"submitted some" from "drew something".

**All five configurations run in CI** — the four dependency-free and `SOFTWARE` ones as of
`STUDIO-33022`/`STUDIO-33023`, and `OPENGL4` as of `STUDIO-04029`, which made the `cna` job a matrix
over the two renderers rather than a second job that could drift from the first. They are still
worth running locally before a push: each CNA leg takes the best part of an hour, and these are
where the latent defects have actually been found.

Baseline at import, for comparison: 442 test cases, 12 CTest suites.

---

## What was completed

Task ids are `STUDIO-PPNNN`; see `plan.md` for the full list. **296 of 576 tasks are complete.**
Per-phase counts and the headline are checked by the test suite — `STUDIO-33018` for `plan.md` and
`STUDIO-33019` for this file — so neither can drift from the phase files again. The second was added
after this file had drifted by nineteen tasks and a hundred and fifty-eight test cases, which is
exactly the failure the first was written to prevent in the other file.

**Phase 0 — Audit and baseline** (14 of 15). Imported `cna-lab/cna-editor` at
`3bce82dd74e9a201a21e31308d43d2ee7761d641`, verified its baseline, re-audited current CNA.

**Phase 1 — Product rename** (13 of 16). `cna-studio` executable, `cna-studio-*` targets, the
`CNA::Studio` namespace, `CNA_STUDIO_*` options. 94 files moved with `git mv`.

**Phase 2 — Architecture refresh** (41 of 47). The architecture record, the CNA gap register, the
roadmap, sixteen architecture guard tests, the restored CNA-backed build, the Studio host
capability contract, the six-axis build target model, and **the standalone export**: `--export=DIR`
writes a project that builds and runs with Studio uninstalled, and `STUDIO-02051` proves it by
doing so. **The modern-API requirement is a requirement now, not a sentence in a document.**
`modernApiAvailable` was a field on a report that nothing read and that the code set to `true`
unconditionally; it is read from `CNA_CNAEXT` and `getEngineLayerVersion()` (which also catches a
header and a library that disagree about the layer version), it is the first outcome the host
evaluation emits, and it decides `canHostStudio()` — with Cases A–D naming what each combination of
modern and compatibility support does, including the two that say no. **The compatibility host
profile is retired now (`STUDIO-02074`).** `StudioHostProfile` has one member, `Modern`;
`resolveStudioUiBackend` takes one evaluation and returns the modern backend or refuses, with no
second profile left to fall back to. It held on for a reason — `SOFTWARE` cannot execute a shader
and was, until `STUDIO-04029` put `OPENGL4` under Xvfb into CI, the only renderer this project's CI
could build at all — and it was retired only once that reason stopped holding. `SOFTWARE`'s CI leg
is repurposed rather than dropped: it still builds and tests `cna-studio` and
`cna-player-software` (a valid *game*-target renderer, capability contract Case D), and now asserts
Studio's refusal diagnostic instead of hosting Studio via the compatibility renderer. This unblocks
`STUDIO-04027`: `CnaUiRenderer` is constructed nowhere left in `src/` or `include/`.

**No service can reach another through a locator or a singleton, and that is a test.**
`STUDIO-02059` scans for the four structures every locator is actually built from — a mutable
`static` holding a Studio type, a `static` function handing out a reference or pointer to one, a
mutable namespace-scope variable of one, and `static T& get()` — plus `typeid`/`std::type_index`,
which is how a heterogeneous registry is keyed. It matches *structures*, so renaming `instance()` to
`shared()` evades nothing, and it deliberately permits a mutable `static` that holds no Studio type,
because a CRC table and a thread-local random engine are caches of pure functions and failing them
is how a guard gets switched off. Proved against fixtures for each prohibited shape, against the
things that look like one, and end to end by putting a real singleton into `StudioBuildService.cpp`.
It was written *before* the services it governs, which is the point: a guard added last has to be
made to pass, and one added first is a rule the next three services are written against.

**The application shell is being decomposed into services** rather than growing into the object
that knows everything: `StudioPlayService` owns the player process, the play state, the build
lookup and the session renderer override; `StudioBuildService` owns the build process, its finish
transition and packaging; `StudioComparisonService` owns the renderer comparison — a half-hour
multi-process run with its own state machine and its own failures, which is why it is not folded
into play although both launch players; and `StudioPreferencesService` owns the rule that a
preference is *applied and then persisted*, so a write that fails still leaves the user looking at
what they chose. Three of the four are testable with a context, a log and a lambda. Neither reaches back into the shell — each takes a notification *sink* and
its dependencies explicitly, and there is no service locator to register with. That is the point:
a locator would make every dependency invisible again, one `get<T>()` at a time.

**Phase 3 — Studio UI core** (31 of 35). Design tokens and two themes, widget identity, retained
state, the draw list, input routing with capture and focus, the five-phase frame lifecycle, cursor
requests, **tooltips with a per-widget delay**, **popup layering and input blocking**, widget
helpers, text measurement, High-DPI correctness including the seams, scrolling with row
virtualisation, a tree view, the text selection model **stepping by grapheme cluster, keyboard and mouse alike**, the
clipboard seam, an editable text field,
and **a drop-down over a deferred popup** — the facility that lets a popup escape the panel it was
opened in, **typed drag and drop**, and **a modal dialog** — a window that owns the frame until it
is answered, which is what About, Save Layout As and every confirmation are built on.

**Phase 4 — CNAEXT UI renderer** (25 of 29). Vertex management, batching, nested scissor clipping,
rounded rectangles, clip culling, real text with kerning and correct baselines, **forty-three
icons drawn as vector paths** with no vendored asset, **a glyph atlas that doubles rather than
losing text** and says so in Diagnostics, **uploads of the changed rectangle rather than four
megabytes**, **screenshot readback with a blank-frame assertion behind it**, and the guard that
checks the host key map against Studio's own key vocabulary.

**And the phase now means what its name says.** `STUDIO-04021` asked which CNA graphics API the UI
actually reaches the GPU through, and the answer — read off the calls, not the names — was the
classic XNA one: `BasicEffect`, `GraphicsDevice::DrawUserIndexedPrimitives`, not one `CNA::Graphics`
type anywhere. `docs/UI-RENDER-PATH.md` records that, and how a phase called "CNAEXT UI renderer"
came to contain no CNAEXT for six phases; `STUDIO-04022` corrected the ledger rather than the
wording. The renderer that closes it is real work, not a rename: `StudioModernUiRenderer` builds a
`ShaderEffect` from a `ShaderPackageEXT` carrying four GLSL variants (desktop and ES, vertex and
fragment), uploads through `DynamicVertexBuffer`/`DynamicIndexBuffer` that grow to powers of two and
never shrink. While the classic backend still existed, both were `StudioUiRenderBackend`
implementations picked at run time, and `CnaStudioUiRenderBackendsAgree` drew the same shell through
each on `OPENGL4` and compared the PNGs **byte for byte** — after first asserting that each run used
the backend it was asked for, so the test could not quietly become a comparison of one renderer with
itself.

**And the phase had a measurement instead of an assumption, which is what decided the rest.**
`STUDIO-04028` is `cna-studio --ui-benchmark`: eight representative frame shapes, reporting what
each backend asks the device to do and what the frame costs to describe. **The classic backend put
17–18× as much geometry on the bus, on every shape measured**, because `DrawUserIndexedPrimitives`
takes user arrays the driver copies per call and the array runs from the command's base vertex to
the end of its list — which, in any list under 65 535 vertices, is the whole array. The ratio is
roughly the draw-call count and grows as the UI is batched more finely. It needs no CNA, no GPU and
no window, and the model is checked against the real backend frame by frame on any run with a frame
limit. `STUDIO-04029` put `OPENGL4` under Xvfb into CI as a second matrix leg, with an assertion
that each leg gets the backend it exists to cover — a silent fallback would be a green tick over
nothing.

**`STUDIO-04027` then deleted the classic backend.** `STUDIO-02074` retired the compatibility host
profile that was `CnaUiRenderer`'s only remaining caller (SOFTWARE, `--ui-renderer=compat`) once
`OPENGL4` under Xvfb gave this project a modern-profile CI leg for real, which left `CnaUiRenderer`
constructed nowhere in `src/` or `include/`. `include/CNA/Studio/UiRenderer/CnaUiRenderer.hpp` and
`src/ui-renderer/CnaUiRenderer.cpp` are gone; `cna-studio-ui-renderer` keeps its module boundary
with one backend in it instead of two; `CnaStudioUiRenderBackendsAgree` and
`cmake/UiRendererAbTest.cmake` are gone with the second backend they compared against.
`studioUiGpuVertexStrideMatches()` — the one piece of `CnaUiRenderer.cpp` that was never about which
backend draws, a `STUDIO-04028` cost-model sanity check — moved to `CnaStudioShellHost.cpp`, its
only caller. The 17–18× figure stays measured, not deleted: `StudioUiBenchmark`'s cost model still
computes what the classic path would have cost, as a pure function of `UiDrawData` with no CNA
dependency, which is the permanent record the deletion decision rested on.

**Phase 5 — Docking** (14 of 15). The dock node tree, splits, draggable splitters with
minimum sizes and cursor shapes, tab strips, opening and closing panels, serialization, restoring
the default, dropping panels a build no longer has, never failing to start on a corrupt layout, the
layout surviving between runs, tab reordering, dragging a panel to another dock with a drop-target
preview, **undocking into a floating window** — moved, resized, given more tabs and docked again,
with its geometry clamped back into view on a smaller screen — and **arrangements saved under a
name**, in the same file, with Save Layout As and Dock All Windows in the Window menu.

**Phase 6 — Studio shell** (24 of 24). The action registry and core action set; an interactive menu
bar, toolbar and tab strips driven entirely by it; nested submenus with hover opening and keyboard
traversal; context menus; shortcut dispatch with scope precedence; the preview entry point;
`--ui=studio`, the native shell in a real window on a real CNA device; the shell opening a project on
the editor's own `StudioContext`; the core commands bound with live enablement; **a status bar that
reports** what is open, whether it is saved, what is running and what the project ships on, with a
progress bar and a Stop button for the running job; **the About dialog**, whose text the host
supplies; **preferences** — a model separate from project settings, a versioned file beside the
workspace layout, and a panel that applies every change as it is made; and **shortcut rebinding**,
where a row takes the next chord rather than typed text, a chord another command holds is refused
with the holder named before the binding is accepted, and the result is stored as an override so it
survives a restart; and **notifications** — a build, a package, a renderer comparison or a crashed
player announcing itself over the corner of the workspace, with the panel that explains it offered
as a button, a failure that stays until it is dismissed, and every one of them written to the log.

**Phase 7 — Panel migration** (45 of 46). The strangler seam itself — one log model
read by both consoles, a panel content seam on the shell, and a module for the ported panels — and
**every prototype panel now ported off Dear ImGui except the material editor**: the Output Log, the
World Outliner, the Details panel (with real editors for every property kind), the Content Browser,
the Build panel, the Problems panel, the Diagnostics panel and the Backends comparison — plus the
History panel and the Layers panel, which are new to the native shell. Binding them moved out of the
CNA-linked module, so the headless preview shows the same panels the editor does. **Coverage parity
is now proven both ways**: the tests read the prototype's own panels, menu bar, toolbars and
shortcut dispatcher and require every item to be accounted for in the inventory, which is how the
toolbars — never inventoried at all, and the only home of Pause, Step, the backend chooser and the
tilemap tools — were found. **Crash recovery is the first unanswered row to close**: everything
about it already worked and was shared, but nobody was running it on the native host, so
`--ui=studio` had none. It is one `StudioRecoverySession` now, driven by whichever host is running,
and the offer arrives as a sticky notification rather than a log line at start-up. **Rename in place**
was the second: an editable tree row, `F2` taken back from Build (which is `Ctrl+B` now, because the
prototype's `F2` is Rename), and the rename through the history like every other edit. The last
toolbar row closed with a **"Play on" strip** in the Backends panel, overriding for the session the
renderer the project names — not persisted, because the project's renderer is what the game ships on.
**File > Exit** became a real command over a `setQuitHandler` seam, asking about unsaved changes
first, and the dialog gained an answer handler because polling `dialogResult()` a frame later only
worked while nothing rendered in between. **Parity is now proven underneath the surface as well**:
`docs/UI-CAPABILITY-PARITY.md` accounts for all thirty-nine `StudioUi` capabilities the prototype's
panels are written against, each with a native answer and a named test the suite checks *exists*,
and the prototype's four dock sides are checked against where the native layout actually resolves.
**The two UIs are captured side by side** in `docs/reference/` at 1280x720 and 1920x1080 through a
real CNA renderer, reviewed in `docs/VISUAL-ACCEPTANCE.md` — with
`native-vq1-1920x1080.png` and `native-vq1-1280x720.png` added beside them as the current state
rather than over them, because overwriting half of a matched pair leaves a comparison of two
different days — which found three things no surface
inventory could: the prototype's Inspector shows the Scene Environment, an editable Grid Snap and
the project's layer list when nothing is selected, and the native Details panel showed a sentence.
All three are answered now, in the Details panel standing idle and through the command history —
and closing them found that `studioTextField` committed **twice** for any caller that normalises
what it stores, which is every numeric field in Studio. **Plugin menus** were the last unanswered
menu row and the one architectural one: a plugin's commands are registry actions now rather than
rows somebody draws, so a shortcut, enablement and the shortcut editor apply to them for free.

**Removing Dear ImGui is still not safe, and that was verified rather than assumed.** The inventory
had been taken over panels, menus, toolbars and shortcuts — and `Add Component` is none of those,
because it is a *button inside a panel*. `STUDIO-07041` inventoried the prototype's controls, and
found five sections of the prototype's Inspector with no native answer: prefab overrides, the sprite
animation preview, the audio preview, the asset inspector and the material asset editor. Deleting
the ImGui panels today would delete, among other things, the only way to add a component to an
entity. One of the five is closed — `STUDIO-07040`, Add and Remove Component, through the history
like every other edit — and the other four are `STUDIO-07042`–`07046`, which `STUDIO-07030` now
depends on. That dependency is the deliverable of this part: "verify that independently" produced a
blocking answer, and the blocking answer is written into the ledger rather than around it.

**One of the five blocking Inspector sections is closed.** `STUDIO-07045`, the asset inspector:
identity, kind and the importer's declared settings, edited through the command history. It was not
only a missing section — the native Content Browser and the native Details panel had *different*
ideas of which asset was selected, so it could not have worked even once written. The selection is
`StudioContext`'s now, as the outliner's entity selection already was, which brings the
one-thing-at-a-time rule with it. Four sections left, and they are the critical path for the classic
UI render backend as well as for Dear ImGui.

**All five are now closed, and Dear ImGui itself is deleted.** `STUDIO-07042`–`07046` — prefab
overrides, the sprite animation preview, the audio preview, the asset inspector and the material
editor — are all ✅, and `STUDIO-07047`'s own accounting of the prototype's *tests* (112 end-to-end
cases in `tests/ApplicationTests.cpp` and `tests/UiTests.cpp`, almost all of them shared code the
panels happened to reach) found and closed nine further gaps (`STUDIO-07050`–`07058`) that reading
the inventory of surfaces alone had no way to see. With every dependency closed, `STUDIO-07030`
deleted the panels, `ImGuiStudioUi`, `StudioApplication` and `CnaStudioHost`, and the tests and guard
entries that existed only to track them against a prototype that is no longer there. What that does
not close by itself is the classic UI render backend `STUDIO-04027` is after: `CnaStudioHost` was one
consumer of it, but `CnaStudioShellHost` — the native host — turned out to have a `CnaUiRenderer` of
its own, a compatibility fallback that `STUDIO-07030` never touched.

**Phase 11 — Viewport** (6 of 15). Perspective and orthographic cameras, orbit/fly/pan navigation,
picking through the 3D projection, and the 2D workflow preserved beside it rather than replaced.
**The navigation preference is read now** (`STUDIO-11015`): Studio's own scheme, Maya's and
Blender's are three different answers to "what does the left button dragging mean", resolved once
at the press by `studioViewportGestureFor` — a pure function over scheme and chord, which is why it
can be tested without a window — and held for the whole drag, so a modifier released mid-gesture
does not turn an orbit into a pan. WASD flying stays under Studio's scheme only, because in Maya's
and Blender's it is typing.

**Phase 16 — Play in editor** (4 of 18). Play and Stop from the native shell with mutually exclusive
enablement; a player that cannot be launched refused at the launch rather than surfacing later as a
process that started and vanished; and the player's ending read from its process status and reported
exactly once. **Pause, Step and Restart**: the protocol has always been there and the editor had
never sent it, so a checkable Pause, a Step live only while paused, and a Restart that is a stop and
a start — which is how a user sees the edits made since pressing Play.

**Phase 17 — Build profiles** (5 of 12). The target profile model, OS/platform/architecture and
renderer selection, build configuration, and the migration of the game's configure command onto the
variables current CNA actually defines.

**Phase 29 — Renderer matrix** (5 of 7). The renderer and platform catalogue, capability-driven
host eligibility, target renderer validation, and the guard that keeps Studio's transcription of
CNA's configure rules from drifting.

**Phase 30 — Large-project performance** (14 of 17).

**Phase 30 — Large-project performance** (14 of 17). Neither was planned for this session and both
came out of `STUDIO-04028`'s benchmark. `STUDIO-30013` made the World Outliner linear in scene size
rather than quadratic — 309 ms a frame at 2 000 entities became 19 ms — through
`SceneDocument::getChildrenByParent()`, which is a *grouping* rather than a cached index because
`findEntity` hands out a mutable pointer and `setParentId` is public, so a cache would go stale
silently and a stale hierarchy index shows up as entities vanishing from the outliner.
`STUDIO-30014` attributed the Content Browser's 21.5 ms a frame at 1 500 assets to about 3 000
synchronous `exists()` calls, measured by stubbing them out rather than by reading the code.

**Phase 31 — Reliability** (1 of 13).

**Phase 35 — Production polish** (14 of 38). **CNA Studio Visual Quality 1.0**, brought forward from
"near the end" to now, for a reason worth repeating: every panel written after this point inherits
whatever visual language exists when it is written, and restyling six panels is a session while
restyling twenty-six is a phase. Tab strips that read as tab strips — a recessed strip, inactive
tabs drawn as real surfaces with a seam, the active one raised and set in the heading face. Panel
chrome with outlines and seams that separate one panel from the next. Scene-content and asset-kind
icons, so an outliner row says *camera* rather than *entity with three components*. List rows with
alternating fill keyed on the model index rather than the drawn index (so scrolling does not make
the stripes crawl), hover, indent guides and disclosure drawn after the fills. Vector fields with
always-visible axis letters in the gizmo's own colours. A Content Browser **card grid** with a
breadcrumb, a list/grid switch and culling. A **viewport toolbar** over the scene. A **visibility
toggle on every outliner row**, through the command history like every other edit — and
`STUDIO-35063`, the guard that keeps it visible, because the first two things put on a tree row
this session were both painted over by the row's own background. A status bar
that is legible at 1920×1080. And the acceptance criterion itself replaced: measuring against the
Dear ImGui prototype had stopped saying anything, so `docs/VISUAL-ACCEPTANCE.md` now asks whether
Studio reads as a serious professional 3D game-development environment on first launch.

**Phase 33 — Docs and CI** (11 of 21). Golden-image infrastructure, visual tests at every tested
resolution and DPI scale, the assertion-macro hardening a sanitizer forced, the plan-arithmetic
guards, and CI coverage for the sanitizer and CNA-backed configurations with the captures kept as
artefacts — which are now **eighty-nine times smaller**: adaptive scanline filtering and a real
LZ77 deflate took a 1920x1080 shell capture from 8.3 MB to 93 KB, and the CNA-backed job's whole
artifact set from about 85 MB to 2.2 MB, verified by an inflate written beside the tests and by an
independent zlib.

---

## Design decisions worth knowing

**The new UI emits `UiDrawData`, and that seam turned out to be the whole reason any of this was
possible.** It was chosen so the native UI could inherit `CnaUiRenderer` rather than need a renderer
written for it, which made the ImGui migration a strangler rather than a rewrite. It then paid a
second time, for a purpose it was not designed for: writing a *modern* renderer meant implementing
one interface against draw data that already existed, with a working implementation beside it to
compare against byte for byte. A renderer swap is normally the most dangerous change a UI can make,
and this one was an A/B test.

**Which is why the classic backend is still here.** The staged migration was: define the backend
interface, move the existing renderer behind it unchanged, write the new one beside it, prove they
agree, default to the new one — and *only then* consider deleting the old one, which has not
happened and is `STUDIO-04027`. Deleting the working renderer first would have meant debugging a
blank screen with nothing to compare it against, and the modern renderer drew a blank screen three
times.

**Four different things were being called "the Studio renderer".** The UI framework architecture
(immediate-mode, `StudioFrame`), the UI's GPU renderer (`CnaUiRenderer` or `StudioModernUiRenderer`),
the CNA renderer Studio's own window runs on (`SOFTWARE`, `OPENGL4`, …), and the renderer a built
game ships with — which is a project setting and has nothing to do with the other three. Conflating
the last two is how a bug report becomes unanswerable. `docs/UI-RENDER-PATH.md` ("The four layers")
and `docs/ARCHITECTURE.md` §5.1 name all four and say which question each one answers.

**`cna-studio-ui-core` links no CNA.** Layout, identity, styling, focus and hit-testing are decided
by code that runs in CI with no GPU. A guard test enforces it. Only the pixels need a device.

**Design tokens are an enum plus a table, not struct members.** A struct cannot be enumerated; this
can, so a test walks every role and fails naming the one a theme forgot.

**Screenshot tests need no GPU.** `UiSoftwareRasterizer` rasterises the same geometry the CNA
renderer will draw, in-process and deterministically. The shell has golden-image coverage now
rather than after graphical CI exists.

**Serialized contracts are pinned, product identity is not.** `"editorState"` (scene) and
`"editorApiVersion"` (plugin manifest) keep their spelling on disk because existing files and built
plugins depend on them. Their C++ carriers were renamed. `.cnaproject`/`.cnascene`/`.cnaasset`/
`.cnaprefab` are CNA ecosystem formats and are untouched.

**The ported panels live above both the widgets and the document.** `cna-studio-ui-core` knows
nothing about scenes and `cna-studio-scene` knows nothing about widgets, which is what lets each be
tested without the other. A panel is the seam where the two are put together, so it gets its own
module — `cna-studio-shell-panels`. The Output Log is the exception that proves it: its model lives
in `cna-studio-ui`, which ui-core already depends on, so it could stay put.

**One log model, read by both consoles.** Two logs would make the migration impossible to check:
every difference between the legacy panel and the ported one would be a difference in what was
logged rather than in how it was drawn, and nobody could tell a faithful port from a
plausible-looking one.

**A widget is a view over rows a caller flattens, not a walker over somebody's data structure.** The
outliner shows a scene graph and the content browser will show a directory; a tree widget that knew
about either would have to learn about both. It takes a flat list with a depth per row, which is
what a tree looks like once it has been drawn.

**An edit commits on Enter or on losing focus, never per keystroke.** A property bound to a field
that wrote per character would put one undo entry per letter and would parse a number while it is
half-typed. And losing focus *commits* rather than abandoning: throwing away somebody's typing
because they clicked elsewhere is the behaviour every form gets wrong and nobody forgives.

**Enablement is a predicate, not a flag.** Asked at the moment the answer is needed, so Undo greys
out the instant the history empties. A stale enablement is worse than none: a control that looks
available and refuses is indistinguishable from one that is broken.

**The exported runtime is embedded from the sources Studio itself compiles.** A shipped Studio has
no source tree beside it, so export has to work from the binary alone — and embedding from the same
files is what stops the exported scene reader drifting from the writer that produced the scene.

**A legacy renderer name migrates and says so.** Silently substituting a renderer would change what
a user's game ships on; silently failing would make an old project look corrupt.

**A menu-bar menu and a context menu are one popup chain with two roots.** A context menu is the
same chain anchored at a point instead of under a title. Submenus, the hover delay, the arrow keys,
Escape backing out one level, the blocking layer and "press elsewhere to cancel" are the same code.
Two implementations would have drifted, and the drift would have shown as a context menu whose
submenus behaved subtly differently from the File menu's.

**A popup's open state is a path, not a pointer into the tree.** Menus are rebuilt from their
definitions every frame and genuinely do change while open — the Window menu's panel list is
refilled whenever a panel registers — so anything holding a node would dangle the first time that
happened.

**A deferred popup cannot answer the widget that queued it.** It runs after that call returned, so
a reference into the caller's stack frame would dangle. The answer goes through retained state and
is collected on the next pass that routes input: one frame of latency, and the alternative was a
use-after-scope that would have worked in every test.

**Binding the panels belongs outside the CNA-linked module.** There is exactly one place a running
Studio is created — behind a CNA checkout — so panels bound there can only be *seen* with CNA, and
the headless preview, which is the only visual test this project has without a GPU, photographed
empty rectangles. Nothing about binding a panel needs CNA.

---

**A benchmark that runs only in the expensive configuration is a benchmark nobody runs.**
`STUDIO-04028` computes everything from `UiDrawData` — the same bytes both backends are handed — so
`--ui-benchmark` needs no CNA, no GPU and no window and runs in the dependency-free build on every
push. What that costs is GPU time, which it does not measure and says so. What it buys is that the
number exists at all.

**A cost model is a second implementation, so it is checked against the thing it models.**
`studioUiFrameCost` reimplements both backends' inner loops. `CnaStudioShellHost` therefore
recomputes it against the backend's own counters on every frame of every run that carries a frame
limit — every capture, every CTest smoke test, both CNA legs of CI — and fails the process naming the
field, both numbers and the frame. Never in an interactive session: an editor should not spend a
user's frame checking its own benchmark.

**A "1500-asset content browser" scenario that measures the default layout is a number about the
layout.** Each benchmark scenario names the panel it is about and the run *refuses* if that panel
cannot be raised. Without that, `content-grid` and the baseline reported identical figures to the
vertex — which was correct, because the Content Browser is already the active bottom tab, and would
have gone on being reported as a measurement of the card grid.

**The property editor is one function now, and that was a prerequisite rather than tidying.**
`STUDIO-07045` needed the same editors — checkbox, drop-down, text, vectors, a quaternion edited as
Euler angles, a colour swatch, a drag-target asset picker — over an *importer's* settings rather than
a component's. `studioPropertyEditor` is used by both. `STUDIO-35033` wants the same seam.

## Things found that were not expected

**A default nobody had switched was hiding a flag only the prototype answered.** `--host-capabilities`
prints the host contract and exits, and that lived on the Dear ImGui host alone. Switching the
default made the query open a window and run until CTest killed it — and left as it was, the flag
would have broken the day `STUDIO-07030` deleted the path answering it, where the failure would have
looked like the deletion rather than like the switch.

**Three viewport preferences were stored, loaded, given rows in the Preferences panel, and read by
nothing.** Camera speed, inverted zoom and the navigation style, in both viewports. Two were a
multiplier and a sign and now reach the camera; the three navigation schemes are `STUDIO-11015`,
because calling them done would have closed a task over a control that still did nothing.

**A command that exists is not a command that does anything.** Half the action registry is declared
with no handler and given one by whatever binds it, so `shell.invoke(id)` on an unbound command
finds the id, "runs" the absent handler, and changes nothing. `--shell-invoke` read the return of
`find()` and threw away the return of `invoke()`, and the windowed host ran it in its constructor —
before `LoadContent` binds the viewport's commands, which needs a graphics device. A whole session
went looking for a missing tool overlay that had simply never been armed. The shell had recorded the
refusal in `refusedActions()` the entire time; nothing read it.

**A screenshot test that asserts on counts passes for a blank window.** The graphical cases check
draw calls and triangles, which separates a shell that submitted geometry from one that submitted
none — and says nothing about a frame whose geometry rendered to nothing. A wrong blend state, a
clip rectangle that excludes the window, or a renderer quietly dropping the calls all report every
draw call and write a perfectly valid PNG of an empty frame. `--screenshot-min-colors` closed it.

**The software rasterizer's texture table kept a pointer `UiTextureRequest` forbids it to keep.**
It worked anyway, because the only texture anybody uploads is a font atlas that outlives the frame.
It stopped working the moment the atlas began sending partial updates: an Update's pointer is the
top-left of a *region*, and read as a whole texture it draws every glyph from somewhere else in the
atlas — which looks like a corrupt font rather than a wrong pointer.

**A caret that moves by code point moves into the middle of a character.** And the mouse had the
same bug in a worse form: a combining mark adds no width, so the code-point boundary inside a letter
and its accent sits at the same x as the one before it. Clicking there put the caret inside one
rendered glyph *invisibly*, and the damage only appeared at the next keystroke, as "café" becoming
"caf" with the accent stranded on the f.

**Two documents describing the same gap is two places for it to be closed in one of them.** The
visual review went on naming the tilemap tool as missing for a commit after it was answered, because
nothing compared the two lists — only three phrases inside them. They are compared now, in both
directions.

**CNA has changed more than the prototype's analysis admits.** Renderer and platform are now
separate axes; there are 50 renderer identities, not 14; and `RendererCapabilityProfile` provides a
real runtime capability model with 32 atomic features, 22 limits and per-format usage masks.

**`EASYGL` is not a renderer any more.** It was the prototype's *default*. It became a renderer
*family* serving five GL profiles, so every `.cnaproject` the prototype wrote names a renderer that
cannot be built. Handled by the alias table; the example project was updated to `OPENGLES3`.

**Studio was configuring every user's game with a variable CNA does not define.** The build runner
passed `-DCNA_GRAPHICS_BACKEND`, the name from before CNA split renderer from platform. CNA ignores
it, so a game built through Studio silently took CNA's *default* renderer rather than the one the
user chose — and the build succeeded, which is precisely why nobody noticed. Fixed with
`STUDIO-17003`; the configure command now passes `CNA_GRAPHICS_RENDERER` and `CNA_PLATFORM`, and a
test asserts the old name appears nowhere in it.

**Two CNA gaps have closed upstream.** `Color` is now default-constructible (G-01), and `SpriteFont`
has a public CNAEXT constructor (G-04, narrowed to the missing rasterizer).

**Two latent defects in inherited code**, both found only by building at `-O3 -Werror`, a
configuration the prototype's CI did not run: an ignored `freopen` result that would have sent a
build's output nowhere while leaving an apparently empty log, and a dangling reference to a member
of a by-value `std::optional` temporary in a recovery test.

**An ODR violation of my own making, and what it cost.** Declaring a second
`CNA::Studio::StudioCommand` for the action registry — the undoable document mutation already had
that name — compiled cleanly, linked cleanly, and corrupted memory at run time. It surfaced as a
`std::string` destructor freeing a pointer into the data segment, in a test hundreds of cases away
from either definition, and it moved when unrelated code changed the allocation pattern. The
registry type is now `StudioAction`, which is the better name anyway, and `STUDIO-02039` is a guard
test that refuses a duplicate type name in one namespace — it found the collision immediately when
pointed at it, and correctly does not flag `CNA::Studio::SceneLoadResult` against
`CNA::Studio::Runtime::SceneLoadResult`.

**A test fixture had quietly become valid.** The "unknown renderer" test used `"glide"`; CNA has a
Glide renderer now, so the test asserted nothing.

**The CNA-backed build was broken against current CNA**, in three separate ways, none of which the
dependency-free CI could see:

- `CNA/GraphicsBackendType.hpp` no longer exists; it is `CNA/GraphicsRendererType.hpp`, and
  `EasyGL` as an enumerator became the five GL profiles it now covers.
- Studio's CMake read `CNA_GRAPHICS_BACKEND`, which current CNA does not define. The player
  therefore built as `cna-player-` with an **empty** suffix, and discovery — which matches on
  `cna-player-<renderer>` — silently found nothing. Play mode reported no installed builds on a
  tree that had just built one. The configure now fails loudly instead.
- Studio and the player both took the default `Reach` graphics profile, under which
  `GetBackBufferData` throws. Both now request `HiDef`.

**Studio's screenshot flag lied.** `CnaStudioHost` set `screenshotWritten = true` inside the
exception handler, so a *failed* capture reported success and the process exited zero having
written no file. That silently defeats the assertion the graphical smoke tests exist to make — the
file appearing **is** the test. Split into `screenshotAttempted` (stop retrying) and
`screenshotWritten` (it really happened). The player had this right; the Studio host did not.

---

**CNA cannot be consumed as a subdirectory out of the box.** Its tests and examples default ON with
no top-level-project guard, and neither can succeed from a subdirectory: the tests want an
initialised googletest submodule, and the examples resolve a helper script through
`CMAKE_SOURCE_DIR`, which from a subdirectory is the *consuming* project's root. Draco defaults ON
too and wants its own submodule. Every consumer must therefore know to pass three options nothing in
CNA mentions — which is CNA gap G-09, and is exactly what a game exported by Studio is. Found by
building an exported project rather than reading it.

**`CNA_ENABLE_VIDEO` is tri-state, and Studio's feature model was a boolean.** `OFF`, `AUTO` or
`ON`, where `ON` *requires* FFmpeg and fails the configure without it. Passing `ON` for a project
that merely wanted video made every exported game demand FFmpeg. Features now carry the spelling of
their own "on"; the real tri-state is `STUDIO-17012`.

**Rounding a content-derived width down produces a box its own text does not fit in.** Menu titles
read "F..." for File while "Project" was fine, depending on nothing but where each measured width
fell relative to half a pixel. Content-derived widths are ceiled now, not rounded.

**The router grants focus the frame *after* a press.** It reports the focused widget as of the start
of the frame and reassigns during `interact()`. A text field that waited for focus to start an edit
session therefore began on a frame where nothing said the pointer was involved — and select-all-on-
focus then wiped a name field somebody had merely clicked into. The press starts the session.

**Following new output has to measure against last frame's extent.** Against the grown content the
view is never already at the end — that is why it grew — so a console that compared with the new
extent would never once auto-scroll.

**The plan's own arithmetic had drifted.** Phase 5 had nine complete tasks and said eight, in both
the phase file and `plan.md`. Now checked by the test suite (`STUDIO-33018`), which found it on the
commit that introduced the check.

**A screenshot test had been photographing nothing.** `--shell-pointer=40,40` is the *gap* between
two toolbar buttons at scale 1, so `CnaStudioShellPreviewPressedToolbar` captured a toolbar at rest
and passed every run. Found while writing the tooltip capture, which failed at the same point. Both
captures use (20, 40) now, pinned by a unit test so moving the toolbar fails loudly rather than
quietly emptying them.

**The hover clock has to advance at end of frame, and that is not a detail.** Hover is decided
during the input pass, so asking which widget is hovered at the *start* of a frame answers with the
previous frame's. The first tooltip implementation did exactly that, and the off-by-one frame ate
the delay: on the frame the pointer crossed between toolbar buttons the change was invisible, the
clock never reset, and the new button's tooltip appeared instantly. The delay worked only for the
first control the pointer ever touched — and no screenshot would have shown it.

**Retained state holds zero the first time a widget is seen.** The drop-down's "nothing pending"
sentinel was `-1`, so every drop-down selected its first item the moment it was described. A
sentinel that collides with the default is not a sentinel.

**A profile carried CNA's spelling of a renderer, and a documented lower-case field was sometimes
upper case.** Every project predating target profiles puts CNA's identity in
`defaultGraphicsBackend`, and that string became the migrated profile's renderer verbatim. Every
renderer comparison in Studio was therefore quietly case-insensitive — a rule that holds until the
one place that forgets it, which turned out to be the new Build panel, whose renderer control was
blank. Validation normalises the spelling in place now, silently, because nothing about the target
changed.

**A missing player binary did not fail the launch.** `fork` succeeds and `execv` fails in the
*child*, which has nothing left to return the failure to, so a player that was never built looked
exactly like one that started and exited at once: Play appeared to work, a Stop button went up, and
the editor waited for a connection that would never arrive. The child reports `errno` over a
close-on-exec pipe now, which stays empty on success precisely because the descriptor closes itself
on exec.

**Collecting a child is one-shot, and the toolbar was collecting it.** Whichever call waits on a
process first gets the status and every later one gets nothing — and `isRunning()` waited. The
toolbar calls it every frame to decide whether Stop is available, so the toolbar consumed the exit
and the poll meant to report it found nothing to report. The editor was at its most likely to lose
the message exactly when it was doing its job.

**Floating windows and deferred popups shared an input layer for one commit.** The router's layers
are a *modal* stack rather than a z-order — `layerAcceptsInput()` is an equality test — so a
drop-down opened inside a floating window could be clicked *through* to the panel holding it. The
whole ordering is written down in one place now, at `StudioFrame::kPopupLayer`, and the case has a
test that fails when the numbering is put back.

**Tab walked out of a modal into the panels behind it.** `registerFocusable` registered a widget
whether or not its layer was taking input, so the focus ring left the dialog and the next Enter
pressed something the user could not see. Fixed in the router rather than in the dialog, which fixed
it for menus and popups too.

**A text field reports `committed` only when the value *changed*.** That is right for a property
grid and wrong for a name prompt, where Enter on a name the user did not edit still means "that
one" — so the dialog takes Enter itself, but only when no button claimed it: a focused button
activates on Enter, and overwriting that with the default would make Enter on a focused Cancel mean
Discard.

**A submenu filled only when it had something to list draws greyed out.** The Layouts submenu was
filled by `setSavedLayouts`, so a fresh Studio could not reach Save Layout As — the command that
creates the first layout. `setMenus` had the same shape of bug from the other side: replacing the
menus emptied both filled-in submenus, so a host that customised its File menu silently lost its
panel list.

**A layout test listed every panel by hand.** Adding the History panel broke it, which is the good
outcome: a hand-written "everything else" stops meaning that the moment a panel is added, and the
test would otherwise have gone on passing while proving less than it said. It reads the shell's own
panel list now.

**A phase called "CNAEXT UI renderer" contained no CNAEXT, and nothing caught it for six phases.**
`ARCHITECTURE.md` said Studio "requires the modern CNAEXT Graphics API" and the renderer's own
header said it drew through it. Both were false: every GPU call was classic XNA — `BasicEffect`,
`DrawUserIndexedPrimitives`. The mis-statement survived because it was *inherited*: the header's
table of what the UI needs is a table of what **Dear ImGui** needs, written when that was the
question, and never re-asked when the renderer was written. Nothing in the build, the tests or the
documentation could contradict it, because the claim was a sentence rather than a check. It is a
check now — `modernApiAvailable` decides whether the host can run Studio at all — which is the
general lesson: an architectural requirement that no test can fail is a preference with a strong
tone of voice.

**A renderer that draws the right geometry can still draw nothing, three different ways.** The
modern renderer produced a blank frame because the projection uniform was set before `Apply()` bound
the program; then because the matrix went up row-major (`&projection.M11`) when CNA hands matrices
to the graphics API untransposed, so `Matrix::ToColumnMajor` is not optional; then because
`DynamicVertexBuffer::SetData`'s options overload takes no byte offset and the data landed at zero.
Every one of the three passed the count assertions — geometry was submitted, draws were issued,
exit code zero. What caught all three was `--screenshot-min-colors`, and only because the visual
matrix asks for 256 rather than 16.

**Every diagonal line in Studio was invisible on a real device.** `StudioDrawList::drawLine`'s
diagonal branch emitted its quad with `kUiTextureNone` and zeroed UVs, so both render backends
dropped it — while the software rasterizer, which has its own idea of an unbound texture, drew it
correctly. So the golden images were right and the screen was wrong, which is the worst way round.
Diagonals are every icon stroke that is not axis-aligned: the close cross, the chevrons, half the
transform icons. Fixed to use the atlas white texel like every other primitive, and
`STUDIO-35036` now asserts that *every* primitive the draw list emits names a texture a backend can
resolve — because the defect was not one line of arithmetic, it was a class of them.

**The migration inventory was complete and still missed a working feature.** It had been taken over
panels, menus, toolbars and shortcuts, all of which were accounted for — and `Add Component` is
none of those. It is a button inside a panel, and so were four other things: prefab override
handling, the sprite animation preview, the audio preview and the asset inspector. Deleting the
prototype on the strength of "the inventory names nothing missing" would have deleted the only way
to add a component to an entity. An inventory answers the question it was built around; the useful
move was to ask a different question (`STUDIO-07041`), not to re-check the same one harder.

**`OPENGL4` runs headless, and a previous session concluded it could not.** Xvfb with Mesa's
llvmpipe gives a real GL 4.x context with no GPU and no display hardware, which is what made the
modern renderer testable, the A/B comparison possible and G-10 a 🟢 instead of a 🔴. The earlier
conclusion was reasonable from what was tried and wrong about what was possible — worth recording
because a documented impossibility is the kind of thing nobody re-tests.

**A tree row is one widget covering the whole line, and that is a paint-order trap.** Anything that
has to win the click against the row must be *described* before the row's background — and is
therefore *drawn* before it, and painted over by it. It happened twice in one session. The
disclosure triangle presented as a data problem: expandable rows lost their triangle on alternate
lines only, which is where the alternating fill is. The visibility toggle presented as nothing at
all — the click toggled, the tooltip appeared, both unit tests passed, and the eye was never on
screen, which is worse than a missing feature because nothing reports it. It was found by reading a
1920×1080 capture of a hovered row while writing this handoff, not by a test. There is a test now
(`STUDIO-35063`), it asserts on the emitted geometry rather than on a capture, and it was verified
by putting the defect back: an ordering assertion that has never been shown to fail is an assertion
about nothing. **Input order and paint order are different orders**, and the code now says so at
both ends.

**Three preferences and a placeholder.** `STUDIO-11015` was ⬜ over a preference that was stored,
loaded, given a row in the Preferences panel and read by nothing — the second time this exact shape
has appeared in this file. And the Details panel's axis letters were styled by a function comparing
upper-case ids against a document that stores them lower-case, so every vector field drew "x y z" in
the secondary colour and the gizmo colours were dead code. Both look like polish and both are the
same failure: a value that flows from a writer to a reader through a step nobody exercised.

---

### Found this session, and none of it was being looked for

**The classic UI render backend had been reporting zero upload bytes since it existed.**
`UiRenderStats::geometryBytesUploaded` is a field both backends fill in — except `CnaUiRenderer`
never touched it. So every comparison of the two on upload bytes was a number against a zero, which
reads as "the classic path uploads nothing" rather than as "nobody counted". Fixing that is what made
the 17× measurable at all, and it is a good example of a metric that is worse than no metric: a zero
is an answer, and it was the flattering one for the path being argued for.

**A CNA UI vertex is 56 bytes and carries 20 bytes of data.** `CNA::Color` alone is 24 — it has a
vtable — and `VertexPositionColorTexture` carries a second one because `IVertexType` has a virtual
destructor. CNA repacks it to a 24-byte stream before upload, so the bus is not charged, but every
caller building a vertex array is. Found by a `static_assert` refusing the estimate of 32 that had
been written into the benchmark, which is the argument for pinning a literal to the type it
describes rather than commenting it. `docs/CNA-GAPS.md` G-11.

**The World Outliner was O(n²) in scene size, and no capture could have shown it.**
`SceneDocument::getChildren` scans every entity; the outliner's flatten called it once per row. 250
entities cost 9.1 ms a frame, 2 000 cost 309 ms — three frames a second on a scene nobody would call
large. Rows are *virtualised*, so the benchmark reported the same 20 draw calls and 7 317 vertices
at five entities and at two thousand: the drawing was flat and the walk was not. Fixed
(`STUDIO-30013`), 16× faster at 2 000, and linear. `SceneValidation` already knew — it derives its
parent set once, with a comment saying `getChildren` is a scan — and that knowledge had stayed local
while the outliner rediscovered the problem the hard way.

**The Content Browser asks the operating system about every asset, every frame.** `isMissing` is a
`std::filesystem::exists()` called once per row, and the missing *count* is a second full pass with
another `exists()` per asset: about 3 000 synchronous stat calls a frame at 1 500 assets. Attributed
by measurement rather than by reading — with both calls stubbed, the same scenario falls from 21.5 ms
to 8.3 ms. And it is *worse* in real use than in the benchmark: with no project open the same assets
cost 8.6 ms, because the paths resolve under a root that does not exist and the stat fails early.
`STUDIO-30014` measured it; `STUDIO-30015` fixes it and waits on `STUDIO-30012`, because when a file
deleted outside the editor becomes visible is a design decision rather than an optimisation.

**The native Content Browser and the native Details panel had different ideas of which asset was
selected.** The browser wrote into a member of `StudioShellPanels`; `StudioContext::selectedAsset_`
existed and was written only by the *prototype*. So the native Details panel's answer was
permanently "nothing", and the asset inspector could not have worked even once it was written. The
same shape as the three preferences that were stored, loaded, given rows in a panel and read by
nothing — except here the writer and the reader were never connected at all.

**`CnaUiRenderer::getBackendName()` answered a question the class has no authority over.** It
returned the *CNA renderer* this binary was compiled against — layer 3 of the four in
`docs/UI-RENDER-PATH.md` — from a static on the *classic UI render backend*, which is layer 2. Five
callers used it, including `src/player`, which draws no editor UI at all and was linking the editor's
UI renderer for one string. That is not tidiness: `STUDIO-04027` deletes a UI backend, and a game
runtime that depends on which one Studio picked has to be rebuilt when the editor changes its mind.

**The `OPENGL4` CI leg's test set is a strict superset of the `SOFTWARE` leg's.** 79 suites against
78, the extra being `CnaStudioUiRenderBackendsAgree`. Measured with `comm` over `ctest -N` rather than
assumed, because it is the evidence `STUDIO-04027` turns on — and it says that deleting the classic
path costs no automated coverage whatsoever, which is the opposite of what the task's recorded
blocker said.

**A `-Werror` build caught `UiRect control = control;`.** A self-initialisation, produced by the
mechanical rename that extracted `studioPropertyEditor`. It compiled; 1 311 Debug cases passed; a
clean ASan run passed. The compiler had reused the parameter's storage, so the value happened to be
right. That is the third defect Release `-Werror` has caught that no other configuration could,
after an ignored `freopen` result and a dangling reference to a subobject of a temporary.

**A guard test that has never been shown to fail is an assertion about nothing, and I wrote one.**
The asset inspector's paint-order case was first written over emitted quads, classified by texture
coordinate and alpha. It passed — and went on passing when the defect it was written for was
deliberately reintroduced, twice, in two different forms. It rasterises now and counts distinct
colours in the band the identity rows occupy: text is dozens, a flat fill is one. Verified by putting
the surface back *after* the rows it holds, where it reports "1 distinct colours" and fails. The
lesson `STUDIO-35063` recorded was about the *defect*; this one is about the *test*, and it is the
reason `STUDIO-03041` is filed as a structural task rather than as a third guard.

## Known gaps and failures

Nothing is failing. What is **not** done, and should not be mistaken for done:

- **Studio is C++-only, and the seam does not change that.** `STUDIO-02080`–`02086` make adding a
  CNA binding an *addition* rather than a rewrite. No second adapter exists, none is planned in this
  tranche, and the Project Hub's language chooser holds one entry. Anybody reading the architecture
  and expecting a second language to be near should read §13.7, which says what this deliberately
  does not do.
- **`StudioPreferences::cmakePath` is the seam's one known non-generic remainder** (`STUDIO-02087`,
  ⛔ deferred). It is a persisted preferences key with one language behind it, and generalising it
  to a per-language map today would buy nothing and cost a format migration.
- **The Project Hub is functional and is not designed.** Rows of full-width buttons, a text field
  for the project location, and no template pictures. `STUDIO-08001`'s acceptance is the window and
  its layout; a template gallery belongs in Phase 35 and does not have a task yet.
- **Open Project has no file dialog.** It has the recent list and a path field, which is enough to
  open any project and is what a terminal user would type anyway. A real file dialog waits on a
  modal *window*, which Studio does not have (`STUDIO-03022` covers the layering, not the window).
- **The CNA-backed suite is 67 CTest cases plus 4 slow template builds, and the `OPENGL4` leg is
  79.** The template cases do not run on the `OPENGL4` leg's list above because that run excluded
  them deliberately for time; CI runs everything on both legs, which is where the difference will
  first be seen. Watch the first `OPENGL4` job that includes them.
- **The recent-projects list is not shared with anything.** It is Studio's own file in the user's
  configuration directory, read on every frame the Hub is drawn. That is deliberate — availability
  is a fact about the filesystem, which changes while Studio is not running — and it means a
  thousand-entry list would cost a stat per row per frame. It is bounded at twenty.

- **Dear ImGui is gone, all the way down.** `STUDIO-07030` deleted every panel, `ImGuiStudioUi`,
  `StudioApplication` and `CnaStudioHost` — `cna-studio` with no flag opens the native shell, and
  there is no longer a `--ui=imgui` window to open instead; that name is kept only as one
  `--headless` still accepts, landing on the same headless rendering `--headless` itself uses.
  `STUDIO-07031` then removed the `CNA_STUDIO_WITH_IMGUI` option and the vendored
  `third_party/imgui/` source, and `STUDIO-07099` widened the dependency guard from `ui-core` to
  the whole tree plus the build files, so the option or the vendor target coming back would fail
  the build again on its own.
- **Studio still ships two UI render backends, and deleting the prototype did not settle it.** The
  modern CNAEXT backend is the default on any host that reports the modern API; the classic one is
  what `SOFTWARE` falls back to, loudly. The old justification — that CI could not run a
  shader-capable renderer — is closed by `STUDIO-04029` and *measured* closed: deleting the classic
  path costs no CI coverage. The prototype's host, which the previous entry here named as the
  blocker, is deleted; the *native* host's own `CnaUiRenderer` construction — a compatibility
  fallback and `--ui-renderer=compat` — was not, and is `STUDIO-04027`'s to decide now.
- **`STUDIO-04027` is open over an obstacle, not a decision.** Its acceptance allows "or the reason
  it stays is written down", and that has deliberately **not** been used to close it. The reason it
  stays is that it cannot yet go, which is not the same thing.
- **The benchmark measures submission, not GPU time.** `--ui-benchmark` reports what each backend
  asks the device to do — bytes, draw calls, texture binds, scissor changes — plus the CPU cost of
  describing the frame. It does not time a driver, deliberately: a vendor's answer to the same
  submission varies, and the question is a property of Studio. Whether the modern backend is faster
  *in wall-clock on a real GPU* remains unmeasured, and there is no task for it because llvmpipe
  cannot answer it either.
- **The Content Browser does synchronous disk I/O in the render loop.** About 3 000 `exists()` calls
  a frame at 1 500 assets, 61% of the panel's frame cost, and worse on a project that actually
  exists on disk. `STUDIO-30015`, after `STUDIO-30012`.
- **Paint order and description order are still the same order.** Three widgets have now been drawn
  under their own backgrounds, and the defence is two guards that each name one widget.
  `STUDIO-03041` is the structural task; it is filed and not started, on purpose.
- **A floating window is inside the Studio window, not an OS window.** `STUDIO-05015`, research
  rather than work.
- **New Project and Open Project are still unbound.** Both need a native file dialog, which CNA has;
  this is Studio wiring and an async seam, not a missing CNA API. `--project` opens one today.
- **The caret does not blink.** Deliberate until there is an animation model (`STUDIO-03030`).
- **Non-Latin text is boxes.** One font, no CJK, Hangul, Arabic or emoji. `STUDIO-04019` is a font
  *fallback* problem; the text model steps by grapheme cluster and is not the reason.
- **Visual Quality 1.0 is thirteen of thirty-seven.** What is still plain, verified by looking at a
  1920×1080 capture this session rather than by reading the list: the Content Browser is one folder
  card in a large empty area with no thumbnails (`STUDIO-35041`) and cannot be searched
  (`STUDIO-35042`); the property grid's label column is a fixed 38%, so labels and values are
  separated by a gap wider than either (`STUDIO-35033`); the viewport grid is a lattice rather than a
  ground plane and there is no orientation widget (`STUDIO-35051`/`35052`); nothing marks the
  selected entity in the viewport (`STUDIO-35053`); the outliner has no filter (`STUDIO-35061`);
  typography is one face at one weight (`STUDIO-35022`/`35023`).
- **The visual suite compares bytes, not appearance.** Ten captures across five resolutions and both
  themes, each asserting at least 256 distinct colours. Enough to catch a blank frame, a dropped
  primitive or a theme that did not apply; not enough to catch a panel that is ugly, and a one-pixel
  layout change fails every golden at once. `STUDIO-35082`.
- **No graphical CI on a real GPU.** `OPENGL4` under Xvfb on Mesa's llvmpipe now runs the whole suite
  **and is in CI** (`STUDIO-04029`), which a previous session recorded as impossible. llvmpipe is a
  correct GL implementation and not a driver: anything a vendor driver does differently is
  unobserved. `STUDIO-33010`.
- **`STUDIO-04029` is written but has not been observed running.** The workflow change is a matrix
  leg with an assertion that the leg gets the backend it exists to cover, and every command in it was
  run end to end on this machine — but nobody has watched GitHub Actions execute it. The first run on
  the next push is the thing to look at.

### Building against a real CNA checkout

Reproduced again this session on Ubuntu 24.04, and it is worth the twenty minutes: it caught a
compile error in `CnaStudioHost.cpp` that no dependency-free configuration can see, because that
file does not exist in one. CNA's SDL is a submodule and is not fetched by a plain clone:

```bash
git clone --branch next https://github.com/libcna/cna       /path/to/cna
git clone --branch next https://github.com/libcna/sharp-runtime /path/to/sharp-runtime
git -C /path/to/cna submodule update --init --depth 1 \
    third_party/SDL third_party/SDL_image third_party/SDL_mixer

apt-get install -y libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxtst-dev \
                   libxkbcommon-dev libwayland-dev wayland-protocols libxfixes-dev libxext-dev

cmake -S . -B build-cna -G Ninja \
  -DCNA_STUDIO_WITH_CNA=ON \
  -DCNA_STUDIO_CNA_ROOT=/path/to/cna \
  -DCNA_SHARP_RUNTIME_ROOT=/path/to/sharp-runtime \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE -DCNA_PLATFORM=SDL3 \
  -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF \
  -DCNA_ENABLE_NET=OFF -DCNA_ENABLE_DRACO=OFF -DCNA_CNAEXT=ON
```

FFmpeg is optional: `CNA_ENABLE_VIDEO=AUTO` detects its absence and disables video. `Draco` and
`ENet` are disabled above because Studio needs neither.

---

## Next recommended tasks

Read from the phase files, not remembered. Ids, titles and blockers are copied from the rows.

**Phases 6, 7 and 8 are closed.** The Dear ImGui chain is finished, the two Phase 7 zombies are
resolved, and the Project Hub is complete with its build proof. Nothing in those three is open.

### The most logical next chain: Content Browser 2, which the Hub has just given something to browse

Until this session there was one project to open — the example — so the asset workflow was never
under pressure. A Hub that creates projects makes Phase 9 the tranche where a real one starts to
hurt, and Phase 10 is behind it.

| Id | Task | Blocked by |
|----|------|-----------|
| `STUDIO-09001` … `09016` | Content Browser 2 | Nothing; Phase 8 is done |
| `STUDIO-10002` | Importer registry, which `STUDIO-28003` waits on | Phase 9's browser |
| `STUDIO-30015` | The Content Browser stops asking the filesystem about every asset every frame | `STUDIO-30012`, which wants `STUDIO-09004` |

### The gameplay-language chain, now that the seam exists to hang it on

`STUDIO-15001` is the decision that gates Phase 15, and the language seam has changed what it is a
decision *about*: the reflection mechanism is now the C++ adapter's answer to the
gameplay-component-metadata boundary (`docs/ARCHITECTURE.md` §13.3) rather than a Studio-wide one.
That is a narrower question than it was, and it should be settled before Phase 15 starts.

| Id | Task | Blocked by |
|----|------|-----------|
| `STUDIO-15001` | Which reflection mechanism for project-defined C++ components | An architectural decision. Narrower now: it is one adapter's answer, not Studio's |
| `STUDIO-15010` | Open project in IDE, open source file, open component source | `STUDIO-06009`. The adapter's `sourceDirectory` and `sourceFileExtensions` are what locate them |
| `STUDIO-28005` | Plugins contribute panels | Nothing — its dependency moved from the retired `STUDIO-07001` to `STUDIO-07016`, which is ✅ |

### Finish the shell decomposition

| Id | Task | Blocked by |
|----|------|-----------|
| `STUDIO-02058` | Move panel binding out of `StudioShellPanels` into per-panel binders | `STUDIO-02050` ✅ |

The last of four, and it got bigger this session: the Project Hub's binding is another ~70 lines in
`bind()`. **Do not move the lambdas into methods that still take `StudioShellPanels&`** — that moves
code rather than separating concerns, which is the failure `STUDIO-02054`'s note warns about. Each
binder should name the few things its panel needs, the way the services do.

### Structural, and still open

| Id | Task | Blocked by |
|----|------|-----------|
| `STUDIO-03041` | Make paint order separable from input order | `STUDIO-03009` ✅ |
| `STUDIO-30012` | Caching strategy with explicit invalidation | `STUDIO-09004` |

`STUDIO-03041` is the one worth doing sooner rather than later: three widgets have been drawn under
their own backgrounds, the defence is one guard per instance, and every new widget on a row starts
from the same trap. It wants design, not an edit.

### Visual Quality 1.0, in the order the panels are looked at

Each is independent. The Project Hub adds one to the list.

| Id | Task |
|----|------|
| `STUDIO-35033` | Property grid alignment: label column, value column, nesting, reset markers |
| `STUDIO-35053` | Selection feedback in the viewport: outline, pivot, bounds |
| `STUDIO-35051` | Viewport grid that reads as a ground plane, with origin axes |
| `STUDIO-35041` | Real content thumbnails, cached and generated off the frame |
| `STUDIO-35042` | Content Browser search and type filters |
| `STUDIO-35061` | World Outliner: search, filter and prefab indicators |
| `STUDIO-35082` | Tolerant golden comparison and region-occupancy probes |

The Hub is *functional* and is not yet designed: it is rows of full-width buttons. A template
gallery with a picture per template is the obvious next thing and does not have a task yet — file
one against Phase 35 rather than reopening `STUDIO-08001`, whose acceptance is the layout and not
its polish.

### Not on any chain, and each worth doing on its own

| Id | Task |
|----|------|
| `STUDIO-04019` | Font fallback — CJK, Hangul, Arabic and emoji are boxes |
| `STUDIO-04010` | Render-resource lifetime and recreation on device loss |
| `STUDIO-02035` | Guard test: every document mutation goes through a command |
| `STUDIO-02037` | Guard test: authored files are byte-deterministic across repeated saves |
| `STUDIO-03013` | Accessibility metadata on every widget: role, name, value, state |
| `STUDIO-11003` … `11012` | Focus selection, standard views, adaptive grid, outlines, wireframe mode |

`STUDIO-02037` is closer than it looks now: `CreatingTheSameProjectTwiceProducesTheSameBytes` is
the same assertion over generated project files, and the machinery generalises.

**Blocked, not forgotten.** `STUDIO-33010` (graphical CI on a real GPU) is narrower than it was:
llvmpipe under Xvfb is in CI now, so what is missing is a *driver* rather than any renderer.
`STUDIO-05015` (a second OS window) is research. `STUDIO-02087` (a per-language toolchain-path
preference) is ⛔ deferred and written down, so the second adapter finds it rather than trips over
it.

## What a reviewer should be sceptical about

Written down because a handoff that only lists what went well is one the next person has to
re-derive.

- **`STUDIO-04029` has never been watched running.** Every command in the new CI leg was executed on
  this machine, and the YAML parses, but GitHub Actions has not run it. The matrix expression
  quoting (`--server-args="-screen 0 1920x1080x24"` inside a matrix value inside a `run:`) is the
  part most likely to be wrong.
- **The benchmark's absolute numbers are this machine's.** The 17–18× ratio is a property of the
  code and will hold anywhere; the microsecond figures are not, and the two performance tasks filed
  from them (`STUDIO-30014`, `STUDIO-30015`) quote both.
- **`FlatteningTheOutlinerCostsLinearTimeInTheSceneRatherThanQuadratic` is a timing test**, which is
  the one kind that can fail for reasons unrelated to the change. Its threshold is 8× where linear
  is about 4× and quadratic about 16×, it takes the best of five samples, and it was verified to
  fail at 13.2× with the defect restored — but if it ever flakes, widen the gap rather than deleting
  it: the defect it guards cost 300 ms a frame and was invisible to every other kind of test.
- **`studioPropertyEditor` was extracted mechanically**, and the mechanical rename produced one
  self-initialisation that only Release `-Werror` caught. The golden images are unchanged, which is
  good evidence and not proof: the editors for kinds no example project exercises — `List`,
  `Structure`, `EntityReference` — are covered by unit tests but were not looked at on screen.
- **The service-locator guard's vocabulary is types declared in headers.** A locator built entirely
  out of `std::` types and function pointers would evade it. That is a deliberate trade for not
  firing on a CRC table, and it is the kind of exemption worth re-reading if a locator ever does
  appear.

## Rules this work is held to

Repeated here because they are easy to lose and expensive to rediscover.

- **CNA Studio produces CNA games, not CNA Studio games.** Anything that makes Studio a runtime or
  build dependency of a shipped game is wrong, however convenient.
- Every commit is authored `Robert Vokac <robertvokac@robertvokac.com>`, and commit messages carry
  **no AI or generated-by attribution of any kind**.
- Push only to `libcna/cna-studio`. `libcna/cna-lab`, `libcna/cna` and `libcna/sharp-runtime` are
  read-only source and dependencies.
- A task is done when its acceptance condition holds and its verification passes — never because
  scaffolding exists.
- New behaviour comes with a test, at the cheapest layer that can actually catch the failure.
