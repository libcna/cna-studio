// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetDependencies.hpp
 * @brief Which assets reference which, in both directions.
 *
 * `plan.md` STUDIO-09012.
 *
 * References are Uuids (ANALYSIS.md decision D-08), which is what makes moving a file free — and
 * what makes "what breaks if I delete this?" unanswerable by looking at the file. Nothing on disk
 * records that `Level.cnascene` uses `player.png`; the scene holds an id, and the only way to find
 * out is to read every file that could hold one.
 *
 * So this builds the reverse map once and keeps it. Both directions come out of the same pass,
 * because they are the same data read two ways, and an index that computed "referenced by" from
 * one walk and "references" from another would be two things that could disagree.
 *
 * ### Where a reference can be
 *
 * - **A scene or a prefab**: any component property of type `AssetReference`, including on a
 *   component whose plugin failed to load — that file is the one most likely to be broken, and a
 *   walk over descriptors rather than over stored properties would skip exactly it.
 * - **A material**: its four texture fields, which are ids for the reason the file header gives.
 * - **Any asset at all**: `AssetRecord::dependencies`, which importers fill in — a model naming
 *   the textures it came with.
 *
 * ### The open scene is not the one on disk
 *
 * A dependency view built only from files is right until the user edits something, which is
 * exactly when they ask. @ref AssetDependencyIndex::observeScene replaces what the index holds for
 * one scene with what the open document says, so an unsaved edit is visible in the answer.
 */

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
{
    class ComponentRegistry;
    class SceneDocument;

    /** @brief One place an asset is referenced from. */
    struct AssetUsage
    {
        /** @brief The asset holding the reference. */
        Uuid holderId;

        /** @brief The holder's project-relative path, which is what a user recognises it by. */
        std::string holderPath;

        /** @brief What kind of file the holder is. */
        AssetType holderType = AssetType::Unknown;

        /** @brief The entity carrying the reference, when the holder is a scene or a prefab. */
        Uuid entityId;

        /** @brief That entity's name. */
        std::string entityName;

        /** @brief The component type id, or empty when the file itself declares the reference. */
        std::string componentTypeId;

        /** @brief The property or material field carrying the id, or empty. */
        std::string propertyName;

        /**
         * @brief One line saying where the reference is, for a list.
         *
         * The holder's *file name* and then the place inside it. A row that showed only the path
         * would make two references in one scene indistinguishable, and one that showed only the
         * entity would make two entities of the same name in different scenes indistinguishable.
         */
        [[nodiscard]] std::string describe() const;
    };

    /** @brief What a build of the index read. */
    struct AssetDependencyScan
    {
        /** @brief How many files were opened and parsed. */
        std::size_t filesRead = 0;

        /** @brief How many references were found, counting each place separately. */
        std::size_t referencesFound = 0;

        /**
         * @brief Files that could not be read, one line each.
         *
         * Collected rather than fatal. A project with one unreadable prefab still has a useful
         * answer for every other asset in it, and an index that refused to build would take the
         * dependency view away precisely when something is wrong.
         */
        std::vector<std::string> warnings;
    };

    /**
     * @brief The project's asset reference graph, both ways round.
     *
     * Built on demand rather than maintained continuously: the walk reads every scene, prefab and
     * material in the project, which is not something to do on every edit. `STUDIO-30001`'s
     * background jobs are what will let it be rebuilt without a pause; until then it is built when
     * asked and kept until something invalidates it.
     */
    class AssetDependencyIndex
    {
    public:
        /**
         * @brief Reads every scene, prefab and material in @p assets and builds both maps.
         *
         * @param assets The project's assets. Its records supply the paths and the
         *        importer-declared dependencies.
         * @param registry Component descriptors, for loading scenes and prefabs. A component it
         *        does not know still has its properties read, and its references counted.
         * @return What was read, and what could not be.
         */
        AssetDependencyScan build(const AssetDatabase& assets, const ComponentRegistry& registry);

        /**
         * @brief Replaces what the index holds for one scene with what the open document says.
         *
         * @param scene The open document, which may hold unsaved edits.
         * @param sceneAssetId The scene's asset id. Nothing happens when it is nil — an unsaved
         *        new scene is not yet an asset anything can reference.
         * @param sourcePath The scene's project-relative path, for the rows.
         */
        void observeScene(const SceneDocument& scene, const Uuid& sceneAssetId,
                          const std::string& sourcePath);

        /**
         * @brief Every place @p id is referenced from.
         * @param id The asset.
         * @return The usages, ordered by holder path and then by the order they appear in it.
         */
        [[nodiscard]] std::vector<AssetUsage> referencedBy(const Uuid& id) const;

        /**
         * @brief The assets @p id references, without duplicates.
         * @param id The asset.
         * @return The ids, in first-seen order.
         */
        [[nodiscard]] std::vector<Uuid> referencesTo(const Uuid& id) const;

        /** @brief Whether anything has been indexed. */
        [[nodiscard]] bool isEmpty() const { return usagesByAsset_.empty(); }

        /** @brief How many assets are referenced by something. */
        [[nodiscard]] std::size_t getReferencedCount() const { return usagesByAsset_.size(); }

        /** @brief Drops everything. */
        void clear();

    private:
        /** @brief Removes every record of @p holderId referencing anything. */
        void forget(const Uuid& holderId);

        /** @brief Records that @p holder references @p target at @p usage. */
        void add(const Uuid& target, AssetUsage usage);

        std::unordered_map<Uuid, std::vector<AssetUsage>> usagesByAsset_;
        std::unordered_map<Uuid, std::vector<Uuid>> referencesByHolder_;
    };
}
