// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Assets/MeshCache.hpp
 * @brief Imported meshes, kept so that a frame does not re-read them (plan.md ED-405).
 *
 * The 3D view asks for a model's geometry once per entity per frame, and importing a glTF is a
 * whole-file parse. Without something between the two, a scene with one model in it would re-read
 * that file sixty times a second. This is that something, and it is deliberately the smallest
 * thing that answers the question.
 *
 * It lives in `cna-editor-assets` and links no CNA, which is what lets the *standalone* build draw
 * real models -- `--headless --view=3d` and the CI wireframe tests included. That is the point of
 * ED-405 landing before ED-402: the geometry is available and testable a whole task before there
 * is a `VertexBuffer` to put it in, and when ED-402 arrives it uploads what this already holds
 * rather than reading the file a second way.
 */

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Assets/ModelImport.hpp"
#include "CNA/Editor/Core/MeshData.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    /**
     * @brief Loads a model asset's geometry on first ask and remembers it.
     *
     * Keyed by `Uuid`, never by path, for the reason D-08 gives: an asset that moves keeps its id,
     * and a cache keyed by path would quietly hold two copies of one model after a rename.
     */
    class MeshCache
    {
    public:
        /**
         * @brief Returns @p assetId's geometry, importing it if this is the first ask.
         *
         * @return The mesh, or nullptr when the id is not a model, its file cannot be read, or the
         *         model turned out to hold no drawable geometry. All of those mean the same thing
         *         to a caller -- there is nothing to draw -- and each is remembered so that a
         *         broken file is not re-read once per frame for as long as it stays broken.
         *
         * An id the database does not hold at all is the exception: that is not remembered, since
         * "not scanned yet" is an answer that changes on its own and caching it would leave a model
         * invisible after the scan that would have found it.
         *
         * The pointer is valid until the next `clear` or `invalidate` for that id. Nothing else
         * moves it: entries are held by `unique_ptr`, so importing another model does not
         * reallocate the ones already there.
         */
        [[nodiscard]] const MeshData* get(const AssetDatabase& assets, const Uuid& assetId);

        /** @brief Forgets @p assetId, so the next `get` re-reads it. For the asset watcher. */
        void invalidate(const Uuid& assetId);

        /** @brief Forgets everything. Called when a project closes. */
        void clear();

        /** @brief Returns a `MeshProvider`-shaped callback bound to this cache and @p assets. */
        [[nodiscard]] MeshProvider makeProvider(const AssetDatabase& assets)
        {
            return [this, &assets](const Uuid& assetId) { return get(assets, assetId); };
        }

        /** @brief How many assets have been looked at, successfully or not. */
        [[nodiscard]] std::size_t getEntryCount() const { return entries_.size(); }

        /** @brief Total vertices held, for the diagnostics panel. */
        [[nodiscard]] std::size_t getVertexCount() const;

    private:
        struct Entry
        {
            /** @brief Empty and unusable when the import failed; `usable` says which. */
            MeshData mesh;

            /**
             * @brief False when this id has nothing to draw, for whatever reason.
             *
             * A remembered failure, which is the whole reason the flag exists rather than the map
             * simply not holding the entry: an absent key means "not tried yet" and would send the
             * importer back at the file every frame.
             */
            bool usable = false;
        };

        std::unordered_map<Uuid, std::unique_ptr<Entry>> entries_;
    };
}
