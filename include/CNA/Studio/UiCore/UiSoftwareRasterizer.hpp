// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/UiSoftwareRasterizer.hpp
 * @brief Rasterises `UiDrawData` on the CPU, so UI appearance can be tested without a GPU.
 *
 * `plan.md` STUDIO-03014, STUDIO-33011.
 *
 * ### Why this exists
 *
 * Screenshot testing a UI normally needs a graphics device, a window system and a CI machine with
 * a display -- which is why so many projects assert that the process exited cleanly and call it a
 * UI test. A window that opens blank and one that works are identical from the outside.
 *
 * `UiDrawData` is CNA-free, and so is this: the same geometry the CNA renderer will draw is
 * rasterised here into an `ImageBuffer`, in-process, deterministically, on any machine. The Studio
 * shell therefore gets golden-image regression tests from its first commit, long before the
 * graphical CI of STUDIO-33010 exists.
 *
 * ### What it is not
 *
 * It is **not** a second renderer, and nothing ships through it. It implements exactly what the UI
 * emits -- flat-shaded triangles with per-vertex colour, alpha blending and scissor clipping --
 * and no more. It is a test instrument, deliberately simple enough to be obviously correct.
 *
 * Its output is not expected to match a GPU's bit for bit: rasterisation fill rules and blend
 * rounding differ. That is why comparisons use the tolerant `compareImages` rather than equality,
 * for the same reason the backend comparison does.
 */

#include "CNA/Studio/Core/ImageDiff.hpp"
#include "CNA/Studio/Core/StudioMath.hpp"
#include "CNA/Studio/Ui/UiDrawData.hpp"

namespace CNA::Studio
{
    /**
     * @brief Rasterises one frame of UI geometry into an RGBA image.
     *
     * @param drawData Geometry to rasterise. Its display size determines the image size unless
     *        overridden.
     * @param clearColor Colour the image starts as.
     * @return The rendered image, top row first, or an empty buffer for empty draw data.
     */
    [[nodiscard]] ImageBuffer rasterizeUiDrawData(const UiDrawData& drawData,
                                                  StudioColor clearColor);

    /**
     * @brief Encodes an image as a PNG.
     *
     * Uncompressed deflate ("stored") blocks with a real zlib wrapper and CRCs: every decoder
     * reads it, and the encoder is small enough to review, which matters more here than file size
     * for an artefact whose purpose is to be looked at when a test fails.
     *
     * @param image Image to encode.
     * @return PNG bytes, or empty for a malformed image.
     */
    [[nodiscard]] std::vector<std::uint8_t> encodeImageAsPng(const ImageBuffer& image);

    /**
     * @brief Writes an image to a PNG file.
     * @param image Image to write.
     * @param path Destination path.
     * @return True when the file was written.
     */
    bool writeImageAsPng(const ImageBuffer& image, const std::string& path);
} // namespace CNA::Studio
