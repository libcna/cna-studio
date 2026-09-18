// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Assets/AssetImporter.hpp
 * @brief What an importer *is*, so that adding a format is implementing one rather than editing one.
 *
 * `plan.md` STUDIO-10002.
 *
 * ### The shape of the problem this replaces
 *
 * Importing was a chain of `if (record->type == AssetType::SpriteFont) … if (… == Model) …`, with
 * each branch reaching whatever library it needed. That works, and it has two properties that get
 * worse with every format added: a new importer means editing a function in the middle of the asset
 * system, and the function that dispatches ends up naming every third-party parser Studio has. A
 * plugin cannot edit that chain at all, which is why `STUDIO-28003` waits on this.
 *
 * ### The interface, and what is deliberately not in it
 *
 * An importer says which importer id it is, which asset types it claims, and how to read *facts*
 * out of a file. It does not say how to edit its settings: that is a `ComponentDescriptor`, which
 * `AssetImporters.hpp` already registers and the inspector already knows how to draw (decision
 * D-05). Two ways to describe one importer's settings would be one too many.
 *
 * It also does not open files on its own account, or decide when to run: `applyImporterFacts` owns
 * the walk, the sidecar write and the "only write when something actually changed" rule, because
 * those are the same for every importer and an importer that got them wrong would produce a
 * repository full of spurious diffs.
 *
 * ### Facts, not settings
 *
 * An importer fills in what is *true of the file* — a texture's pixel size, a model's mesh count —
 * and never touches what the user chose. That distinction is `STUDIO-10001`'s and it is the reason
 * a reimport does not silently revert somebody's decision.
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/Studio/Assets/AssetDatabase.hpp"

namespace CNA::Studio
{
    /**
     * @brief One kind of asset Studio knows how to read facts out of.
     *
     * Implemented in whichever module owns the library it needs — the glTF importer lives beside
     * cgltf in `cna-studio-assets`, and a plugin's importer will live in the plugin. Nothing above
     * this interface links a parser.
     */
    class StudioAssetImporter
    {
    public:
        virtual ~StudioAssetImporter() = default;

        /**
         * @brief The importer type id, e.g. `"CNA.TextureImporter"`.
         *
         * The same id an `AssetRecord` carries and the same id the settings schema is registered
         * under, because an asset's record is how a *file* and an *importer* are connected.
         */
        [[nodiscard]] virtual std::string_view id() const = 0;

        /**
         * @brief Whether this importer reads assets of @p type.
         *
         * Asked of the importer rather than looked up in a table, so that adding one is adding a
         * file: a table would be the same chain of `if`s with a different shape.
         */
        [[nodiscard]] virtual bool handles(AssetType type) const = 0;

        /**
         * @brief Fills in what is true of @p record's file, leaving every *setting* alone.
         *
         * Called with the record already found and the caller holding the walk. An importer may
         * read the file; it must not delete the record, rescan, or write a sidecar — the caller
         * does that, once, and only when this returns true.
         *
         * @param assets The database, for `resolvePath` and for the mutable record.
         * @param record The asset. Const, because changing it is done through @p assets so that the
         *        database can keep its indexes right.
         * @return True when a fact changed and the sidecar is worth writing.
         */
        [[nodiscard]] virtual bool readFacts(AssetDatabase& assets,
                                             const AssetRecord& record) const = 0;
    };

    /**
     * @brief The importers this build has.
     *
     * Ordered by registration, and the first that claims a type wins — so a plugin registering an
     * importer for a type Studio already handles is a decision somebody made rather than an
     * ambiguity resolved by hash order.
     */
    class StudioImporterRegistry
    {
    public:
        /** @brief Adds @p importer. A null importer, or a duplicate id, is refused. */
        bool add(std::unique_ptr<StudioAssetImporter> importer);

        /** @brief The importer claiming @p type, or null when nothing does. */
        [[nodiscard]] const StudioAssetImporter* forType(AssetType type) const;

        /** @brief The importer with @p importerId, or null. */
        [[nodiscard]] const StudioAssetImporter* find(std::string_view importerId) const;

        /** @brief How many importers there are. */
        [[nodiscard]] std::size_t getCount() const { return importers_.size(); }

        /** @brief Every importer's id, in registration order. */
        [[nodiscard]] std::vector<std::string> getIds() const;

    private:
        std::vector<std::unique_ptr<StudioAssetImporter>> importers_;
    };

    /**
     * @brief The importers Studio ships with, registered into @p registry.
     *
     * Separate from `registerBuiltinImporters`, which registers their *settings schemas* into a
     * `ComponentRegistry`. Two registries because they answer different questions — "who can read
     * this file" and "what can the user change about it" — and a build could reasonably have one
     * without the other.
     */
    void registerBuiltinAssetImporters(StudioImporterRegistry& registry);

    /** @brief The registry `applyImporterFacts` uses when no other is given. */
    [[nodiscard]] const StudioImporterRegistry& getBuiltinAssetImporters();
}
