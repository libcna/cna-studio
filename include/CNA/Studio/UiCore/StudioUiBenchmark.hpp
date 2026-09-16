// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioUiBenchmark.hpp
 * @brief What a frame of Studio's UI costs, and what each render backend does with it.
 *
 * `plan.md` STUDIO-04028. The measurement that has to exist before `STUDIO-04027` can delete a
 * renderer: `StudioModernUiRenderer` was written on the assumption that persistent GPU buffers
 * beat per-draw user arrays, and until now that was an assumption repeated rather than a number.
 *
 * ### Why this is CNA-free, and what that costs
 *
 * Everything below is computed from `UiDrawData` — the same bytes both backends are handed — so it
 * runs in the dependency-free build, on every push, with no GPU and no window. That matters more
 * than it sounds: a benchmark that only runs in the expensive CNA job is a benchmark nobody runs.
 *
 * What it therefore does **not** measure is GPU time. It measures what each backend *asks the
 * device to do*: bytes of geometry handed over, draw calls issued, textures bound, scissor
 * rectangles set. That is the right unit for this comparison anyway — a driver's answer to the
 * same submission varies by vendor, and the question `STUDIO-04027` has to answer is whether one
 * backend submits more work than the other, which is a property of Studio rather than of Mesa.
 *
 * **The model is checked against the renderers it models**, on the CNA-backed build, by
 * `TheCostModelAgreesWithWhatTheBackendsActuallyReport`. A model nobody compared against reality
 * is a second implementation with no tests.
 *
 * ### The difference the two backends actually have
 *
 * `CnaUiRenderer` draws each command with `DrawUserIndexedPrimitives`, which takes *user arrays*:
 * the driver copies the vertices and indices out on every call. And the vertex range it is handed
 * runs from the command's `vertexOffset` to the end of the list, because that is what CNA's
 * vertex-offset convention requires. So a list of V vertices drawn by N commands is copied
 * something close to N×V times — the cost is quadratic in how finely the UI is batched.
 *
 * `StudioModernUiRenderer` uploads each list once into a `DynamicVertexBuffer`/`DynamicIndexBuffer`
 * that grows to powers of two and never shrinks, then issues N `DrawIndexedPrimitives` against it.
 * V vertices, once.
 *
 * Both numbers come out of the same `UiDrawData` here, so the ratio is a fact about a real frame
 * of Studio rather than about a microbenchmark written to show one of them winning.
 */

#include "CNA/Studio/Ui/UiDrawData.hpp"

#include <cstddef>
#include <cstdint>

namespace CNA::Studio
{
    /**
     * @brief Bytes per vertex that Studio hands CNA.
     *
     * `sizeof(Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture)`. Fifty-six, for a
     * vertex whose data is twenty: a `Vector3` (12), a `Color` (**24**, because CNA's `Color`
     * carries a vtable of its own) and a `Vector2` (8), plus eight for this type's own vtable
     * pointer — it derives from `IVertexType`, which has a virtual destructor.
     *
     * This is the array both backends build and both hand to the graphics API, so it is the cost
     * of the scratch conversion and of the copy into CNA. It is **not** what reaches the GPU; see
     * @ref kStudioUiGpuVertexBytes.
     *
     * Stated here rather than taken from `sizeof` because this header is CNA-free by design, and
     * pinned to the real type by a `static_assert` in `CnaUiRenderer.cpp` — so a change to CNA's
     * vertex layout is a compile error naming this constant rather than a silent change to every
     * number `--ui-benchmark` prints.
     */
    inline constexpr std::size_t kStudioUiSubmittedVertexBytes = 56;

