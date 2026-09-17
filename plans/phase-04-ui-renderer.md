# Phase 4 — CNAEXT UI renderer

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-04001` … `STUDIO-04999` and are never reused.

**Purpose.** Draw the Studio UI through CNA's public modern graphics API: text, icons, batching, clipping, render resources and DPI, with no renderer-specific code.

**Exit criteria.** The UI draws correctly and efficiently on every renderer that satisfies the host capability contract, with one implementation, **through CNA's modern graphics API**.

> **This phase's ledger was reconciled by `STUDIO-04022` after the audit in `STUDIO-04021`.** Read
> that section before reading the table: several tasks were ✅ against a classic XNA implementation
> in a phase named for the modern one, and the corrections are recorded rather than quietly applied.

**Progress:** 25 of 29 complete `████████░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-04001` | Create the `cna-studio-ui-renderer` module inside the CNA-linking boundary | ✅ | `STUDIO-02020` |
| `STUDIO-04002` | CPU-side vertex and index assembly for UI geometry | ✅ | — |
| `STUDIO-04003` | Draw-call batching by texture, clip rectangle and blend state | ✅ | `STUDIO-04002` |
| `STUDIO-04004` | Scissor-based clipping, including nested clip stacks | ✅ | `STUDIO-04002` |
| `STUDIO-04005` | Font atlas construction and glyph rasterization | ✅ | — |
| `STUDIO-04006` | Text rendering with kerning, and correct baseline and line metrics | ✅ | `STUDIO-04005` |
| `STUDIO-04007` | Dynamic glyph upload without frame stalls or dropped glyphs | ✅ | `STUDIO-04005` |
| `STUDIO-04008` | Icons, drawn as vector paths rather than sampled | ✅ | — |
| `STUDIO-04009` | Select and document legally redistributable fonts and icons | ✅ | — |
| `STUDIO-04010` | Render-resource lifetime and recreation on device loss | ⬜ | `STUDIO-04002` |
| `STUDIO-04011` | Window resize handling without artefacts | 🔄 | `STUDIO-04010` |
| `STUDIO-04012` | Render-target composition for the viewport panel | ✅ | `STUDIO-04004` |
| `STUDIO-04013` | Screenshot and readback support for visual testing | ✅ | — |
| `STUDIO-04014` | Rounded rectangles, borders and separators as first-class primitives | ✅ | `STUDIO-04002` |
| `STUDIO-04015` | Per-renderer smoke test: draw a reference panel and assert non-empty output | 🔄 | `STUDIO-04013` |
| `STUDIO-04029` | Make `OPENGL4` under Xvfb a second tested configuration in CI | ✅ | `STUDIO-04026` |
| `STUDIO-04016` | Cull geometry that lies entirely outside the clip in force | ✅ | `STUDIO-04004` |
| `STUDIO-04017` | Upload only the changed region of the atlas | ✅ | `STUDIO-04005` |
| `STUDIO-04018` | Grow or evict when the glyph atlas fills | ✅ | `STUDIO-04005` |
| `STUDIO-04019` | Font fallback, so text outside the shipped faces is readable rather than boxes | ⬜ | `STUDIO-04005` |
| `STUDIO-04020` | Guard test: every key Studio can ask about is one the host reports | ✅ | — |
| `STUDIO-04021` | Audit which CNA graphics API the UI actually reaches the GPU through | ✅ | — |
| `STUDIO-04022` | Reconcile this phase's ledger with what was actually implemented | ✅ | `STUDIO-04021` |
| `STUDIO-04023` | GPU vertex and index buffers for UI geometry | ✅ | `STUDIO-04001` |
| `STUDIO-04024` | `StudioModernUiRenderer`: draw `UiDrawData` through `ShaderEffect` | ✅ | `STUDIO-04023` |
| `STUDIO-04025` | A/B verification: both backends draw the same frame | ✅ | `STUDIO-04024` |
| `STUDIO-04026` | Default the native host to the modern backend | ✅ | `STUDIO-04025` |
| `STUDIO-04027` | Remove the classic UI GPU path, or justify retaining it | ✅ | `STUDIO-04029` ✅, `STUDIO-07030` ✅, `STUDIO-02074` ✅ |
| `STUDIO-04028` | UI render benchmarks: CPU time, upload bytes, counts, state changes | ✅ | `STUDIO-04001` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-04021` — Audit which CNA graphics API the UI actually reaches the GPU through

**Acceptance.** The path from a widget call to a CNA graphics call is traced and written down, and
the answer is established by reading the calls rather than by reading the names.

**Done, and the answer was no.** `docs/UI-RENDER-PATH.md` holds the trace, the complete list of
graphics calls the UI makes, the four layers the phrase "the Studio renderer" had been used for all
four of, and the migration's stages. The short version: the native Studio UI reaches the GPU through
`BasicEffect` and `DrawUserIndexedPrimitives` — the classic XNA 4.0 surface it inherited from the
Dear ImGui prototype. `CnaUiRenderer.cpp` includes no CNAEXT header and would compile against a CNA
built with `-DCNA_CNAEXT=OFF`.

**Nothing was mis-stated on purpose, which is the part worth understanding.** The seam between the
UI and the renderer is genuinely toolkit-independent; the native UI genuinely inherited a working
renderer across it, and doing so was right — a strangler migration that also rewrote the GPU layer
would have had no working state to fall back to. What none of that establishes is which CNA API the
pixels come out of, and `UiDrawData` is the seam's *name*: inheriting the seam inherited the
implementation behind it.

**The audit found two live consequences** beyond the ledger: the host contract did not enforce its
own headline (`STUDIO-02070`), and modern-API availability was a literal `true` at both host call
sites (`STUDIO-02071`)

### `STUDIO-04022` — Reconcile this phase's ledger with what was actually implemented

**Acceptance.** No task in this phase is ✅ on the strength of work that a later task has to redo,
and no ✅ task depends on a ⬜ one. Each affected row is corrected for a stated reason.

