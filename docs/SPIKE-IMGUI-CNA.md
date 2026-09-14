# Spike ED-100 — Can Dear ImGui render through CNA's *public* API?

> **Result: yes, completely.** Every capability an immediate-mode UI needs is present in
> `Microsoft::Xna::Framework::*` and `CNA::*`. No `CNA::Internal::*` header is used, no shader is
> authored, and no per-backend code exists — one implementation serves every backend CNA can host
> the editor UI on.
>
> This closes ANALYSIS.md question **Q-01** and is the concrete evidence for decision **D-01**
> ("the editor uses CNA's public API only").
>
> **Verified against** `openeggbert/cna` at `ac3aaae` and `openeggbert/sharp-runtime` at
> `b797928`, by compiling `src/viewport/CnaUiRenderer.cpp` and `src/viewport/CnaUiPlatform.cpp`
> against the real headers with `-std=c++23 -Wall -Wextra`. Not a paper exercise.

---

## 1. Why this mattered

The alternative to a public-API renderer was one ImGui backend per CNA graphics backend — seven of
them for the *Editor Supported* tier alone, each needing its own shader, buffer management and
scissor handling, each a place for the editor to render differently from the game.

Writing the renderer against `SpriteBatch`-level primitives instead makes it backend-independent
*by construction*. If CNA can draw the editor's UI, it can draw anything a 2D game needs; if it
could not, that would be a gap in CNA worth finding, which is the whole point of D-01.

## 2. Capability mapping

| An immediate-mode UI needs | CNA public API | Status |
|---|---|:--:|
| Textured, vertex-coloured triangle lists | `GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType::TriangleList, const VertexPositionColorTexture*, int, int, const uint16_t*, int, int)` | ✅ Exact overload exists |
| Pixel-space orthographic projection | `Matrix::CreateOrthographicOffCenter` | ✅ |
| Unlit, textured, vertex-coloured shading | `BasicEffect` with `VertexColorEnabled`, `setTextureEnabledProperty`, `setLightingEnabledProperty(false)` | ✅ No custom shader needed |
| Font atlas creation | `Texture2D(GraphicsDevice&, int, int)` + `SetData(const Color*, int)` | ✅ |
| Incremental atlas growth | `Texture2D::SetData(int level, const Rectangle*, const Color*, int, int)` | ✅ Sub-rectangle upload supported |
| Per-command clipping | `RasterizerState::setScissorTestEnableProperty` + `GraphicsDevice::setScissorRectangleProperty` | ✅ Implemented in all 14 backends |
| Straight-alpha blending | `BlendState::NonPremultiplied` | ✅ |
| No depth testing | `DepthStencilState::None` | ✅ |
| Bilinear clamped sampling | `SamplerState::LinearClamp` via `GraphicsDevice::getSamplerStatesProperty()` | ✅ |
| >65535 vertices per draw list | Base-vertex offset, applied to the vertex pointer | ✅ |
| Cursor position and buttons | `Mouse::GetState()` | ✅ |
| Vertical scroll | `MouseState::getScrollWheelValueProperty` | ✅ Cumulative; differenced per frame |
| Horizontal scroll | `MouseState::getHorizontalScrollWheelValueEXTProperty` | ✅ CNA extension |
| Key state | `Keyboard::GetState()`, `KeyboardState::IsKeyDown` | ✅ |
| **Typed characters and IME** | `TextInputEXT::TextInput` (UTF-16), `StartTextInput`, `StopTextInput` | ✅ **The one I expected to be missing** |
| Clipboard | `CNA::Devices::Clipboard::getTextProperty` / `setTextProperty` | ⚠️ Behind CNA's optional `CNA_DEVICES` |

### The text input finding

This was the risk going in. XNA 4.0 has no text-input event, and the traditional workaround —
synthesising characters from `Keyboard::GetState()` plus a shift flag — is wrong for every
non-US layout and for every IME. For a project whose author writes Czech, that is not hypothetical:
`ě š č ř ž ý á í é` would all have been unreachable in a text field.

CNA already has the right answer. `Microsoft::Xna::Framework::Input::TextInputEXT` mirrors FNA's
`Action<char>` and delivers UTF-16 code units, with `StartTextInputWithTypeEXT` for on-screen
keyboard hints on top. `CnaUiPlatform` uses it and never touches key codes for printable text.

