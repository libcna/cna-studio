// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Ui/UiDrawData.hpp
 * @brief The geometry an immediate-mode UI produces, described without reference to any toolkit.
 *
 * This type is the seam between "what to draw" and "how to draw it" (ANALYSIS.md decision D-14).
 * `ImGuiEditorUi` fills it; `CnaUiRenderer` consumes it through CNA's public graphics API. Neither
 * knows about the other, and in particular **the renderer never includes a Dear ImGui header** --
 * which is what makes "the editor UI renders through CNA's public API" a structural property
 * rather than a claim.
 *
 * It costs nothing. ImGui's own vertex layout (`ImVec2 pos; ImVec2 uv; ImU32 col`) does not match
 * CNA's `VertexPositionColorTexture` (`Vector3; Color; Vector2`), so a per-vertex repack is
 * required whichever way this is structured. Doing it while filling UiDrawData means it happens
 * exactly once.
 *
 * The three payoffs:
 *
 * 1. The renderer can be reviewed, and its CNA API usage checked, without ImGui in the picture.
 * 2. The whole editor UI runs headless in CI: build ImGui draw data, assert it is well-formed,
 *    with no window and no GPU. `tests/UiTests.cpp` does exactly this.
 * 3. A second UI toolkit, or a second renderer, plugs in at a stable boundary.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Editor
{
    /**
     * @brief An opaque renderer-side texture handle.
     *
     * Zero is the "no texture" sentinel. The renderer assigns these; the UI layer only ever
     * carries them around.
     */
    using UiTextureId = std::uint64_t;

    /** @brief The value meaning "no texture". */
    inline constexpr UiTextureId kUiTextureNone = 0;

    /**
     * @brief One UI vertex.
     *
     * Position is in framebuffer-relative pixels, not normalised coordinates -- the renderer
     * builds the orthographic projection that maps them.
     *
     * @c rgba packs the colour as R in the lowest byte through A in the highest, matching Dear
     * ImGui's default `IM_COL32` layout. The renderer unpacks it into a CNA `Color`; keeping the
     * packed form here avoids widening every vertex by three bytes for no gain.
     */
    struct UiVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint32_t rgba = 0xFFFFFFFFu;
    };

    /**
     * @brief A scissor rectangle in framebuffer pixels, as (left, top, right, bottom).
     *
     * Stored as edges rather than as position-plus-size because that is the form both ImGui and
     * every clipping operation use; converting to a CNA `Rectangle` happens once, in the renderer.
     */
    struct UiClipRect
    {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;

        /** @brief Returns true when the rectangle selects no pixels. */
        [[nodiscard]] bool isEmpty() const { return right <= left || bottom <= top; }

        /** @brief Returns the intersection of this rectangle with @p other. */
        [[nodiscard]] UiClipRect intersect(const UiClipRect& other) const;

        /** @brief Clamps this rectangle to a framebuffer of @p width by @p height pixels. */
        [[nodiscard]] UiClipRect clampTo(float width, float height) const;

        friend bool operator==(const UiClipRect& lhs, const UiClipRect& rhs)
        {
            return lhs.left == rhs.left && lhs.top == rhs.top
                && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
        }
    };

    /**
     * @brief One draw call: a run of indices sharing a texture and a clip rectangle.
     *
     * @c vertexOffset is added to every index before lookup. It exists because a UI can emit more
     * than 65535 vertices in one list while still using 16-bit indices -- the toolkit splits the
     * list and offsets the base vertex, exactly as ImGui's `ImDrawCmd::VtxOffset` does.
     */
    struct UiDrawCommand
    {
        std::uint32_t indexOffset = 0;
        std::uint32_t indexCount = 0;
        std::uint32_t vertexOffset = 0;
        UiClipRect clipRect;
        UiTextureId texture = kUiTextureNone;
    };

    /** @brief One draw list: a vertex buffer, a 16-bit index buffer, and the commands over them. */
    struct UiDrawList
    {
        std::vector<UiVertex> vertices;
        std::vector<std::uint16_t> indices;
        std::vector<UiDrawCommand> commands;
    };

    /** @brief What the renderer is being asked to do with a texture this frame. */
    enum class UiTextureAction
    {
        /** @brief Create the texture from the supplied pixels and report back the assigned id. */
        Create,
        /** @brief Replace a rectangular region of an existing texture. */
        Update,
        /** @brief Release the texture. */
        Destroy
    };

    /**
     * @brief A texture creation, update or destruction the renderer must honour before drawing.
     *
     * The UI toolkit owns font atlases and may grow or re-rasterise them at any time, so texture
     * lifetime is driven from the UI side rather than set up once. Pixels are always 32-bit RGBA,
     * eight bits per channel, tightly packed at @c pitch bytes per row.
     */
    struct UiTextureRequest
    {
        UiTextureAction action = UiTextureAction::Create;

        /** @brief The texture to act on. Zero for a Create, which assigns one. */
        UiTextureId texture = kUiTextureNone;

        /** @brief Full texture dimensions, in pixels. */
        int width = 0;
        int height = 0;

        /** @brief Region to write, for Update. Equals the whole texture for Create. */
        int updateX = 0;
        int updateY = 0;
        int updateWidth = 0;
        int updateHeight = 0;

        /**
         * @brief Pointer to the top-left pixel of the region, owned by the UI toolkit.
         *
         * Valid only for the duration of the frame that produced this request. The renderer must
         * copy what it needs rather than retain the pointer.
         */
        const std::uint8_t* pixels = nullptr;

        /** @brief Bytes per row in @c pixels. */
        int pitch = 0;
    };

    /**
     * @brief Everything the renderer needs for one frame.
     *
     * @c framebufferScale is the ratio of framebuffer pixels to logical points, which is 1 on an
     * ordinary display and 2 on a HiDPI one. Vertex positions and clip rectangles are in *logical*
     * units; the renderer multiplies by this to reach framebuffer pixels.
     */
    struct UiDrawData
    {
        std::vector<UiDrawList> lists;

        /** @brief Texture work to perform before any drawing. */
        std::vector<UiTextureRequest> textureRequests;

        /** @brief Top-left of the drawing area, in logical units. */
        float displayX = 0.0f;
        float displayY = 0.0f;

        /** @brief Size of the drawing area, in logical units. */
        float displayWidth = 0.0f;
        float displayHeight = 0.0f;

        float framebufferScaleX = 1.0f;
        float framebufferScaleY = 1.0f;

        /** @brief Removes every list, command and texture request, keeping the display metrics. */
        void clearGeometry();

        /** @brief Returns the total number of vertices across every list. */
        [[nodiscard]] std::size_t getTotalVertexCount() const;

        /** @brief Returns the total number of indices across every list. */
        [[nodiscard]] std::size_t getTotalIndexCount() const;

        /** @brief Returns the total number of draw commands across every list. */
        [[nodiscard]] std::size_t getTotalCommandCount() const;

        /** @brief Returns true when there is nothing to draw. */
        [[nodiscard]] bool isEmpty() const { return getTotalIndexCount() == 0; }
    };

    /**
     * @brief What UiDrawData::validate found wrong, if anything.
     *
     * Validation exists because a renderer fed malformed draw data reads out of bounds, and an
     * out-of-bounds read in a UI renderer is a crash the user sees rather than a test failure.
     * Checking is cheap next to the drawing itself, and it turns a class of would-be crashes into
     * a diagnosable message.
     */
    struct UiDrawDataValidation
    {
        bool valid = true;
        std::vector<std::string> problems;
    };

    /**
     * @brief Checks that every command indexes within its list's buffers.
     *
     * Verifies: index ranges lie inside the index buffer; every index plus its command's vertex
     * offset lies inside the vertex buffer; index counts are multiples of three, since the
     * renderer draws triangle lists; and no command carries a degenerate clip rectangle.
     */
    [[nodiscard]] UiDrawDataValidation validate(const UiDrawData& drawData);
}
