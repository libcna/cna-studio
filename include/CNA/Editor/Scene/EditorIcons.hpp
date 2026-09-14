// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/EditorIcons.hpp
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

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/EditorCamera2D.hpp"

namespace CNA::Editor
{
    class EditorEntity;
    class SceneDocument;

    /** @brief Which icon an entity gets. */
    enum class EditorIconKind
    {
        None,
        Camera,
        Light,
        AudioSource,
        /** @brief A model the 2D viewport cannot draw yet. */
        Model
    };

    /** @brief Half-extent of an icon badge, in screen pixels. */
    inline constexpr float kEditorIconExtent = 13.0f;

    /**
     * @brief Returns the icon @p entity should carry, or EditorIconKind::None.
     *
     * An entity with several qualifying components gets the first match in the order the
     * enumeration declares -- one entity, one icon, and a stable choice rather than one that
     * depends on the order the components happen to be stored in.
     *
     * Note that a sprite does not suppress the icon: "this entity is a camera" is information the
     * viewport cannot convey any other way, and the icon and the sprite belong to the same entity
     * so there is nothing for a click to be ambiguous about.
     */
    [[nodiscard]] EditorIconKind getEditorIconKind(const EditorEntity& entity);

    /** @brief One icon to draw, positioned in screen pixels. */
    struct EditorIconPlacement
    {
        Uuid entityId;
        EditorIconKind kind = EditorIconKind::None;

        /** @brief Badge centre, in viewport pixels. */
        EditorVector2 center;
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
    [[nodiscard]] std::vector<EditorIconPlacement> collectEditorIcons(const SceneDocument& scene,
                                                                     const EditorCamera2D& camera);

    /** @brief Returns true when @p screenPoint falls inside the badge centred at @p center. */
    [[nodiscard]] bool hitTestEditorIcon(const EditorVector2& center, const EditorVector2& screenPoint);
}
