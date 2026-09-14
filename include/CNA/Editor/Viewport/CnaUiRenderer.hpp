// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Viewport/CnaUiRenderer.hpp
 * @brief Draws UiDrawData through CNA's public graphics API.
 *
 * This is the answer to ANALYSIS.md question Q-01, and the proof of decision D-01: the editor's
 * own user interface is rendered using only the API a CNA *game* has. No `CNA::Internal::*`, no
 * per-backend code, no shader authored here. One implementation serves every backend CNA supports
 * for the editor UI, because everything it uses is backend-independent by construction:
 *
 * | ImGui needs                            | CNA public API used                                    |
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
 * | Bilinear clamped sampling              | `SamplerState::LinearClamp`                             |
 *
 * The one thing that is *not* free is the vertex repack: ImGui's layout differs from
 * `VertexPositionColorTexture`. That conversion already happens on the UI side while filling
 * UiDrawData, so this class consumes a layout it can hand almost straight to CNA.
 */

#include <memory>
#include <string>

#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Ui/UiDrawData.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Editor
{
    /** @brief Per-frame counters, for the profiler panel and for tests. */
    struct UiRenderStats
    {
        std::size_t drawCalls = 0;
        std::size_t triangles = 0;
        std::size_t texturesCreated = 0;
        std::size_t texturesUpdated = 0;
        std::size_t texturesDestroyed = 0;

        /** @brief Commands skipped because their clip rectangle selected no pixels. */
        std::size_t clippedAway = 0;
    };

    /**
     * @brief Renders an immediate-mode UI with CNA.
     *
     * The GraphicsDevice is borrowed, never owned: the application owns the window and the device,
     * and this class only draws into whatever it is handed.
     */
    class CnaUiRenderer
    {
    public:
        CnaUiRenderer();
        ~CnaUiRenderer();

        CnaUiRenderer(const CnaUiRenderer&) = delete;
        CnaUiRenderer& operator=(const CnaUiRenderer&) = delete;

        /**
         * @brief Binds the renderer to a device.
         * @param device The device to draw with; must outlive this renderer.
         */
        void initialize(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Releases every texture. Safe to call more than once. */
        void shutdown();

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
        UiRenderStats render(const UiDrawData& drawData);

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
        UiRenderStats applyTextureRequests(const UiDrawData& drawData);

        /**
         * @brief Draws @p drawData's geometry, touching no textures.
         *
         * Assumes applyTextureRequests() has already run for this frame's data.
         */
        UiRenderStats renderGeometry(const UiDrawData& drawData);

        /** @brief Returns the stats from the most recent render(). */
        [[nodiscard]] const UiRenderStats& getLastStats() const { return lastStats_; }

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
        UiTextureId adoptTexture(Microsoft::Xna::Framework::Graphics::Texture2D& texture);

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
                                 Microsoft::Xna::Framework::Graphics::Texture2D& texture);

        /** @brief Drops the borrowed entry for @p key. Safe to call for a key never adopted. */
        void releaseAdoptedTexture(const Uuid& key);

        /** @brief Returns the number of textures currently held. */
        [[nodiscard]] std::size_t getTextureCount() const;

        /** @brief Returns the name of the CNA backend this build was compiled against. */
        [[nodiscard]] static std::string getBackendName();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        UiRenderStats lastStats_;
    };
}
