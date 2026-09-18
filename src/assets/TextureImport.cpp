// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/TextureImport.hpp"

#include "CNA/Studio/Core/Json.hpp"

#include <algorithm>
#include <string>

namespace CNA::Studio
{
    namespace
    {
        /** @brief The bytes one uncompressed or block-compressed mip level occupies. */
        std::uint64_t levelBytes(std::uint32_t width, std::uint32_t height,
                                 std::uint32_t bytesPerBlock)
        {
            // Zero marks the uncompressed case, where a pixel is four bytes and there are no
            // blocks. Everything else is DXT, whose smallest unit is a 4x4 block even when the
            // level is 1x1 -- which is exactly the part a hand-rolled estimate gets wrong, and the
            // reason the bottom of a mip chain is not as free as it looks.
            if (bytesPerBlock == 0)
            {
                return static_cast<std::uint64_t>(width) * height * 4u;
            }

            const std::uint64_t blocksAcross = (static_cast<std::uint64_t>(width) + 3u) / 4u;
            const std::uint64_t blocksDown = (static_cast<std::uint64_t>(height) + 3u) / 4u;
            return blocksAcross * blocksDown * bytesPerBlock;
        }
    }

    StudioTextureImportSettings StudioTextureImportSettings::fromJson(const JsonValue& importerSettings)
    {
        StudioTextureImportSettings settings;

        // Absent means "the user never chose", which is the declared default rather than a zero.
        // Reading a missing boolean as false would silently turn premultiplied alpha off for every
        // asset nobody has touched, which is most of them.
        const JsonValue& format = importerSettings["outputFormat"];
        if (format.asString() == "DxtCompressed")
        {
            settings.outputFormat = StudioTextureOutputFormat::DxtCompressed;
        }

        const JsonValue& srgb = importerSettings["srgbRead"];
        if (!srgb.isNull()) { settings.srgbRead = srgb.asBoolean(settings.srgbRead); }

        const JsonValue& mips = importerSettings["generateMipmaps"];
        if (!mips.isNull()) { settings.generateMipmaps = mips.asBoolean(settings.generateMipmaps); }

        const JsonValue& premultiply = importerSettings["premultiplyAlpha"];
        if (!premultiply.isNull())
        {
            settings.premultiplyAlpha = premultiply.asBoolean(settings.premultiplyAlpha);
        }

        return settings;
    }

    std::uint32_t studioMipLevelCount(int width, int height)
    {
        if (width <= 0 || height <= 0) { return 0; }

        std::uint32_t longest = static_cast<std::uint32_t>(std::max(width, height));
        std::uint32_t levels = 1;
        while (longest > 1u)
        {
            longest /= 2u;
            ++levels;
        }
        return levels;
    }

    StudioTextureImportPlan studioPlanTextureImport(const StudioTextureImportSettings& settings,
                                                    const StudioTextureSource& source)
    {
        StudioTextureImportPlan plan;

        // Refused before anything else, because every answer below rests on the two facts an
        // unmeasured file does not have: the size decides whether compression is possible at all,
        // and the alpha channel decides DXT1 against DXT5. A plan built on the struct's defaults
        // would answer "Dxt1" and "0 bytes" with exactly the confidence of a measured file -- and
        // a wrong answer stated plainly is worse here than no answer, because "0 bytes" reads as
        // "this costs nothing" and "Dxt1" reads as a decision somebody can rely on.
        if (!source.isMeasured())
        {
            plan.surfaceFormat.clear();
            plan.mipLevels = 0;
            plan.estimatedBytes = 0;
            plan.notes.push_back("Studio cannot read this file's header, so it cannot say what "
                                 "these settings produce.");
            return plan;
        }

        const bool wantsCompression = settings.outputFormat == StudioTextureOutputFormat::DxtCompressed;

        // DXT's unit is a 4x4 block, so an edge that is not a multiple of four has no whole-block
        // answer. Padding it is a decision about the user's art -- a row of invented pixels that
        // shows up as a seam when it is sampled -- so the setting is resolved back to Color and
        // said out loud instead.
        const bool blockAligned = source.width % 4 == 0 && source.height % 4 == 0;
        bool compresses = wantsCompression;

        if (wantsCompression && !blockAligned)
        {
            compresses = false;
            plan.notes.push_back("DXT needs both edges to be a multiple of 4, and this is "
                                 + std::to_string(source.width) + " x " + std::to_string(source.height)
                                 + ", so it imports uncompressed.");
        }

        std::uint32_t bytesPerBlock = 0;

        if (compresses)
        {
            if (source.hasAlphaChannel)
            {
                plan.surfaceFormat = settings.srgbRead ? "Dxt5SrgbEXT" : "Dxt5";
                bytesPerBlock = 16;
            }
            else if (!settings.srgbRead)
            {
                plan.surfaceFormat = "Dxt1";
                bytesPerBlock = 8;
            }
            else
            {
                // A real gap in CNA rather than a decision of Studio's: `SurfaceFormat` has
                // `Dxt5SrgbEXT` and `Bc7SrgbEXT` but no sRGB DXT1, although both D3D and OpenGL
                // define one (`BC1_UNORM_SRGB`, `COMPRESSED_SRGB_S3TC_DXT1_EXT`). So an opaque
                // texture that wants hardware sRGB has to be given the format with alpha it does
                // not need, at twice the size. Recorded in `plans/phase-10-asset-pipeline.md`
                // under STUDIO-10003 as a CNA gap, not worked around silently.
                plan.surfaceFormat = "Dxt5SrgbEXT";
                bytesPerBlock = 16;
                plan.notes.push_back("CNA has no sRGB DXT1, so this opaque texture imports as "
                                     "Dxt5SrgbEXT at twice the size. Turning sRGB Read off would "
                                     "make it Dxt1.");
            }
        }
        else
        {
            plan.surfaceFormat = settings.srgbRead ? "ColorSrgbEXT" : "Color";
        }

        const std::uint32_t fullChain = studioMipLevelCount(source.width, source.height);
        plan.mipLevels = settings.generateMipmaps ? fullChain : 1u;

        auto width = static_cast<std::uint32_t>(source.width);
        auto height = static_cast<std::uint32_t>(source.height);
        for (std::uint32_t level = 0; level < plan.mipLevels; ++level)
        {
            plan.estimatedBytes += levelBytes(width, height, bytesPerBlock);
            width = std::max(1u, width / 2u);
            height = std::max(1u, height / 2u);
        }

        return plan;
    }
}
