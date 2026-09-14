// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioFontAtlas.hpp
 * @brief Real glyphs: rasterization, packing, metrics and kerning for the Studio UI.
 *
 * `plan.md` STUDIO-04005, STUDIO-04006, STUDIO-04009.
 *
 * ### Why an atlas, and why Studio owns it
 *
 * A GPU draws textured triangles. Text is therefore a texture with every glyph in it and one quad
 * per glyph, and somebody has to rasterise the outlines and decide where each one goes. CNA has no
 * public glyph rasterizer (CNA gap G-04), and Studio needs one it controls anyway: its UI must
 * look identical on every renderer, and a font resolved from the host system would make Studio
 * look different on every machine — which for a tool whose screenshots are its documentation is a
 * real cost, not a cosmetic one.
 *
 * ### Rasterised at the size it is drawn at
 *
 * Each `(face, pixel size)` pair is rasterised separately rather than scaled from one master size.
 * Scaling a bitmap font is exactly the blurry text that makes an application look amateur at 125%
 * and 150% DPI — the two most common scales on Windows laptops. The atlas builds the sizes it is
 * asked for, and a DPI change asks for different ones.
 *
 * ### One atlas, one texture, one draw call
 *
 * Every face and size shares one texture. Text in three fonts and four sizes therefore costs the
 * same single draw call as text in one, which is what keeps a dense editor frame in the small
 * bounded number of draw calls `STUDIO-04003` asserts.
 *
 * ### The shelf packer
 *
 * Glyphs are packed into horizontal shelves: a new glyph goes on the current shelf if it fits, and
 * starts a new one otherwise. This wastes some vertical space against a perfect packer, and it is
 * a hundred lines rather than a thousand, deterministic, and easy to look at when something is
 * wrong. A UI font atlas holds a few hundred glyphs of similar height, which is the case shelf
 * packing is good at.
 */

