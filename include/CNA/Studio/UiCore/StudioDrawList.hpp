// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioDrawList.hpp
 * @brief Turns styled primitives into the `UiDrawData` the CNA renderer already consumes.
 *
 * `plan.md` STUDIO-03014, STUDIO-04002, STUDIO-04003, STUDIO-04004.
 *
 * This is the piece that makes the UI migration a migration rather than a rewrite. `UiDrawData` is
 * the existing seam between "what to draw" and "how to draw it", and `CnaUiRenderer` already draws
 * it through CNA's public graphics API on every renderer. So the native Studio UI produces the
 * same structure the prototype's toolkit did, and inherits a working, tested CNA renderer on its
 * first frame instead of needing one written for it.
 *
 * Everything here is CNA-free and deterministic: the same calls produce the same vertices, which
 * is what makes golden-image testing possible with no GPU.
 *
 * ### Batching
 *
 * Consecutive primitives that share a texture and a clip rectangle extend the current draw command
 * rather than starting a new one. A panel of flat rectangles therefore costs one draw call, not
 * one per rectangle, and the command count is a number a test can assert on -- which is how
 * STUDIO-04003's batching requirement stays true rather than becoming true once and rotting.
 */

#include "CNA/Studio/Core/StudioMath.hpp"
#include "CNA/Studio/Ui/UiDrawData.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief Accumulates drawing commands for one frame.
     *
     * Usage is begin(), draw, end(), then hand the result to a renderer. The list owns its buffers
     * across frames so a steady-state frame allocates nothing.
     */
    class StudioDrawList
    {
    public:
        StudioDrawList();

        /**
         * @brief Starts a frame over a display of the given logical size.
         * @param displayWidth Width in logical units.
         * @param displayHeight Height in logical units.
         * @param framebufferScale Framebuffer pixels per logical unit.
         */
        void begin(float displayWidth, float displayHeight, float framebufferScale = 1.0f);

        /** @brief Finishes the frame and closes the open draw command. */
        void end();

        /** @brief The accumulated frame. Valid until the next begin(). */
        [[nodiscard]] const UiDrawData& drawData() const { return data_; }

        /**
         * @brief Pushes a clip rectangle, intersected with the one already in force.
         *
         * Nested clipping composes: a scroll area inside a panel inside a dock cannot draw outside
         * any of them, which is enforced here rather than trusted to each widget.
         *
         * @param rect Clip rectangle in logical units.
         */
        void pushClip(const UiRect& rect);

        /** @brief Pops the innermost clip rectangle. */
        void popClip();

        /** @brief The clip rectangle currently in force. */
        [[nodiscard]] UiRect currentClip() const;

        /**
         * @brief Fills a rectangle.
         * @param rect Area to fill, in logical units.
         * @param color Fill colour.
         */
        void fillRect(const UiRect& rect, StudioColor color);

        /**
         * @brief Draws a rectangle outline inside the given bounds.
         *
         * The border is drawn *inside* the rectangle rather than centred on its edge, so a
         * bordered control occupies exactly the space it was given. A centred border makes every
         * control half a pixel larger than its layout says, and the error accumulates down a
         * column of them.
         *
         * @param rect Bounds, in logical units.
         * @param color Border colour.
         * @param thickness Border thickness in logical units.
         */
        void strokeRect(const UiRect& rect, StudioColor color, float thickness = 1.0f);

        /**
         * @brief Fills a rectangle with rounded corners.
         *
         * A radius of zero, or one too large for the rectangle, degrades to a plain fill rather
         * than producing geometry that folds through itself.
         *
         * @param rect Area to fill.
         * @param color Fill colour.
         * @param radius Corner radius in logical units.
         */
        void fillRoundedRect(const UiRect& rect, StudioColor color, float radius);

        /**
         * @brief Fills a triangle.
         *
         * The primitive arrows, check marks and disclosure indicators are built from. Exposed
         * because the alternative -- approximating them with rotated rectangles -- produces
         * visibly wrong tips at small sizes, which is exactly the size these are drawn at.
         *
         * @param x0 First vertex x.
         * @param y0 First vertex y.
         * @param x1 Second vertex x.
         * @param y1 Second vertex y.
         * @param x2 Third vertex x.
         * @param y2 Third vertex y.
         * @param color Fill colour.
         */
        void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2,
                          StudioColor color);

        /**
         * @brief Draws a one-dimensional rule.
         * @param x0 Start x.
         * @param y0 Start y.
         * @param x1 End x.
         * @param y1 End y.
         * @param color Line colour.
         * @param thickness Line thickness.
         */
        void drawLine(float x0, float y0, float x1, float y1, StudioColor color,
                      float thickness = 1.0f);

        /**
         * @brief Draws a horizontal separator across the width of a rectangle.
         * @param rect Area whose top edge the rule sits on.
         * @param color Rule colour.
         * @param thickness Rule thickness.
         */
        void drawHorizontalSeparator(const UiRect& rect, StudioColor color, float thickness = 1.0f);

        /**
         * @brief Draws a focus ring just inside a rectangle.
         * @param rect Bounds of the focused element.
         * @param color Ring colour.
         * @param thickness Ring thickness.
         */
        void drawFocusRing(const UiRect& rect, StudioColor color, float thickness = 1.0f);

        /**
         * @brief Reserves a text run as a solid block of its measured extent.
         *
         * Glyph rasterization is STUDIO-04005 and needs a font atlas. Until that exists, text
         * occupies its measured box so that layout, clipping, batching and golden images are all
         * exercised on real geometry. This is scaffolding with a deliberate end date, not a
         * placeholder that will quietly ship: the block is drawn in the caller's text colour at a
         * reduced alpha so that a build which still has it is visibly unfinished.
         *
         * @param rect Box the text occupies.
         * @param color Text colour.
         */
        void drawTextPlaceholder(const UiRect& rect, StudioColor color);

        /** @brief Number of draw commands emitted so far this frame. */
        [[nodiscard]] std::size_t commandCount() const;

        /** @brief Number of vertices emitted so far this frame. */
        [[nodiscard]] std::size_t vertexCount() const;

    private:
        /** @brief Ensures the open command matches this texture and clip, starting a new one if not. */
        void ensureCommand(UiTextureId texture);

        /** @brief Whether an axis-aligned bound lies entirely outside the clip in force. */
        [[nodiscard]] bool isClippedAway(float x0, float y0, float x1, float y1) const;

        /** @brief Appends one quad, batching into the open command. */
        void addQuad(float x0, float y0, float x1, float y1, StudioColor color);

        /** @brief Appends one triangle. */
        void addTriangle(float x0, float y0, float x1, float y1, float x2, float y2,
                         StudioColor color);

        UiDrawData data_;
        std::vector<UiRect> clipStack_;
        bool commandOpen_ = false;
    };

    /**
     * @brief Packs a colour into the RGBA byte order `UiVertex` uses.
     * @param color Colour to pack.
     * @return The packed value, R in the lowest byte.
     */
    [[nodiscard]] std::uint32_t packUiColor(StudioColor color);
} // namespace CNA::Studio
