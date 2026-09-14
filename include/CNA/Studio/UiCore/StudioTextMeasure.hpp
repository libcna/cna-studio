// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioTextMeasure.hpp
 * @brief How wide a string is, and how tall its line is, without knowing how glyphs are drawn.
 *
 * `plan.md` STUDIO-03026, STUDIO-04006.
 *
 * Layout needs text extents long before anything can rasterise a glyph: a menu is only as wide as
 * its widest item, a toolbar button only as wide as its label, and a tab strip can only decide
 * what fits once it can measure. So measurement is a seam of its own rather than a method on a
 * font atlas.
 *
 * The seam has two implementations and the caller never chooses between them. @ref
 * StudioFontSet is the real one, backed by glyph metrics. @ref approximateStudioTextMetrics is
 * the fallback used when no font set has been supplied — deliberately crude, deliberately close
 * enough for a layout to be well-formed, and deliberately *not* silently wrong about line height,
 * which is the measurement that decides whether text is vertically centred or a pixel off in every
 * control in the window.
 *
 * Widths are counted in **code points**, not bytes. A UTF-8 string of ten accented characters is
 * not twenty characters wide, and a layout that thinks it is will wrap a menu item that fits.
 */

#include "CNA/Studio/UiCore/StudioTheme.hpp"

#include <cstddef>
#include <string_view>

namespace CNA::Studio
{
    /**
     * @brief The measured extent of one line of text.
     *
     * Ascent and descent are carried separately rather than folded into a height, because the
     * baseline is what text is positioned on. Centring a label by its bounding box puts every
     * string with a descender a pixel low, and the error is invisible per control and obvious down
     * a column of them.
     */
    struct StudioTextMetrics
    {
        /** @brief Advance width of the whole string, in logical units. */
        float width = 0.0f;
        /** @brief Distance from the baseline to the top of the tallest glyph. */
        float ascent = 0.0f;
        /** @brief Distance from the baseline to the bottom of the deepest glyph. */
        float descent = 0.0f;
        /** @brief Baseline-to-baseline distance for consecutive lines. */
        float lineHeight = 0.0f;

        /** @brief Total ink height: ascent plus descent. */
        [[nodiscard]] float height() const { return ascent + descent; }

        /**
         * @brief The baseline y that vertically centres this line inside @p boxTop..@p boxBottom.
         *
         * Centres the *ink*, not the line box: a UI label is read as a shape, and a line box
         * padded by the font's own leading is not the shape the user sees.
         *
         * @param boxTop Top edge of the containing box.
         * @param boxHeight Height of the containing box.
         * @return The baseline y.
         */
        [[nodiscard]] float centeredBaseline(float boxTop, float boxHeight) const
        {
            return boxTop + (boxHeight - height()) * 0.5f + ascent;
        }
    };

    /**
     * @brief A source of text measurements.
     *
     * Implemented by the font atlas once it exists (STUDIO-04005). Until then the approximate
     * function below stands in, and because both answer the same question the layout code does not
     * change when the real one arrives.
     */
    class StudioFontSet
    {
    public:
        StudioFontSet() = default;
        virtual ~StudioFontSet() = default;

        StudioFontSet(const StudioFontSet&) = delete;
        StudioFontSet& operator=(const StudioFontSet&) = delete;
        StudioFontSet(StudioFontSet&&) = delete;
        StudioFontSet& operator=(StudioFontSet&&) = delete;

        /**
         * @brief Measures one line of UTF-8 text.
         * @param style Font to measure in, already DPI-scaled.
         * @param utf8 Text to measure. Must not contain line breaks.
         * @return The measured extent.
         */
        [[nodiscard]] virtual StudioTextMetrics measure(const StudioFontStyle& style,
                                                        std::string_view utf8) const = 0;
    };

    /**
     * @brief Counts the code points in a UTF-8 string, tolerating malformed input.
     *
     * Continuation bytes are not counted, so a malformed sequence measures short rather than
     * throwing or running off the end. Text arriving from a project file cannot be trusted to be
     * well-formed, and a measurement function is the wrong place to discover that.
     *
     * @param utf8 Text to count.
     * @return The number of code points.
     */
    [[nodiscard]] std::size_t countUtf8CodePoints(std::string_view utf8);

    /**
     * @brief Measures text without a font, from the style's size alone.
     *
     * The width is the code-point count times a fixed fraction of the size. That is wrong for any
     * particular string and right on average for UI labels, which is all a layout needs before
     * glyphs exist. Ascent, descent and line height use the proportions typical of a UI sans-serif
     * so that baselines computed against it land where the real ones will.
     *
     * @param style Font style, already DPI-scaled.
     * @param utf8 Text to measure.
     * @return The approximate extent.
     */
    [[nodiscard]] StudioTextMetrics approximateStudioTextMetrics(const StudioFontStyle& style,
                                                                 std::string_view utf8);
} // namespace CNA::Studio
