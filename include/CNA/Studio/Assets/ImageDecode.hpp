// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Assets/ImageDecode.hpp
 * @brief Turning an image file into pixels, with no graphics device and on any thread.
 *
 * `plan.md` STUDIO-10014.
 *
 * ### Why this exists
 *
 * The only thing in Studio that turned an image into pixels went through CNA's `Texture2D`, and
 * therefore through the graphics device — which is not thread-safe and belongs to the main thread.
 * So nothing could decode an image off the frame, and `STUDIO-09003` ("thumbnail generation as
 * cancellable background jobs") had no work to put in a job: a job that read the bytes and handed
 * them to the main thread to decode would have moved the cheap half off the frame and left the
 * expensive half on it.
 *
 * This is the CPU side. It is in `cna-studio-assets`, which is one of the CNA-free modules, so a
 * worker thread can call it in a build that has no CNA at all.
 *
 * ### What it will and will not read
 *
 * PNG, JPEG and BMP, which is exactly the set `readImageSize` already reads headers for — the two
 * must agree, or an asset would report a size in the inspector that nothing could then draw.
 * Formats are enabled one at a time rather than by taking everything the decoder offers: each one
 * is a parser reading files Studio did not write, and a format nobody imports is attack surface
 * with no user.
 *
 * Dimensions are bounded (@ref kMaximumDimension). A header claiming 2 000 000 000 pixels a side is
 * a few bytes to write and an allocation nobody survives, and refusing it by arithmetic before any
 * memory is asked for is cheaper and more reliable than hoping an allocator fails politely.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief The largest edge, in pixels, that will be decoded.
     *
     * Sixteen thousand is four times the longest edge any real texture has and still only about a
     * gigabyte at four bytes a pixel if both edges are at the limit, so the bound is felt by
     * malformed files and by nothing a user made.
     */
    inline constexpr std::uint32_t kMaximumDimension = 16384;

    /** @brief Pixels, and what shape they are in. */
    struct StudioDecodedImage
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        /**
         * @brief Always four: red, green, blue, alpha, eight bits each.
         *
         * Asked for rather than reported. A caller that had to handle one, three and four channels
         * would grow three code paths for a distinction it does not care about, and every consumer
         * Studio has — a thumbnail, a preview, an atlas — wants RGBA anyway.
         */
        static constexpr std::size_t kChannels = 4;

        /** @brief `width * height * kChannels` bytes, row-major, top row first. */
        std::vector<unsigned char> pixels;

        [[nodiscard]] bool isEmpty() const { return pixels.empty(); }
    };

    /** @brief What a decode produced, or why it did not. */
    struct StudioImageDecodeResult
    {
        StudioDecodedImage image;

        /** @brief Empty when the decode succeeded. */
        std::string error;

        [[nodiscard]] bool succeeded() const { return error.empty(); }
    };

    /**
     * @brief Decodes @p bytes into RGBA pixels.
     *
     * Reentrant and safe to call from several threads at once: it holds no state between calls and
     * touches no graphics device. That is the whole point of it.
     *
     * Failure is *reported*, never thrown and never a crash. The input is a file somebody put in a
     * project folder; it may be truncated, it may be a rename of something that is not an image at
     * all, and it may be hostile. A decoder that aborted the editor on a bad file would make one
     * broken asset cost the user their session.
     *
     * @param bytes The file's contents.
     * @param debugName What to call the file in an error message. Not opened or touched.
     * @return The pixels, or the reason there are none.
     */
    [[nodiscard]] StudioImageDecodeResult studioDecodeImage(const std::vector<unsigned char>& bytes,
                                                            const std::string& debugName = {});

    /**
     * @brief Reads @p absolutePath and decodes it.
     *
     * The file is read whole before decoding rather than streamed, because the decoder wants a
     * buffer and because a file that changes under a partial read is a decode nobody can reason
     * about — the watcher may well be rewriting it.
     *
     * @param absolutePath The file.
     * @return The pixels, or the reason there are none.
     */
    [[nodiscard]] StudioImageDecodeResult studioDecodeImageFile(const std::string& absolutePath);

    /**
     * @brief Whether @p path's extension names a format this build can decode.
     *
     * For a caller deciding whether a thumbnail is worth queueing at all. It answers from the name
     * only: a file that claims `.png` and holds something else still fails at the decode, which is
     * where a wrong answer is cheap.
     */
    [[nodiscard]] bool studioCanDecodeImageExtension(const std::string& path);
}
