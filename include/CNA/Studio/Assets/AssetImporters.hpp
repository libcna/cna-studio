// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetImporters.hpp
 * @brief What each importer's settings are, declared the same way a component's are.
 *
 * An importer's settings are a named list of typed, defaulted fields -- which is exactly what
 * `ComponentDescriptor` already describes. Reusing it rather than inventing a parallel schema
 * means the inspector needs no new code to edit them, a plugin's importer is editable on the same
 * terms as a built-in one, and the JSON round-trip is the one `PropertyValue` already has
 * (ANALYSIS.md decision D-05).
 *
 * The registry is separate from the component registry, not shared: an importer id and a component
 * type id are different namespaces, and a project that happened to name a component
 * "CNA.TextureImporter" should not silently become editable as one.
 */

#include <optional>
#include <string>
#include <string_view>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetImporter.hpp"
#include "CNA/Studio/Core/ComponentDescriptor.hpp"

namespace CNA::Studio
{
    /** @brief The importer type ids the editor ships with. */
    namespace ImporterIds
    {
        inline constexpr const char* kTexture = "CNA.TextureImporter";
        inline constexpr const char* kSpriteFont = "CNA.SpriteFontImporter";
        inline constexpr const char* kSoundEffect = "CNA.SoundEffectImporter";
        inline constexpr const char* kSong = "CNA.SongImporter";
        inline constexpr const char* kModel = "CNA.ModelImporter";
    }

    /**
     * @brief Registers the built-in importers' settings schemas into @p registry.
     *
     * Only the importers with settings worth editing are declared. An asset whose importer has no
     * schema is still tracked and still imported -- the inspector simply has nothing to offer for
     * it, which is honest rather than an empty form.
     */
    void registerBuiltinImporters(ComponentRegistry& registry);

    /**
     * @brief What a `.spritefont` file declares about itself.
     *
     * Everything here is a *fact about the file*, never a setting. XNA's `.spritefont` is the
     * content pipeline's own input, so the editor keeping an editable copy of these would produce
     * two answers to one question -- and the one the build actually reads is the file's.
     */
    struct SpriteFontDescription
    {
        std::string fontName;
        float pointSize = 0.0f;
        float spacing = 0.0f;
        bool useKerning = true;

        /** @brief Inclusive character-code range, as declared by the first `CharacterRegion`. */
        int firstCharacter = 0;
        int lastCharacter = 0;
    };

    /**
     * @brief Reads a `.spritefont` description, or returns nothing when the file is not one.
     *
     * A targeted tag scan rather than an XML parser. The schema is fixed, tiny and machine-written,
     * six fields are wanted from it, and taking on an XML dependency to read six fields would be
     * the larger risk. A file that does not look like a `.spritefont` is reported as such rather
     * than guessed at.
     */
    [[nodiscard]] std::optional<SpriteFontDescription> readSpriteFontDescription(const std::string& path);

    /** @brief An image's dimensions in pixels. */
    struct ImageSize
    {
        int width = 0;
        int height = 0;
    };

    /**
     * @brief Reads an image's dimensions from its header, without decoding it.
     *
     * PNG, BMP and JPEG. The first two state their size in a fixed header, so a few dozen bytes
     * answer the question; a JPEG has no fixed offset at all and its segments are walked until a
     * start-of-frame turns up, bounded so a corrupt file cannot send the walk to the end of a
     * hundred megabytes. Anything else needs a decoder, which does not belong in the editor's
     * CNA-free layer and is better answered by the importer that will load the file for real.
     *
     * @return The size, or std::nullopt when the file cannot be read or its format is not one of
     *         those three. Callers must treat "unknown" as unknown rather than as zero.
     */
    [[nodiscard]] std::optional<ImageSize> readImageSize(const std::string& absolutePath);

    /** @brief What an image file's header says about itself. */
    struct ImageDescription
    {
        int width = 0;
        int height = 0;

