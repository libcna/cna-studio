# CNA Studio — the UI render path

> What actually reaches the GPU when the native Studio shell draws a frame, traced from the widget
> call to the CNA graphics call, and the four distinct layers that the phrase "the Studio renderer"
> has so far been used for all four of.
>
> Audit of `STUDIO-04021`. Read with [`ARCHITECTURE.md`](ARCHITECTURE.md) §3, §4 and §5.

## Why this document exists

"CNA Studio requires a renderer capable of the modern CNAEXT Graphics API" has been an architectural
statement in this repository since Phase 2. This document is the result of checking whether it is
true, by reading the calls rather than the names.

**It was not true when this was written, and it is true now.** The audit's finding and the
migration that answered it are both recorded here; §"Where it stands" has the current state. What
follows describes what the audit found.

The native Studio UI reached the GPU through the classic XNA-compatible
pipeline it inherited from the Dear ImGui prototype. Nothing was mis-stated deliberately: the seam
between the UI and the renderer is genuinely toolkit-independent, the native UI genuinely inherited
a working renderer across it, and the phase named "CNAEXT UI renderer" genuinely contains fifteen
completed tasks. What none of that establishes is which CNA API the pixels come out of.

## The four layers

These are four separate things. Conflating any two of them is how a claim like the one above
survives for six phases without anybody being wrong on purpose.

| # | Layer | What it is | Where it lives | Links CNA |
|---|-------|------------|----------------|:---------:|
| 1 | **UI framework** | Widgets, identity, retained state, layout, input routing, focus, theming, docking. Produces geometry; owns no device resource. | `cna-studio-ui-core` | **no** |
| 2 | **UI GPU renderer** | Turns that geometry into graphics calls. Owns textures, buffers and effects. | `cna-studio-viewport` today; `cna-studio-ui-renderer` is where it belongs | yes |
| 3 | **CNA host renderer** | The CNA renderer the `cna-studio` process was compiled against. Draws layers 1–2 and the scene viewport. | chosen by `CNA_GRAPHICS_RENDERER` at Studio's configure time | — |
| 4 | **Game target renderer** | The CNA renderer the *user's game* ships on. Runs in `cna-player` or in the packaged build. | chosen by the project's target profile | — |

Layers 3 and 4 are independent and §3 of `ARCHITECTURE.md` says why at length. Layers 1 and 2 are
what this document adds: the host capability contract is a statement about what layer 2 *asks of*
layer 3, and it can only be enforced once layer 2 is a thing with a name.

## The traced path

```
  StudioShell::describeFrame…            src/ui-core/StudioShell.cpp        │ layer 1
    StudioFrame::…                       src/ui-core/StudioFrame.cpp        │ CNA-free
      StudioDrawList::addQuad/addGlyph…  src/ui-core/StudioDrawList.cpp     │
        ▼ fills
  UiDrawData                             include/CNA/Studio/Ui/UiDrawData.hpp
    ├─ lists[].vertices  : UiVertex  { x y u v rgba }
    ├─ lists[].indices   : uint16
    ├─ lists[].commands  : { texture, clipRect, indexOffset, indexCount, vertexOffset }
    └─ textureRequests   : { Create | Update | Destroy, pixels, region }
        ▼ consumed by
  CnaStudioShellHost::Draw               src/viewport/CnaStudioShellHost.cpp:524   ┐
    CnaUiRenderer::applyTextureRequests   src/viewport/CnaUiRenderer.cpp           │ layer 2
    CnaUiRenderer::renderGeometry         src/viewport/CnaUiRenderer.cpp           ┘
        ▼ calls
  Microsoft::Xna::Framework::Graphics     ← classic XNA 4.0 surface, layer 3
```

### What layer 2 actually calls

Every graphics call the native Studio UI makes today, read out of
`src/viewport/CnaUiRenderer.cpp`:

