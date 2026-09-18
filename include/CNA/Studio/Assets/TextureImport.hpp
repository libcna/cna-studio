// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/TextureImport.hpp
 * @brief What a texture's import settings mean, worked out rather than stored.
 *
 * `plan.md` STUDIO-10003.
 *
 * ### Why this is a function and not a sidecar field
 *
 * An importer setting is what the *user* chose and a fact is what the *file* says
 * (`STUDIO-10001`). "Which `SurfaceFormat` does this end up as", "how many mip levels is that" and
 * "what does it cost" are neither: they are the two combined, and writing a combination into the
 * sidecar is how a project ends up with a `mipLevels: 11` sitting next to a "Generate Mipmaps" that
 * was switched off half an hour ago. So the derivation lives here, is computed where it is shown,
 * and is stored nowhere.
 *
 * ### What Studio does and does not do here
 *
 * It decides, and it says what it decided. It does not compress anything: block compression is the
 * content build's work in CNA, and moving it into the editor would be exactly the responsibility
 * shift the project forbids. The value of this file is that a user can see *before* the build what
 * their settings will produce, including the cases where what they asked for cannot be given to
 * them.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Studio
{
    class JsonValue;

    /**
     * @brief The output format a texture is imported as.
     *
     * Two options, both spelled as XNA's `TextureProcessorOutputFormat` spells them. XNA's third,
     * `NoChange`, is deliberately absent: it means "keep the source bitmap's own format", and the
     * only decoder Studio has (`ImageDecode.hpp`) produces eight-bit RGBA whatever it is handed —
     * so `NoChange` would be a choice with one outcome, which is not a choice but a place for a
     * user to look for behaviour that is not there.
     */
    enum class StudioTextureOutputFormat
    {
        /** @brief Eight bits a channel, uncompressed. */
        Color,

        /**
         * @brief Block compression: DXT1 without alpha at four bits a pixel, DXT5 with it at eight.
         *
         * An eighth and a quarter of uncompressed, respectively -- not the "a quarter" both are
         * loosely called, which is DXT5's number applied to DXT1's format.
         */
        DxtCompressed,
    };

    /** @brief What a texture's file says about itself, as far as its header goes. */
    struct StudioTextureSource
    {
        int width = 0;
        int height = 0;

        /**
         * @brief Whether the encoding carries an alpha channel.
         *
         * Not whether any pixel actually uses it — that needs a decode, and the question this
         * answers is "may this become DXT1", which the encoding settles on its own.
         */
        bool hasAlphaChannel = false;

        /** @brief False when nothing has measured the file, which is different from "0 x 0". */
        [[nodiscard]] bool isMeasured() const { return width > 0 && height > 0; }
    };

    /** @brief What the user chose. Every field is a setting; none of them is ever a fact. */
    struct StudioTextureImportSettings
    {
        StudioTextureOutputFormat outputFormat = StudioTextureOutputFormat::Color;

        /**
         * @brief Whether the hardware converts this texture from sRGB to linear as it samples it.
         *
         * Off by default, which is XNA's own behaviour and not an oversight: XNA renders in gamma
         * space, so its textures are sampled as the bytes stand and a `Color` surface is what
         * every existing XNA game expects. On is for colour art in a linear-lighting pipeline, and
         * is wrong for a normal map, a mask or a lookup table whatever the renderer does.
         */
        bool srgbRead = false;

        bool generateMipmaps = false;
        bool premultiplyAlpha = true;

        /** @brief Reads the four settings out of an asset's `importerSettings`. */
        [[nodiscard]] static StudioTextureImportSettings fromJson(const JsonValue& importerSettings);
    };

    /** @brief What @ref studioPlanTextureImport worked out. */
    struct StudioTextureImportPlan
    {
        /**
         * @brief The CNA `SurfaceFormat` this resolves to, by name.
         *
         * A name rather than the enum, because `cna-studio-assets` is one of the CNA-free modules
         * (`ANALYSIS.md` decision D-03) and a build with no CNA still has to be able to tell a user
         * what their settings do.
         */
        std::string surfaceFormat = "Color";

        /** @brief One when mipmaps are off, the full chain when they are on, zero when unmeasured. */
        std::uint32_t mipLevels = 1;

        /** @brief What the whole chain occupies on the GPU. Zero when the file is unmeasured. */
        std::uint64_t estimatedBytes = 0;

        /**
         * @brief Why the plan is not exactly what was asked for. Empty when it is.
         *
         * Notes rather than errors, because none of these stops an import: each one is a setting
         * that had to be resolved to something else, and a user who cannot see *which* is left
         * with a texture that is quietly the wrong format.
         */
        std::vector<std::string> notes;

        [[nodiscard]] bool isExactlyAsAsked() const { return notes.empty(); }
    };

    /** @brief The number of mip levels a @p width by @p height texture has, down to 1x1. */
    [[nodiscard]] std::uint32_t studioMipLevelCount(int width, int height);

    /**
     * @brief Works out what @p settings applied to @p source actually produce.
     *
     * Pure: no filesystem, no graphics device, no state. That is what lets the inspector call it
     * every frame and a test call it a thousand times.
     */
    [[nodiscard]] StudioTextureImportPlan studioPlanTextureImport(
        const StudioTextureImportSettings& settings, const StudioTextureSource& source);
}