This is why `UiInputState::characters` is `std::vector<char16_t>` rather than a UTF-8 string:
re-encoding to UTF-8 and straight back to UTF-16 for ImGui's `AddInputCharacterUTF16` would be
pure loss, and would have to get surrogate pairs right twice instead of once.

---

## 3. Gaps found in CNA

Two, both minor. Neither blocks the editor; both are worth filing upstream, since finding exactly
this kind of thing is what D-01 exists for.

### G-01 — `Color` is not default-constructible

`Microsoft::Xna::Framework::Color` declares constructors but no default one, so

```cpp
std::vector<Color> pixels;
pixels.resize(count);   // does not compile
```

fails with *"no matching function for call to `Color::Color()`"*. XNA's `Color` is a `struct` and
is default-constructible, so this is a real behavioural difference from the framework CNA
reimplements, and it will bite any porting effort that resizes a colour buffer — a completely
ordinary thing to do when generating a texture.

**Workaround in use:** `pixels.assign(count, Color(0, 0, 0, 0))`, in
`src/viewport/CnaUiRenderer.cpp`.

**Suggested fix upstream:** add `Color() = default;` with zero-initialised members, matching XNA.

### G-02 — Clipboard requires the optional `CNA_DEVICES` feature

`CNA::Devices::Clipboard` has its entire body inside `#ifdef CNA_DEVICES`, which defaults to `OFF`.
An editor built against a default CNA configuration therefore has no clipboard, and copy/paste in
inspector text fields silently does nothing.

This is a reasonable place for CNA to draw the line, and the editor degrades cleanly
(`CnaUiPlatform::hasClipboard()` reports it), but it is worth knowing that **a usable editor build
needs `-DCNA_DEVICES=ON`**. Worth documenting on the CNA side as "tooling expects this on".

### G-03 — A `RenderTarget2D` sampled as a texture is not portable

Found when the scene viewport started rendering to an offscreen target and displaying it inside the
docked viewport panel. The same code produced three different results:

| Backend | Result |
|---|---|
| **EASYGL** | Correct content, but **vertically flipped** when sampled |
| **SOFTWARE** | Sampled as **solid white** — no content at all |
| (untested) | D3D-family expected upright, by convention |

The flip is the ordinary OpenGL-versus-Direct3D texture-origin difference — bottom-left against
top-left. CNA normalises this for *presentation* (a game drawing to a render target and then to the
back buffer looks right on every backend) but not for a render target subsequently **sampled as a
texture**, which is what "render the scene into a panel" requires.

**Workaround in use:** `EditorViewport::isRenderTextureFlippedVertically()` reports the convention
per backend, resolved at compile time from `CNA::getCurrentGraphicsBackendType()`, and the UI swaps
the V coordinates when drawing the image. Swapping UVs rather than geometry keeps the widget
rectangle unchanged, so hit-testing and the cursor-to-world mapping are unaffected.

That is backend knowledge living in the editor, which decision D-01 would rather avoid. It is here
because the alternative — probing at run time with a render target and a read-back — costs more to
learn something CNA already knows.

**SOFTWARE returning white is the more serious half**, and it is a genuine limitation rather than a
convention difference. It is also consistent with the tier table (F-02): SOFTWARE is classified
*Preview Only* precisely because it cannot host the editor UI. The editor is not broken there so
much as unsupported there, which is what the table already said.

**Suggested fix upstream:** normalise render-target texture orientation on sample, or expose the
convention through `GraphicsCapability` so tooling can ask rather than hard-code a table.

---

## 4. Costs, stated honestly

**One vertex repack per frame.** ImGui's `ImDrawVert` is `{ImVec2 pos; ImVec2 uv; ImU32 col}`;
CNA's `VertexPositionColorTexture` is `{Vector3; Color; Vector2}`. Different layouts, different
sizes, different field order — a copy is unavoidable. At a typical editor frame of 5–20k vertices
this is tens of microseconds, against a 16ms budget.

It happens exactly once, on the UI side, while filling `UiDrawData` (ANALYSIS.md decision D-14),
so the renderer receives a layout it can hand almost straight to CNA. Structuring it any other way
would perform the same copy in a worse place.

