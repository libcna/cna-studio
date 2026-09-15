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
         * @brief Points untextured geometry at a white texel in a texture.
         *
         * Every flat fill then samples one opaque white pixel and multiplies it by its vertex
         * colour, which is a no-op visually and means a fill and the glyphs beside it share a draw
         * call instead of forcing a texture change between them. Set to @ref kUiTextureNone to go
         * back to binding no texture, which is what a build with no font atlas does.
         *
         * @param texture Texture holding the white texel.
         * @param u Texture coordinate of the texel.
         * @param v Texture coordinate of the texel.
         */
        void setDefaultTexture(UiTextureId texture, float u, float v);

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
         * @brief Draws one glyph from a texture atlas.
         *
         * The colour multiplies the atlas sample, so one white-with-coverage-in-alpha atlas serves
         * every text colour in the theme -- which is what keeps text in three fonts, four sizes and
         * a dozen colours inside a single draw call.
         *
         * @param rect Where the glyph's ink goes, in logical units.
         * @param u0 Left texture coordinate.
         * @param v0 Top texture coordinate.
         * @param u1 Right texture coordinate.
         * @param v1 Bottom texture coordinate.
         * @param texture The atlas.
         * @param color Text colour.
         */
        void drawGlyph(const UiRect& rect, float u0, float v0, float u1, float v1,
                       UiTextureId texture, StudioColor color);

        /**
         * @brief Draws a whole texture into a rectangle.
         *
         * The same primitive as @ref drawGlyph, named for what it is used for: compositing an
         * offscreen render — the 3D viewport, an asset thumbnail — into the UI.
         *
         * @param rect Where it goes.
         * @param texture Texture to sample.
         * @param flipVertically Sample bottom-up. Some CNA renderers present a sampled render
         *        target flipped relative to others and CNA does not normalise it or publish the
         *        convention (CNA gap G-03), so the caller that knows which renderer it is on says
         *        so here. Swapping the coordinates rather than the geometry keeps the rectangle's
         *        own layout, hit-testing and clipping untouched.
         * @param tint Multiplied into the sampled colour. White leaves it alone.
         */
        void drawImage(const UiRect& rect, UiTextureId texture, bool flipVertically = false,
                       StudioColor tint = StudioColor{255, 255, 255, 255});

        /**
         * @brief Queues a texture creation or update for the renderer to perform before drawing.
         *
         * Requests are applied ahead of every draw command in the frame, so a glyph rasterised at
         * any point during the frame still reaches the GPU before the quad that samples it. The
         * prototype shipped the opposite once (legacy ED-119): glyphs first needed on a frame that
         * did not upload appeared one frame late, which reads as text flickering in.
         *
         * @param request The request. Its pixels must stay valid until the frame is rendered.
         */
        void addTextureRequest(const UiTextureRequest& request);

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

        /** @brief Appends one textured quad, batching into the open command. */
        void addTexturedQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                             float v1, UiTextureId texture, StudioColor color);

        /** @brief Appends one triangle. */
        void addTriangle(float x0, float y0, float x1, float y1, float x2, float y2,
                         StudioColor color);

        UiDrawData data_;
        std::vector<UiRect> clipStack_;
        bool commandOpen_ = false;

        UiTextureId defaultTexture_ = kUiTextureNone;
        float defaultU_ = 0.0f;
        float defaultV_ = 0.0f;
    };

    /**
     * @brief Packs a colour into the RGBA byte order `UiVertex` uses.
     * @param color Colour to pack.
     * @return The packed value, R in the lowest byte.
     */
    [[nodiscard]] std::uint32_t packUiColor(StudioColor color);
} // namespace CNA::Studio
