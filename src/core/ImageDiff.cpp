// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/ImageDiff.hpp"

#include <algorithm>
#include <cstdlib>

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns the largest per-channel difference between the pixels at @p offset. */
        int channelDelta(const ImageBuffer& a, const ImageBuffer& b, std::size_t offset)
        {
            int largest = 0;
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const int delta = std::abs(static_cast<int>(a.pixels[offset + channel])
                                           - static_cast<int>(b.pixels[offset + channel]));
                largest = std::max(largest, delta);
            }
            return largest;
        }

        /** @brief Returns why @p a and @p b cannot be compared, or an empty string. */
        std::string findIncomparableReason(const ImageBuffer& a, const ImageBuffer& b)
        {
            if (!a.isWellFormed() || !b.isWellFormed())
            {
                return "one of the images is empty or its pixel buffer does not match its size";
            }
            if (a.width != b.width || a.height != b.height)
            {
                return "the images are different sizes: " + std::to_string(a.width) + "x"
                       + std::to_string(a.height) + " and " + std::to_string(b.width) + "x"
                       + std::to_string(b.height);
            }
            return {};
        }
    }

    ImageDifference compareImages(const ImageBuffer& a, const ImageBuffer& b, int tolerance)
    {
        ImageDifference difference;
        difference.incomparableReason = findIncomparableReason(a, b);
        if (!difference.incomparableReason.empty()) { return difference; }

        difference.comparable = true;
        difference.totalPixels = a.getPixelCount();

        const int allowed = std::clamp(tolerance, 0, 255);

        int minX = a.width;
        int minY = a.height;
        int maxX = -1;
        int maxY = -1;

        for (int y = 0; y < a.height; ++y)
        {
            for (int x = 0; x < a.width; ++x)
            {
                const auto offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width)
                     + static_cast<std::size_t>(x)) * 4;

                const int delta = channelDelta(a, b, offset);

                // The largest delta is recorded even when it is within tolerance. It is the number
                // that says whether two backends are *nearly* identical or merely close enough,
                // and a comparison that only reported the count could not tell those apart.
                difference.maxChannelDelta = std::max(difference.maxChannelDelta, delta);
                if (delta <= allowed) { continue; }

                ++difference.differingPixels;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }

        if (maxX >= 0)
        {
            difference.boundingBox =
                EditorRectangle{minX, minY, maxX - minX + 1, maxY - minY + 1};
        }
        return difference;
    }

    ImageBuffer makeDifferenceImage(const ImageBuffer& a, const ImageBuffer& b, int tolerance)
    {
        if (!findIncomparableReason(a, b).empty()) { return {}; }

        ImageBuffer result;
        result.width = a.width;
        result.height = a.height;
        result.pixels.resize(a.pixels.size());

        const int allowed = std::clamp(tolerance, 0, 255);

        for (std::size_t pixel = 0; pixel < a.getPixelCount(); ++pixel)
        {
            const std::size_t offset = pixel * 4;

            if (channelDelta(a, b, offset) > allowed)
            {
                // Flat magenta: it appears in no rendered scene by accident, and a colour that
                // could have come from the picture would be read as part of it.
                result.pixels[offset + 0] = 255;
                result.pixels[offset + 1] = 0;
                result.pixels[offset + 2] = 255;
                result.pixels[offset + 3] = 255;
                continue;
            }

            // Dimmed rather than dropped: the matching picture is the context that makes the
            // marked pixels mean something, and at a quarter brightness it cannot be confused
            // with them.
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                result.pixels[offset + channel] = static_cast<std::uint8_t>(a.pixels[offset + channel] / 4);
            }
            result.pixels[offset + 3] = 255;
        }
        return result;
    }
}
