# CNA Studio — Handoff

State of the work in progress, for whoever continues it. Updated at the end of each long session.

**Last updated:** 2026-09-15

---

## Where things are

| | |
|---|---|
| Repository | <https://github.com/libcna/cna-studio> |
| Branch | `claude/studio-baseline-audit-51dyxr` |
| HEAD | commit **79** — `docs: bring the handoff up to the state it describes` |
| Working tree | Clean (everything below is committed and pushed) |
| Commits on this branch | 79, all authored `Robert Vokac <robertvokac@robertvokac.com>` |

> **Why HEAD is recorded as a count and a subject rather than a hash.** The previous handoff named
> `8fe23bf` and was two commits stale within the same session, because a file cannot contain the
> hash of the commit that adds it: whatever hash is written is necessarily the *previous* one, and
> the next commit makes it wrong. A commit count and a subject line are both knowable before the
> commit is made, so they are correct the moment it lands. Check with
> `git rev-list --count HEAD` and `git log -1 --format=%s`.

Read in this order to pick the work up:

1. [`plan.md`](plan.md) — the master roadmap and the source of truth for what is done
2. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the current architecture; supersedes `ANALYSIS.md`
3. [`docs/ORIGIN.md`](docs/ORIGIN.md) — where this repository came from and its verified baseline
4. [`docs/CNA-GAPS.md`](docs/CNA-GAPS.md) — CNA deficiencies Studio has found

---

## Verification commands

The default build has no external dependencies — no CNA checkout, no GPU, no window:

