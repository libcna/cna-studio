// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/GameCamera.hpp
 * @brief What the *game* sees: the view a scene's own camera describes.
 *
 * The editor's camera is a tool -- the user pans and zooms it and it belongs to nobody but them.
 * The game's camera is data: an entity with a `CNA.Camera` component, its position in the scene and
 * its orthographic size in world units. `cna-player` needs the second one, and so does anything
 * that ever asks "what will this look like when it runs".
 *
 * CNA-free and pure, like everything else in this module, so the answer can be checked in CI
 * against a document rather than against a screenshot.
 */

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/EditorCamera2D.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief The view a scene's primary camera describes, ready to render with. */
    struct GameView
    {
        /** @brief Positioned and zoomed so the camera's orthographic size fills the height. */
        EditorCamera2D camera;

        /** @brief The colour to clear to, from the camera's own `clearColor`. */
        EditorColor clearColor{100, 149, 237, 255};

        /** @brief The camera entity this came from, or a nil id when the scene has none. */
        Uuid cameraId;

        /** @brief True when a real camera was found rather than the fallback used. */
        [[nodiscard]] bool hasCamera() const { return cameraId.isValid(); }
    };

    /**
     * @brief Returns the view @p scene's primary camera describes at @p viewportSize pixels.
     *
     * `orthographicSize` is the visible **height** in world units -- the descriptor says so and the
     * inspector shows it -- so the zoom is the pixel height divided by it. Width follows from the
     * viewport's aspect, which is what makes a window resize show more of the world rather than
     * stretching what was already there.
     *
     * Falls back to a camera centred on the origin at 1:1 when the scene has no enabled camera. A
     * player that refused to draw a scene without one would be unable to show the very scene a user
     * is trying to fix; drawing it from the origin is wrong in a way they can see and act on.
     *
     * Where several cameras claim to be primary, the first in document order wins -- and validation
     * already reports the duplicate (`duplicate-primary-camera`), so the editor has said so before
     * the player has to choose.
     */
    [[nodiscard]] GameView computeGameView(const SceneDocument& scene, const EditorVector2& viewportSize);
}