| Call | Header | Era |
|------|--------|-----|
| `BasicEffect(device)`, `Apply()` | `…/Graphics/BasicEffect.hpp` | XNA 4.0 |
| `setProjectionProperty`, `setWorldProperty`, `setViewProperty` | `IEffectMatrices` | XNA 4.0 |
| `GraphicsDevice::DrawUserIndexedPrimitives` | `…/Graphics/GraphicsDevice.hpp` | XNA 4.0 |
| `Texture2D(device, w, h)`, `SetData`, `SetData(region)` | `…/Graphics/Texture2D.hpp` | XNA 4.0 |
| `BlendState::NonPremultiplied` | `…/Graphics/BlendState.hpp` | XNA 4.0 |
| `DepthStencilState::None` | `…/Graphics/DepthStencilState.hpp` | XNA 4.0 |
| `RasterizerState::CullNone` + `ScissorTestEnable` | `…/Graphics/RasterizerState.hpp` | XNA 4.0 |
| `GraphicsDevice::ScissorRectangle` | `…/Graphics/GraphicsDevice.hpp` | XNA 4.0 |
| `SamplerState::LinearClamp` | `…/Graphics/SamplerState.hpp` | XNA 4.0 |

Nothing from `CNA::Graphics`. No `ShaderEffect`, no `ConstantBuffer`, no `VertexBuffer`, no
`IndexBuffer`, no `ShaderPackageEXT`. `CnaUiRenderer.cpp` includes no CNAEXT header and would
compile against a CNA built with `-DCNA_CNAEXT=OFF`.

That is a *correct* renderer and a *portable* one — it is the reason the native shell drew real text
on a real device the week the shell existed. It is not the modern one.

### Where the claim came from

`CnaUiRenderer.hpp`'s own header comment is the fossil. Its table is headed **"ImGui needs"** and
maps Dear ImGui's requirements onto CNA calls; it was written for `STUDIO-00xxx`-era work, when the
question was whether an ImGui editor could be drawn with a game's API at all. The answer was yes,
using `BasicEffect`, and the file has been right about that ever since.

The native UI then adopted the same seam — deliberately, and it was the right call: a strangler
migration that also rewrote the GPU layer would have had no working state to fall back to. But
`UiDrawData` is the seam's *name*, and inheriting the seam inherited the implementation behind it.
Phase 4 was titled "CNAEXT UI renderer" and its tasks were ticked as the UI gained the behaviours
they name — batching, clipping, atlases, readback — every one of which was implemented on the
classic path. §"Ledger" below reconciles that.

## What "the modern CNAEXT Graphics API" means here

CNA's modern surface, for a UI renderer's purposes, is:

| Facility | Type | Gate |
|----------|------|------|
| Programmable effects from source | `Microsoft::…::Graphics::ShaderEffect` | always compiled; marked `CNAEXT` |
| Typed shader payloads and packages | `CNA::Graphics::ShaderCodeEXT`, `ShaderPackageEXT` | `CNA_CNAEXT` |
| Shared compilation cache | `CNA::Graphics::ShaderEffectFactory` | `CNA_CNAEXT` |
| std140 uniform blocks | `ShaderEffect::DeclareUniformBlockEXT` | `CNA_CNAEXT` |
| Engine-layer revision | `CNA::Graphics::getEngineLayerVersion()` | `CNA_CNAEXT` |
| Capability profile | `GraphicsDevice::GetRendererCapabilityProfileEXT()` | always compiled; marked `CNAEXT` |
| Persistent GPU buffers | `VertexBuffer` / `DynamicVertexBuffer`, `IndexBuffer` / `DynamicIndexBuffer` | XNA 4.0 shapes, used by the modern path |

The renderer-side features a device must classify for the modern path are
`ShaderEffects`, `ShaderEffectSourceExecution` and at least one `ShaderDialect*`.

`BasicEffect` is not on that list, and that is the whole distinction: a renderer can implement
`BasicEffect` — fixed-function or otherwise — without being able to execute a shader anybody wrote.
A Studio host that can only do the former can draw today's UI and can never draw a material preview,
a shader-graph node, a post-process or anything else Phases 19–23 exist for.

## Consequences that were live before this audit

1. **The host contract did not enforce its headline requirement.** `StudioHostEvaluation` carried
   `modernApiAvailable` into its report and no requirement consulted it, so a renderer with no
   modern API at all passed `canHostStudio` on the strength of `ThreeDimensionalPipeline`,
   `DepthStencilBuffer` and a 2048-pixel texture limit. Closed by `STUDIO-02070`.

2. **`modernApiAvailable` was a literal `true`.** Both hosts passed
   `/*modernApiAvailable=*/true` to `captureStudioCapabilitySnapshot`. The field therefore reported
   what the call site asserted rather than what the build had. Closed by `STUDIO-02071`.

