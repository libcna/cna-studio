# CNA gaps found by CNA Studio

CNA Studio is one of CNA's largest real-world consumers, and finding CNA's rough edges is a
deliverable of that relationship rather than a side effect. This document is the register.

**CNA Studio does not fix CNA.** When Studio hits a CNA deficiency it is recorded here, worked
around in Studio where a safe workaround exists, and left for CNA's own maintainers and test
suite. Studio never reaches into `CNA::Internal::*`, and never adds renderer-specific code to
normal Studio modules to paper over a gap — doing either would hide the problem instead of
reporting it.

## Audit basis

| Repository | Branch | Commit | Audited |
|------------|--------|--------|---------|
| `libcna/cna` | `next` | `e05b3d0f026e0926741f89459daf02579240399d` | 2026-09-14 |
| `libcna/sharp-runtime` | `next` | `0c82d9b888bdf5f7d5663c77942f339bcb2a7445` | 2026-09-14 |

The gaps numbered G-01 … G-05 were recorded against an **older** CNA revision by the CNA Editor
prototype; G-06 through G-09 are new, filed by CNA Studio. All five of the inherited ones were
re-verified against the commit above as part of the CNA Studio bootstrap; two have since been fixed
upstream and are kept here, marked closed, so the record stays honest.

## Status legend

| Symbol | Meaning |
|--------|---------|
| 🔴 | Open — confirmed against the audited commit |
| 🟡 | Narrowed — the blocking part is fixed, something smaller remains |
| ✅ | Closed — fixed upstream, verified against the audited commit |

---

## ✅ G-01 — `Color` had no default constructor

**Closed.** `Microsoft::Xna::Framework::Color` now declares `Color();`
(`modules/math/include/Microsoft/Xna/Framework/Color.hpp:375`), documented explicitly in terms of
.NET's `default(Color)`.

`std::vector<Color>::resize(n)` compiles. The behavioural difference from XNA that this gap
described is gone.

---

## 🔴 G-02 — `CNA::Devices::Clipboard` is behind a default-off option

| Field | Value |
|-------|-------|
| Affected API | `CNA::Devices::Clipboard` |
| Current behaviour | The whole devices layer is inside `CNA_DEVICES`, which is `option(... OFF)` at `CMakeLists.txt:123` |
| Expected behaviour | A tooling-grade consumer can rely on a clipboard being present, or can detect its absence before the user tries to paste |
| Studio impact | Copy/paste in text fields does nothing on a default CNA build |
| Workaround | `CnaUiPlatform::hasClipboard()` reports the absence and Studio degrades visibly rather than silently; documentation tells Studio builders to pass `-DCNA_DEVICES=ON` |
| Suggested fix | Either default the option on for desktop targets, or document that tooling consumers require it — the current state makes a *silently* less capable build the default |
| Test needed in CNA | A build matrix entry that exercises the clipboard path |

---

## 🔴 G-03 — Render-target sampling origin is not normalised across renderers

| Field | Value |
|-------|-------|
| Affected API | `RenderTarget2D` sampled as a `Texture2D` |
| Current behaviour | Some renderers present a sampled render target vertically flipped relative to others |
| Expected behaviour | Either the sampling origin is normalised by CNA, or the convention is stated in the public API so a consumer can compensate deterministically |
| Studio impact | The viewport panel composites the scene through a render target. Getting this wrong shows the scene upside down — a total failure that looks like a Studio bug |
| Workaround | A compile-time per-renderer constant in Studio's viewport. This is exactly the kind of renderer-specific knowledge Studio should not hold, and it is isolated to one function so it can be deleted in one edit when CNA settles the convention |
| Suggested fix | Normalise in the renderers, or publish the convention as a queryable property |
| Test needed in CNA | A cross-renderer test that renders a known asymmetric pattern to a target, samples it, and compares |

**Note.** This gap predates the renderer registry rewrite and the audited commit has a far larger
renderer set than when it was filed. Its restatement above is deliberately renderer-agnostic;
re-measuring which of the 50 registered renderer identities are actually affected is
`STUDIO-02010`.

---

## 🟡 G-04 — No public path from a font file to a `SpriteFont`

**Narrowed.** The blocking half is fixed: `SpriteFont` now has a **public** `CNAEXT` constructor
taking the atlas texture and the four glyph tables directly
(`modules/graphics/include/Microsoft/Xna/Framework/Graphics/SpriteFont.hpp:44`), and matching
`getGlyphBoundsEXT()` / `getCroppingEXT()` / `getKerningEXT()` accessors that round-trip it. A
consumer can now construct a `SpriteFont` without touching `CNA::Internal::Xnb::SpriteFontReader`.

