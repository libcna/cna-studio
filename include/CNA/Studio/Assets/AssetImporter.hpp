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
 * It also does not decide when to run, compare its facts against what is on record, or write a
 * sidecar: `applyImporterFacts` owns the walk, the sidecar write and the "only write when something
 * actually changed" rule, because those are the same for every importer and an importer that got
 * them wrong would produce a repository full of spurious diffs.
 *
 * ### It is given a path and settings, and nothing else
 *
 * `plan.md` STUDIO-10011. Reading a file is the slow half of importing and the half that must
 * happen off the frame, so `gatherFacts` takes only values a job body can be handed: a resolved
 * path and a copy of the settings. A signature that took an `AssetDatabase&` could not be called
 * from a worker at all, and one that took it and promised not to touch it would be a promise
 * nothing checks.
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
     * @brief What an importer made of a file: its facts, or the reason there are none.
     *
     * `plan.md` STUDIO-10013. Three answers, not two, and the third is the one that matters:
     *
     * - **Read** — @ref facts is an object. The file was understood.
     * - **Declined** — both empty. This importer does not claim this file, which is ordinary: a
     *   project is full of files Studio does not import, and reporting each of them as a problem
     *   makes a list nobody reads.
     * - **Failed** — @ref error says why. The file *is* the kind this importer reads and could not
     *   be read anyway: a truncated PNG, a glTF whose `.bin` is missing. This is the one a user
     *   has to be told about, because the asset is in their project on purpose.
     *
     * Collapsing the last two is how a broken file becomes silent. They looked identical before
     * this -- both were "returned nothing" -- and a texture truncated by a failed copy therefore
     * imported as quietly as a readme.
     */
    struct StudioImportedFacts
    {
        /** @brief The facts, as a JSON object. Null when nothing was read. */
        JsonValue facts;

        /**
         * @brief Why the file could not be read, in words a person can act on.
         *
         * Empty when the file was read *and* when this importer simply does not claim it. Phrased
         * as what the editor cannot do rather than as what the file did wrong, because the reader
         * is usually the person who made the file.
         */
        std::string error;

        [[nodiscard]] bool succeeded() const { return !facts.isNull(); }
        [[nodiscard]] bool failed() const { return facts.isNull() && !error.empty(); }
        [[nodiscard]] bool declined() const { return facts.isNull() && error.empty(); }
    };

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
         * @brief Reads what is true of the file at @p absolutePath, given @p settings.
         *
         * **Runs on a worker thread** (`plan.md` STUDIO-10011). It is given a path and a copy of
         * the asset's settings, and it is given nothing else: no database, no record, no document,
         * no graphics device. That is not politeness, it is the reason importing can move off the
         * frame at all — a signature that took an `AssetDatabase&` could not be called from a job
         * body, and one that took it and promised not to touch it would be a promise nobody can
         * check.
         *
         * It reads facts and it returns them. It does not compare them against what is on record,
         * does not write a sidecar and does not decide when to run: those are identical for every
         * importer, they need the database, and an importer that got them wrong would fill a
         * repository with spurious diffs.
         *
         * @param absolutePath The file. Already resolved, because resolving needs the database.
         * @param settings The asset's `importerSettings`, because some facts depend on a choice —
         *        a model's size is measured at its `scaleFactor`, and reporting one taken at 1.0
         *        beside a scale of 100 would be two answers to one question.
         * @return The facts, or the reason there are none -- and, separately, nothing at all when
         *         this is simply not a file it claims. See @ref StudioImportedFacts.
         */
        [[nodiscard]] virtual StudioImportedFacts gatherFacts(const std::string& absolutePath,
                                                              const JsonValue& settings) const = 0;
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