3. **Phase 4's ledger described a module that does not exist.** `STUDIO-04001` — create
   `cna-studio-ui-renderer` — is ⬜, and eight of its dependants are ✅. Reconciled by
   `STUDIO-04022`.

## The staged migration

Deleting a working renderer to install an unproven one is how a tool loses a week. The sequence is:

| Stage | State | Task | |
|-------|-------|------|---|
| 0 | `CnaUiRenderer` draws every frame. | — | where the audit found things |
| 1 | The contract enforces the modern requirement; availability is read from the build. | `STUDIO-02070`, `STUDIO-02071` | ✅ |
| 2 | `cna-studio-ui-renderer` exists as a module with `CnaUiRenderer` moved into it unchanged, behind `StudioUiRenderBackend`. | `STUDIO-04001` | ✅ |
| 3 | `StudioModernUiRenderer` implements the same interface over `ShaderEffect` and GPU buffers. | `STUDIO-04023`, `STUDIO-04024` | ✅ |
| 4 | A/B verification: both backends draw the same frame and the captures are compared. | `STUDIO-04025` | ✅ |
| 5 | The native host defaults to the modern backend. | `STUDIO-04026` | ✅ |
| 6 | The classic backend is removed, or retained only where a justification is written down. | `STUDIO-04027` | |

No stage removed a working path before the one after it had passed.

### Where it stands

**Stages 1 to 5 are done.** On a host that meets the modern profile, `cna-studio` with no flags
draws its entire UI through a `ShaderEffect` Studio compiled from a `ShaderPackageEXT`, over a
`DynamicVertexBuffer` and a `DynamicIndexBuffer` written once per draw list with
`SetDataOptions::Discard`. Verified on `OPENGL4` under Xvfb on Mesa's llvmpipe: `Modern graphics
API: CNA engine layer 18`, `UI renderer: modern`, 1920x1080, 90 draw calls, 16140 triangles, with
text, icons, the composited scene and tinted log rows all drawn by it.

**Stage 6 is deliberately not done**, and the reason is stage 6's own acceptance condition: the
classic backend is retained *with the justification written down*. CNA's `SOFTWARE` renderer cannot
execute a shader, and it is the renderer that needs no display and no GPU. Deleting the
compatibility backend today would leave Studio unable to start in the configuration all of its
dependency-free automation runs in. `STUDIO-04029` makes `OPENGL4` a second CI configuration;
`STUDIO-04027` and `STUDIO-02074` come after that, not before.

### The A/B result, which was not the expected one

The task was written expecting a tolerance: a fixed-function path and a fragment shader resolve the
same triangle's edge pixels differently. That is true *across renderers*. Within one renderer it is
not — both backends submit the same geometry, in the same order, with the same blend, sampler and
scissor state, and only the program and the buffer route differ. Neither changes where a triangle
lands or what colour it is.

So `CnaStudioUiRenderBackendsAgree` asserts **byte equality**, which is a far stronger contract than
any threshold would have been, and it holds. It also refuses to become a tautology: before comparing
anything it checks that each run used the backend it asked for, because a host that quietly ran the
same backend twice is the single most likely way for an A/B comparison to pass while proving
nothing.

### Three defects the migration produced, and the one assertion that caught all three

Each produced exactly the same symptom — a completely blank frame with every draw call reported —
and none would have been caught by a counts assertion. `--screenshot-min-colors`, added by
`STUDIO-04015` for precisely this class of failure, caught all three:

1. **Uniforms set before the program was bound.** CNA's uniform setters write to the currently bound
   program, and binding is what `Apply()` does. The projection went to whatever program the previous
   caller had left bound.
2. **The projection passed row-major.** CNA hands the array to the graphics API untransposed and
   XNA's `Matrix` is row-major. Every vertex landed outside the clip volume.
3. **A `SetData` overload that takes no byte offset**, which was at least a compile error.

The first two are worth stating plainly: a UI renderer that is *completely* wrong looks exactly like
a UI renderer that was never called.

**No backend-specific code at any stage.** Studio calls Vulkan, D3D, GL, Metal and WebGPU through
exactly zero lines of its own; `STUDIO-02033`'s guard test fails the build on a direct backend call
and it applies to the new module exactly as it applies to the old one.
