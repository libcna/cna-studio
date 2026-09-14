// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Ui/UiDrawData.hpp"

#include <algorithm>

namespace CNA::Editor
{
    UiClipRect UiClipRect::intersect(const UiClipRect& other) const
    {
        UiClipRect result;
        result.left = std::max(left, other.left);
        result.top = std::max(top, other.top);
        result.right = std::min(right, other.right);
        result.bottom = std::min(bottom, other.bottom);

        // A non-overlapping intersection would otherwise come out inverted (right < left), which
        // every downstream consumer would have to special-case. Normalising to empty here means
        // isEmpty() is the only check anyone needs.
        if (result.right < result.left) { result.right = result.left; }
        if (result.bottom < result.top) { result.bottom = result.top; }
        return result;
    }

    UiClipRect UiClipRect::clampTo(float width, float height) const
    {
        return intersect(UiClipRect{0.0f, 0.0f, width, height});
    }

    void UiDrawData::clearGeometry()
    {
        lists.clear();
        textureRequests.clear();
    }

    std::size_t UiDrawData::getTotalVertexCount() const
    {
        std::size_t total = 0;
        for (const UiDrawList& list : lists) { total += list.vertices.size(); }
        return total;
    }

    std::size_t UiDrawData::getTotalIndexCount() const
    {
        std::size_t total = 0;
        for (const UiDrawList& list : lists) { total += list.indices.size(); }
        return total;
    }

    std::size_t UiDrawData::getTotalCommandCount() const
    {
        std::size_t total = 0;
        for (const UiDrawList& list : lists) { total += list.commands.size(); }
        return total;
    }

    UiDrawDataValidation validate(const UiDrawData& drawData)
    {
        UiDrawDataValidation result;

        const auto fail = [&result](std::string problem) {
            result.valid = false;
            result.problems.push_back(std::move(problem));
        };

        for (std::size_t listIndex = 0; listIndex < drawData.lists.size(); ++listIndex)
        {
            const UiDrawList& list = drawData.lists[listIndex];
            const std::string prefix = "list " + std::to_string(listIndex) + ": ";

            for (std::size_t commandIndex = 0; commandIndex < list.commands.size(); ++commandIndex)
            {
                const UiDrawCommand& command = list.commands[commandIndex];
                const std::string where = prefix + "command " + std::to_string(commandIndex) + ": ";

                if (command.indexCount == 0) { continue; }

                if (command.indexCount % 3 != 0)
                {
                    fail(where + "index count " + std::to_string(command.indexCount)
                         + " is not a multiple of 3, but the renderer draws triangle lists");
                }

                const std::size_t indexEnd =
                    static_cast<std::size_t>(command.indexOffset) + command.indexCount;
                if (indexEnd > list.indices.size())
                {
                    fail(where + "indices [" + std::to_string(command.indexOffset) + ", "
                         + std::to_string(indexEnd) + ") exceed the index buffer of "
                         + std::to_string(list.indices.size()));
                    continue;
                }

                // Scan the actual indices rather than trusting a declared vertex count: a
                // renderer fed one bad index reads out of bounds, and an out-of-bounds read in
                // the UI renderer is a crash the user sees rather than a test failure.
                std::size_t highestVertex = 0;
                for (std::size_t offset = command.indexOffset; offset < indexEnd; ++offset)
                {
                    const std::size_t vertex =
                        static_cast<std::size_t>(list.indices[offset]) + command.vertexOffset;
                    highestVertex = std::max(highestVertex, vertex);
                }
                if (highestVertex >= list.vertices.size())
                {
                    fail(where + "highest referenced vertex " + std::to_string(highestVertex)
                         + " exceeds the vertex buffer of " + std::to_string(list.vertices.size()));
                }
            }
        }

        for (std::size_t requestIndex = 0; requestIndex < drawData.textureRequests.size(); ++requestIndex)
        {
            const UiTextureRequest& request = drawData.textureRequests[requestIndex];
            const std::string where = "texture request " + std::to_string(requestIndex) + ": ";

            if (request.action == UiTextureAction::Destroy) { continue; }

            if (request.pixels == nullptr) { fail(where + "no pixel data"); continue; }
            if (request.width <= 0 || request.height <= 0)
            {
                fail(where + "non-positive texture dimensions");
                continue;
            }
            if (request.updateWidth <= 0 || request.updateHeight <= 0)
            {
                fail(where + "non-positive update region");
                continue;
            }
            if (request.updateX < 0 || request.updateY < 0
                || request.updateX + request.updateWidth > request.width
                || request.updateY + request.updateHeight > request.height)
            {
                fail(where + "update region falls outside the texture");
            }
            if (request.pitch < request.updateWidth * 4)
            {
                fail(where + "pitch " + std::to_string(request.pitch)
                     + " is too small for an RGBA region " + std::to_string(request.updateWidth)
                     + " pixels wide");
            }
        }

        return result;
    }
}
