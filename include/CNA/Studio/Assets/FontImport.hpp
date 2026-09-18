// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/FontImport.hpp
 * @brief What a TrueType or OpenType file says about itself, read without rasterising it.
 *
 * `plan.md` STUDIO-10006.
 *
 * ### Why a `.ttf` is not a `.spritefont`
 *
 * Both were `AssetType::SpriteFont`, and they are not the same kind of thing. A `.spritefont` is
 * the content pipeline's own *description*: it already says which font, at what size, with which
 * characters, so every one of its facts is read-only and an editable copy would be a second answer
 * to a settled question (`AssetImporters.hpp`). A `.ttf` settles none of that. It is the typeface,
 * and the size, the character range and the spacing are decisions nobody has made yet — which is
 * exactly what import *settings* are for.
 *
 * So a `.ttf` dropped in a project used to be handed the sprite-font importer, which looked for
 * `<Asset … FontDescription>` in a binary file, declined it, and left the inspector showing five
 * empty read-only fields. It now has an importer of its own.
 *
 * ### The tables are read by hand rather than through stb_truetype
 *
 * `StudioFontAtlas.cpp` owns stb_truetype and is one of four translation units allowed to reach a
 * vendored library at all (`tests/AssetImporterTests.cpp` enforces that). Widening that list to
 * read four numbers out of a fixed-layout table directory would be the wrong trade: this is header
 * reading, like `readImageDescription` and `readAudioDescription` beside it, and the sfnt directory
 * is as simple as a PNG's IHDR. Rasterising a glyph is the part that needs a library, and this does
 * not rasterise anything.
 *
 * Every offset out of the file is bounds-checked against its length. A font is a file somebody put
 * in a project folder, it may be hostile, and a table record claiming to start four gigabytes in is
 * a few bytes to write.
 */

#include <cstdint>
#include <optional>
#include <string>

namespace CNA::Studio
{
    class JsonValue;

    /** @brief What a font file's tables say about itself. */
    struct StudioFontDescription
    {
        /**
         * @brief "TrueType", "OpenType" or "TrueType Collection", from the sfnt version.
         *
         * OpenType here means CFF outlines (`OTTO`). The distinction is worth reporting because it
         * decides what a rasteriser has to be able to do, and because "why does this font look
         * wrong" is sometimes answered by it.
         */
        std::string format;

        /** @brief The typeface family, e.g. "Inter". Name ID 1. */
        std::string family;

        /** @brief The style within the family, e.g. "Bold Italic". Name ID 2. */
        std::string style;

        /** @brief How many glyphs the font holds, from `maxp`. */
        std::uint32_t glyphCount = 0;

        /**
         * @brief The font's design grid, from `head`. Usually 1000 or 2048.
         *
         * Not decoration: it is what a point size is measured against, so a font with an unusual
         * grid renders at a size nobody expected and this is the number that says why.
         */
        std::uint32_t unitsPerEm = 0;

        /**
         * @brief Whether the font carries kerning data at all, in `kern` or `GPOS`.
         *
         * Reported because the `useKerning` setting beside it is meaningless without it: a font
         * with no pairs kerns identically whichever way the box is ticked, and a user toggling it
         * and seeing nothing happen has no way to tell that from a bug.
         */
        bool hasKerning = false;

        /** @brief False when nothing could be read, which is different from a font with no glyphs. */
        [[nodiscard]] bool isMeasured() const { return glyphCount > 0 && unitsPerEm > 0; }
    };

    /**
     * @brief Reads @p absolutePath's font tables.
     *
     * @param absolutePath The file.
     * @param outProblem When set, receives a reason *only* when the file announces itself as an
     *        sfnt and then cannot be read anyway. A file that is not a font at all leaves it empty
     *        and is declined in silence (`plan.md` STUDIO-10013).
     * @return The description, or std::nullopt when the file cannot be read or is not a font.
     */
    [[nodiscard]] std::optional<StudioFontDescription> readFontDescription(
        const std::string& absolutePath, std::string* outProblem = nullptr);

    /** @brief What the user chose about a font asset. */
    struct StudioFontImportSettings
    {
        /**
         * @brief The size, in points, the font is rasterised at.
         *
         * A font asset becomes a bitmap at build time, so this is a decision made once at import
         * rather than per draw -- the same bargain XNA's `.spritefont` makes, and the reason it
         * declares a `<Size>` too.
         */
        float pointSize = 16.0f;

        /** @brief First character code to rasterise. 32 is space, which is where XNA's range starts. */
        int firstCharacter = 32;

        /** @brief Last character code, inclusive. 126 is `~`, the end of printable ASCII. */
        int lastCharacter = 126;

        /** @brief Extra horizontal space between glyphs, in pixels. May be negative. */
        float spacing = 0.0f;

        /** @brief Whether to apply the font's own kerning pairs. See @ref StudioFontDescription::hasKerning. */
        bool useKerning = true;

        [[nodiscard]] static StudioFontImportSettings fromJson(const JsonValue& importerSettings);

        /** @brief How many characters the range covers, or zero when it is empty or inverted. */
        [[nodiscard]] std::size_t characterCount() const;
    };
}
