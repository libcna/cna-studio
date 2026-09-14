# CNA Studio — Handoff

State of the work in progress, for whoever continues it. Updated at the end of each long session.

**Last updated:** 2026-09-14

---

## Where things are

| | |
|---|---|
| Repository | <https://github.com/libcna/cna-studio> |
| Branch | `claude/studio-baseline-audit-51dyxr` |
| HEAD | `8fe23bf` — `studio: model CNA's real renderer and platform inventory, and guard the architecture` |
| Working tree | Clean (everything below is committed and pushed) |
| Commits this session | 8, all authored `Robert Vokac <robertvokac@robertvokac.com>` |

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

Also run the configurations CI does not yet cover, because they are where the latent defects fixed
this session were found — an ignored `freopen` result, a dangling reference, and an ODR violation
that no compiler diagnosed:

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

See the native Studio UI, headless:

```bash
./build/cna-studio --shell-preview=shell.png --shell-size=1280x720
./build/cna-studio --shell-preview=light.png --shell-theme=light --shell-scale=2.0
```

Collect the visual-test captures as CI artefacts:

```bash
CNA_STUDIO_TEST_ARTIFACTS=./artifacts ./build/tests/cna-studio-tests
```

### Last measured result

| Configuration | Result |
|---------------|--------|
| GCC 13.3 Debug, no CNA | **566 assertions, 17 CTest suites, 0 failures, 0 warnings** |
| GCC 13.3 Release `-Werror`, no CNA | **566 assertions, 17 CTest suites, 0 failures, 0 warnings** |
| GCC 13.3 Debug, **against real CNA** (`next`, SOFTWARE renderer, SDL3 platform) | **22 CTest suites, 0 failures** — including the window smoke test, the 3D viewport smoke test, the scene-loader demo and the player window smoke test |

Baseline at import, for comparison: 442 assertions, 12 CTest suites.

The CNA-backed configuration was **restored** this session: the prototype's viewport did not
compile against current CNA, and its player binary was named `cna-player-` with an empty suffix so
play-mode discovery found nothing. Both are fixed; see *Things found* below.

---

## What was completed

Task ids are `STUDIO-PPNNN`; see `plan.md` for the full list. 65 of 445 tasks are complete.

**Phase 0 — Audit and baseline** (12 of 15). Imported `cna-lab/cna-editor` at
`3bce82dd74e9a201a21e31308d43d2ee7761d641` into the repository root, verified its baseline, and
re-audited current CNA.

**Phase 1 — Product rename** (13 of 16). `cna-studio` executable, 12 `cna-studio-*` targets, the
`CNA::Studio` namespace, `include/CNA/Studio/`, `CNA_STUDIO_*` options, user-visible text, README.
94 files moved with `git mv` so per-file history survived.

**Phase 2 — Architecture refresh** (10 of 22). `docs/ARCHITECTURE.md`, `docs/CNA-GAPS.md`,
`docs/LEGACY-EDITOR-TASK-MAP.md`, the roadmap, and ten architecture guard tests.

**Phase 3 — Studio UI core** (7 of 27). Design tokens and two themes, widget identity with
per-frame collision detection, retained widget state with reclamation, geometry primitives, and
the draw-list layer.

**Phase 4 — CNAEXT UI renderer** (4 of 15). Vertex/index management, draw-call batching, nested
scissor clipping, rounded rectangles and separators.

**Phase 6 — Studio shell** (1 of 16, 4 in progress). Menu bar, toolbar, status bar and the shell
preview entry point.

**Phase 29 — Renderer matrix** (2 of 6). The renderer and platform catalogue.

**Phase 33 — Docs and CI** (1 of 22). Golden-image test infrastructure.

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

**A legacy renderer name migrates and says so.** Silently substituting a renderer would change what
a user's game ships on; silently failing would make an old project look corrupt.

---

## Things found that were not expected

