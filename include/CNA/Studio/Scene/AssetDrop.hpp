// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Scene/AssetDrop.hpp
 * @brief Turning an asset dropped into the scene into an entity that uses it.
 *
 * `plan.md` STUDIO-09008.
 *
 * Dragging a model into the viewport and having a model appear is the gesture that makes a content
 * browser feel like part of the editor rather than a file list beside it. The alternative — create
 * an entity, add a `ModelRenderer`, find the model again in a picker — is four steps for the thing
 * people do most.
 *
 * ### One place decides what an asset becomes
 *
 * The viewport, the hierarchy and anything else that grows a drop target ask the same question and
 * must get the same answer: a `.gltf` is a `ModelRenderer` wherever it lands. Two call sites each
 * with their own `switch` is two answers that drift, and the drift shows up as "it works if I drop
 * it on the tree".
 *
 * ### A prefab is not one of these
 *
 * Dropping a prefab instantiates it — `InstantiatePrefabCommand`, which copies a whole subtree and
 * records the link back to the asset. That is a different operation with a different command, so
 * @ref studioAssetDropKind names it rather than this returning half of it.
 */

#include <string>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/StudioEntity.hpp"

namespace CNA::Studio
{
    class ComponentRegistry;

    /** @brief What dropping an asset into the scene does. */
    enum class StudioAssetDropKind
    {
        /** @brief Nothing: there is no sensible entity for this kind of asset. */
        Unsupported,

        /** @brief Creates one entity carrying a component that references the asset. */
        Entity,

        /** @brief Instantiates the prefab, which is a subtree and its own command. */
        Prefab,
    };

    /**
     * @brief What dropping @p type into a scene would do.
     *
     * Separate from building the entity so that a drop *target* can refuse a drag before it lands —
     * a highlight that promises something and then does nothing is worse than no highlight.
     *
     * @param type The asset's kind.
     * @return Which of the three cases it is.
     */
    [[nodiscard]] StudioAssetDropKind studioAssetDropKind(AssetType type);

    /**
     * @brief Says, in one line, why an asset cannot be dropped. Empty when it can.
     *
     * Reported rather than silent: a drag that is refused with no explanation is one the user
     * repeats, and then repeats more slowly.
     */
    [[nodiscard]] std::string describeStudioAssetDropRefusal(const AssetRecord& record);

    /**
     * @brief Builds the entity that uses @p assetId, ready for `CreateEntityCommand`.
     *
     * The component is chosen by the asset's kind — a texture gets a `SpriteRenderer`, a model a
     * `ModelRenderer`, a sound an `AudioSource` — and the entity is named after the file, because
     * an entity called "Entity" in a scene of forty is a thing nobody finds twice.
     *
     * Every component is given its declared defaults before the reference is set, so a dropped
     * asset behaves like one added through the inspector rather than like an entity with one
     * property and no others.
     *
     * @param assets The database, for the record and its name.
     * @param assetId The asset being dropped.
     * @param registry Component descriptors, for the defaults.
     * @param position Where to put it, in world units.
     * @param outEntity Filled in on success; untouched otherwise.
     * @return False when the asset is unknown or its kind has no entity — see
     *         @ref describeStudioAssetDropRefusal.
     */
    [[nodiscard]] bool studioEntityForAsset(const AssetDatabase& assets, const Uuid& assetId,
                                            const ComponentRegistry& registry,
                                            const StudioVector3& position,
                                            StudioEntity& outEntity);
}