**One `DrawUserIndexedPrimitives` call per ImGui draw command.** The editor's typical frame is
20–60 of them. `DrawUserIndexedPrimitives` re-uploads its vertex data each call, so a very heavy UI
would eventually want persistent `DynamicVertexBuffer`/`DynamicIndexBuffer` objects instead. That
is a contained optimisation inside `CnaUiRenderer` — `plan.md` ED-115 — and profiling should ask
for it before anyone does it.

**One RGBA→`Color` widen per texture upload.** `Texture2D::SetData` takes `Color`, not raw bytes,
so atlas pixels are widened through a scratch buffer. Atlas uploads happen on creation and when
ImGui rasterises glyphs it has not seen before — not per frame.

---

## 5. What was built

| File | Role | CNA? |
|---|---|:--:|
| `include/CNA/Editor/Ui/UiDrawData.hpp` | Toolkit-independent geometry description | no |
| `include/CNA/Editor/Ui/UiInputState.hpp` | Toolkit-independent input snapshot | no |
| `src/ui/imgui/ImGuiEditorUi.cpp` | ImGui implementation of `EditorUi` | no |
| `src/viewport/CnaUiRenderer.cpp` | Draws `UiDrawData` via CNA's public API | **yes** |
| `src/viewport/CnaUiPlatform.cpp` | Fills `UiInputState` from CNA's public API | **yes** |

The seam matters as much as the result: because the toolkit talks to `UiDrawData` and the renderer
reads it, **`CnaUiRenderer.cpp` contains no ImGui header**. "The editor UI renders through CNA's
public API" is therefore a structural property of the build graph, not a claim to be re-checked by
hand.

It also means the entire UI is testable with no GPU. `tests/UiTests.cpp` runs the real
`EditorApplication` over the real Dear ImGui, drives eight frames of synthetic input, and validates
every draw command's index ranges, vertex offsets and clip rectangles — on a build machine with no
window system.

---

## 6. Texture protocol note

Dear ImGui 1.92 replaced "build one atlas at start-up" with an incremental request protocol
(`ImGuiBackendFlags_RendererHasTextures`): the toolkit asks the backend to create, update and
destroy textures as fonts grow and re-rasterise.

One detail cost a debugging round and is worth recording. ImGui asserts the moment a draw command
references a texture whose `TexID` is still unset — and draw commands are read in the same pass
that collects texture requests. A design where the *renderer* assigns ids and reports them back
therefore cannot work without splitting the frame into two phases.

`ImGuiEditorUi` owns the `UiTextureId` namespace instead. A request always arrives with its id
already allocated, and the renderer keeps a plain `UiTextureId → Texture2D` map. The ordering
hazard disappears rather than being worked around.

---

## 7. Verified in a real window

The spike's claim was checked by building the whole thing against a real CNA checkout
(`CNA_GRAPHICS_BACKEND=SOFTWARE`, SDL and FFmpeg dependencies installed, `CNA_DEVICES=ON`) and
running the editor for real:

```
$ SDL_VIDEODRIVER=dummy ./cna-editor --project=examples/HelloSprites/HelloSprites.cnaproject \
      --frames=20 --screenshot=editor.png
cna-editor: backend SOFTWARE, 56 frames, 1600x900 display, 14 draw calls, 1858 triangles,
            1 textures created, 0 texture updates, 0 commands clipped away
```

![The editor running on EASYGL](images/editor-easygl.png)

The PNG shows a working editor: a menu bar, Scene Hierarchy on the left listing the example
project's three entities, a central Viewport, an Inspector on the right, and Assets/Console tabbed
at the bottom showing the real start-up log. All of it drawn through
`DrawUserIndexedPrimitives` + `BasicEffect` + `Texture2D`, with no backend-specific code.

`tests/CMakeLists.txt` registers this as `CnaEditorWindowSmoke` when the CNA build is enabled. The
screenshot is the assertion: a run that merely exits cleanly cannot distinguish a working editor
from one that opened a blank window.

### Two things the real run found

**The window size must come from the graphics device, not the window.** The first attempt read
`GameWindow::getClientBoundsProperty()`, which reports 0×0 on SOFTWARE — that backend creates no
SDL window at all. The editor dutifully rendered a zero-sized UI and issued zero draw calls. The
fix is to take the size from `GraphicsDevice::getViewportProperty()`, the surface actually being
drawn into, and fall back to the window only if that is unavailable. This is more correct
generally: on a scaled or letterboxed presentation the two differ, and the viewport is what the
renderer needs.

