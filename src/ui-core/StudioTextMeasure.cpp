// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTextMeasure.cpp
 * @brief UTF-8 code-point counting and the font-free approximate metrics.
 */

#include "CNA/Studio/UiCore/StudioTextMeasure.hpp"

namespace CNA::Studio
{
    namespace
    {
        /** @brief Mean advance as a fraction of the em size, for a UI sans-serif. */
        constexpr float kApproximateAdvanceRatio = 0.52f;
        /** @brief Ascent as a fraction of the em size. */
        constexpr float kApproximateAscentRatio = 0.78f;
        /** @brief Descent as a fraction of the em size. */
        constexpr float kApproximateDescentRatio = 0.22f;
        /** @brief Baseline-to-baseline distance as a fraction of the em size. */
        constexpr float kApproximateLineHeightRatio = 1.32f;
    } // namespace

    std::size_t countUtf8CodePoints(std::string_view utf8)
    {
        std::size_t count = 0;
        for (const char byte : utf8)
        {
            // Continuation bytes are 10xxxxxx. Everything else starts a code point, including the
            // malformed leading bytes -- counting those as one each keeps the measurement bounded
            // and monotonic on input nobody validated.
            if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U) { ++count; }
        }
        return count;
    }

    StudioTextMetrics approximateStudioTextMetrics(const StudioFontStyle& style,
                                                   std::string_view utf8)
    {
        StudioTextMetrics metrics;
        metrics.width = static_cast<float>(countUtf8CodePoints(utf8)) * style.sizePx
                      * kApproximateAdvanceRatio;
        metrics.ascent = style.sizePx * kApproximateAscentRatio;
        metrics.descent = style.sizePx * kApproximateDescentRatio;
        metrics.lineHeight = style.sizePx * kApproximateLineHeightRatio;
        return metrics;
    }
} // namespace CNA::Studio
