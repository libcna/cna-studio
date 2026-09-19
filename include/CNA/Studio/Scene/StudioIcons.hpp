// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Scene/StudioIcons.hpp
 * @brief Icons for entities the viewport cannot otherwise draw.
 *
 * A camera, a light and an audio source have a position and nothing to render. Without an icon
 * they are invisible in the viewport, and -- worse -- unclickable, because picking tests against
 * sprite bounds and they have none. The only way to reach them would be the hierarchy panel, which
 * is exactly the wrong answer for an object whose whole point is *where it is*.
 *
 * `ModelRenderer` is in the same position for a different reason: it has geometry, but the 2D
 * viewport cannot draw it yet (plan.md ED-402). Until it can, an icon is the honest stand-in.
 *
 * CNA-free, like the camera, the picker and the gizmo. Where an icon sits and whether the cursor is
 * over it are arithmetic; only the pixels need a graphics device.
 *
 * Icons are sized in **screen** pixels, so they stay the same size at any zoom. An icon that shrank
 * as you zoomed out would vanish exactly when it is the only way to find the entity.
 */

#include <vector>

#include "CNA/Studio/Core/StudioMath.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/StudioCamera2D.hpp"

namespace CNA::Studio
{
    class StudioEntity;
    class SceneDocument;

    /** @brief Which icon an entity gets. */
    enum class StudioIconKind
    {
        None,
        Camera,
        Light,
        AudioSource,
        /** @brief A model the 2D viewport cannot draw yet. */
        Model,

        /**
         * @brief An entity with a place in the world and nothing that draws: a marker.
         *
         * `plan.md` STUDIO-11009. A spawn point, a trigger, an empty grouping node. These used to
         * get no icon at all, so the only thing showing them was the small bounds box every
         * entity gets -- which reads as a tiny object rather than as a marker, and which a user
         * cannot tell from a piece of geometry too small to see.
         *
         * Last in the enumeration because the order is the match order, and this is the case that
         * applies when none of the others do.
         */
        Empty
    };

    /** @brief Half-extent of an icon badge, in screen pixels. */
    inline constexpr float kStudioIconExtent = 13.0f;

    /**
     * @brief Returns the icon @p entity should carry, or StudioIconKind::None.
     *
     * An entity with several qualifying components gets the first match in the order the
     * enumeration declares -- one entity, one icon, and a stable choice rather than one that
     * depends on the order the components happen to be stored in.
     *
     * Note that a sprite does not suppress the icon: "this entity is a camera" is information the
     * viewport cannot convey any other way, and the icon and the sprite belong to the same entity
     * so there is nothing for a click to be ambiguous about.
     */
    [[nodiscard]] StudioIconKind getStudioIconKind(const StudioEntity& entity);

    /** @brief One icon to draw, positioned in screen pixels. */
    struct StudioIconPlacement
    {
        Uuid entityId;
        StudioIconKind kind = StudioIconKind::None;

        /** @brief Badge centre, in viewport pixels. */
        StudioVector2 center;
    };

    /**
     * @brief Returns every icon the viewport should draw, in document order.
     *
     * Document order matters: the renderer draws them in this order, and the picker treats the last
     * match as the one on top, so the two agree about overlapping icons without either having to
     * know how the other sorts.
     *
     * Disabled entities are omitted, matching both the sprite pass and the picker -- what cannot be
     * clicked should not be drawn.
     */
    [[nodiscard]] std::vector<StudioIconPlacement> collectStudioIcons(const SceneDocument& scene,
                                                                     const StudioCamera2D& camera);

    /** @brief Returns true when @p screenPoint falls inside the badge centred at @p center. */
    [[nodiscard]] bool hitTestStudioIcon(const StudioVector2& center, const StudioVector2& screenPoint);
}
