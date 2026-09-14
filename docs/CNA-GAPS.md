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
prototype. All five were re-verified against the commit above as part of the CNA Studio bootstrap;
two have since been fixed upstream and are kept here, marked closed, so the record stays honest.

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

---

## How to add a gap

A gap is worth filing when Studio cannot do something through CNA's public API that CNA plausibly
ought to support. Record: the affected API, current behaviour, expected behaviour, Studio impact,
the workaround if any, a suggested fix, and the test CNA would need. A gap with no reproduction is
a complaint, not a report.