```bash
cmake -S . -B build -G Ninja
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

CI runs all four configurations now. Run them locally anyway before a push: the CNA job takes the
best part of an hour, and these are where the latent defects have actually been found — an ignored
`freopen` result, a dangling reference to a subobject of a temporary, and an ODR violation that no
compiler diagnosed:

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

Collect the visual-test captures as CI artefacts:

```bash
CNA_STUDIO_TEST_ARTIFACTS=./artifacts ./build/tests/cna-studio-tests
```

### Last measured result

| Configuration | Result |
|---------------|--------|
| GCC 13.3 Debug, no CNA | **1222 test cases, 45 CTest suites, 0 failures, 0 warnings** |
| GCC 13.3 Release `-Werror`, no CNA | **1222 test cases, 45 CTest suites, 0 failures, 0 warnings** |
| GCC 13.3 Debug + ASan + UBSan, no CNA | **1222 test cases, 45 CTest suites, 0 failures, no sanitizer reports** |
| GCC 13.3 Debug, **against real CNA** (`next`, SOFTWARE renderer, SDL3 platform) | **1224 test cases, 61 CTest suites, 0 failures** |

The two extra *cases* in the CNA-backed run are `STUDIO-29007`, which reads CNA's own
`RendererSelection.cmake`, and `STUDIO-04020`, which checks the host key map — both need a CNA
checkout to exist at all. The sixteen extra CTest *suites* are the window, screenshot, play-mode and
standalone-export runs, which need a real device.

The CNA-backed suite now includes the native shell on a real device in both themes and at 2x, the
shell with a project open, the workspace surviving a real process exit, and
`CnaStudioStandaloneExport` — which exports the example project, configures it with nothing but
CMake and a CNA checkout, compiles it and runs it (about four minutes, most of it CNA).

Every capturing case now passes `--screenshot-min-colors=16`, so a run whose geometry rendered to
nothing fails instead of writing a blank PNG and exiting zero (`STUDIO-04015`). The counts those
cases already asserted separate "submitted no geometry" from "submitted some"; this separates
"submitted some" from "drew something".

**All four configurations run in CI** as of `STUDIO-33022`/`STUDIO-33023`. They are still worth
running locally before a push: the CNA job takes the best part of an hour.

Baseline at import, for comparison: 442 test cases, 12 CTest suites.

---

## What was completed

Task ids are `STUDIO-PPNNN`; see `plan.md` for the full list. **182 of 490 tasks are complete.**
Per-phase counts and the headline are checked by the test suite — `STUDIO-33018` for `plan.md` and
`STUDIO-33019` for this file — so neither can drift from the phase files again. The second was added
after this file had drifted by nineteen tasks and a hundred and fifty-eight test cases, which is
exactly the failure the first was written to prevent in the other file.

**Phase 0 — Audit and baseline** (14 of 15). Imported `cna-lab/cna-editor` at
`3bce82dd74e9a201a21e31308d43d2ee7761d641`, verified its baseline, re-audited current CNA.

**Phase 1 — Product rename** (13 of 16). `cna-studio` executable, `cna-studio-*` targets, the
`CNA::Studio` namespace, `CNA_STUDIO_*` options. 94 files moved with `git mv`.

**Phase 2 — Architecture refresh** (22 of 27). The architecture record, the CNA gap register, the
roadmap, ten architecture guard tests, the restored CNA-backed build, the Studio host capability
contract, the six-axis build target model, and **the standalone export**: `--export=DIR` writes a
project that builds and runs with Studio uninstalled, and `STUDIO-02051` proves it by doing so.

**Phase 3 — Studio UI core** (30 of 33). Design tokens and two themes, widget identity, retained
state, the draw list, input routing with capture and focus, the five-phase frame lifecycle, cursor
requests, **tooltips with a per-widget delay**, **popup layering and input blocking**, widget
helpers, text measurement, High-DPI correctness including the seams, scrolling with row
virtualisation, a tree view, the text selection model **stepping by grapheme cluster, keyboard and mouse alike**, the
clipboard seam, an editable text field,
and **a drop-down over a deferred popup** — the facility that lets a popup escape the panel it was
opened in, **typed drag and drop**, and **a modal dialog** — a window that owns the frame until it
is answered, which is what About, Save Layout As and every confirmation are built on.

**Phase 4 — CNAEXT UI renderer** (15 of 20). Vertex management, batching, nested scissor clipping,
rounded rectangles, clip culling, real text with kerning and correct baselines, **twenty-three
icons drawn as vector paths** with no vendored asset, **a glyph atlas that doubles rather than
losing text** and says so in Diagnostics, **uploads of the changed rectangle rather than four
megabytes**, **screenshot readback with a blank-frame assertion behind it**, and the guard that
checks the host key map against Studio's own key vocabulary.

**Phase 5 — Docking** (14 of 15). The dock node tree, splits, draggable splitters with
minimum sizes and cursor shapes, tab strips, opening and closing panels, serialization, restoring
the default, dropping panels a build no longer has, never failing to start on a corrupt layout, the
layout surviving between runs, tab reordering, dragging a panel to another dock with a drop-target
preview, **undocking into a floating window** — moved, resized, given more tabs and docked again,
with its geometry clamped back into view on a smaller screen — and **arrangements saved under a
name**, in the same file, with Save Layout As and Dock All Windows in the Window menu.

**Phase 6 — Studio shell** (23 of 24). The action registry and core action set; an interactive menu
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

**Phase 7 — Panel migration** (21 of 27, 3 in progress). The strangler seam itself — one log model
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
real CNA renderer, reviewed in `docs/VISUAL-ACCEPTANCE.md` — which found three things no surface
inventory could: the prototype's Inspector shows the Scene Environment, an editable Grid Snap and
the project's layer list when nothing is selected, and the native Details panel showed a sentence.
All three are answered now, in the Details panel standing idle and through the command history —
and closing them found that `studioTextField` committed **twice** for any caller that normalises
what it stores, which is every numeric field in Studio. **Plugin menus** were the last unanswered
menu row and the one architectural one: a plugin's commands are registry actions now rather than
rows somebody draws, so a shortcut, enablement and the shortcut editor apply to them for free.

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

**Phase 31 — Reliability** (1 of 13).

**Phase 33 — Docs and CI** (11 of 21). Golden-image infrastructure, visual tests at every tested
resolution and DPI scale, the assertion-macro hardening a sanitizer forced, the plan-arithmetic
guards, and CI coverage for the sanitizer and CNA-backed configurations with the captures kept as
artefacts — which are now **eighty-nine times smaller**: adaptive scanline filtering and a real
LZ77 deflate took a 1920x1080 shell capture from 8.3 MB to 93 KB, and the CNA-backed job's whole
artifact set from about 85 MB to 2.2 MB, verified by an inflate written beside the tests and by an
independent zlib.

---

## Design decisions worth knowing

**The new UI emits `UiDrawData`.** That is the existing seam `CnaUiRenderer` already draws through
CNA's public graphics API. The native UI therefore inherits a working, tested CNA renderer rather
than needing one written for it, and the ImGui migration is a strangler rather than a rewrite.

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

## Things found that were not expected

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

---

## Known gaps and failures

Nothing is failing. What is **not** done, and should not be mistaken for done:

- **The native shell is not the default UI.** `--ui=studio` runs it; `cna-studio` with no flag still
  runs the ImGui editor. It stays that way until both presentations can coexist in one process
  rather than being two entry points (`STUDIO-07001`), which is what `STUDIO-06015` waits on.
- **One panel is still ImGui-only**: the material editor, which is Phase 19 work rather than a port
  — there is no `.cnamaterial` model to port *to* yet. The menu bar, toolbar and status bar exist
  natively but have not been checked against the prototype's inventory as *ports*
  (`STUDIO-07002`–`07004`), which is what `STUDIO-07020` is for.
- **The 3D view and tilemap painting have no native equivalent.** They are the substance of what
  keeps `STUDIO-06015` from being true: a default that lost them would be a regression however many
  panels are ported.
- **Shortcuts can be rebound but not from the UI.** The registry refuses a conflicting chord and the
  preferences file stores and reapplies rebindings, as the chord text the menus show. What is
  missing is the editor — a list of commands, a row that takes the next keystroke, and the conflict
  shown before it is accepted (`STUDIO-06012`).
- **A floating window is inside the Studio window, not an OS window.** It moves, resizes, takes more
  tabs and docks again; it cannot be dragged onto a second monitor. CNA *does* offer a second window
  — `IPlatform::CreateWindow` behind a `MultipleWindows` capability, and `PresentationParameters`
  carries a device window handle — so the open question is whether Studio can drive a second
  `GraphicsDevice` at all, not whether the platform has windows. Recorded as `STUDIO-05015`,
  research rather than work.
- **New Project and Open Project are still unbound.** Both need a native file dialog, and CNA has
  one — `IPlatformDialogs::ShowOpenFileDialog`, callback-shaped because a file dialog on every
  platform CNA targets is asynchronous, plus `CNA::Devices::FileDialog` behind the same default-off
  option as the clipboard (G-02). So this is Studio wiring and an async seam through `StudioShell`,
  not a missing CNA API. `--project` opens one today.
- **The caret does not blink.** Deliberate until there is an animation model (`STUDIO-03030`): a
  caret that blinks off is one a golden image catches half the time.
- **Text moves by code point, not by grapheme cluster.** A flag emoji is one thing a reader sees and
  several code points, so the caret steps inside some characters. `STUDIO-03026` needs a breaker
  and a table this repository does not have.
- **The glyph atlas re-uploads whole.** A dirty atlas sends all four megabytes rather than the
  changed region (`STUDIO-04017`), and a full one drops glyphs and counts them rather than growing
  (`STUDIO-04018`). Both are start-up costs — the atlas settles within a few frames.
- **No graphical CI on a GPU.** `STUDIO-33023` covers the CNA seam on `SOFTWARE`, which needs no
  display. Anything a GPU does differently, and the cases labelled `needs-display`, wait on
  `STUDIO-33010` — infrastructure rather than code.
- **Visual-test PNGs are large** (~8 MB at 1080p): stored deflate, correct and reviewable but
  uncompressed. `STUDIO-33016`.

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

In dependency order. The 3D view — which the previous handoff named as the single gate on making the
native shell the default — is answered: `2` and `3` switch views, a drag orbits, Shift pans, the
wheel dollies, W/A/S/D/Q/E fly, and a click picks along a ray. The migration inventory now names
**nothing the prototype does that the native shell does not**, apart from forwarding input to a
running player.

| Id | Task |
|----|------|
| `STUDIO-07009` | Forward input to a running player — the viewport's last unported piece |
| `STUDIO-06015` | Make the native shell the default, with the legacy UI behind a flag |
| `STUDIO-07001` | Both UIs in one running Studio, so the switch can be made without a rebuild |
| `STUDIO-07030` | Remove the Dear ImGui panels |
| `STUDIO-07031` | Remove the `CNA_STUDIO_WITH_IMGUI` option and the vendored source |
| `STUDIO-07099` | Guard test: the production Studio UI has no dependency on Dear ImGui |

Not on that chain, and each worth doing on its own:

| Id | Task |
|----|------|
| `STUDIO-11015` | Maya and Blender navigation schemes — the preference exists and nothing reads it |
| `STUDIO-04019` | Font fallback — CJK, Hangul and emoji are boxes today, and the caret is not the reason |
| `STUDIO-04010` | Render-resource lifetime and recreation on device loss |
| `STUDIO-04011` | The device half of window resize: back buffer and viewport render targets |
| `STUDIO-03013` | Accessibility metadata on every widget: role, name, value, state |
| `STUDIO-03027` | IME support where the platform provides it |
| `STUDIO-11003` … `11012` | Focus selection, standard views, adaptive grid, outlines, wireframe mode |
| `STUDIO-19NNN` | The material editor — the one panel with no prototype to port from |

**Blocked, not forgotten.** `STUDIO-33010` (graphical CI with a display) and the per-renderer half of
`STUDIO-04015` both wait on CNA gap G-10: every renderer that can host Studio needs a display and
undocumented sibling checkouts. `STUDIO-15001` (the C++ reflection mechanism) is 🔬 blocked on an
architectural decision and should be decided before Phase 15 work begins, not during it.

---

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