#include "CNA/Studio/UiCore/StudioTextMeasure.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/Ui/UiDrawData.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CNA::Studio
{
    class StudioFontAtlas;

    /** @brief Which shipped typeface a font style resolves to. */
    enum class StudioTypeface : std::uint8_t
    {
        /** @brief IBM Plex Sans Regular: body text, labels, menus. */
        SansRegular,
        /** @brief IBM Plex Sans SemiBold: headings and emphasis. */
        SansSemiBold,
        /** @brief IBM Plex Mono Regular: logs, identifiers and numeric columns that must align. */
        Monospace,
        /** @brief Number of shipped typefaces; not itself one. */
        Count
    };

    /** @brief Returns a stable English name for a typeface, for diagnostics and theme files. */
    [[nodiscard]] std::string_view studioTypefaceName(StudioTypeface typeface);

    /**
     * @brief Resolves a theme font style to a shipped typeface.
     *
     * By family name when the style names one of the shipped families, and by weight otherwise —
     * so a theme that asks for an unknown family gets Studio's own font at the right weight rather
     * than nothing at all. A missing glyph is a visible hole; a missing *font* is a blank window.
     *
     * @param style Font style from the theme.
     * @return The typeface to rasterise with.
     */
    [[nodiscard]] StudioTypeface resolveStudioTypeface(const StudioFontStyle& style);

    /** @brief One rasterised glyph's placement, in the atlas and on the baseline. */
    struct StudioGlyph
    {
        /** @brief The code point this is for. */
        char32_t codepoint = 0;

        /** @brief How far the pen moves after drawing it, in pixels. */
        float advance = 0.0f;
        /** @brief Offset from the pen to the left edge of the ink. */
        float bearingX = 0.0f;
        /**
         * @brief Offset from the **baseline** to the top edge of the ink, downwards positive.
         *
         * Negative for anything with ink above the baseline, which is most glyphs. Stored the way
         * the rasterizer reports it rather than flipped, because flipping it here would mean
         * flipping it back in the one place that positions a quad.
         */
        float bearingY = 0.0f;

        /** @brief Ink width in pixels. */
        int width = 0;
        /** @brief Ink height in pixels. */
        int height = 0;

        /** @brief Texture coordinates of the ink in the atlas, normalised. */
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;

        /** @brief Whether this glyph has ink at all. A space does not. */
        [[nodiscard]] bool hasInk() const { return width > 0 && height > 0; }
    };

    /** @brief One typeface rasterised at one pixel size. */
    class StudioFontFace
    {
    public:
        /** @brief Vertical metrics, in pixels at this face's size. */
        [[nodiscard]] float ascent() const { return ascent_; }
        /** @brief Distance from the baseline to the deepest descender, positive. */
        [[nodiscard]] float descent() const { return descent_; }
        /** @brief Baseline-to-baseline distance for consecutive lines. */
        [[nodiscard]] float lineHeight() const { return lineHeight_; }
        /** @brief The pixel size this face was rasterised at. */
        [[nodiscard]] float sizePx() const { return sizePx_; }
        /** @brief Which typeface this is. */
        [[nodiscard]] StudioTypeface typeface() const { return typeface_; }

        /**
         * @brief Returns a glyph, rasterising it on first use.
         * @param codepoint Code point to look up.
         * @return The glyph, or nullptr when the face has no outline for it.
         */
        [[nodiscard]] const StudioGlyph* glyph(char32_t codepoint) const;

        /**
         * @brief The kerning adjustment between two code points, in pixels.
         *
         * Usually negative: kerning pulls pairs like `AV` and `To` together. Without it a UI's
         * text looks subtly loose in exactly the places a reader notices — which is why it is here
         * rather than on a list of refinements.
         *
         * @param left Preceding code point.
         * @param right Following code point.
         * @return The adjustment to add to the pen after @p left.
         */
        [[nodiscard]] float kerning(char32_t left, char32_t right) const;

        /** @brief Number of glyphs rasterised into the atlas for this face so far. */
        [[nodiscard]] std::size_t glyphCount() const { return glyphs_.size(); }

    private:
        friend class StudioFontAtlas;

        StudioFontAtlas* atlas_ = nullptr;
        StudioTypeface typeface_ = StudioTypeface::SansRegular;
        float sizePx_ = 13.0f;
        float scale_ = 1.0f;
        float ascent_ = 0.0f;
        float descent_ = 0.0f;
        float lineHeight_ = 0.0f;

        mutable std::unordered_map<char32_t, StudioGlyph> glyphs_;
        mutable std::unordered_map<std::uint64_t, float> kerning_;
    };

    /**
     * @brief Rasterises, packs and measures the Studio UI's text.
     *
     * Implements @ref StudioFontSet, so a @ref StudioFrame given one measures real text and every
     * layout that already asked for measurements gets correct answers with no change.
     */
    class StudioFontAtlas final : public StudioFontSet
    {
    public:
        /** @brief Side of the atlas texture, in pixels. */
        static constexpr int kAtlasSize = 1024;

        /**
         * @brief The texture id the atlas uses.
         *
         * Deliberately far from where the Dear ImGui path allocates. The two UIs never draw in the
         * same frame, so a collision cannot happen today — but one that *could* happen would be
         * found as a font atlas drawn where a scene thumbnail belongs, which is a long way from
         * its cause.
         */
        static constexpr UiTextureId kTextureId = 0x5000'0001ULL;

        StudioFontAtlas();
        ~StudioFontAtlas() override;

        /**
         * @brief Returns a face, rasterising its metrics on first use.
         * @param typeface Typeface wanted.
         * @param sizePx Pixel size, already DPI-scaled.
         * @return The face; never null.
         */
        [[nodiscard]] const StudioFontFace& face(StudioTypeface typeface, float sizePx);

        /**
         * @brief Returns the face a theme font style resolves to.
         * @param style Font style, already DPI-scaled.
         * @return The face; never null.
         */
        [[nodiscard]] const StudioFontFace& face(const StudioFontStyle& style);

        [[nodiscard]] StudioTextMetrics measure(const StudioFontStyle& style,
                                                std::string_view utf8) const override;

        /**
         * @brief Ensures every glyph of @p utf8 is rasterised into the atlas.
         *
         * Called before the draw pass emits quads, because a glyph first needed *while* emitting
         * geometry would be uploaded after the draw command that references it. The prototype
         * shipped exactly that bug once (legacy ED-119), and the fix is to make the phase explicit
         * rather than to remember.
         *
         * @param style Font style to rasterise in.
         * @param utf8 Text whose glyphs are needed.
         */
        void prepare(const StudioFontStyle& style, std::string_view utf8);

        /** @brief Whether the atlas has pixels that the renderer has not been given yet. */
        [[nodiscard]] bool hasPendingUpload() const { return dirty_; }

        /**
         * @brief Produces the texture request that uploads the atlas, and clears the dirty flag.
         *
         * The whole atlas, not the changed region: a partial update is a correctness liability for
         * a handful of kilobytes on a texture that settles within a few frames of start-up.
         *
         * @return The request. Its pixels are owned by the atlas and valid until the next
         *         rasterization.
         */
        [[nodiscard]] UiTextureRequest takeUploadRequest();

        /**
         * @brief Texture coordinate of a fully opaque white texel reserved in the atlas.
         *
         * Untextured geometry samples this rather than binding no texture, so a frame of panels,
         * borders and text is one draw call instead of one per alternation between them. Without
         * it, drawing a label inside a button costs three: the fill, the glyphs, the next fill.
         *
         * @return The texture coordinate.
         */
        [[nodiscard]] float whitePixelU() const { return whiteU_; }

        /** @brief Texture coordinate of the reserved white texel, on v. */
        [[nodiscard]] float whitePixelV() const { return whiteV_; }

        /** @brief The atlas pixels, RGBA, top row first. For tests and for the software renderer. */
        [[nodiscard]] const std::vector<std::uint8_t>& pixels() const { return pixels_; }

        /** @brief Fraction of the atlas area used so far, in `[0, 1]`. */
        [[nodiscard]] float occupancy() const;

        /** @brief Number of glyphs the atlas could not fit. Non-zero means the atlas is full. */
        [[nodiscard]] std::size_t droppedGlyphs() const { return dropped_; }

        /**
         * @brief Decodes the next code point of a UTF-8 string.
         *
         * Malformed input yields U+FFFD and advances one byte, so a corrupt string renders as
         * replacement characters rather than looping or running off the end. Text arriving from a
         * project file is not text this code wrote.
         *
         * @param utf8 The string.
         * @param offset Byte offset to read from; advanced past the code point.
         * @return The code point.
         */
        [[nodiscard]] static char32_t decodeUtf8(std::string_view utf8, std::size_t& offset);

    private:
        struct Impl;

        friend class StudioFontFace;

        /** @brief Rasterises one glyph into the atlas, or reports that it has no outline. */
        [[nodiscard]] const StudioGlyph* rasterize(const StudioFontFace& face,
                                                   char32_t codepoint) const;

        /** @brief Looks up kerning between two code points for a face. */
        [[nodiscard]] float lookupKerning(const StudioFontFace& face, char32_t left,
                                          char32_t right) const;

        std::unique_ptr<Impl> impl_;
        mutable std::vector<std::uint8_t> pixels_;
        float whiteU_ = 0.0f;
        float whiteV_ = 0.0f;
        mutable bool dirty_ = true;
        mutable std::size_t dropped_ = 0;
    };
} // namespace CNA::Studio
