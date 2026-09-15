// SPDX-License-Identifier: MS-PL
/**
 * @file StudioViewportPanel.cpp
 * @brief The camera and the selection, over a rectangle the scene is drawn into.
 */

#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cmath>
#include <memory>
#include <vector>

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

    namespace
    {
        /**
         * @brief Writes one transform property through the history, merging into the drag's entry.
         *
         * The first frame of a drag opens an undo entry and every frame after merges into it, so
         * the whole gesture is one Ctrl+Z rather than sixty a second.
         */
        void commitDragEdit(StudioContext& context, StudioViewportState& state,
                            const Uuid& entityId, const char* property, PropertyValue value)
        {
            context.execute(
                std::make_unique<SetPropertyCommand>(context.getScene(), entityId,
                                                     BuiltinComponentIds::kTransform, property,
                                                     std::move(value)),
                state.dragHasEdited ? MergePolicy::MergeWithPrevious : MergePolicy::NewEntry);
            state.dragHasEdited = true;
        }

        /** @brief How much a drag rounds by while the snap modifier is held. */
        GizmoSnap snapFor(const StudioContext& context, const StudioCamera2D& camera, bool held)
        {
            if (!held) { return {}; }

            GizmoSnap snap;
            // The project's own step when it declares one, and the visible grid when it does not:
            // a project laid out on a 16-pixel tile grid says so once rather than having every
            // user zoom until the drawn grid happens to agree.
            const float projectStep = context.hasProject() ? context.getProject().getGridSnap() : 0.0f;
            snap.translate = projectStep > 0.0f
                ? projectStep
                : chooseGridSpacing(camera.getZoom(), kGridTargetPixels);
            snap.rotate = kDefaultRotationSnap;
            snap.scale = kDefaultScaleSnap;
            return snap;
        }

        /**
         * @brief Starts a manipulator drag when the press landed on one of its handles.
         * @return True when a drag began, meaning the press must not also pick.
         */
        bool beginGizmoDrag(StudioContext& context, StudioCamera2D& camera,
                            StudioViewportState& state, const Uuid& entityId,
                            const StudioVector2& pointer)
        {
            const SceneDocument& scene = context.getScene();

            switch (state.mode)
            {
                case GizmoMode::Translate:
                {
                    const auto layout =
                        computeTranslateGizmoLayout(scene, camera, entityId, state.space);
                    if (!layout) { return false; }
                    const GizmoHandle handle = hitTestTranslateGizmo(*layout, pointer);
                    if (handle == GizmoHandle::None) { return false; }
                    return state.translate.begin(scene, camera, entityId, handle, pointer,
                                                 state.space);
                }
                case GizmoMode::Rotate:
                {
                    const auto layout = computeRotateGizmoLayout(scene, camera, entityId);
                    if (!layout) { return false; }
                    if (hitTestRotateGizmo(*layout, pointer) == GizmoHandle::None) { return false; }
                    return state.rotate.begin(scene, *layout, entityId, pointer);
                }
                case GizmoMode::Scale:
                {
                    const auto layout = computeScaleGizmoLayout(scene, camera, entityId);
                    if (!layout) { return false; }
                    const GizmoHandle handle = hitTestScaleGizmo(*layout, pointer);
                    if (handle == GizmoHandle::None) { return false; }
                    return state.scale.begin(scene, *layout, entityId, handle, pointer);
                }
                case GizmoMode::None:
                    break;
            }
            return false;
        }

        /** @brief Applies one frame of whichever drag is in flight. @return True when it moved. */
        bool updateGizmoDrag(StudioContext& context, const StudioCamera2D& camera,
                             StudioViewportState& state, const StudioVector2& pointer,
                             const GizmoSnap& snap)
        {
            const SceneDocument& scene = context.getScene();

            if (state.translate.isActive())
            {
                if (const auto position = state.translate.update(scene, camera, pointer, snap))
                {
                    commitDragEdit(context, state, state.translate.getEntityId(), "position",
                                   PropertyValue{*position});
                    return true;
                }
                return false;
            }
            if (state.rotate.isActive())
            {
                const auto layout =
                    computeRotateGizmoLayout(scene, camera, state.rotate.getEntityId());
                if (!layout) { return false; }
                if (const auto rotation = state.rotate.update(*layout, pointer, snap))
                {
                    commitDragEdit(context, state, state.rotate.getEntityId(), "rotation",
                                   PropertyValue{*rotation});
                    return true;
                }
                return false;
            }
            if (state.scale.isActive())
            {
                const auto layout =
                    computeScaleGizmoLayout(scene, camera, state.scale.getEntityId());
                if (!layout) { return false; }
                if (const auto scale = state.scale.update(*layout, pointer, snap))
                {
                    commitDragEdit(context, state, state.scale.getEntityId(), "scale",
                                   PropertyValue{*scale});
                    return true;
                }
            }
            return false;
        }
    }

    StudioViewportResult studioViewportPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioCamera2D& camera,
                                             StudioViewportState& state,
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

        // --- Manipulate -------------------------------------------------------------------------
        const GizmoSnap snap = snapFor(context, camera, frame.input().modifiers.control);

        if (state.dragging())
        {
            if (!router.mouseDown(UiMouseButton::Left))
            {
                // Ended on the release, wherever the pointer is. A drag that only ended when the
                // release landed back inside the viewport would leave the gizmo stuck to the
                // cursor the moment somebody let go over a panel.
                state.endDrag();
            }
            else if (updateGizmoDrag(context, camera, state, pointer, snap))
            {
                result.transformed = true;
            }
            // While a manipulator has the pointer nothing else does: the press that grabbed it
            // must not also select, and the drag must not also pan.
            return result;
        }

        const std::vector<Uuid>& selection = context.getSelection();
        if (surface.pressed && state.mode != GizmoMode::None && !selection.empty()
            && beginGizmoDrag(context, camera, state, selection.back(), pointer))
        {
            return result;
        }

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
        WidgetState& panState = frame.state().get(id);
        if (panning && (surface.hovered || panState.active))
        {
            if (panState.active)
            {
                const StudioVector2 delta{router.mouseX() - panState.scrollX,
                                          router.mouseY() - panState.scrollY};
                if (delta.x != 0.0f || delta.y != 0.0f)
                {
                    camera.panByScreenDelta(delta);
                    result.cameraChanged = true;
                }
            }
            panState.active = true;
            panState.scrollX = router.mouseX();
            panState.scrollY = router.mouseY();
        }
        else
        {
            panState.active = false;
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
