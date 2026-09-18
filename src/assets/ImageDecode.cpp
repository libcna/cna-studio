// SPDX-License-Identifier: MS-PL
/**
 * @file ImageDecode.cpp
 * @brief The CPU image decoder, and the only translation unit that instantiates stb_image.
 *
 * `plan.md` STUDIO-10014. See the header for why Studio decodes on the CPU at all.
 */

#include "CNA/Studio/Assets/ImageDecode.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

// One format at a time, rather than everything the decoder offers. Each one is a parser reading
// files Studio did not write; a format nobody imports is attack surface with no user. These three
// are exactly what `readImageSize` reads headers for, and the two have to agree or an asset would
// report a size in the inspector that nothing could then draw.
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP

// Studio reads the file itself and hands over a buffer, so the decoder needs no file access of its
// own -- and a decoder that cannot open a file is a decoder that cannot be talked into opening one.
#define STBI_NO_STDIO

// Not `STBI_NO_FAILURE_STRINGS`: the reason a file failed is the whole content of the error Studio
// shows a user, and "could not decode" without it sends them looking at the wrong thing.

// Internal linkage for every entry point, exactly as `StudioFontAtlas.cpp` does for stb_truetype
// and for the same reason: nothing outside this file has any business calling the decoder, and a
// symbol that is not exported cannot collide with a second copy somebody links later.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace CNA::Studio
{
    namespace
    {
        /** @brief @p text lowercased, for an extension comparison that does not care about case. */
        std::string lowered(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return text;
        }

        StudioImageDecodeResult failure(std::string reason)
        {
            StudioImageDecodeResult result;
            result.error = std::move(reason);
            return result;
        }
    }

    bool studioCanDecodeImageExtension(const std::string& path)
    {
        const std::size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) { return false; }

        const std::string extension = lowered(path.substr(dot));
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg"
            || extension == ".bmp";
    }

    StudioImageDecodeResult studioDecodeImage(const std::vector<unsigned char>& bytes,
                                              const std::string& debugName)
    {
        const std::string what = debugName.empty() ? std::string{"image"} : debugName;

        if (bytes.empty()) { return failure("'" + what + "' is empty"); }

        // Bounded before anything is allocated (see kMaximumDimension). A header claiming two
        // billion pixels a side is a few bytes to write and an allocation nobody survives, and
        // arithmetic is a more reliable refusal than hoping an allocator fails politely.
        int width = 0;
        int height = 0;
        int channels = 0;
        if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                  &channels)
            == 0)
        {
            const char* reason = stbi_failure_reason();
            return failure("'" + what + "' is not a PNG, JPEG or BMP this build can read"
                           + (reason != nullptr ? std::string{" ("} + reason + ")" : std::string{}));
        }

        if (width <= 0 || height <= 0)
        {
            return failure("'" + what + "' reports a zero dimension");
        }

        if (static_cast<std::uint32_t>(width) > kMaximumDimension
            || static_cast<std::uint32_t>(height) > kMaximumDimension)
        {
            return failure("'" + what + "' is " + std::to_string(width) + "x"
                           + std::to_string(height) + ", beyond the "
                           + std::to_string(kMaximumDimension) + " pixel limit");
        }

        // Four channels asked for rather than whatever the file holds, so a caller never grows a
        // code path for a distinction it does not care about.
        int decodedWidth = 0;
        int decodedHeight = 0;
        int fileChannels = 0;
        unsigned char* pixels = stbi_load_from_memory(
            bytes.data(), static_cast<int>(bytes.size()), &decodedWidth, &decodedHeight,
            &fileChannels, static_cast<int>(StudioDecodedImage::kChannels));

        if (pixels == nullptr)
        {
            const char* reason = stbi_failure_reason();
            return failure("'" + what + "' could not be decoded"
                           + (reason != nullptr ? std::string{": "} + reason : std::string{}));
        }

        // Checked rather than assumed: the size the header promised and the size the decode
        // produced disagreeing would mean reading past the buffer below, and a malformed file is
        // exactly where that would happen.
        if (decodedWidth != width || decodedHeight != height)
        {
            stbi_image_free(pixels);
            return failure("'" + what + "' decoded to a different size than its header declared");
        }

        StudioImageDecodeResult result;
        result.image.width = static_cast<std::uint32_t>(decodedWidth);
        result.image.height = static_cast<std::uint32_t>(decodedHeight);

        const std::size_t count = static_cast<std::size_t>(decodedWidth)
                                * static_cast<std::size_t>(decodedHeight)
                                * StudioDecodedImage::kChannels;
        result.image.pixels.assign(pixels, pixels + count);

        stbi_image_free(pixels);
        return result;
    }

    StudioImageDecodeResult studioDecodeImageFile(const std::string& absolutePath)
    {
        std::ifstream stream{absolutePath, std::ios::binary};
        if (!stream) { return failure("cannot open '" + absolutePath + "'"); }

        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{stream},
                                               std::istreambuf_iterator<char>{}};
        return studioDecodeImage(bytes, absolutePath);
    }
}
