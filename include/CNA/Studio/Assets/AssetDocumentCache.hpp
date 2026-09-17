// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetDocumentCache.hpp
 * @brief Asset files read when they change rather than when they are drawn.
 *
 * `plan.md` STUDIO-30016, under `STUDIO-30012`'s caching strategy.
 *
 * Two of the Details panel's sections have a *file* for a document: the material editor reads the
 * `.cnamaterial` it is showing, and the prefab section loads the `.cnaprefab` and walks both
 * subtrees to find the overrides. Both already halve the obvious cost by working on the input pass
 * and keeping what they found for the draw pass — so it is one file open per frame rather than two.
 * It is still one per frame, for as long as the thing is selected.
 *
 * ### What invalidates an entry, which is the same question `STUDIO-30012` answered
 *
 * - **The record's stamp changed.** `AssetRecord` carries the size and modification time the last
 *   scan or watcher poll saw. Comparing against it costs nothing — no syscall — because the watcher
 *   has already done the asking. A file edited outside Studio therefore reloads within the
 *   watcher's interval, exactly as a missing file becomes visible within it.
 * - **Something wrote it.** Studio's own writes — `SetMaterialCommand`, a prefab Apply — change the
 *   file without changing the record, so the caller invalidates. Coarsely is fine: dropping the
 *   whole cache on any command costs one reload of whatever is on screen.
 *
 * ### It is a cache, not a document store
 *
 * Nothing edits what comes out of here. The editors write files through commands and then
 * invalidate; an entry is only ever a copy of what was on disk, so a stale one is a display that is
 * behind rather than an edit that is lost.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/MaterialDocument.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/PrefabDocument.hpp"

namespace CNA::Studio
{
    class ComponentRegistry;

    /** @brief Materials and prefabs read from disk, kept until something says they changed. */
    class StudioAssetDocumentCache
    {
    public:
        /**
         * @brief The material @p id names, read from disk at most once per change.
         *
         * @param assets The database, for the record and its stamp.
         * @param id The material asset.
         * @return The document, or null when @p id is not a material or its file will not read.
         *         The pointer is valid until the next call that reloads or invalidates that entry.
         */
        [[nodiscard]] const MaterialDocument* material(const AssetDatabase& assets, const Uuid& id);

        /**
         * @brief The prefab @p id names, read from disk at most once per change.
         *
         * @param assets The database.
         * @param id The prefab asset.
         * @param registry Component descriptors, for loading it.
         * @return The document, or null when @p id is not a prefab or its file will not read.
         */
        [[nodiscard]] const PrefabDocument* prefab(const AssetDatabase& assets, const Uuid& id,
                                                   const ComponentRegistry& registry);

        /**
         * @brief Drops what is held for @p id, or everything when @p id is nil.
         *
         * Called after anything writes an asset file. Coarse on purpose: the cost of dropping too
         * much is one reload of what is on screen, and the cost of dropping too little is an editor
         * showing a file it has already overwritten.
         */
        void invalidate(const Uuid& id = {});

        /** @brief How many entries are held. */
        [[nodiscard]] std::size_t getEntryCount() const { return entries_.size(); }

        /**
         * @brief How many times this cache has opened a file.
         *
         * Exposed so "the Details panel does not open a file on every frame" is something a test
         * asserts rather than something a benchmark implies — the same instrument, and for the same
         * reason, as `AssetDatabase::getPresenceProbeCount`.
         */
        [[nodiscard]] std::uint64_t getFileReadCount() const { return fileReads_; }

    private:
        /** @brief One cached document, with the stamp it was read at. */
        struct Entry
        {
            std::uint64_t size = 0;
            std::int64_t modifiedTime = 0;
            std::string sourcePath;

            /** @brief Whether the read succeeded. A failure is cached too, or a broken file would
             *         be reopened every frame — which is the case this exists to stop. */
            bool loaded = false;

            std::unique_ptr<MaterialDocument> material;
            std::unique_ptr<PrefabDocument> prefab;
        };

        /** @brief Returns the entry for @p record, reloading it when its stamp has moved. */
        Entry& entryFor(const AssetRecord& record, bool& outNeedsLoad);

        std::unordered_map<Uuid, Entry> entries_;
        std::uint64_t fileReads_ = 0;
    };
}
