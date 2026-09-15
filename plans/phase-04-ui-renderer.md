# Phase 4 — CNAEXT UI renderer

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-04001` … `STUDIO-04999` and are never reused.

**Purpose.** Draw the Studio UI through CNA's public modern graphics API: text, icons, batching, clipping, render resources and DPI, with no renderer-specific code.

**Exit criteria.** The UI draws correctly and efficiently on every renderer that satisfies the host capability contract, with one implementation.

**Progress:** 14 of 20 complete `████████░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-04001` | Create the `cna-studio-ui-renderer` module inside the CNA-linking boundary | ⬜ | `STUDIO-03014`, `STUDIO-02020` |
| `STUDIO-04002` | Vertex and index buffer management for UI geometry | ✅ | `STUDIO-04001` |
| `STUDIO-04003` | Draw-call batching by texture, clip rectangle and blend state | ✅ | `STUDIO-04002` |
| `STUDIO-04004` | Scissor-based clipping, including nested clip stacks | ✅ | `STUDIO-04002` |
| `STUDIO-04005` | Font atlas construction and glyph rasterization | ✅ | `STUDIO-04001` |
| `STUDIO-04006` | Text rendering with kerning, and correct baseline and line metrics | ✅ | `STUDIO-04005` |
| `STUDIO-04007` | Dynamic glyph upload without frame stalls or dropped glyphs | ✅ | `STUDIO-04005` |
| `STUDIO-04008` | Icons, drawn as vector paths rather than sampled | ✅ | — |
| `STUDIO-04009` | Select and document legally redistributable fonts and icons | ✅ | — |
| `STUDIO-04010` | Render-resource lifetime and recreation on device loss | ⬜ | `STUDIO-04002` |
| `STUDIO-04011` | Window resize handling without artefacts | ⬜ | `STUDIO-04010` |
| `STUDIO-04012` | Render-target composition for the viewport panel | ✅ | `STUDIO-04004` |
| `STUDIO-04013` | Screenshot and readback support for visual testing | ⬜ | `STUDIO-04001` |
| `STUDIO-04014` | Rounded rectangles, borders and separators as first-class primitives | ✅ | `STUDIO-04002` |
| `STUDIO-04015` | Per-renderer smoke test: draw a reference panel and assert non-empty output | ⬜ | `STUDIO-04013` |
| `STUDIO-04016` | Cull geometry that lies entirely outside the clip in force | ✅ | `STUDIO-04004` |
| `STUDIO-04017` | Upload only the changed region of the atlas | ✅ | `STUDIO-04005` |
| `STUDIO-04018` | Grow or evict when the glyph atlas fills | ✅ | `STUDIO-04005` |
| `STUDIO-04019` | Font fallback, so text outside the shipped faces is readable rather than boxes | ⬜ | `STUDIO-04005` |
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