**The inconsistency.** `STUDIO-04001` — create `cna-studio-ui-renderer` — was ⬜, and four ✅ tasks
named it as a dependency. The module does not exist; the work was done in `cna-studio-viewport`,
beside the scene renderer and the platform layer.

Resolved one at a time, and the resolutions are deliberately not all the same. The table above is
the record; these are the reasons.

- **04002 — retitled, not reopened.** It read "Vertex and index buffer management for UI geometry"
  and depended on `04001`. What exists is `StudioDrawList` plus two scratch `std::vector`s handed
  to `DrawUserIndexedPrimitives`: vertex and index *assembly*, which is complete and is not buffer
  management — there is no `VertexBuffer` anywhere in Studio. So the title now says what was built,
  and the GPU half is `STUDIO-04023`, a **new** task. Reopening 04002 would have hidden a genuine
  piece of remaining work inside a task somebody had already reasoned about and closed.
- **04003 and 04004 — unchanged, and correctly so.** Batching by texture and clip rectangle lives
  in `StudioDrawList`, and nested clipping is decided in the UI core; the renderer sets one scissor
  rectangle per command, which both backends do identically. Both survive the migration untouched.
  Not everything in a phase named for a renderer is renderer work.
- **04005 and 04013 — dependency on `04001` dropped.** `StudioFontAtlas` links no CNA at all, and
  readback is `GetBackBufferData` on the host rather than anything in the UI renderer. Neither ever
  needed the module; the dependency was written when the phase was planned and never re-examined.
- **04001 — still ⬜, and now with no ✅ task depending on it.** The module is stage 2 of the
  migration and has not been created.

**No status was changed to preserve a percentage**, and the phase's completion went *down* as a
fraction — 15 of 20 to 17 of 28 — because eight real tasks were added and two were closed. A ledger
that only ever improves is a ledger that is being managed rather than kept

### `STUDIO-04023` — GPU vertex and index buffers for UI geometry

**Acceptance.** A UI frame's geometry reaches the device through `DynamicVertexBuffer` and
`DynamicIndexBuffer` with `SetData`, rather than through a user-pointer draw, and the buffers are
reused across frames rather than reallocated.

**Why it is separate from the shader.** These are two independent changes to the same file and they
fail differently: a wrong buffer shows as geometry in the wrong place, a wrong shader as the right
geometry in the wrong colours. Landing them together would make the first failure indistinguishable
from the second.

### `STUDIO-04024` — `StudioModernUiRenderer`: draw `UiDrawData` through `ShaderEffect`

**Acceptance.** The UI's pixels are produced by a `ShaderEffect` Studio compiled, not by
`BasicEffect`; the effect is authored once in GLSL and is renderer-neutral; nothing in Studio names
a graphics backend.

### `STUDIO-04025` — A/B verification: both backends draw the same frame

**Acceptance.** The same `UiDrawData` rendered through both backends produces captures that match
within a stated tolerance, and the comparison runs as a test rather than as a screenshot somebody
looked at.

**Not bit-exact**, and the tolerance is the point: a fixed-function path and a fragment shader
resolve the same triangle's edge pixels differently, and demanding equality would either fail
forever or be loosened until it asserted nothing.

### `STUDIO-04026` — Default the native host to the modern backend

**Acceptance.** `resolveStudioUiBackend` prefers the modern backend on a host that meets its
profile, that host is what a release build gets, and the fallback is reached only by hosts that
cannot run it.

### `STUDIO-04027` — Remove the classic UI GPU path, or justify retaining it

**Acceptance.** Either `CnaUiRenderer` is deleted, or the reason it stays is written down on this
task with the renderers it serves named. Two full UI GPU stacks are not kept by default.

**The blocker that was recorded here has expired, and a different one was found in its place.**

**Expired.** This task used to say: "CNA's `SOFTWARE` renderer cannot execute a shader, and it is
the only renderer this project's CI can build (gap G-10). Deleting the classic path before a
modern-capable renderer runs in CI would delete the only automated graphical coverage Studio has."
`STUDIO-04029` closed that, and the cost of deletion was then measured rather than assumed: the
`OPENGL4` leg's CTest set is a **strict superset** of the `SOFTWARE` leg's — 79 suites against 78,
the extra being `CnaStudioUiRenderBackendsAgree`, which only a renderer that can run both backends
can declare at all. Deleting the classic path costs **no** automated coverage.

```
$ comm -23 <(ctest --test-dir build-cna -N) <(ctest --test-dir build-gl -N)
(nothing)
```

**And the case for deleting is stronger than "two stacks is one too many".** `STUDIO-04028`
measured the classic path at **17–18× the modern one's geometry submission**, on every one of eight
frame shapes, for structural reasons that get worse as the UI is batched more finely. It also
cannot draw material, shader or post-process previews, which Phases 19, 20 and 22 are built on — so
it is a path that will stop being able to draw the editor rather than merely being slower at it.

**The real blocker: the Dear ImGui host draws through it, unconditionally.**
`CnaStudioHost::LoadContent` — the `--ui=imgui` host — does `std::make_unique<CnaUiRenderer>()` with
no chooser and no alternative. `STUDIO-04026` defaulted *the native host* to the modern backend and
left the prototype's host where it was, which was right at the time and is why this went unrecorded.
The native shell host picks through `resolveStudioUiBackend`; the prototype's host does not pick at
all.

So deleting `CnaUiRenderer` today would delete the renderer the Dear ImGui prototype draws with, and
the prototype is still shipped and still reachable — it is `STUDIO-07030`'s to remove, which is in
turn blocked on `STUDIO-07042`–`07046`. **This task now depends on `STUDIO-07030`.** Porting the
prototype's host to the modern backend was considered and rejected: it is work spent on a host whose
own task is deletion, and it would make `--ui=imgui` refuse to start on `SOFTWARE`, which is where
the prototype's remaining coverage runs.

