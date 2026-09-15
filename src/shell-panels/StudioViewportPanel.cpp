// SPDX-License-Identifier: MS-PL
/**
 * @file StudioViewportPanel.cpp
 * @brief The camera and the selection, over a rectangle the scene is drawn into.
 */

#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"

#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /**
         * @brief How much one wheel notch zooms.
         *
         * Multiplicative, not additive: zoom is a ratio, and a fixed step makes the first notch
         * out of a close view do almost nothing and the first notch out of a far one leap.
         */
        constexpr float kZoomPerNotch = 1.15f;
    }

    StudioViewportResult studioViewportPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioCamera2D& camera,
                                             const SpriteSizeProvider& sizeProvider)
    {
        StudioViewportResult result;
        if (bounds.isEmpty()) { return result; }

        // The camera is told the size it is drawing into, every frame, because a docked panel
        // changes size whenever anything else does and a camera projecting for the wrong extent
        // puts every entity somewhere the user did not click.
        camera.setViewportSize(StudioVector2{bounds.width, bounds.height});

        // The whole body is one widget. It takes focus so that the keyboard can reach the viewport
        // later, and it is what makes the panels underneath stop responding to a drag that began
        // here and wandered off.
        const WidgetId id = frame.ids().make("viewport.surface");
        const StudioInteraction surface = frame.interact(id, bounds, /*enabled=*/true);

        StudioInputRouter& router = frame.router();
        const StudioVector2 pointer{router.mouseX() - bounds.left(), router.mouseY() - bounds.top()};

        result.pointerInside = surface.hovered;
        result.pointerWorld = camera.screenToWorld(pointer);

        if (!frame.isInputPass()) { return result; }

        // --- Zoom -------------------------------------------------------------------------------
        if (surface.hovered && router.wheelY() != 0.0f)
        {
            // About the pointer, not about the centre. Zooming about the centre makes a user chase
            // the thing they were looking at across the screen.
            camera.zoomAt(pointer, std::pow(kZoomPerNotch, router.wheelY()));
            result.cameraChanged = true;
        }

        // --- Pan --------------------------------------------------------------------------------
        //
        // Middle or right. A trackpad has no middle button, and a viewport a laptop cannot pan is a
        // viewport half the users cannot use.
        const bool panning = router.mouseDown(UiMouseButton::Middle)
                          || router.mouseDown(UiMouseButton::Right);
        WidgetState& state = frame.state().get(id);
        if (panning && (surface.hovered || state.active))
        {
            if (state.active)
            {
                const StudioVector2 delta{router.mouseX() - state.scrollX,
                                          router.mouseY() - state.scrollY};
                if (delta.x != 0.0f || delta.y != 0.0f)
                {
                    camera.panByScreenDelta(delta);
                    result.cameraChanged = true;
                }
            }
            state.active = true;
            state.scrollX = router.mouseX();
            state.scrollY = router.mouseY();
        }
        else
        {
            state.active = false;
        }

        // --- Select -----------------------------------------------------------------------------
        if (surface.clicked)
        {
            const ScenePickResult pick =
                pickEntityAt(context.getScene(), camera, pointer, sizeProvider);

            result.picked = pick.entityId;
            const bool additive = frame.input().modifiers.control || frame.input().modifiers.shift;

            if (pick.entityId.isValid())
            {
                if (additive) { context.toggleSelection(pick.entityId); }
                else { context.select(pick.entityId); }
                result.selectionChanged = true;
            }
            else if (!additive && !context.getSelection().empty())
            {
                // A click on nothing clears the selection, which is how a user deselects without
                // reaching for the keyboard. Additive clicks are exempt: a missed Ctrl-click that
                // wiped a careful multi-selection would be unforgivable.
                context.clearSelection();
                result.selectionChanged = true;
            }
        }

        return result;
    }
}
