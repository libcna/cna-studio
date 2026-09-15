// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiRenderer/StudioModernUiRenderer.hpp
 * @brief The modern UI render backend: `UiDrawData` through CNA's CNAEXT graphics API.
 *
 * `plan.md` STUDIO-04023, STUDIO-04024. Architecture: `docs/UI-RENDER-PATH.md`, layer 2, stage 3.
 *
 * ### What makes it the modern one
 *
 * | | Compatibility (`CnaUiRenderer`) | Modern (this) |
 * |---|---|---|
 * | Program | `BasicEffect` | `ShaderEffect` from a `ShaderPackageEXT` Studio ships |
 * | Geometry | `DrawUserIndexedPrimitives` from scratch vectors | `DynamicVertexBuffer` + `DynamicIndexBuffer`, `DrawIndexedPrimitives` |
 * | Uniforms | effect properties | `SetUniformMat4`, `SetTexture` |
 * | Needs `-DCNA_CNAEXT=ON` | no | **yes** |
 * | Needs a renderer that executes a shader | no | **yes** |
 *
 * The last two rows are the whole of `STUDIO-02070`'s modern host profile, which is why that task
 * came first: a backend that cannot be selected on a host that cannot run it needs the host
 * contract to know the difference.
 *
 * ### Why the buffers, and not only the shader
 *
 * `DrawUserIndexedPrimitives` hands the renderer a CPU pointer on every call, and the renderer
 * copies it into a staging buffer it owns — every frame, for every draw command, whether or not the
 * geometry changed. A persistent `DynamicVertexBuffer` written once per frame with
 * `SetDataOptions::Discard` replaces that with one upload per list, and `Discard` is what keeps it
 * from stalling: the driver hands back fresh memory rather than waiting for the previous frame's
 * draws to finish reading the old.
 *
 * The buffers **grow and never shrink**. A UI's geometry is bounded by the window and settles
 * within a few frames of a resize; a buffer that shrank would reallocate on the next large frame,
 * and reallocating is the one thing this arrangement exists to avoid.
 *
 * ### Device resources are RAII and the device is borrowed
 *
 * Every GPU object is a `unique_ptr` destroyed by `shutdown()`, which the destructor calls. The
 * `GraphicsDevice` is a raw pointer and is never owned: the application owns the window and the
 * device, and this class draws into whatever it is handed.
 */

#include <memory>
#include <string>
#include <string_view>

#include "CNA/Studio/UiRenderer/StudioUiRenderBackend.hpp"

namespace CNA::Studio
{
    /** @brief Draws the Studio UI through `ShaderEffect` and GPU buffers. */
    class StudioModernUiRenderer final : public StudioUiRenderBackend
    {
    public:
        StudioModernUiRenderer();
        ~StudioModernUiRenderer() override;

        StudioModernUiRenderer(const StudioModernUiRenderer&) = delete;
        StudioModernUiRenderer& operator=(const StudioModernUiRenderer&) = delete;

        void initialize(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) override;
        void shutdown() override;

        UiRenderStats applyTextureRequests(const UiDrawData& drawData) override;
        UiRenderStats renderGeometry(const UiDrawData& drawData) override;

        UiTextureId adoptTexture(
            Microsoft::Xna::Framework::Graphics::Texture2D& texture) override;
        UiTextureId adoptTexture(
            const Uuid& key, Microsoft::Xna::Framework::Graphics::Texture2D& texture) override;
        void releaseAdoptedTexture(const Uuid& key) override;

        [[nodiscard]] std::size_t getTextureCount() const override;
        [[nodiscard]] const UiRenderStats& lastStats() const override { return lastStats_; }

        /** @brief `"modern"`, matching `studioUiBackendChoiceName`. */
        [[nodiscard]] std::string_view name() const override { return "modern"; }

        /**
         * @brief Whether the shader compiled and the backend can draw.
         *
         * False after an `initialize` whose shader CNA refused. The backend then draws nothing
         * rather than drawing wrongly, and @ref shaderDiagnostic says why — which is the answer a
         * host needs to fall back with a reason instead of showing a black window.
         */
        [[nodiscard]] bool isUsable() const;

        /**
         * @brief CNA's own account of the shader selection and compilation.
         *
         * Populated on success as well as on failure: which dialect a renderer selected is the
         * first question about a UI that draws slightly wrong, and it is not otherwise knowable
         * from outside CNA.
         */
        [[nodiscard]] const std::string& shaderDiagnostic() const;

        /** @brief How many vertices the vertex buffer currently holds room for. */
        [[nodiscard]] std::size_t vertexCapacity() const;

        /** @brief How many indices the index buffer currently holds room for. */
        [[nodiscard]] std::size_t indexCapacity() const;

        /** @brief How many times the buffers have been reallocated since `initialize`. */
        [[nodiscard]] std::size_t bufferGrowths() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        UiRenderStats lastStats_;
    };
} // namespace CNA::Studio
