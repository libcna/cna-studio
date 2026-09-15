# Phase 4 — CNAEXT UI renderer

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-04001` … `STUDIO-04999` and are never reused.

**Purpose.** Draw the Studio UI through CNA's public modern graphics API: text, icons, batching, clipping, render resources and DPI, with no renderer-specific code.

**Exit criteria.** The UI draws correctly and efficiently on every renderer that satisfies the host capability contract, with one implementation.

**Progress:** 8 of 19 complete `█████░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-04001` | Create the `cna-studio-ui-renderer` module inside the CNA-linking boundary | ⬜ | `STUDIO-03014`, `STUDIO-02020` |
| `STUDIO-04002` | Vertex and index buffer management for UI geometry | ✅ | `STUDIO-04001` |
| `STUDIO-04003` | Draw-call batching by texture, clip rectangle and blend state | ✅ | `STUDIO-04002` |
| `STUDIO-04004` | Scissor-based clipping, including nested clip stacks | ✅ | `STUDIO-04002` |
| `STUDIO-04005` | Font atlas construction and glyph rasterization | ✅ | `STUDIO-04001` |
| `STUDIO-04006` | Text rendering with kerning, and correct baseline and line metrics | ✅ | `STUDIO-04005` |
| `STUDIO-04007` | Dynamic glyph upload without frame stalls or dropped glyphs | 🔄 | `STUDIO-04005` |
| `STUDIO-04008` | Icon atlas with DPI-appropriate variants | ⬜ | `STUDIO-04005` |
| `STUDIO-04009` | Select and document legally redistributable fonts and icons | 🔄 | — |
| `STUDIO-04010` | Render-resource lifetime and recreation on device loss | ⬜ | `STUDIO-04002` |
| `STUDIO-04011` | Window resize handling without artefacts | ⬜ | `STUDIO-04010` |
| `STUDIO-04012` | Render-target composition for the viewport panel | ⬜ | `STUDIO-04004` |
| `STUDIO-04013` | Screenshot and readback support for visual testing | ⬜ | `STUDIO-04001` |
| `STUDIO-04014` | Rounded rectangles, borders and separators as first-class primitives | ✅ | `STUDIO-04002` |
| `STUDIO-04015` | Per-renderer smoke test: draw a reference panel and assert non-empty output | ⬜ | `STUDIO-04013` |
| `STUDIO-04016` | Cull geometry that lies entirely outside the clip in force | ✅ | `STUDIO-04004` |
| `STUDIO-04017` | Upload only the changed region of the atlas | ⬜ | `STUDIO-04005` |
| `STUDIO-04018` | Grow or evict when the glyph atlas fills | ⬜ | `STUDIO-04005` |
| `STUDIO-04020` | Guard test: every key Studio can ask about is one the host reports | ✅ | — |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-04001` — Create the `cna-studio-ui-renderer` module inside the CNA-linking boundary

**Acceptance.** Consumes `UiDrawData` and produces pixels; contains no UI toolkit header, preserving the seam that keeps the core CNA-free

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

**In progress.** The **dropped-glyph** half is done and tested: the upload request is emitted at
*end of frame* rather than at the start of the draw pass, so a glyph rasterised at any point in the
frame — by a measurement during layout, or by a widget that draws text nothing measured — still
reaches the renderer before the quad that samples it, because texture requests are applied ahead of
every draw command. The **stall** half is not: a dirty atlas re-uploads all four megabytes rather
than the changed region (`STUDIO-04017`), and a full atlas drops glyphs and counts them rather than
growing (`STUDIO-04018`)

### `STUDIO-04017` — Upload only the changed region of the atlas

**Acceptance.** A frame that rasterised one new glyph uploads that glyph's rectangle, not the whole
texture. The atlas settles within a few frames of start-up, so this is a start-up cost rather than a
steady-state one — which is why it is a follow-up rather than part of `STUDIO-04005`

### `STUDIO-04018` — Grow or evict when the glyph atlas fills

**Acceptance.** Running out of atlas stops being a silent loss. Today a glyph that will not fit is
counted in `droppedGlyphs()` and not drawn, which is honest but is still text that stops appearing
partway down a panel

### `STUDIO-04007` — Dynamic glyph upload without frame stalls or dropped glyphs

**Acceptance.** Glyphs first needed on a frame that does not draw are still uploaded — the prototype shipped this bug once (legacy ED-119) and it must not return

**Verification.** A regression test for the update/draw phase split

### `STUDIO-04009` — Select and document legally redistributable fonts and icons

**Acceptance.** Licences recorded in `THIRD_PARTY_NOTICES.md`; no proprietary third-party assets of any kind

**Fonts: done.** IBM Plex Sans Regular and SemiBold and IBM Plex Mono Regular, SIL OFL 1.1, with
upstream URLs and SHA-256 digests recorded in `THIRD_PARTY_NOTICES.md` beside the reasoning that
chose them. Embedded into the binary rather than loaded from a data directory: a tool that cannot
draw text until it finds a file shows a blank window when somebody moves the executable, and text
is not optional content.

**Icons: not done**, and the decision that shapes `STUDIO-04008` is recorded here so it is not
re-litigated. The obvious route is to vendor an icon font, which costs another ~200 KB, another
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