**CNA has changed more than the prototype's analysis admits.** Renderer and platform are now
separate axes; there are 50 renderer identities, not 14; and `RendererCapabilityProfile` provides a
real runtime capability model with 32 atomic features, 22 limits and per-format usage masks.

**`EASYGL` is not a renderer any more.** It was the prototype's *default*. It became a renderer
*family* serving five GL profiles, so every `.cnaproject` the prototype wrote names a renderer that
cannot be built. Handled by the alias table; the example project was updated to `OPENGLES3`.

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

## Known gaps and failures

Nothing is failing. What is **not** done, and should not be mistaken for done:

- **The shell is not yet wired to the input layer.** Hover, click, capture, focus, Tab and the
  action registry all exist and are tested (`STUDIO-03007`…`03012`, `STUDIO-06001`/`06002`), but
  the shell still only *draws*: its menus and toolbar do not yet call `interact()`. That wiring is
  the next commit-sized piece of work, and it is why the entry point is still `--shell-preview`
  rather than `--ui=studio`.
- **Text is a placeholder.** `drawTextPlaceholder` fills a measured box, deliberately at reduced
  alpha so an unfinished build looks unfinished. Real glyphs need the font atlas of `STUDIO-04005`.
- **Studio's CMake still uses `CNA_GRAPHICS_BACKEND`**, the variable name from before CNA split
  renderer from platform. Current CNA uses `CNA_GRAPHICS_RENDERER` and `CNA_PLATFORM`. Migrating
  the build option is `STUDIO-02040`/`STUDIO-17003`.
- **The host capability contract is designed but not implemented** (`STUDIO-02020`…`02022`).
- **No graphical CI.** `STUDIO-00013`'s reference screenshots and the real-device smoke tests are
  blocked on `STUDIO-33010`.
- **Visual-test PNGs are large** (~8 MB at 1080p): the encoder uses stored deflate, which is
  correct and reviewable but uncompressed. `STUDIO-33016`.

### Building against a real CNA checkout

Reproduced this session on Ubuntu 24.04. CNA's SDL is a submodule and is not fetched by a plain
clone:

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

In dependency order. The first block is what makes the shell a UI rather than a picture.

| Id | Task |
|----|------|
| `STUDIO-03015` | Frame lifecycle: build, layout, input, draw, retain — then wire the shell to the input router |
| `STUDIO-03003` | Widget helpers over `interact()`: button, toggle, tab, menu item |
| `STUDIO-06004` | Menus that actually open, driven by the action registry |
| `STUDIO-05001` | Dock node tree model |
| `STUDIO-05003` | Resizable splitters with minimum sizes and cursor shapes |
| `STUDIO-02020` | Define the Studio host capability contract as data |
| `STUDIO-02021` | Evaluate the contract against the live device at start-up |
| `STUDIO-02022` | Fail cleanly when the compiled renderer cannot host Studio |
| `STUDIO-04005` | Font atlas construction and glyph rasterization |
| `STUDIO-04006` | Text rendering with kerning and correct line metrics |
| `STUDIO-04009` | Choose and document redistributable fonts and icons |
| `STUDIO-02040` | Target-profile model, and migrate `CNA_GRAPHICS_BACKEND` to the split axes |
| `STUDIO-02050` | Service decomposition of the application shell |
| `STUDIO-02051` | Early guard: an exported project builds with Studio unavailable |
| `STUDIO-01014` | Decide and document the compatibility-shim policy for the renamed API |
| `STUDIO-01015` | Rename the state/configuration directory, with migration |
| `STUDIO-00014` | Record the prototype panel/menu/shortcut inventory as the migration checklist |
| `STUDIO-33010` | Graphical CI with a real CNA build and a display |
| `STUDIO-02010` | Re-measure CNA gap G-03 across the current renderer set |

`STUDIO-15001` (the C++ reflection mechanism) is 🔬 blocked on an architectural decision and should
be decided before Phase 15 work begins, not during it.

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
