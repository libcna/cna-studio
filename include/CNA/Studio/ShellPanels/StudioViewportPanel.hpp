// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioViewportPanel.hpp
 * @brief Navigating and selecting in the native shell's viewport.
 *
 * `plan.md` STUDIO-07009.
 *
 * ### The panel does not draw the scene
 *
 * The scene arrives as a texture the shell composites (`STUDIO-04012`); this is everything *else* a
 * viewport is — the camera the pointer moves, and what a click in it selects. Keeping the two apart
 * is what lets this half be tested with no graphics device at all: navigation and picking are
 * arithmetic over a camera and a document, and neither needs a pixel.
 *
 * ### Gestures are the ones the prototype's viewport used
 *
 * Wheel zooms about the pointer rather than about the centre, because zooming about the centre
 * makes a user chase the thing they were looking at. Middle drag pans; so does right drag, because
 * a trackpad has no middle button. Left click selects, and a click that hits nothing clears the
 * selection — which is how a user deselects without a keyboard.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/StudioCamera2D.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

namespace CNA::Studio
{
    class StudioContext;

    /** @brief What the user did in the viewport this frame. */
    struct StudioViewportResult
    {
        /** @brief The camera moved, so the scene must be re-rendered. Input pass only. */
        bool cameraChanged = false;

        /** @brief The selection changed. Input pass only. */
        bool selectionChanged = false;

        /** @brief What was picked, or the nil id when the click hit nothing. Input pass only. */
        Uuid picked;

        /** @brief The world point under the pointer, for a status bar or a ruler. */
        StudioVector2 pointerWorld;

        /** @brief Whether the pointer is over the viewport at all. */
        bool pointerInside = false;
    };

    /**
     * @brief Drives the camera and the selection from input over @p bounds.
     *
     * @param frame The frame.
     * @param bounds The viewport panel's body, in window coordinates.
     * @param context The editor. Its scene is picked against; its selection is written.
     * @param camera The editor camera, panned and zoomed in place.
     * @param sizeProvider Resolves a sprite's texel size for picking. An empty provider makes
     *        every sprite pick at its default size, which is what a build with no device can know.
     * @return What happened.
     */
    StudioViewportResult studioViewportPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioCamera2D& camera,
                                             const SpriteSizeProvider& sizeProvider = {});
}
