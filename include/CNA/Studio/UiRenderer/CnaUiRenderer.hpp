// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiRenderer/CnaUiRenderer.hpp
 * @brief The compatibility UI render backend: `UiDrawData` through CNA's classic XNA 4.0 surface.
 *
 * `plan.md` STUDIO-04001. Architecture: `docs/UI-RENDER-PATH.md`, layer 2, stage 0.
 *
 * ### What it is, and what it is not
 *
 * This is the renderer the native Studio UI inherited from the Dear ImGui prototype, and it is
 * correct, portable and complete: `BasicEffect`, `DrawUserIndexedPrimitives`, `Texture2D` and four
 * render states, all XNA 4.0, all available on every CNA renderer with a 3D pipeline.
 *
 * It is **not** the modern CNAEXT path this phase is named after, and the header comment it used to
 * carry — a table headed "ImGui needs" — is why that went unnoticed for six phases. The audit in
 * `STUDIO-04021` traced the calls and `docs/UI-RENDER-PATH.md` records them. `StudioModernUiRenderer`
 * is the intended implementation; this one is the fallback for hosts that cannot execute a shader,
 * which is not a hypothetical class — CNA's `SOFTWARE` renderer is one.
 *
 * | The UI needs                           | CNA public API used                                    |
 * |----------------------------------------|--------------------------------------------------------|
 * | Textured, vertex-coloured triangles    | `GraphicsDevice::DrawUserIndexedPrimitives` with        |
 * |                                        | `VertexPositionColorTexture` and 16-bit indices         |
 * | Pixel-space orthographic projection    | `BasicEffect` (`Projection`, `VertexColorEnabled`,      |
 * |                                        | `TextureEnabled`, `LightingEnabled = false`)            |
 * | Font atlas upload and incremental grow | `Texture2D(device, w, h)` + `Texture2D::SetData`        |
 * | Per-command clipping                   | `RasterizerState::ScissorTestEnable` +                  |
 * |                                        | `GraphicsDevice::ScissorRectangle`                      |
 * | Straight-alpha blending                | `BlendState::NonPremultiplied`                          |
 * | No depth testing                       | `DepthStencilState::None`                               |
 * | Bilinear clamped sampling              | `SamplerState::LinearClamp`                            |
 *
 * Nothing here is renderer-specific and nothing here is `CNA::Internal`; the file would compile
 * unchanged against any CNA build, including one with `-DCNA_CNAEXT=OFF`. That last property is
 * exactly what distinguishes it from the modern backend.
 */
#include <memory>
#include <string>

#include "CNA/Studio/UiRenderer/StudioUiRenderBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Studio
{
    /**
     * @brief Renders an immediate-mode UI with CNA.
     *
     * The GraphicsDevice is borrowed, never owned: the application owns the window and the device,
     * and this class only draws into whatever it is handed.
     */
    class CnaUiRenderer final : public StudioUiRenderBackend
    {
    public:
        CnaUiRenderer();
        ~CnaUiRenderer() override;

        CnaUiRenderer(const CnaUiRenderer&) = delete;
        CnaUiRenderer& operator=(const CnaUiRenderer&) = delete;

        /**
         * @brief Binds the renderer to a device.
         * @param device The device to draw with; must outlive this renderer.
         */
        void initialize(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) override;

        /** @brief Releases every texture. Safe to call more than once. */
        void shutdown() override;

        /**
         * @brief Honours @p drawData's texture requests, then draws its geometry.
         *
         * Restores the device's blend, depth, rasterizer and scissor state before returning, so a
         * caller that draws a scene after the UI is not silently affected by the UI's own state.
         *
         * Convenience for callers that do both in one place; a host with a separate update and
         * draw phase should call the two halves below instead, and the reason is not cosmetic --
         * see applyTextureRequests().
         *
         * @return Counters for the frame just drawn.
         */
        /**
         * @brief Uploads, updates and releases textures, drawing nothing.
         *
         * **Call this every frame that produces draw data, not only frames that get drawn.** Dear
         * ImGui requires a texture request to be acknowledged in the same frame it was issued, and
         * under a fixed-timestep game loop many Update frames run without a matching Draw. Doing
         * the uploads in the draw phase means every glyph first rasterised on an Update-only frame
         * is acknowledged but never actually uploaded -- and ImGui, believing the texture current,
         * never asks again. Those glyphs then sample blank atlas for the rest of the session.
         *
         * That was a real bug: uppercase `V` and `I` were invisible in the editor's tab labels
         * because they happened to be the characters first needed on such a frame.
         */
        UiRenderStats applyTextureRequests(const UiDrawData& drawData) override;

        /**
         * @brief Draws @p drawData's geometry, touching no textures.
         *
         * Assumes applyTextureRequests() has already run for this frame's data.
         */
        UiRenderStats renderGeometry(const UiDrawData& drawData) override;

        /** @brief Returns the stats from the most recent render(). */
        [[nodiscard]] const UiRenderStats& lastStats() const override { return lastStats_; }

        /** @brief `"compatibility"`, matching `studioUiBackendChoiceName`. */
        [[nodiscard]] std::string_view name() const override { return "compatibility"; }

        /**
         * @brief Gives @p texture a UI texture id without taking ownership of it.
         *
         * Used for the scene viewport's render target, which the scene renderer owns and re-creates
         * whenever the panel is resized. Adopting rather than copying means the UI draws the live
         * target with no per-frame blit; the borrowed entry is replaced on every call, so a
         * re-created target never leaves a dangling pointer behind.
         *
         * @return A stable id for this renderer's borrowed slot.
         */
        UiTextureId adoptTexture(Microsoft::Xna::Framework::Graphics::Texture2D& texture) override;

        /**
         * @brief Borrows @p texture under @p key, returning an id stable for that key.
         *
         * The unkeyed overload above owns a single slot, which is right for the scene's render
         * target and wrong for anything there can be many of. Asset thumbnails are the case: each
         * needs its own id, and that id must stay the same across frames or the UI would see a
         * different texture every time it drew the same row.
         *
         * The renderer does **not** own the texture. Whoever does must call
         * releaseAdoptedTexture() before destroying it, or this map keeps a dangling pointer.
         */
        UiTextureId adoptTexture(const Uuid& key,
                                 Microsoft::Xna::Framework::Graphics::Texture2D& texture) override;

        /** @brief Drops the borrowed entry for @p key. Safe to call for a key never adopted. */
        void releaseAdoptedTexture(const Uuid& key) override;

        /** @brief Returns the number of textures currently held. */
        [[nodiscard]] std::size_t getTextureCount() const override;

        /** @brief Returns the name of the CNA backend this build was compiled against. */
        [[nodiscard]] static std::string getBackendName();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        UiRenderStats lastStats_;
    };

    /**
     * @brief Whether CNA still uploads the vertex stride `kStudioUiGpuVertexBytes` assumes.
     *
     * `STUDIO-04028`. Every GPU byte figure `--ui-benchmark` prints is that constant times a
     * vertex count, and the constant is a literal because `StudioUiBenchmark.hpp` is CNA-free. The
     * submitted-bytes half is pinned by a `static_assert`; this half cannot be, because the stride
     * lives on a `VertexDeclaration` rather than in a type's size -- so it is asked at run time,
     * through the public declaration, by the host that is already checking the model per frame.
     *
     * @return True when the declaration's stride is what the benchmark assumes.
     */
    [[nodiscard]] bool studioUiGpuVertexStrideMatches();

}