        /**
         * @brief "PNG", "JPEG" or "BMP", read from the file's own magic bytes.
         *
         * Never from the extension. A `.png` that is really a JPEG is a file somebody renamed, and
         * the editor reporting the name back at them is the one answer that helps nobody.
         */
        std::string format;

        /**
         * @brief Whether the encoding carries an alpha channel.
         *
         * Not whether any pixel uses it -- that needs a decode, and the question this answers is
         * whether a compressed import may be DXT1, which the encoding settles on its own. A
         * paletted PNG is the one case that needs looking past the fixed header, since its
         * transparency lives in a `tRNS` chunk.
         */
        bool hasAlphaChannel = false;
    };

    /**
     * @brief Reads an image's dimensions, format and alpha from its header, without decoding it.
     *
     * The same three formats and the same bounded walk as readImageSize(), which is a thin wrapper
     * over this. Separate because most callers want a size and nothing else, and the texture
     * importer (`plan.md` STUDIO-10003) wants all of it.
     *
     * @param absolutePath The file.
     * @param outProblem When set, receives a reason *only* when the file announces itself as one of
     *        the three formats and then cannot be read anyway -- a truncated PNG, a BMP whose
     *        header stops short. A file that is simply not an image leaves it empty, because that
     *        is not a problem: a project is full of files Studio does not import, and reporting
     *        each of them makes a list nobody reads (`plan.md` STUDIO-10013).
     * @return The description, or std::nullopt when the file cannot be read or is not one of the
     *         three formats.
     */
    [[nodiscard]] std::optional<ImageDescription> readImageDescription(const std::string& absolutePath,
                                                                       std::string* outProblem = nullptr);

    /**
     * @brief Fills in the facts an importer can determine by reading a file.
     *
     * Called after a scan. Only writes where the value would actually change, so a project whose
     * assets have not moved produces no sidecar churn -- a scan that rewrote every sidecar on
     * every open would show up as a repository full of spurious diffs.
     *
     * @return The number of records whose settings changed.
     */
    std::size_t applyImporterFacts(AssetDatabase& assets);

    /** @brief As above, through @p importers rather than the built-in set (`plan.md` STUDIO-10002). */
    std::size_t applyImporterFacts(AssetDatabase& assets, const StudioImporterRegistry& importers);

    /**
     * @brief Fills in the facts for one asset, leaving every *setting* alone.
     *
     * `plan.md` STUDIO-10001. The per-asset half of applyImporterFacts(), which a reimport needs:
     * re-reading the whole project because one file changed is the reason a reimport feels like a
     * pause rather than an action.
     *
     * Only writes where the value would actually change, like the wholesale pass, so an asset
     * reimported twice produces one sidecar diff rather than two.
     *
     * @param assets The database.
     * @param id The asset.
     * @return True when a fact changed.
     */
    bool applyImporterFacts(AssetDatabase& assets, const Uuid& id);

    /** @brief As above, through @p importers rather than the built-in set (`plan.md` STUDIO-10002). */
    bool applyImporterFacts(AssetDatabase& assets, const Uuid& id,
                            const StudioImporterRegistry& importers);

    /**
     * @brief Merges @p facts into @p id's sidecar, writing only where a value would change.
     *
     * The second half of an import, split out because the first half moved off the frame
     * (`plan.md` STUDIO-10011): a worker gathers facts with `StudioAssetImporter::gatherFacts`,
     * and this puts them on the record. **Main thread only** — it writes a sidecar and may
     * reallocate the record store.
     *
     * The "only where a value would change" rule lives here rather than in each importer because
     * it is identical for all of them, and an importer that got it wrong would rewrite every
     * sidecar on every open and fill a repository with spurious diffs.
     *
     * @return True when something was written.
     */
    bool studioApplyImporterFacts(AssetDatabase& assets, const Uuid& id, const JsonValue& facts);
}
