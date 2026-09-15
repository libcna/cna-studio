// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiRenderer/StudioUiRenderBackend.hpp
 * @brief The seam between the Studio UI's geometry and the graphics calls that draw it.
 *
 * `plan.md` STUDIO-04001. Architecture: `docs/UI-RENDER-PATH.md`, layer 2.
 *
 * ### What this interface is for
 *
 * Studio has two UI GPU implementations while the migration in `docs/UI-RENDER-PATH.md` runs, and
 * the whole point of the staged plan is that the new one is proved against the old one rather than
 * replacing it on trust. That needs both to be addressable through one type: an A/B comparison
 * between an object and a differently-named object is a comparison nobody can write.
 *
 * It is **not** an abstraction over graphics backends. Studio contains no Vulkan, D3D, GL, Metal or
 * WebGPU code and `STUDIO-02033`'s guard fails the build on any; both implementations here are
 * renderer-neutral CNA code, and the thing they differ in is which *era* of CNA's API they use.
 *
 * ### Why the header names no CNA type it does not have to
 *
 * `GraphicsDevice` and `Texture2D` are forward-declared. Everything else this interface speaks in —
 * `UiDrawData`, `UiTextureId`, `UiRenderStats` — is Studio's own, so a caller can hold a
 * `StudioUiRenderBackend*` without CNA's headers reaching it.
 */

#include <cstddef>
#include <string_view>

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Ui/UiDrawData.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Studio
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

        /** @brief Vertices submitted this frame, across every list. */
        std::size_t vertices = 0;
        /** @brief Indices submitted this frame. */
        std::size_t indices = 0;
        /** @brief Bytes uploaded to vertex and index buffers this frame. */
        std::size_t geometryBytesUploaded = 0;
        /** @brief Bytes uploaded to textures this frame. */
        std::size_t textureBytesUploaded = 0;
        /** @brief Times a different texture was bound. */
        std::size_t textureChanges = 0;
        /** @brief Times the scissor rectangle was changed. */
        std::size_t clipChanges = 0;
    };

    /** @brief Draws `UiDrawData` with CNA. One implementation per era of CNA's graphics API. */
    class StudioUiRenderBackend
    {
    public:
        virtual ~StudioUiRenderBackend() = default;

        /**
         * @brief Creates the device resources this backend needs.
         * @param device The device to draw with. Borrowed; it must outlive this object.
         */
        virtual void initialize(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) = 0;

        /** @brief Releases every device resource. Safe to call twice. */
        virtual void shutdown() = 0;

        /**
         * @brief Creates, updates and destroys textures the UI asked for.
         *
         * Separate from @ref renderGeometry, and the separation is load-bearing: a glyph
         * rasterised during layout has to reach the GPU before the quad that samples it, and the
         * host applies requests in its update pass rather than in its draw pass.
         *
         * @param drawData The frame.
         * @return What was created, updated and destroyed.
         */
        virtual UiRenderStats applyTextureRequests(const UiDrawData& drawData) = 0;

        /**
         * @brief Draws the frame's geometry.
         * @param drawData The frame.
         * @return What was drawn.
         */
        virtual UiRenderStats renderGeometry(const UiDrawData& drawData) = 0;

        /**
         * @brief Gives the UI an id for a texture somebody else owns, such as the scene target.
         * @param texture The texture. Not owned; the caller keeps it alive.
         * @return The id to put in a draw command.
         */
        virtual UiTextureId adoptTexture(
            Microsoft::Xna::Framework::Graphics::Texture2D& texture) = 0;

        /**
         * @brief Gives the UI a stable id for a keyed texture, such as an asset thumbnail.
         * @param key The caller's identity for it; the same key keeps the same id.
         * @param texture The texture. Not owned.
         * @return The id.
         */
        virtual UiTextureId adoptTexture(
            const Uuid& key, Microsoft::Xna::Framework::Graphics::Texture2D& texture) = 0;

        /**
         * @brief Forgets a keyed borrow.
         * @param key The key it was adopted under.
         */
        virtual void releaseAdoptedTexture(const Uuid& key) = 0;

        /** @brief How many textures this backend is holding, owned and borrowed. */
        [[nodiscard]] virtual std::size_t getTextureCount() const = 0;

        /** @brief The last frame's counters. */
        [[nodiscard]] virtual const UiRenderStats& lastStats() const = 0;

        /**
         * @brief Which implementation this is: `"compatibility"` or `"modern"`.
         *
         * Matches `studioUiBackendChoiceName`, so the name in a status bar, a log line and a
         * capability report is one string rather than three that have to be kept in step.
         */
        [[nodiscard]] virtual std::string_view name() const = 0;

        /**
         * @brief Draws the frame: textures first, then geometry.
         * @param drawData The frame.
         * @return Everything, merged.
         */
        UiRenderStats render(const UiDrawData& drawData)
        {
            UiRenderStats stats = applyTextureRequests(drawData);
            const UiRenderStats geometry = renderGeometry(drawData);

            stats.drawCalls = geometry.drawCalls;
            stats.triangles = geometry.triangles;
            stats.clippedAway = geometry.clippedAway;
            stats.vertices = geometry.vertices;
            stats.indices = geometry.indices;
            stats.geometryBytesUploaded = geometry.geometryBytesUploaded;
            stats.textureChanges = geometry.textureChanges;
            stats.clipChanges = geometry.clipChanges;
            return stats;
        }
    };
} // namespace CNA::Studio