**`STUDIO-07030` is done, and this task is still blocked — on `STUDIO-02074` now, not on the
prototype.** The prototype's host is deleted, so the blocker recorded above has expired exactly as
expected. What was not expired, and was not visible until the prototype's own consumer went away:
`CnaStudioShellHost` — the *native* host, not the prototype's — also does
`std::make_unique<CnaUiRenderer>()`, at two call sites (`makeUiRenderBackend()`'s
`StudioUiBackendChoice::Compatibility` case, and the runtime fallback when a host the capability
report called modern-capable refuses Studio's UI shader anyway). Both are reached deliberately: the
first is what `--ui-renderer=compat` asks for and what `StudioHostProfile::Compatibility`
(`STUDIO-02070`) resolves to on a host like `SOFTWARE`, which cannot execute a shader at all; the
second is what keeps a wrongly-optimistic capability report from opening a black window instead of
a working one. Deleting `CnaUiRenderer` today would delete both, and the CI workflow's own
`SOFTWARE` leg (`.github/workflows/build.yml`, `expect_backend: compatibility`) asserts that this
exact path is taken and passes.

`CnaUiRenderer` therefore has no *prototype* caller left, but it has a live, tested, native one —
which is exactly the caller `STUDIO-02074`'s acceptance names ("a host that cannot run the modern UI
renderer refuses to start" instead of falling back). `STUDIO-02074`'s own "deliberately not now"
note gave two conditions for revisiting it: `STUDIO-04026` done, and CI able to build a
modern-profile renderer (gap G-10). Both are now true — `STUDIO-04029` is the second one, verified
running in `.github/workflows/build.yml`'s `cna` job today, not merely reproduced locally. That does
not make retiring the compatibility profile a mechanical cleanup, though: doing it would turn the
`SOFTWARE` CI leg from "runs Studio on the compatibility renderer and asserts so" into "refuses to
start", which needs its own decision about what that leg becomes — dropped, repointed at a
build-only check, or something else — not a side effect of deleting one renderer file. **This task
now depends on `STUDIO-02074`, and that task is the one with a decision left in it.**

**The renderers the classic path serves, named as the acceptance asks.** Not a list of renderer
identities — Studio never decides this by name, and `STUDIO-02030`'s guard exists to keep it that
way — but a capability class: **every CNA renderer that reports `ShaderEffects` or
`ShaderEffectSourceExecution` as unsupported.** Of the two this project builds, `SOFTWARE` is in that
class and `OPENGL4` is not; `HEADLESS` and `STUB` are CPU rasterisers and are in it by the same
reasoning. When this path goes, Studio stops being hostable on a renderer that cannot execute a
shader — which its own capability contract (`STUDIO-02070`, Cases A–D) already says is the intended
answer, and which `STUDIO-02074` then finishes by retiring the compatibility profile.

**One thing was done now rather than waiting**, because it is what made the dependency visible:
`CnaUiRenderer::getBackendName()` — a static on the classic backend that answered *which CNA
renderer this build was compiled against*, layer 3 rather than layer 2 — is
`studioHostCnaRendererName()` in its own header. `src/player` linked the editor's UI renderer for
that one string and now links no UI render backend at all, which
`ThePlayerDependsOnNoUiRenderBackend` keeps true.

**Done.** `STUDIO-02074` landed and gave this task exactly the caller-free state its own
"deliberately not now" note was waiting on: `CnaUiRenderer` was constructed nowhere in `src/` or
`include/`, its only two call sites (`makeUiRenderBackend()`'s `Compatibility` case and the
runtime shader-rejection fallback) both deleted with that task. `include/CNA/Studio/UiRenderer/
CnaUiRenderer.hpp` and `src/ui-renderer/CnaUiRenderer.cpp` are deleted. `cna-studio-ui-renderer`
keeps its module boundary — it is still layer 2, separate from `cna-studio-viewport`'s scene
renderer, for the reason it always was — with one backend in it (`StudioModernUiRenderer`) instead
of two; `CMakeLists.txt`'s `add_library(cna-studio-ui-renderer …)` lost one source file and nothing
else.

**Two things outside `CnaUiRenderer` itself needed a home, not a deletion.** `src/viewport/
CnaStudioViewport.cpp` and `src/viewport/CnaSceneRenderer.cpp` each included `CnaUiRenderer.hpp`
for one reason — `StudioUiRenderBackend&`, a type that header pulled in transitively — and now
include `StudioUiRenderBackend.hpp` directly. `studioUiGpuVertexStrideMatches()` and its paired
`static_assert` (`STUDIO-04028`'s GPU-vertex-stride sanity check) were declared in
`CnaUiRenderer.hpp` and defined in `CnaUiRenderer.cpp`, but answer a question that was never about
which backend draws — only about what CNA uploads — and their one caller,
`CnaStudioShellHost::checkCostModelAgrees`, is in a different file entirely. Both moved into
`CnaStudioShellHost.cpp` as a file-local (anonymous-namespace) helper, single-TU, no header
declaration needed. `checkCostModelAgrees` itself lost a now-dead branch: it used to pick between
`predicted.modernSubmittedBytes` and `predicted.classicSubmittedBytes` by checking
`renderer_->name()`, and `renderer_` can only ever be `StudioModernUiRenderer` now, so the check
and the branch it guarded are gone — the cost model always compares against the modern figure.

**What the classic path cost stays measured, not deleted.** `StudioUiBenchmark`'s cost model
(`STUDIO-04028`) still computes `classicSubmittedBytes`/`classicGpuBytes` for every scenario
`--ui-benchmark` runs: it is a pure function of `UiDrawData` with no CNA dependency and no
`CnaUiRenderer` reference, kept deliberately as the permanent record of what the deleted path used
to cost — the 17–18× figure this task's own case for deleting rested on. Measuring a retired path's
hypothetical cost is not "two full UI GPU stacks kept by default"; running one is, and after this
task there is exactly one.

Full five-configuration matrix green: Debug, Release with -Werror, ASan+UBSan (1295/1295, 61/61
CTest suites on each), CNA on SOFTWARE (67/67 CTest suites), CNA on OPENGL4 under Xvfb (1304/1304,
80/80 CTest suites) — including a manual, frame-limited run on each CNA configuration confirming
`checkCostModelAgrees` still agrees with the relocated `studioUiGpuVertexStrideMatches()`.

### `STUDIO-04028` — UI render benchmarks: CPU time, upload bytes, counts, state changes

**Acceptance.** A repeatable measurement of representative UI frames — idle shell, a large outliner,
a large content browser, a details panel with many properties, typing, atlas growth, resize —
reporting CPU generation time, upload bytes, vertex and index counts, draw calls, texture changes
and clip changes. Run before and after the migration, so "the modern renderer is not slower" is a
number rather than an impression.

**Done.** `cna-studio --ui-benchmark`, eight scenarios, all seven frame shapes above covered. The
result and the reasoning are in `docs/UI-RENDER-PATH.md` ("What the two backends actually cost").

**The headline: the classic backend puts 17–18× as much geometry on the bus, on every shape of
frame measured.** Structural rather than incidental — `DrawUserIndexedPrimitives` takes user arrays
and the driver copies them per call, and the array a command is handed runs from its base vertex to
the end of its list, which in any list under 65 535 vertices is the whole array. The ratio is
roughly the draw-call count and grows as the UI is batched more finely, which is the direction every
added panel pushes it. That is what `STUDIO-04027` now decides on.

**It needs no CNA, no GPU and no window.** Every figure is computed from the `UiDrawData` both
backends are handed, so it runs on every push rather than only in the hour-long CNA job — a
benchmark that only runs in the expensive configuration is a benchmark nobody runs.

**What it does not measure is GPU time**, deliberately. A driver's answer to the same submission
varies by vendor; the question here is whether one backend submits more work than the other, which
is a property of Studio rather than of Mesa.

**The model is held to account by the thing it models.** It is a second implementation of both
inner loops and can therefore be quietly wrong, so `CnaStudioShellHost` recomputes it against the
backend's own counters on every frame of every run that carries a frame limit — every capture, every
CTest smoke test, both CNA legs of CI, and no interactive session. A disagreement fails the process
naming the field, both numbers and the frame. Verified by making the model wrong on purpose.

**Three things it found that it was not looking for.**

- **`CnaUiRenderer` had been reporting zero upload bytes.** The field existed, the modern backend
  filled it in, and the classic one never touched it — so every comparison of the two was a number
  against a zero, which reads as "the classic path uploads nothing" rather than "nobody counted".
  Fixing that is what made the 17× measurable.
- **A UI vertex is 56 bytes for 20 bytes of data** — `CNA::Color` alone is 24, because it carries a
  vtable. Found by a `static_assert` refusing the estimate of 32 that had been written down.
  `docs/CNA-GAPS.md` G-11. The benchmark reports what is handed to CNA and what reaches the bus
  separately, because they are 2.3× apart and only one of them is the bus.
- **The World Outliner is O(n²) in scene size**, and the Content Browser costs 23 ms a frame at
  1 500 assets. `STUDIO-30013` and `STUDIO-30014`. Neither is a rendering problem and neither would
  be visible in a screenshot, which is the argument for reporting CPU time beside the counts.

### `STUDIO-04001` — Create the `cna-studio-ui-renderer` module inside the CNA-linking boundary

**Acceptance.** Consumes `UiDrawData` and produces pixels; contains no UI toolkit header, preserving the seam that keeps the core CNA-free.

**Stage 2 of `docs/UI-RENDER-PATH.md`'s migration**, and it moves `CnaUiRenderer` in unchanged
behind a `StudioUiRenderBackend` interface before anything new is written. A module created by
*adding* the modern renderer to it would have one backend in it and one somewhere else, which is the
arrangement that makes an A/B comparison impossible to write.

### `STUDIO-04003` — Draw-call batching by texture, clip rectangle and blend state

**Acceptance.** A typical frame issues a small bounded number of draw calls

**Verification.** A test asserts the batch count for a reference panel layout

### `STUDIO-04005` — Font atlas construction and glyph rasterization

**Acceptance.** Studio owns its atlas; `SpriteFont` is constructed through CNA's public CNAEXT constructor (see CNA gap G-04)

**How it was met.** `StudioFontAtlas` rasterises each `(typeface, pixel size)` pair separately
rather than scaling one master size — scaling a bitmap font is exactly the blurry text that makes an
application look amateur at 125% and 150%, the two most common DPI scales on Windows laptops — and
packs every face and size into one 1024² texture with a shelf packer, so text in three fonts and
four sizes costs the same single draw call as text in one. A fully opaque white texel is reserved in
the same atlas and sampled by every untextured primitive, which is what keeps a shell frame at 11
draw calls with real text instead of 30.

Outline rasterization is the vendored `stb_truetype` (public domain / MIT), included by exactly one
translation unit with internal linkage; atlas packing, glyph caching, metrics, kerning lookup, UTF-8
decoding and text layout are Studio's own. The `SpriteFont` half of this task is not needed by the
Studio UI and belongs with previewing a user's `.spritefont` asset, which is Phase 9

**Verification.** `tests/StudioFontTests.cpp`: every shipped typeface embeds and parses, glyphs have
real coverage in the atlas, packed glyphs never overlap, the reserved white texel is opaque, each
size is a separate rasterization rather than a scaled copy, and sizes that round to the same whole
pixel share one face so a DPI scale landing on 13.02 does not double the atlas

### `STUDIO-04006` — Text rendering with kerning, and correct baseline and line metrics

**How it was met.** Text is laid out on a baseline rounded to a whole pixel — a fractional baseline
lands every glyph in the line on a half pixel, which is the difference between crisp text and text
that looks faintly smeared at exactly the sizes a UI uses — with per-pair kerning applied *before*
each glyph, so a run clipped mid-word still positions every glyph it draws where an unclipped run
would have.

**Kerning is what chose the fonts.** Most modern fonts express kerning as GPOS lookups and the
rasterizer reads only the common `PairPos` form. Open Sans and JetBrains Mono were shipped first and
then measured: neither carries a `kern` feature at all, so text in them had no kerning whatsoever.
IBM Plex Sans does, so Studio ships IBM Plex — see `THIRD_PARTY_NOTICES.md`

**Verification.** `tests/StudioFontTests.cpp`: measurement equals the sum of advances plus kerning;
the classic pairs are pulled together and `AV` measures narrower than its two advances; capitals sit
above the baseline and descenders below it, within the face's own descent; measurement is
deterministic across atlases; and text drawn through a frame emits glyph quads against the atlas
with the upload riding in the same frame

### `STUDIO-04007` — Dynamic glyph upload without frame stalls or dropped glyphs

**Acceptance.** Glyphs first needed on a frame that does not draw are still uploaded — the prototype shipped this bug once (legacy ED-119) and it must not return

**Done, in both halves.** The **dropped-glyph** half: the upload request is emitted at *end of
frame* rather than at the start of the draw pass, so a glyph rasterised at any point in the frame —
by a measurement during layout, or by a widget that draws text nothing measured — still reaches the
renderer before the quad that samples it, because texture requests are applied ahead of every draw
command. The **stall** half followed in `STUDIO-04017` (a dirty atlas uploads the changed rectangle
rather than all four megabytes) and `STUDIO-04018` (a full atlas doubles rather than dropping)

**Verification.** A regression test for the update/draw phase split

### `STUDIO-04017` — Upload only the changed region of the atlas

**Acceptance.** A frame that rasterised one new glyph uploads that glyph's rectangle, not the whole
texture. The atlas settles within a few frames of start-up, so this is a start-up cost rather than a
steady-state one — which is why it is a follow-up rather than part of `STUDIO-04005`

**Done, and it is not only a start-up cost.** A user typing into a text field rasterises a glyph
they have not used before, and a UI re-uploading four megabytes on a keystroke is a stutter in the
one place a stutter is most visible. The atlas now tracks the smallest rectangle covering everything
rasterised since the last request: a `Create` of the whole texture the first time and after a growth,
because the texture itself has to be allocated, and an `Update` of that rectangle otherwise.

**A union rather than a list of rectangles.** Two glyphs on opposite shelves upload the rows between
them too. A list would send fewer bytes and would need the requests to stay in order across a frame
boundary — a correctness problem in exchange for bytes, on a texture that settles within a few
frames.

**And it found a contract violation underneath.** `UiTextureTable`, the software rasterizer's
texture store, kept the request's *pointer* rather than copying — which `UiTextureRequest` forbids
in as many words, and which worked anyway because the only texture anybody uploads is a font atlas
that outlives the frame. It stops working the moment an `Update` arrives, because an Update's
pointer is the top-left of a *region*: read as a whole texture it draws every glyph from somewhere
else in the atlas, which looks like a corrupt font rather than a wrong pointer. The table owns its
pixels now and blits each region into them.

**The test is a byte-for-byte comparison** of the table's copy against the atlas after an upload
that sent only part of it, driven at three different sizes so the regions land on different shelves.
Both halves of the arithmetic were checked by breaking them: the request's pointer, and the table's
placement of it.

### `STUDIO-04018` — Grow or evict when the glyph atlas fills

**Acceptance.** Running out of atlas stops being a silent loss. Today a glyph that will not fit is
counted in `droppedGlyphs()` and not drawn, which is honest but is still text that stops appearing
partway down a panel

**Done: grow, not evict.** The atlas doubles, from 1024 to a cap of 4096, and re-rasterises. Evicting
is the other answer and is worse here: a UI redraws the same text every frame, so a least-recently-
used policy under pressure evicts glyphs that are about to be needed again and thrashes — and the
frame in which it thrashes is the frame the user is looking at. Growth is bounded, settles, and at
the cap goes back to counting, so the honest last resort survives rather than being replaced.

**Where it grows is the whole of why it is safe.** Texture coordinates are normalised by the atlas
side, and every cached `StudioGlyph` holds them. So doubling invalidates every coordinate and every
glyph pointer handed out — and doing it where the need is *discovered*, inside a pack that failed
partway through a draw pass, would move the glyphs out from under quads already written against
them. That is legacy ED-119's shape again, and it would not read as a missing glyph: it would read
as letters drawn out of pieces of other letters, which looks like a corrupt font file.

So `StudioFrame::beginFrame` calls `growIfNeeded()` before any pass, which is the one point at which
no pointer is held and no quad exists. The frame that ran out draws without those glyphs and the
next one has them. One frame of a missing glyph at start-up is not something a user can see; a
permanent one partway down a panel is.

**The count resets on a growth**, because the glyphs it counted are about to be tried again, and a
count that survived would report an atlas as full while it was filling up. A non-zero
`droppedGlyphs()` at `kMaxAtlasSize` is therefore exactly the state in which text really is lost.

**And it is finally visible.** The atlas has counted its drops since it was written and nothing
displayed the count, which made it a diagnostic only a debugger could read — so the Diagnostics
panel now carries the atlas's size, how full it is, how often it has doubled, and, in the error
colour when there are any, the glyphs it dropped, named as missing text rather than as a statistic.
One decimal on the percentage: a Latin UI at 1x uses a fraction of a percent of a 1024-pixel atlas,
and "0% full" reads as an atlas that is not working.

### `STUDIO-04009` — Select and document legally redistributable fonts and icons

**Acceptance.** Licences recorded in `THIRD_PARTY_NOTICES.md`; no proprietary third-party assets of any kind

**Fonts: done.** IBM Plex Sans Regular and SemiBold and IBM Plex Mono Regular, SIL OFL 1.1, with
upstream URLs and SHA-256 digests recorded in `THIRD_PARTY_NOTICES.md` beside the reasoning that
chose them. Embedded into the binary rather than loaded from a data directory: a tool that cannot
draw text until it finds a file shows a blank window when somebody moves the executable, and text
is not optional content.

**Icons: done, and no third-party asset was needed.** The decision recorded below was taken, and
`STUDIO-04008` implements it: twenty-three icons drawn as vector paths. There is nothing to licence
because there is nothing vendored. The obvious route is to vendor an icon font, which costs another ~200 KB, another
licence, and a set whose visual language was designed for somebody else's product. The alternative
is to *draw* Studio's two dozen editor icons as vector paths in code, over the primitives the draw
list already has: no third-party asset, no licence question, perfectly crisp at every DPI scale
rather than at the sizes somebody baked, and a visual language that is Studio's own. That is the
intended route unless it proves impractical

### `STUDIO-04016` — Cull geometry that lies entirely outside the clip in force

**Acceptance.** A primitive with no overlap with the current clip emits no vertices at all, rather
than being left for the scissor test to discard. A virtualised list that describes a thousand rows
to show twenty must not fill a vertex buffer with 980 invisible ones — and "is this row actually
hidden" becomes a question a headless test can answer

**Verification.** `tests/StudioFrameTests.cpp`: a widget clipped away is neither hovered nor drawn

### `STUDIO-04012` — Render-target composition for the viewport panel

**Acceptance.** Accounts for CNA gap G-03's sampling origin in exactly one place

**The scene appears in the native shell.** Until this, the viewport panel drew a grid and nothing
else — the shell links no CNA and cannot render a scene. Whoever owns the graphics device renders it
into an offscreen target and hands the shell the texture id; the shell composites it across the
viewport body and knows nothing else about it — not the renderer, not the camera, not what is in it.
That is the same `UiTextureId` seam the font atlas already used, so the CNA renderer needed no new
concept at all.

**The flip is one parameter, passed by the one thing that knows.** CNA does not normalise the
sampling origin of a render target or publish the convention (gap G-03), so the viewport says which
it is on and `drawImage` swaps the texture coordinates rather than the geometry — which keeps the
rectangle's layout, hit-testing and clipping untouched. The shell does not and must not know which
renderer it is running on.

**Sized from the previous frame's rectangle.** The shell decides the viewport's rectangle while it
describes a frame, and the render has to happen before that — so a resize shows the scene stretched
for one frame rather than anything a user would name. Docking the viewport away clears the texture
rather than leaving the last picture up: a stale viewport is worse than an empty one, because it
looks live.

**Verification.** `TheViewportCompositesASceneWhenOneIsHandedToItAndTheGridWhenNot` covers the
CNA-free half — the texture reaching the draw data, and the placeholder returning when it is taken
away. On a real device, `CnaStudioNativeShellCompositesTheScene` asserts on the words *compositing
the scene* rather than on a screenshot, because a viewport drawing its grid and one drawing the
scene produce the same draw-call count and the same perfectly valid picture

### `STUDIO-04011` — Window resize handling without artefacts

**Acceptance.** Resizing the window changes what is on screen and nothing else. No stretched frame,
no stale geometry, and no silent edit to the arrangement.

**The arrangement half is done; the device half waits on `STUDIO-04010`.**

*The arrangement.* Docked panels resize proportionally because a split stores a fraction rather than
a pixel width, which was right from the start. Floats store units, because a palette that grew with
the window would be a palette nobody could keep the size of — and they are clamped into the
workspace so that a window saved at (3000, 1800) on a large display is still reachable on a laptop.

The clamp wrote itself back into the position it was clamping. So a window briefly dragged small —
or a laptop lid opened beside a large display — carried every float into the corner and **left them
there** when it grew again. The user did not move those palettes, and finding them moved is the kind
of thing people stop trusting a saved layout over. Resizing is not an edit; dragging is. The clamp
now resolves into `bounds` and leaves the window's own fields alone, so they hold the *intent* and
what is drawn is the intent clamped.

That needed a matching change at the other end. While the workspace is small, what the user grabs is
the clamped rectangle, and a drag anchored on the un-clamped request would throw the window
off-screen on the first pixel of movement. So a press on a float's title bar or corner grip adopts
its visible position and size as the new intent first: grabbing a window means "it is here now".

Tested at both levels, because they fail differently — the tree, where the round-trip is arithmetic,
and the shell, where the drag is a gesture. Both were checked by restoring the destructive clamp.

*The device.* A back buffer recreated under the renderer, and the render targets the viewport panel
holds, are `STUDIO-04010`. The scene target is already re-created on every panel resize, one frame
behind the layout that decides its size — which shows as the scene stretching for a single frame
rather than as anything a user would name, and is recorded here rather than left to be rediscovered.

### `STUDIO-04013` — Screenshot and readback support for visual testing

**Acceptance.** A scripted run can capture the frame it drew, so a test can assert on the picture
rather than on the process having survived.

**Done, and recorded late.** All three hosts — the native shell, the Dear ImGui editor and
`cna-player` — read back the device's back buffer with `GetBackBufferData` and write a PNG, gated on
a frame limit because there is no final frame without one. The capture reports failure honestly:
`screenshotAttempted` and `screenshotWritten` are two flags rather than one, because setting
"written" in the failure path makes a failed capture report success and silently defeats the
assertion. Eleven CTest cases rest on it.

**Reach forbids readback**, so the hosts ask for the HiDef profile. A player previewing a
Reach-profile game is a different question and belongs to the target profile (`STUDIO-17007`).

### `STUDIO-04015` — Per-renderer smoke test: draw a reference panel and assert non-empty output

**Acceptance.** Every renderer that satisfies the host contract draws the reference panel, and the
output is asserted to be non-empty rather than merely present.

**The "non-empty output" half is done; the "per-renderer" half is blocked.**

*Non-empty output.* The graphical cases asserted on counts — draw calls, triangles, rows — and the
comment above one of them said the screenshot *is* the test. It was not: a file appears for a blank
window too. Counts separate a shell that submitted geometry from one that submitted none, and say
nothing about a frame whose geometry rendered to nothing — a wrong blend state, a clip rectangle
that excludes the window, a vertex colour with no alpha, or a renderer quietly dropping the calls
all report every draw call and produce a blank picture. So `--screenshot-min-colors=N` fails the run
when the captured frame holds fewer than N distinct colours, and every capturing case in CI passes
16. A blank frame has one colour, or two where something was cleared to another shade; a real shell
frame has over a thousand, because antialiased text alone spreads at every glyph edge.

The count runs on the *captured pixels before the file is written*, so a blank capture does not also
leave a picture behind for somebody to look at and believe. It counts packed values rather than
channels, because the question is only whether two texels differ and packing is a fixed permutation
of the same bits — an assumption about channel order would be invisible if it were wrong.

*Per-renderer.* Still one renderer: CI builds `SOFTWARE`, because it needs no GPU and no display.
The rest is `docs/CNA-GAPS.md` G-10 — the renderers that can host Studio need a display and
undocumented sibling checkouts — and is the reason this task is 🔄 rather than ✅.

### `STUDIO-04019` — Font fallback, so text outside the shipped faces is readable rather than boxes

**Acceptance.** A scene, asset or entity named in Chinese, Japanese, Korean, Thai or Devanagari
reads as itself in the outliner, the content browser and the inspector, rather than as a row of
replacement boxes.

**Found by finishing `STUDIO-03026`.** The caret model steps over CJK, Hangul and emoji correctly —
that is what the cluster rules are for — and the shipped IBM Plex faces have no outlines for any of
them, so what a user sees is a row of ◇. The model being right and the glyph being absent are
different failures, and it was worth seeing both on screen to tell them apart.

This is not a Unicode bug and not a CNA gap. It is the cost of embedding the faces rather than
loading them, which `STUDIO-04009` chose deliberately: a tool that cannot draw text until it finds
a file shows a blank window when somebody moves the executable. A full CJK face is several
megabytes, which is a different trade from the ~400 KB of Latin, so this is its own decision rather
than "add another font to the list".

**What it needs.** A face list per typeface rather than one face, a per-code-point lookup that
falls through it, and a source for the fallback faces — either the platform's own (which needs a
seam, because a CNA-free build has no platform) or a vendored subset. The atlas already keys glyphs
by face, so the packing and the growth need no change. The replacement box stays as the last
resort, because a box is still better than a gap.

### `STUDIO-04020` — Guard test: every key Studio can ask about is one the host reports

**Acceptance.** The platform's key map is checked against `UiKey`: every key in the vocabulary has
exactly one mapping, and no host key is bound twice

**The failure it catches is the quietest one in the input path.** A key Studio can ask about and the
platform never maps is a shortcut that does not fire — nothing on screen, no error anywhere. It is
also the easiest to introduce: adding a key to the enumeration is one edit and mapping it is
another, in a different file, in the one module that does not build without a CNA checkout

**It found two.** `Digit2` and `Digit3`, the 2D/3D view toggles, which the ImGui path mapped and the
native one did not. Nothing was visibly broken, because nothing in the native shell binds them yet
— which is exactly how this class of gap survives until somebody does

**What it needed.** The test binary did not link the CNA-linked module at all, so the key map, the
capability bridge and the shell host had no unit coverage — only the window smoke tests, where a
wrong mapping shows up as a shortcut that quietly does nothing. It links it now, in the CNA-backed
configuration

**Verification.** `EveryKeyStudioCanAskAboutIsOneTheHostCanReport`, confirmed to fail on the two
missing bindings before they were added

### `STUDIO-04008` — Icons, drawn as vector paths rather than sampled

**Acceptance.** Every icon Studio draws, authored once and crisp at any size and DPI scale, with no
vendored asset and no second atlas

**How.** Each path is written on a 0..16 grid and mapped onto whatever rectangle it is asked for.
Stroke widths scale with the icon and are floored at one *physical* pixel, because a hairline that
rounds to zero is a hairline that disappears. That one definition is what serves a 14-pixel toolbar
and a 32-pixel one at 200%

**Arcs are ribbons, not thick polylines.** A polyline of thick segments gives each segment its own
quad, so consecutive ones overlap on the inside of the curve and leave a notch on the outside. At a
toolbar's size that reads as a lumpy, hand-drawn stroke, and it is the first thing that makes an icon
set look amateur

**Drawn for the size they are actually used at.** Two icons were redrawn after looking at them at
1x: `Scale` lost the diagonal arrow joining its two squares, because at sixteen pixels the small
square is three pixels and the arrow is a smudge; and `Build` stopped being a symmetrical mallet,
because a bar with a stalk under its middle is a letter T

**The toolbar shows icons alone.** The label still decides the button's identity and is still what a
tooltip and a screen reader will read — dropping it from the model to save the space would leave the
toolbar with nothing to say about itself

**Verification.** `EveryIconDrawsSomethingAndNoTwoAreTheSamePicture` compares the geometry each icon
emits: one that draws nothing is a blank button nothing else would notice, and two that draw the
same shape are worse than that, because the user learns to trust a picture that lies about which
command it runs. Plus the name round-trip, and a check that every toolbar command has an icon — one
without would be an empty square once the labels came off

### `STUDIO-04023` — GPU vertex and index buffers for UI geometry

**Acceptance.** A UI frame's geometry reaches the device through `DynamicVertexBuffer` and
`DynamicIndexBuffer`, reused across frames rather than reallocated.

**Done.** Written once per draw list with `SetDataOptions::Discard`, which is the half that matters:
the driver hands back fresh memory rather than waiting for the previous frame's draws to finish
reading the old, so the upload does not stall. `DrawUserIndexedPrimitives` — what the compatibility
backend does — hands the renderer a CPU pointer on every call and the renderer copies it into a
staging buffer it owns, every frame, for every command, whether or not the geometry changed.

**The buffers grow and never shrink**, to the next power of two. A UI's geometry is bounded by the
window and settles within a few frames of a resize; exact fits would reallocate on alternate frames
for a UI oscillating by one quad, and shrinking would reallocate on the next large frame — which is
the one thing this arrangement exists to avoid. `bufferGrowths()` is the count a benchmark asserts
on.

### `STUDIO-04024` — `StudioModernUiRenderer`: draw `UiDrawData` through `ShaderEffect`

**Acceptance.** The UI's pixels come from a shader Studio compiled, not from `BasicEffect`; the
shader is renderer-neutral; nothing in Studio names a graphics backend.

**Done, through a `ShaderPackageEXT` rather than through `ShaderEffect(device, vert, frag)`.** That
constructor takes one GLSL string and would make the Studio UI undrawable on any renderer whose
shading language is not GLSL — the renderer-specific coupling `STUDIO-02033` exists to prevent,
arrived at by writing *less* code rather than more. The package holds every dialect Studio has and
CNA's `selectFor(device)` chooses, asking the live renderer what it implements and producing a
deterministic diagnostic naming every candidate when none is usable. Adding HLSL, MSL or WGSL is one
entry in `studioUiShaderVariants()` and changes nothing else.

**Three defects found by running it**, each of which produced the same symptom — a completely empty
frame — and each of which the `--screenshot-min-colors` assertion caught rather than a count:

- **Uniforms were set before the program was bound.** CNA's uniform setters write to whatever
  program is currently bound, and binding is what `Apply()` does. Setting the projection first sent
  it to whatever program the previous caller left bound — on a frame that drew a scene first, a real
  program, silently taking a matrix meant for this one.
- **The projection was passed row-major.** CNA hands the array straight to the graphics API with no
  transpose and XNA's `Matrix` is row-major; `Matrix::ToColumnMajor` is what every CNA renderer
  uses. The wrong order is not a wrong-looking UI: every vertex lands outside the clip volume.
- **`DynamicVertexBuffer::SetData`'s options overload takes no byte offset.** A compile error rather
  than a defect, but it is the third way the same afternoon produced a black window.

### `STUDIO-04025` — A/B verification: both backends draw the same frame

**Acceptance.** The same frame rendered through both backends is compared by a test rather than by
somebody looking at two screenshots.

**Byte-identical, and the exactness is a finding rather than an assumption.** The task was written
expecting a tolerance — "a fixed-function path and a fragment shader resolve the same triangle's
edge pixels differently" — and that is true across *renderers*. Within one renderer it is not: both
backends submit the same geometry, in the same order, with the same blend, sampler and scissor
state, and only the program and the buffer route differ. Neither changes where a triangle lands or
what colour it is. So the test asserts equality, which is a far stronger contract than any threshold
would have been, and it holds: 1280x720, 126 draw calls, 12664 triangles, identical md5.

**`--ui-renderer=compat`** exists for this, and for the first question anybody asks about something
that draws wrong: does it happen on the other renderer. Without the flag the only way to ask is to
rebuild.

**The test refuses to become a tautology.** Before comparing anything it asserts that each run
actually used the backend it asked for, because the single most likely failure of an A/B comparison
is a host that quietly ran the same backend twice. Confirmed by breaking it: a one-line tint in the
vertex shader fails the comparison with both md5s and both paths named.

### `STUDIO-04026` — Default the native host to the modern backend

**Acceptance.** A host meeting the modern profile draws with the modern renderer, with no flag.

**Done, and the fallback has a second trigger the contract cannot see.** The capability profile says
whether a renderer *can* execute a shader; it cannot say whether it accepted *this* shader. A modern
backend whose program CNA refused would draw a black window on a host the report called capable —
which reads as Studio being broken rather than as one shader being rejected. So `isUsable()` is
checked after `initialize`, and a refusal falls back with CNA's own compile error in the reason.

Verified on `OPENGL4` under Xvfb on Mesa's llvmpipe: the log reads `Modern graphics API: CNA engine
layer 18` and `UI renderer: modern`, and the whole shell — text, icons, the composited scene, the
tinted log rows — is drawn through `ShaderEffect` and GPU buffers.

### `STUDIO-04029` — Make `OPENGL4` under Xvfb a second tested configuration in CI

**Acceptance.** CI builds and tests `OPENGL4` alongside `SOFTWARE`, so the modern UI renderer, the
`needs-display` cases and `CnaStudioUiRenderBackendsAgree` are covered by machinery rather than by
somebody running them.

**Newly possible**, and it was not when the previous session concluded otherwise: `docs/CNA-GAPS.md`
G-10 recorded "there is simply no renderer available that both satisfies Studio's capability
contract and needs a display", and that search stopped at the GL *family*. `OPENGL2`, `OPENGL33` and
`OPENGLES3` are EasyGL and want sibling checkouts nothing names; `OPENGL4` is a separate renderer
and configures from a plain CNA checkout with `libgl1-mesa-dev` alone.

**Why this is the most valuable test task in the phase.** Until now Studio's only automated renderer
was the one renderer its intended UI path cannot run on at all.

**Done.** The `cna` job is a matrix over `SOFTWARE` and `OPENGL4` rather than a second job, so the
twenty-odd steps of checkout, submodules, dependencies and export cannot drift between the two.
`fail-fast: false`, because which renderer failed is the interesting half of the answer.

**With an assertion, because a silent fallback would be a green tick over nothing.** The OPENGL4 leg
exists to cover the *modern* UI render backend; a host that quietly fell back to the classic one
would produce an identical pass while covering exactly what `SOFTWARE` already covers — the failure
this job was added to close. So the leg reads `--host-capabilities` and fails unless the backend it
names is the one that leg is for. **Both** legs are asserted, not only the new one: `SOFTWARE`
reporting `modern` would mean the capability report is claiming a shader path a CPU rasteriser does
not have, which is the class of defect `STUDIO-02070` exists for.

**Two details that would each have made it pass while proving nothing.** The job must not set
`SDL_VIDEODRIVER` — how a windowed case gets a surface is decided per test in `tests/CMakeLists`
from the renderer, and a job-level `dummy` wins over the per-test `DISPLAY` and aborts an OPENGL4
run with a message about SDL rather than about the test. And `CNA_CNAEXT` must be `ON`, because it
is what decides whether the host reports the modern graphics API at all; off, the OPENGL4 leg would
build, pass, and exercise the classic path it exists to stop being the only one tested.

**Measured on this machine before it was written**, end to end under `xvfb-run --server-num=99`:
79 CTest suites, 0 failures, including `CnaStudioUiRenderBackendsAgree` — which only a renderer that
can run both backends can declare at all.
