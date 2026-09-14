// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/SceneSprites3D.hpp
 * @brief Sprites as textured quads, so the 3D view shows the scene and not only the models.
 *
 * Until ED-402 the 3D view drew no sprites at all, and the reason was real: `SpriteBatch` cannot
 * draw the trapezoid a sprite becomes when seen from an angle, so a 2D-facing renderer had nothing
 * to draw them with. ED-402 removed the obstacle rather than the reason -- there is now a
 * `VertexBuffer` path with a depth buffer behind it, and through that path a sprite is an ordinary
 * textured quad. So this file decides where those quads go, in the same CNA-free module and for the
 * same reason as `SceneModels.hpp`: it is arithmetic, and arithmetic should be checked by tests
 * rather than by looking at a picture.
 *
 * **Three decisions are made here and each had a plausible alternative.**
 *
 * **1. A sprite lies in the scene's XY plane. It does not turn to face the camera.** Billboarding
 * is the more flattering choice -- a billboarded sprite looks right from every angle, and one
 * lying flat becomes a line when seen edge-on. It is also the wrong one twice over. The picker,
 * `computeEntityBounds3D` and the gizmos already treat a sprite as a flat box in the XY plane, so
 * a billboarded sprite would be drawn somewhere other than where it can be clicked -- and the game
 * draws sprites through `SpriteBatch`, in the scene plane, so a billboarded editor view would be
 * showing a picture the game will never produce. A sprite seen edge-on *is* a line; that is not a
 * rendering artefact to be papered over, it is the truth about a flat thing.
 *
 * **2. Only the Z rotation is used, exactly as `SpriteBatch` uses it.** An entity's world rotation
 * may have pitch and roll in it, and a quad in three dimensions could honour them. `SpriteBatch`
 * cannot, so a game showing that sprite will not -- and the editor drawing a tilted sprite the
 * player renders flat is the same silent disagreement in a new place. The 3D view is allowed to
 * show more than the 2D one; it is not allowed to show more than the game.
 *
 * **3. Sizes come from the same `SpriteSizeProvider` the 2D view and the picker use.** A second
 * way of asking how big a sprite is would be a second answer, free to differ, and the symptom
 * would be a sprite that is one size in the 2D view and another in the 3D one.
 */

#include <array>
#include <cstddef>
#include <vector>

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/EditorCamera3D.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief One sprite, as four world-space corners and the texture to stretch across them. */
    struct SpriteQuad3D
    {
        Uuid entityId;

        /** @brief The texture asset: a sprite's own, or an animation's sheet. */
        Uuid textureId;

        /**
         * @brief The corners, in world space, wound so the quad faces the camera side of the plane.
         *
         * Order is top-left, top-right, bottom-right, bottom-left in the sprite's *own* frame, so
         * a caller builds two triangles as 0-1-2 and 0-2-3 and gets consistent winding whatever
         * the entity's rotation and flips are.
         */
        std::array<EditorVector3, 4> corners{};

        /**
         * @brief Texture coordinates for those four corners, in the same order.
         *
         * Carried per corner rather than as a rectangle because a sprite's source rectangle, its
         * horizontal flip and its vertical flip all land here, and resolving them once is cheaper
         * than teaching every consumer the same three rules.
         */
        std::array<EditorVector2, 4> texCoords{};

        /** @brief The sprite's tint, applied as a colour multiply. */
        EditorColor tint{255, 255, 255, 255};

        /**
         * @brief Distance from the camera to the quad's centre, for back-to-front ordering.
         *
         * Sprites are transparent and transparency does not commute: two overlapping quads blended
         * in the wrong order give a different picture, and the one that looks right is furthest
         * first. Computed here because the camera is here.
         */
        float cameraDistance = 0.0f;

        /** @brief True when the entity is selected, so the viewport can mark it. */
        bool selected = false;
    };

    /** @brief The sprites of a scene, ready to draw, furthest from the camera first. */
    struct SceneSpriteBatch3D
    {
        std::vector<SpriteQuad3D> quads;

        /** @brief Sprites whose texture asset could not be sized, and so were left out. */
        std::size_t skipped = 0;
    };

    /**
     * @brief Returns @p scene's sprites as world-space quads, sorted back to front.
     *
     * @param sizeProvider How big a texture is, in texels. The same one the 2D view and the picker
     *        ask, deliberately: see this file's third decision.
     * @param preview Which animated entity is being previewed, and at which frame. Every other
     *        animated sprite shows frame zero, exactly as the 2D content pass does -- an editor
     *        that played every clip at once would be unreadable.
     *
     * @param components Supplies declared defaults when reading an animation clip, the same way
     *        the 2D content pass reads them: a clip authored by hand may omit its frame size, and
     *        reading zero there draws nothing and explains nothing.
     *
     * Disabled entities are skipped. A sprite whose texture size is unknown is counted in
     * `skipped` rather than guessed at: a quad of a made-up size would be drawn somewhere the
     * picker does not agree with.
     */
    [[nodiscard]] SceneSpriteBatch3D buildSceneSpriteQuads(const SceneDocument& scene,
                                                           const EditorCamera3D& camera,
                                                           const SpriteSizeProvider& sizeProvider,
                                                           const AnimationPreview& preview = {},
                                                           const std::vector<Uuid>& selection = {},
                                                           const ComponentRegistry* components = nullptr);
}
