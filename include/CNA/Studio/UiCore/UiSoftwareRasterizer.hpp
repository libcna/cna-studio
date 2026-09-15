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

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief Textures uploaded so far, kept across frames the way a real renderer keeps them.
     *
     * A `UiDrawData` carries a texture *request* only on the frame the texture changed. That is
     * right for a renderer, which uploads once and keeps the result — and wrong for a rasterizer
     * that rebuilt its table from each frame's requests, because the second frame of any session
     * then has no font atlas and every glyph draws as a solid rectangle. That is not a subtle
     * degradation: it renders text as a row of blocks, and it did, in every multi-frame capture,
     * until a drop-preview screenshot made it obvious.
     *
     * One-frame callers need not bother with this: the overload without it builds a table from the
     * frame it is given, which for a single frame is the same thing.
     */
    class UiTextureTable
    {
    public:
        /**
         * @brief Applies @p drawData's texture requests: uploads, updates and destroys.
         *
         * The pixels are **copied**, as `UiTextureRequest` requires: its pointer is valid only for
         * the frame that produced it. This table used to keep the pointer instead, which worked
         * only because the one texture anybody uploads is a font atlas that outlives the frame
         * anyway — and stopped working the moment an Update arrived, because an Update's pointer
         * is the top-left of a *region* and reading it as a whole texture draws every glyph from
         * the wrong place (`STUDIO-04017`).
         *
         * @param drawData A frame whose requests should be applied.
         */
        void apply(const UiDrawData& drawData);

        /** @brief How many textures are currently held. */
        [[nodiscard]] std::size_t size() const { return entries_.size(); }

        /** @brief Whether @p id has been uploaded. */
        [[nodiscard]] bool contains(UiTextureId id) const { return entries_.count(id) != 0; }

        /** @brief One texture's pixels and shape. The pixels are this table's own copy. */
        struct Entry
        {
            int width = 0;
            int height = 0;
            int pitch = 0;
            std::vector<std::uint8_t> storage;

            /** @brief The top-left pixel, or nullptr when this entry holds nothing. */
            [[nodiscard]] const std::uint8_t* pixels() const
            {
                return storage.empty() ? nullptr : storage.data();
            }
        };

        /** @brief The texture for @p id, or nullptr. */
        [[nodiscard]] const Entry* find(UiTextureId id) const;

    private:
        std::map<UiTextureId, Entry> entries_;
    };

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
     * @brief Rasterises one frame against textures uploaded over several frames.
     *
     * @param drawData Geometry to rasterise.
     * @param clearColor Colour the image starts as.
     * @param textures Textures uploaded so far. @p drawData's own requests are applied to it first,
     *        so a caller can simply pass the same table every frame.
     * @return The rendered image.
     */
    [[nodiscard]] ImageBuffer rasterizeUiDrawData(const UiDrawData& drawData,
                                                  StudioColor clearColor,
                                                  UiTextureTable& textures);

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