Worth noting that SOFTWARE having no window is not a defect — it is exactly why the backend table
classifies it *Preview Only*. The tier table predicted this.

**Dear ImGui does not place windows into a dock space by itself.** Without a saved layout, every
panel floated at the same default position, stacked on top of one another — one small window in a
sea of empty grey. `ImGuiEditorUi` now builds a default arrangement with the `DockBuilder` API on
first run, using each panel's declared `DockSide`, and never overrides a layout the user has saved.

---

## 8. The missing-glyph bug, and what caused it

The first hosted run rendered docked tab labels as `iewport` and `nspector` — uppercase `V` and
`I` were invisible, while every other character was fine. It reproduced identically on SOFTWARE and
EASYGL. **Found and fixed**; recorded here because the cause is a contract that is easy to get
wrong again, and because the elimination is more useful than the answer alone.

### What it was

Dear ImGui rasterises glyphs lazily and marks its atlas dirty when it does. Crucially, it allows
**no pending state**: a texture request is marked satisfied the instant it is issued, and ImGui
asserts on the next frame if a texture has an id but a status other than `OK`. So "assign the id
now, confirm the upload once the renderer has done it" is not expressible.

That places a hard obligation on the consumer: `getDrawData()`'s texture requests must be honoured
on **every frame that produces draw data** — not only on frames that get drawn.

The host was uploading textures in its `Draw` phase. Under CNA's fixed-timestep loop many `Update`
frames run without a matching `Draw` (in one measured session, 55 update frames produced a handful
of draws). Any glyph first needed on such a frame was marked uploaded while nothing was uploaded,
and ImGui — believing the atlas current — never asked again. Those glyphs sampled blank atlas for
the rest of the session. `V` and `I` simply happened to be the characters first required on one of
those frames, which is why the failure looked arbitrary and character-specific.

The tell was in the numbers all along: **`0 texture updates`** across a run in which ImGui was
demonstrably rasterising new glyphs. After the fix the same run reports 1–2.

### The fix

`CnaUiRenderer` splits into `applyTextureRequests()` and `renderGeometry()`. The host calls the
first in `Update`, immediately after `endFrame()`, and the second in `Draw`. Uploads can no longer
be lost, whatever the loop does with draw frames.

### What was ruled out on the way

Worth keeping, because each of these was a plausible cause and each was killed by evidence rather
than by argument:

| Hypothesis | Evidence against |
|---|---|
| The glyphs are absent from the font atlas | `ImGuiUiEmitsAQuadForEveryVisibleGlyph` renders "VIVID" headless and asserts exactly 20 vertices — four per character. The quads exist |
| A clip rectangle is cutting them | The run reported **0 commands clipped away**, and a per-command dump showed every command's geometry inside its own clip rectangle |
| The scissor conversion is too tight | The left edge truncates (expanding the region) while the width rounds up, so the scissor is at worst one pixel *generous* |
| The sub-rectangle upload path is wrong | Forcing every update to re-upload the whole texture changed nothing |
| A backend rasteriser bug | Identical output on SOFTWARE and EASYGL — two unrelated rasterisers do not share a bug |

The regression guard is `ImGuiUiRequestsAnUpdateWhenNewGlyphsAppear`, which asserts headless that
characters ImGui has not seen before produce a well-formed `Update` request against the existing
atlas.

### A side benefit

Getting SOFTWARE and EASYGL to agree pixel-for-pixel — before *and* after the fix — is exactly the
property plan.md ED-510's backend comparison mode is meant to check automatically. Doing it by hand
here suggests the automated version will be worth having.

---

## 9. Follow-on work

| Task | Why it is separate |
|---|---|
| `plan.md` ED-111 | Window creation and presentation. The renderer draws into a device; something must create the window and the device. |
| `plan.md` ED-112 | Dock layout persistence — `loadLayout`/`saveLayout` exist and are unused. |
| `plan.md` ED-115 | Persistent dynamic vertex/index buffers, **if profiling asks**. |
| CNA issue | G-01: add `Color() = default`. |
| CNA docs | G-02: note that tooling expects `CNA_DEVICES=ON`. |