    /**
     * @brief Bytes per vertex that actually reach the GPU.
     *
     * `sizeof(CNA::Internal::Graphics::PositionColorTextureStream)`: three floats, four bytes of
     * colour, two floats. CNA repacks each `VertexPositionColorTexture` into this before upload,
     * and the vertex declaration's stride is this one — so the bus sees 24 where Studio wrote 56.
     *
     * Reported separately because they answer different questions. The submitted figure is what
     * Studio spends building and copying; the GPU figure is what the upload costs. Both differ
     * between the two backends by the same factor, because both backends build the same array and
     * differ only in how often they hand it over.
     *
     * Pinned the same way, in the same `static_assert`.
     */
    inline constexpr std::size_t kStudioUiGpuVertexBytes = 24;

    /** @brief Bytes per index: the draw lists use 16-bit indices, as `UiDrawList` declares. */
    inline constexpr std::size_t kStudioUiUploadIndexBytes = sizeof(std::uint16_t);

    /**
     * @brief What one frame of UI costs, in the units a render backend is charged in.
     *
     * Counts are what the *backends* count, which means they follow the same skips: a list with no
     * commands or no vertices is not drawn, a command with no indices is not drawn, and a command
     * whose clip rectangle selects no pixels is counted as clipped away rather than as a draw call.
     * A cost model that counted what was *emitted* rather than what is *submitted* would
     * consistently over-report both backends by the same amount and quietly change the ratio.
     */
    struct StudioUiFrameCost
    {
        /** @brief Draw lists that reached a backend's inner loop. */
        std::size_t drawLists = 0;
        /** @brief Vertices submitted, across every list. */
        std::size_t vertices = 0;
        /** @brief Indices submitted. */
        std::size_t indices = 0;
        /** @brief Triangles submitted. */
        std::size_t triangles = 0;
        /** @brief Draw calls issued. */
        std::size_t drawCalls = 0;
        /** @brief Commands skipped because their clip rectangle selected no pixels. */
        std::size_t clippedAway = 0;
        /** @brief Times a different texture was bound. */
        std::size_t textureChanges = 0;
        /** @brief Times the scissor rectangle was changed. */
        std::size_t clipChanges = 0;

        /** @brief Bytes of texture pixels uploaded this frame — atlas growth shows up here. */
        std::size_t textureBytesUploaded = 0;
        /** @brief Textures created this frame. */
        std::size_t texturesCreated = 0;
        /** @brief Textures partially updated this frame. */
        std::size_t texturesUpdated = 0;

        /**
         * @brief Bytes the classic backend hands CNA, across its user arrays.
         *
         * Per draw call, from the command's vertex offset to the end of its list, which is what
         * `DrawUserIndexedPrimitives` is given. In a list the toolkit has not had to split past
         * 65535 vertices — which is every list in practice — that offset is zero, so this is the
         * whole vertex array copied once per draw call.
         *
         * This is the quantity `UiRenderStats::geometryBytesUploaded` reports, which is what the
         * CNA host checks this model against frame by frame.
         */
        std::size_t classicSubmittedBytes = 0;

        /**
         * @brief Bytes the modern backend hands CNA, into its persistent buffers.
         *
         * Per list, once, whatever the commands over it do.
         */
        std::size_t modernSubmittedBytes = 0;

        /** @brief What the classic backend's submissions cost on the bus, after CNA repacks. */
        std::size_t classicGpuBytes = 0;

        /** @brief What the modern backend's submissions cost on the bus, after CNA repacks. */
        std::size_t modernGpuBytes = 0;
    };

    /**
     * @brief Measures what this frame costs each backend.
     *
     * @param drawData The frame, exactly as a backend receives it.
     * @return The counts and the two upload totals.
     */
    [[nodiscard]] StudioUiFrameCost studioUiFrameCost(const UiDrawData& drawData);

    /**
     * @brief Adds @p addend into @p total, for accumulating a run of frames.
     *
     * @param total Running total, modified in place.
     * @param addend One frame's cost.
     */
    void studioUiAccumulateCost(StudioUiFrameCost& total, const StudioUiFrameCost& addend);
} // namespace CNA::Studio