| Field | Value |
|-------|-------|
| What remains | There is no public glyph **rasterizer** or atlas builder in CNA. A consumer that has a `.ttf` and wants a `SpriteFont` must rasterize the glyphs itself |
| Studio impact | Moderate, and it cuts both ways. Studio cannot preview an arbitrary `.spritefont` asset without its own rasterizer — but Studio's *own* UI needs a font atlas it controls anyway, so building one is work Studio wants to own regardless |
| Workaround | Studio builds and owns its UI font atlas, and constructs `SpriteFont` through the now-public constructor |
| Suggested fix | A public font-atlas builder in CNAEXT, or a public `ContentManager::Load<SpriteFont>` specialisation for the `.xnb` path |

---

## 🔴 G-05 — `PbrEffect` draws nothing on some renderers, and reports no error

| Field | Value |
|-------|-------|
| Affected API | `PbrEffect` (`modules/graphics/src/Xna/PbrEffect.cpp`) |
| Current behaviour | On at least one renderer it constructs without throwing, accepts every parameter, issues its draw calls, and puts no pixels on screen, while `BasicEffect` renders the same geometry, matrices and lights correctly in the same frame |
| Expected behaviour | Either it draws, or it refuses — silence is the failure mode that costs the most time |
| Studio impact | Studio's model pass cannot use PBR where this reproduces, which blocks the material authoring workstream from previewing what it authors |
| Secondary defect (same investigation) | `PbrEffect::FillGpuDrawParams` sets `textureEnabled = true` unconditionally while binding `texture0` only when a texture exists, so a material with a base-colour *factor* and no map samples an unbound texture |
| Suggested fix | Bind no texture **and** clear `textureEnabled` when there is none; and check whether the affected renderer's PBR path draws at all |
| Test needed in CNA | A per-renderer test that draws one lit triangle through `PbrEffect` and asserts the framebuffer is not empty |

**Re-audit status.** The secondary defect was still visible in the audited source. The primary
"draws nothing" symptom was originally observed on a renderer that predates the registry rewrite,
and has not been re-measured against the current renderer set. Re-measuring it is `STUDIO-02011`.

---

## 🔴 G-06 — Studio-host capability requirements have no single queryable answer

**New, filed by CNA Studio.**

| Field | Value |
|-------|-------|
| Affected API | `GraphicsDevice::GetRendererCapabilityProfileEXT()`, `CNA::RendererFeature` |
| Current behaviour | CNA exposes an excellent runtime capability model — 32 atomic `RendererFeature` entries with a four-state `Supported`/`Restricted`/`Unsupported`/`Unknown` answer, 22 numeric limits, and per-`SurfaceFormat` usage masks. What it does not have is a way to ask "does this device satisfy *this set* of requirements", and `Unknown` is common: a feature the renderer has simply not classified |
| Expected behaviour | A consumer with a fixed requirement set should be able to evaluate it in one call and receive a structured list of what is missing, with `Unknown` distinguishable from `Unsupported` |
| Studio impact | Studio must decide at start-up whether the compiled renderer can host its UI, and must explain precisely why not when it cannot. Studio implements this itself in one module (`StudioHostRequirements`) rather than scattering the knowledge |
| Workaround | Studio owns the requirement set and evaluates it against the profile. This is arguably the right place for it — the requirement is Studio's, not CNA's — so this may be better framed as a request for a small helper than a defect |
| Suggested fix | A `RendererCapabilityProfile::Evaluate(std::span<const RendererFeature>)` returning the unmet and unknown subsets |

**Studio treats `Unknown` as not-satisfied for required features**, and says so in its diagnostic
rather than assuming the best. A tool that starts and then fails to draw is worse than one that
refuses with a reason.

**Implemented in Studio, and the gap stands.** `CNA/Studio/Project/StudioHostRequirements.hpp`
owns the requirement set and evaluates it; `CNA/Studio/Viewport/CnaCapabilityBridge.hpp` reads a
live profile into it. Building it confirmed the workaround is practical and confirmed the shape of
the missing helper: what Studio wrote is a general evaluation over a requirement set, with nothing
Studio-specific in the mechanism — only in the *membership* of the set. That is the part that
belongs to the consumer; the evaluation is the part that does not. Re-framing this as a request for
`RendererCapabilityProfile::Evaluate(std::span<const RendererFeature>)` returning the unmet and
unknown subsets is therefore right, and every consumer that needs it will otherwise write the same
loop.

---

## 🔴 G-07 — `GetBackBufferData` is unavailable under the Reach profile, and that is only discoverable by trying

**New, filed by CNA Studio.**

