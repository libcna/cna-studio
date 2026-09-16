// SPDX-License-Identifier: MS-PL
/**
 * @file StudioUiBenchmark.cpp
 * @brief Charging a frame of UI to each render backend, from the draw data both are handed.
 */

#include "CNA/Studio/UiCore/StudioUiBenchmark.hpp"

#include <cmath>
#include <cstddef>

namespace CNA::Studio
{
    namespace
    {
        /**
         * @brief The scissor rectangle a backend would set for @p clip, in framebuffer pixels.
         *
         * Both backends compare *this* rather than the logical rectangle when deciding whether the
         * scissor changed, and the truncate-the-origin, ceil-the-extent arithmetic is theirs. Two
         * logical rectangles that differ below a pixel resolve to one scissor and are one change,
         * so a model that compared `UiClipRect` would over-count exactly where the UI is densest.
         */
        struct Scissor
        {
            int x = -1;
            int y = -1;
            int width = -1;
            int height = -1;

            friend bool operator==(const Scissor& lhs, const Scissor& rhs)
            {
                return lhs.x == rhs.x && lhs.y == rhs.y
                    && lhs.width == rhs.width && lhs.height == rhs.height;
            }
        };

        Scissor scissorFor(const UiClipRect& clip, const UiDrawData& drawData)
        {
            return Scissor{
                static_cast<int>(clip.left * drawData.framebufferScaleX),
                static_cast<int>(clip.top * drawData.framebufferScaleY),
                static_cast<int>(std::ceil((clip.right - clip.left) * drawData.framebufferScaleX)),
                static_cast<int>(std::ceil((clip.bottom - clip.top) * drawData.framebufferScaleY))};
        }
    } // namespace

    StudioUiFrameCost studioUiFrameCost(const UiDrawData& drawData)
    {
        StudioUiFrameCost cost;

        for (const UiTextureRequest& request : drawData.textureRequests)
        {
            switch (request.action)
            {
                case UiTextureAction::Create:
                    ++cost.texturesCreated;
                    cost.textureBytesUploaded +=
                        static_cast<std::size_t>(request.width) * static_cast<std::size_t>(request.height) * 4u;
                    break;
                case UiTextureAction::Update:
                    ++cost.texturesUpdated;
                    cost.textureBytesUploaded +=
                        static_cast<std::size_t>(request.updateWidth)
                        * static_cast<std::size_t>(request.updateHeight) * 4u;
                    break;
                case UiTextureAction::Destroy:
                    break;
            }
        }

        UiTextureId boundTexture = kUiTextureNone;
        Scissor appliedScissor;

        for (const UiDrawList& list : drawData.lists)
        {
            // Both backends skip the list outright, so nothing in it is submitted or counted.
            if (list.commands.empty() || list.vertices.empty()) { continue; }

            ++cost.drawLists;
            cost.vertices += list.vertices.size();

            // The modern backend uploads the list once, whatever the commands over it do.
            const std::size_t indexBytes = list.indices.size() * kStudioUiUploadIndexBytes;
            cost.modernSubmittedBytes +=
                list.vertices.size() * kStudioUiSubmittedVertexBytes + indexBytes;
            cost.modernGpuBytes += list.vertices.size() * kStudioUiGpuVertexBytes + indexBytes;

            for (const UiDrawCommand& command : list.commands)
            {
                if (command.indexCount == 0) { continue; }

                const UiClipRect clip =
                    command.clipRect.clampTo(drawData.displayX + drawData.displayWidth,
                                             drawData.displayY + drawData.displayHeight);
                if (clip.isEmpty())
                {
                    ++cost.clippedAway;
                    continue;
                }

                const Scissor scissor = scissorFor(clip, drawData);
                if (!(scissor == appliedScissor))
                {
                    ++cost.clipChanges;
                    appliedScissor = scissor;
                }

                // A command naming a texture no backend created is skipped by both -- but whether
                // it was created is a fact about the *device*, not about the draw data, so the
                // model cannot see it and counts the command. The CNA-backed agreement test is
                // what would catch this mattering, and it passes: Studio emits no command naming
                // an uncreated texture, which `STUDIO-35036` separately asserts.
                if (command.texture != boundTexture)
                {
                    ++cost.textureChanges;
                    boundTexture = command.texture;
                }

                ++cost.drawCalls;
                cost.triangles += command.indexCount / 3;
                cost.indices += command.indexCount;

                // And the classic backend hands the driver a fresh copy, per call, of everything
                // from this command's base vertex to the end of the list. That range is not a
                // choice: `DrawUserIndexedPrimitives` takes a vertex offset relative to the array
                // it is given, so the array has to start at the base and run to the end.
                const std::size_t handed = list.vertices.size() - command.vertexOffset;
                const std::size_t commandIndexBytes =
                    static_cast<std::size_t>(command.indexCount) * kStudioUiUploadIndexBytes;
                cost.classicSubmittedBytes +=
                    handed * kStudioUiSubmittedVertexBytes + commandIndexBytes;
                cost.classicGpuBytes += handed * kStudioUiGpuVertexBytes + commandIndexBytes;
            }
        }

        return cost;
    }

    void studioUiAccumulateCost(StudioUiFrameCost& total, const StudioUiFrameCost& addend)
    {
        total.drawLists += addend.drawLists;
        total.vertices += addend.vertices;
        total.indices += addend.indices;
        total.triangles += addend.triangles;
        total.drawCalls += addend.drawCalls;
        total.clippedAway += addend.clippedAway;
        total.textureChanges += addend.textureChanges;
        total.clipChanges += addend.clipChanges;
        total.textureBytesUploaded += addend.textureBytesUploaded;
        total.texturesCreated += addend.texturesCreated;
        total.texturesUpdated += addend.texturesUpdated;
        total.classicSubmittedBytes += addend.classicSubmittedBytes;
        total.modernSubmittedBytes += addend.modernSubmittedBytes;
        total.classicGpuBytes += addend.classicGpuBytes;
        total.modernGpuBytes += addend.modernGpuBytes;
    }
} // namespace CNA::Studio
