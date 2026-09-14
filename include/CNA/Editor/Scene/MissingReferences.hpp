// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/MissingReferences.hpp
 * @brief Finds asset references in a scene that will not resolve.
 *
 * `AssetDatabase::getMissingAssets()` answers a different question: which *tracked* assets have
 * lost their file. This one asks what the user actually cares about -- which entities point at
 * something that is not going to load. The two overlap but neither contains the other: an id
 * deleted from the database is missing here and invisible there, and an unused asset whose file
 * vanished is missing there and harmless here.
 *
 * Left unreported, a broken reference shows up as a placeholder rectangle in the viewport and
 * nothing at all in a build. Finding which of two hundred entities carries it is the sort of hunt
 * an editor exists to prevent.
 *
 * CNA-free, and driven off the entity's *stored* properties rather than off its descriptors, so a
 * component whose plugin failed to load still gets its references checked -- that scene is exactly
 * the one most likely to be broken.
 */

#include <string>
#include <vector>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief One asset reference that cannot be resolved. */
    struct MissingReference
    {
        /** @brief Why the reference will not resolve. */
        enum class Reason
        {
            /** @brief No record with that id. The asset was deleted, or never imported. */
            NotInDatabase,
            /** @brief Tracked, but its source file is gone from disk. */
            FileMissing
        };

        Uuid entityId;
        std::string entityName;
        std::string componentTypeId;
        std::string propertyName;

        /** @brief The unresolvable id, as stored on the property. */
        Uuid assetId;

        Reason reason = Reason::NotInDatabase;
    };

    /** @brief Returns the display name of @p reason. */
    const char* toString(MissingReference::Reason reason);

    /**
     * @brief Returns every unresolvable asset reference in @p scene, in document order.
     *
     * A nil reference is not missing -- it is an empty slot, which is a perfectly ordinary state
     * for a sprite that has not been given a texture yet.
     */
    [[nodiscard]] std::vector<MissingReference> findMissingReferences(const SceneDocument& scene,
                                                                      const AssetDatabase& assets);

    /** @brief Returns the distinct asset ids named by @p references, in first-seen order. */
    [[nodiscard]] std::vector<Uuid> collectMissingAssetIds(const std::vector<MissingReference>& references);
}