| Field | Value |
|-------|-------|
| Affected API | `GraphicsDevice::GetBackBufferData`, `GraphicsProfile` |
| Current behaviour | Under `GraphicsProfile::Reach` — the default a `GraphicsDeviceManager` starts with — `GetBackBufferData` throws `"GetBackBufferData is not supported by the Reach graphics profile"` |
| Expected behaviour | Reasonable as an XNA-compatible restriction. What is missing is a way to ask *before* calling: nothing in the capability model reports it, so a tool discovers it by catching an exception at the moment it wanted the pixels |
| Studio impact | Every screenshot, every golden image and the whole renderer-comparison harness are built on this call. Studio now requests `HiDef` explicitly, which fixes it — but only after the failure had been observed |
| Workaround | Request `GraphicsProfile::HiDef` at device creation. Studio does |
| Suggested fix | Report profile-gated operations through `RendererCapabilityProfile`, so a consumer can check rather than catch |
| Test needed in CNA | A test asserting the capability answer matches the actual behaviour under both profiles |

**Note on how this was found.** It was invisible for a different reason first: Studio's own host
set its "screenshot written" flag inside the exception handler, so a failed capture reported
success and the process exited zero having written nothing. That was a Studio bug, fixed here, and
it is worth recording because it is the exact failure mode the graphical smoke tests exist to
prevent — the file appearing *is* the assertion, and a flag that lies about it makes the test pass
while proving nothing.

---

## 🔴 G-08 — Which renderers a target can build is stated only as CMake conditions

**New, filed by CNA Studio.**

| Field | Value |
|-------|-------|
| Affected API | `cmake/RendererSelection.cmake`; no runtime or build-time query |
| Current behaviour | CNA knows precisely which of its 50 renderers can be built for which operating system — `DIRECTX*`, `DIRECT2D`, `GLIDE` and `GDI` are Windows-only; `CANVAS`, `HTML_DOM`, `SVG_DOM`, `PIXIJS`, `WEBGL1` and `WEBGL2` are Emscripten-only; `MAGNUM` and the desktop GL profiles are the reverse; `NANOVG` and `RLGL` are desktop-only; `GLIDE` additionally needs a 32-bit x86 ABI. Every one of those is a `FATAL_ERROR` inside a CMake `if`, reachable only by attempting a configure |
| Expected behaviour | A consumer building a target-selection UI can ask, without configuring anything, which renderers are valid for a given platform and architecture — a generated table, a queryable CMake target property, or a small JSON manifest beside the registry |
| Studio impact | Studio's Build panel offers target profiles, and offering a combination CNA will refuse is a build that fails minutes later with a message about a missing header. So Studio **transcribes** the gates from `RendererSelection.cmake` into `CNA/Studio/Project/TargetProfile.cpp` — a second copy of CNA's own rules, which is exactly what `docs/ARCHITECTURE.md` says Studio should not have to hold |
| Workaround | The transcription, plus `STUDIO-29007`: a guard test that reads CNA's `RendererSelection.cmake` and fails when a renderer CNA gates is one Studio still offers. It runs only in the CNA-backed configuration, because it needs a CNA checkout to read |
| Suggested fix | Emit the identity → allowed-system map from `RendererRegistry.cmake`, the same way the renderer descriptor table is already generated. The data exists; only the export is missing |
| Test needed in CNA | A test asserting the generated map agrees with the `FATAL_ERROR` gates, so the two cannot drift |

**Note on severity.** This is the mildest of the open gaps and the most annoying to live with. Nothing
is broken; the information is simply not reachable except by reading CMake, so every consumer that
needs it writes the same table and each one rots independently.

---

## 🔴 G-09 — Consuming CNA as a subdirectory builds its tests and examples, and its examples cannot build that way at all

**New, filed by CNA Studio.**

