// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/ImageDiff.hpp
 * @brief Comparing two rendered frames, pixel by pixel.
 *
 * plan.md ED-510. The question "do these two graphics backends draw the same picture?" is the one
 * CNA's compile-time backend (ANALYSIS.md finding F-01) makes both important and awkward: important
 * because a game ships on backends its author cannot all run at once, awkward because answering it
 * means comparing the output of several *processes*.
 *
 * The comparison itself is plain arithmetic over two buffers, and it lives here -- CNA-free, pure,
 * and tested against images built in the test rather than against a GPU. Decoding a PNG is not
 * arithmetic and needs a graphics API, so it stays behind a callback the caller supplies; see
 * `ImageReader` in `RuntimeBridge/BackendComparison.hpp`.
 *
 * **Why a tolerance exists at all.** Two backends rendering the same scene are not required to
 * produce bit-identical output and never will: different rasterisation rules, different filtering
 * precision, different rounding in the same blend equation. A comparison with no tolerance reports
 * every backend as different from every other, which is true and useless. A comparison with a
 * tolerance reports the differences a human would notice, which is the actual question.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Editor/Core/EditorMath.hpp"

namespace CNA::Editor
{
    /** @brief An 8-bit RGBA image in memory, top row first. */
    struct ImageBuffer
    {
        int width = 0;
        int height = 0;

        /** @brief `width * height * 4` bytes, in R, G, B, A order. */
        std::vector<std::uint8_t> pixels;

        [[nodiscard]] bool isEmpty() const { return width <= 0 || height <= 0 || pixels.empty(); }

        /** @brief Returns the number of pixels, which is not the size of @c pixels. */
        [[nodiscard]] std::size_t getPixelCount() const
        {
            return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        }

        /** @brief Returns true when @c pixels holds exactly the bytes @c width and @c height imply. */
        [[nodiscard]] bool isWellFormed() const
        {
            return !isEmpty() && pixels.size() == getPixelCount() * 4;
        }
    };

    /** @brief What two images differ by. */
    struct ImageDifference
    {
        /**
         * @brief False when the two could not be compared at all -- different sizes, or one empty.
         *
         * Kept apart from "they differ", because the two call for different actions: a size
         * mismatch means the capture went wrong, not that the backends disagree.
         */
        bool comparable = false;

        /** @brief Why they could not be compared. Empty when they could. */
        std::string incomparableReason;

        /** @brief Pixels whose largest channel difference exceeded the tolerance. */
        std::size_t differingPixels = 0;

        /** @brief Pixels compared, which is the whole image when comparable. */
        std::size_t totalPixels = 0;

        /** @brief The largest per-channel difference seen anywhere, 0..255. */
        int maxChannelDelta = 0;

        /**
         * @brief The smallest rectangle containing every differing pixel.
         *
         * Empty when nothing differed. Worth reporting because *where* two backends disagree is
         * usually the whole diagnosis -- a band along one edge is a viewport or scissor problem, a
         * scattering over one sprite is a filtering one.
         */
        EditorRectangle boundingBox;

        /** @brief Returns the differing fraction, 0 when nothing was compared. */
        [[nodiscard]] double getDifferingFraction() const
        {
            return totalPixels == 0 ? 0.0
                                    : static_cast<double>(differingPixels) / static_cast<double>(totalPixels);
        }

        /** @brief Returns true when the images are comparable and identical within the tolerance. */
        [[nodiscard]] bool matches() const { return comparable && differingPixels == 0; }
    };

    /**
     * @brief The default per-channel tolerance.
     *
     * Two backends drawing the same scene routinely differ by a step or two in a channel; a human
     * cannot see it and it says nothing about correctness. Anything a person would call "the same
     * picture" fits inside this, and anything they would call different does not.
     */
    inline constexpr int kDefaultImageTolerance = 2;

    /**
     * @brief Compares @p a and @p b, counting pixels that differ by more than @p tolerance.
     *
     * @param tolerance Largest per-channel difference still considered equal, 0..255.
     */
    [[nodiscard]] ImageDifference compareImages(const ImageBuffer& a,
                                                const ImageBuffer& b,
                                                int tolerance = kDefaultImageTolerance);

    /**
     * @brief Returns an image marking where @p a and @p b differ.
     *
     * The matching parts are kept as a dimmed copy of @p a and the differing ones are painted a
     * flat magenta, so the result reads as "here, on this picture" rather than as an abstract mask.
     * A plain XOR image is mathematically purer and nearly unreadable: the eye cannot tell a
     * one-step difference from a total mismatch in it, which is exactly the distinction being
     * looked for.
     *
     * Returns an empty image when the two cannot be compared.
     */
    [[nodiscard]] ImageBuffer makeDifferenceImage(const ImageBuffer& a,
                                                  const ImageBuffer& b,
                                                  int tolerance = kDefaultImageTolerance);
}