| Field | Value |
|-------|-------|
| Affected API | `CMakeLists.txt` (`CNA_BUILD_TESTS`, `CNA_BUILD_EXAMPLES`); `modules/graphics/examples/CMakeLists.txt` |
| Current behaviour | Both options default `ON` with no top-level-project guard, so `add_subdirectory(cna)` from a consuming project builds CNA's entire test suite and every example program as part of that project's build. Neither can succeed in the default case: the tests `FATAL_ERROR` on a checkout whose `vendor/googletest` submodule is not initialised, and the examples resolve their content-staging helper as `${CMAKE_SOURCE_DIR}/cmake/CopyDirectoryLocked.cmake` — which, from a subdirectory, is the **consuming project's** root, not CNA's, so the custom command fails with `Not a file` |
| Expected behaviour | A project that consumes CNA gets CNA. `CNA_BUILD_TESTS` and `CNA_BUILD_EXAMPLES` default to `ON` only when CNA is the top-level project (CMake has `PROJECT_IS_TOP_LEVEL` for exactly this), and anything CNA's own targets reference inside its tree is addressed through `CNA_SOURCE_DIR` or `CMAKE_CURRENT_SOURCE_DIR` rather than `CMAKE_SOURCE_DIR` |
| Studio impact | Every game CNA Studio exports consumes CNA this way — that is what a CNA game *is*. An exported project that did not know to turn both options off would not configure on a fresh CNA clone, and the message it failed with would name googletest, which has nothing to do with the game |
| Workaround | The generated `CMakeLists.txt` sets `CNA_BUILD_TESTS OFF` and `CNA_BUILD_EXAMPLES OFF` as cache entries, with a comment saying why, and `STUDIO-02051` builds an exported project on every CNA-backed run so a regression here fails a test rather than a user's first build |
| Suggested fix | `option(CNA_BUILD_TESTS "..." ${PROJECT_IS_TOP_LEVEL})` and the same for examples; replace `CMAKE_SOURCE_DIR` with `CNA_SOURCE_DIR` in `modules/graphics/examples/CMakeLists.txt` (two occurrences, one of them a Python test script path) |
| Test needed in CNA | A CI leg that configures a trivial consumer project which does nothing but `add_subdirectory(cna)` and link `CNA`. It would have caught both halves of this, and it is the configuration every downstream user is in |

**How it was found.** By building an exported game rather than reading it. `STUDIO-02051` exports the
example project, configures it with nothing but CMake and a CNA checkout, compiles it and runs it.
Both halves of this gap stopped that build, and neither is visible in the exported tree.

---

## 🟡 G-10 — The renderers that can host Studio and need a display all need undocumented sibling checkouts

**New, filed by CNA Studio.**

| Field | Value |
|-------|-------|
| Affected API | `CNA_GRAPHICS_RENDERER`; `cmake/RendererSelection.cmake` |
| Current behaviour | Of the renderers that configure from a plain CNA + sharp-runtime checkout, `SOFTWARE` and `HEADLESS` need no display and are what CI uses; `SDL_RENDERER` needs one but cannot host Studio (it reports no `ThreeDimensionalPipeline` and no `DepthStencilBuffer`); `SDL_GPU` and `VULKAN` configure but need a Vulkan ICD, which a bare Linux runner does not have. Every OpenGL family — `OPENGL2`, `OPENGL33`, `OPENGLES3` — fails at configure time asking for an `easy-gl` sibling checkout, which in turn asks for a `meta-gl` sibling of its own. Neither is a submodule and neither is named anywhere a consumer would look before trying |
| Expected behaviour | The renderer list in `CNA_GRAPHICS_RENDERER`'s cache docstring says which renderers a given checkout can actually build, or CNA documents the sibling checkouts each family needs where the option is declared. Failing at configure time with a clear message is already much better than most; what is missing is being able to find out *first* |
| Studio impact | `STUDIO-33010` — graphical CI on a renderer that needs a real graphics context — is blocked on this rather than on Studio. With Xvfb running and `CNA_STUDIO_TEST_DISPLAY` pointed at it, the fourteen `needs-display` ctests appear and run; there is simply no renderer available to them that both satisfies Studio's capability contract and needs a display |
| Workaround | None from Studio's side. The CI job for `STUDIO-33010` needs `easy-gl` and `meta-gl` checked out beside CNA, plus Mesa's software GL and Xvfb, and is a shopping list rather than a code change |
| Suggested fix | Extend `G-08`'s answer: whatever CNA grows to report which renderers a target can build should also report which of them this checkout has the sources for. The information exists at configure time — the message that refuses `OPENGLES3` proves it |
| Test needed in CNA | A CI leg that configures each renderer the docstring advertises from a clean checkout and asserts that it either configures or refuses with a message naming what is missing |

**How it was found.** By trying to close `STUDIO-33010` rather than reasoning about it. The display
plumbing turned out to be the easy half: `CNA_STUDIO_TEST_DISPLAY` already exists, Xvfb works, and
the fourteen labelled tests appear the moment a renderer that needs a display is configured. What
does not exist is such a renderer.

**And one thing that worked.** `SDL_RENDERER` builds a complete Studio, and Studio refuses to start
on it — naming `ThreeDimensionalPipeline` and `DepthStencilBuffer`, saying what each is for, and
telling the user to build against a renderer that satisfies them. That is the host capability
contract (`STUDIO-02021`) exercised against a real inadequate renderer for the first time rather
than against a synthetic capability set, and it behaved exactly as designed: no renderer names, no
whitelist, a reason per requirement.

---

## How to add a gap

A gap is worth filing when Studio cannot do something through CNA's public API that CNA plausibly
ought to support. Record: the affected API, current behaviour, expected behaviour, Studio impact,
the workaround if any, a suggested fix, and the test CNA would need. A gap with no reproduction is
a complaint, not a report.
