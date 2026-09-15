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
#include "CNA/Studio/Scene/SceneTransform.hpp"
#include "CNA/Studio/Scene/SceneWireframe.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
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
                            StudioViewportState& state, const std::vector<Uuid>& selection,
                            const StudioVector2& pointer)
        {
            const SceneDocument& scene = context.getScene();
            const Uuid entityId = selection.back();

            // A selection of more than one puts the manipulator at the *average* of their
            // positions, and the renderer draws it there. Hit-testing anywhere else would mean
            // grabbing a gizmo that is not where it is drawn.
            const std::optional<StudioVector2> pivot = selection.size() > 1
                ? computeSelectionPivot(scene, selection)
                : std::nullopt;

            bool began = false;
            switch (state.mode)
            {
                case GizmoMode::Translate:
                {
                    auto layout = computeTranslateGizmoLayout(scene, camera, entityId, state.space);
                    if (!layout) { return false; }
                    if (pivot) { placeGizmoAt(*layout, camera, *pivot); }

                    const GizmoHandle handle = hitTestTranslateGizmo(*layout, pointer);
                    if (handle == GizmoHandle::None) { return false; }
                    began = state.translate.begin(scene, camera, entityId, handle, pointer,
                                                  state.space);
                    break;
                }
                case GizmoMode::Rotate:
                {
                    auto layout = computeRotateGizmoLayout(scene, camera, entityId);
                    if (!layout) { return false; }
                    if (pivot) { placeGizmoAt(*layout, camera, *pivot); }

                    if (hitTestRotateGizmo(*layout, pointer) == GizmoHandle::None) { return false; }
                    began = state.rotate.begin(scene, *layout, entityId, pointer);
                    break;
                }
                case GizmoMode::Scale:
                {
                    auto layout = computeScaleGizmoLayout(scene, camera, entityId);
                    if (!layout) { return false; }
                    if (pivot) { placeGizmoAt(*layout, camera, *pivot); }

                    const GizmoHandle handle = hitTestScaleGizmo(*layout, pointer);
                    if (handle == GizmoHandle::None) { return false; }
                    began = state.scale.begin(scene, *layout, entityId, handle, pointer);
                    break;
                }
                case GizmoMode::None:
                    break;
            }

            if (began && pivot)
            {
                // The roots only: a child moves when its parent does, and applying a drag to both
                // would move it twice.
                state.multi.begin(scene, selection, *pivot);
                ++state.multiDragId;
            }
            return began;
        }

        /** @brief Applies one frame of a drag that is moving a whole selection. */
        bool updateMultiDrag(StudioContext& context, const StudioCamera2D& camera,
                             StudioViewportState& state, const StudioVector2& pointer,
                             const GizmoSnap& snap)
        {
            const SceneDocument& scene = context.getScene();
            std::vector<EntityTransformEdit> edits;

            if (state.translate.isActive())
            {
                edits = state.multi.translate(
                    scene, state.translate.getWorldDelta(camera, pointer, snap));
            }
            else if (state.rotate.isActive())
            {
                auto layout = computeRotateGizmoLayout(scene, camera, state.rotate.getEntityId());
                if (!layout) { return false; }
                // The pivot captured when the drag began, not a fresh centroid: the entities are
                // moving as the drag proceeds, and a centre recomputed from them chases itself.
                placeGizmoAt(*layout, camera, state.multi.getPivot());
                edits = state.multi.rotate(scene, state.rotate.getDeltaAngle(*layout, pointer, snap));
            }
            else if (state.scale.isActive())
            {
                auto layout = computeScaleGizmoLayout(scene, camera, state.scale.getEntityId());
                if (!layout) { return false; }
                placeGizmoAt(*layout, camera, state.multi.getPivot());

                const float factor = state.scale.getFactor(*layout, pointer, snap);
                const GizmoHandle handle = state.scale.getHandle();
                edits = state.multi.scale(
                    scene, StudioVector2{handle == GizmoHandle::YAxis ? 1.0f : factor,
                                         handle == GizmoHandle::XAxis ? 1.0f : factor});
            }

            if (edits.empty()) { return false; }

            // One command for the whole selection, and one undo entry for the whole drag. A command
            // per entity would make undoing one gesture several presses of Ctrl+Z, and would undo
            // them one at a time through arrangements the scene was never in.
            context.execute(
                std::make_unique<TransformEntitiesCommand>(
                    context.getScene(), std::move(edits),
                    "transform-many:" + std::to_string(state.multiDragId)),
                state.dragHasEdited ? MergePolicy::MergeWithPrevious : MergePolicy::NewEntry);
            state.dragHasEdited = true;
            return true;
        }

        /** @brief Applies one frame of whichever drag is in flight. @return True when it moved. */
        bool updateGizmoDrag(StudioContext& context, const StudioCamera2D& camera,
                             StudioViewportState& state, const StudioVector2& pointer,
                             const GizmoSnap& snap)
        {
            if (state.multi.isActive())
            {
                return updateMultiDrag(context, camera, state, pointer, snap);
            }

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

    std::string_view studioViewportToolName(StudioViewportTool tool)
    {
        switch (tool)
        {
            case StudioViewportTool::Select: return "Select";
            case StudioViewportTool::PaintTiles: return "Paint Tiles";
            case StudioViewportTool::EraseTiles: return "Erase Tiles";
            case StudioViewportTool::PickTile: return "Pick Tile";
            case StudioViewportTool::FillTiles: return "Fill Tiles";
        }
        return "Select";
    }

    bool studioViewportToolPaints(StudioViewportTool tool)
    {
        return tool != StudioViewportTool::Select;
    }

    namespace
    {
        /**
         * @brief The tile the cursor is over, on the selected entity's tilemap.
         *
         * @param context The editor.
         * @param camera The viewport camera.
         * @param pointer Cursor position in panel coordinates.
         * @param report Whether to say why when there is no tilemap to paint into. Once per press
         *        rather than per frame: a brush over a sprite is a near miss, and sixty lines a
         *        second about it is how a console stops being read.
         * @return The cell, or nothing.
         */
        std::optional<TileCoordinate> tileUnder(StudioContext& context, const StudioCamera2D& camera,
                                                const StudioVector2& pointer, bool report)
        {
            const Uuid selected = context.getPrimarySelection();
            const StudioEntity* entity = context.getScene().findEntity(selected);
            const StudioComponent* tilemap =
                entity != nullptr ? entity->findComponent(BuiltinComponentIds::kTilemap) : nullptr;

            if (tilemap == nullptr)
            {
                if (report)
                {
                    context.log(LogSeverity::Warning,
                                "Select an entity with a Tilemap component to paint into.");
                }
                return std::nullopt;
            }

            const std::optional<WorldTransform> transform =
                computeWorldTransform(context.getScene(), selected);
            if (!transform) { return std::nullopt; }

            const ComponentDescriptor* descriptor =
                context.getComponentRegistry().find(BuiltinComponentIds::kTilemap);

            return worldToTile(
                *transform,
                static_cast<int>(tilemap->getPropertyOrDefault(TilemapKeys::kTileWidth, descriptor)
                                     .get<std::int64_t>(0)),
                static_cast<int>(tilemap->getPropertyOrDefault(TilemapKeys::kTileHeight, descriptor)
                                     .get<std::int64_t>(0)),
                camera.screenToWorld(pointer));
        }

        /** @brief Writes one cell, opening an undo entry on the first of a stroke. */
        bool paintCell(StudioContext& context, StudioViewportState& state,
                       const TileCoordinate& cell, std::int64_t value)
        {
            auto command = std::make_unique<PaintTilesCommand>(context.getScene(),
                                                               context.getComponentRegistry(),
                                                               context.getPrimarySelection(),
                                                               state.paintStroke);
            if (!command->paint(cell.x, cell.y, value)) { return false; }

            // The first cell of a stroke opens a new entry and every later one merges into it,
            // which is what makes a drag across forty tiles one Ctrl+Z.
            const MergePolicy policy = state.paintStrokeHasEdited ? MergePolicy::MergeWithPrevious
                                                                  : MergePolicy::NewEntry;
            state.paintStrokeHasEdited = true;
            context.execute(std::move(command), policy);
            return true;
        }

        /**
         * @brief Runs whichever tile tool is active. Returns true when it took the press.
         *
         * Before selection and before the gizmo, because a tool that painted *and* selected would
         * move the inspector out from under the user on every stroke -- and the tilemap they are
         * painting into is the thing that has to stay selected for the next cell to land.
         */
        bool applyTileTool(StudioFrame& frame, StudioContext& context, const StudioCamera2D& camera,
                           StudioViewportState& state, const StudioInteraction& surface,
                           const StudioVector2& pointer, StudioViewportResult& result)
        {
            if (!studioViewportToolPaints(state.tool)) { return false; }

            StudioInputRouter& router = frame.router();
            const bool held = router.mouseDown(UiMouseButton::Left);

            switch (state.tool)
            {
                case StudioViewportTool::PaintTiles:
                case StudioViewportTool::EraseTiles:
                {
                    const bool starting = surface.pressed;
                    if (!starting && !(held && state.paintStrokeHasEdited)) { return true; }

                    if (starting)
                    {
                        ++state.paintStroke;
                        state.paintStrokeHasEdited = false;
                    }

                    const std::optional<TileCoordinate> cell =
                        tileUnder(context, camera, pointer, starting);
                    if (!cell) { return true; }

                    const std::int64_t value = state.tool == StudioViewportTool::EraseTiles
                        ? kEmptyTile : state.paintTile;
                    if (paintCell(context, state, *cell, value)) { result.tilesPainted = true; }
                    return true;
                }

                case StudioViewportTool::PickTile:
                {
                    if (!surface.pressed) { return true; }

                    const std::optional<TileCoordinate> cell =
                        tileUnder(context, camera, pointer, true);
                    if (!cell) { return true; }

                    const StudioEntity* entity =
                        context.getScene().findEntity(context.getPrimarySelection());
                    const StudioComponent* tilemap = entity != nullptr
                        ? entity->findComponent(BuiltinComponentIds::kTilemap) : nullptr;
                    if (tilemap == nullptr) { return true; }

                    const TilemapGrid grid = readTilemapGrid(
                        *tilemap, context.getComponentRegistry().find(BuiltinComponentIds::kTilemap));

                    // An empty cell is not a brush. Taking one would leave the user painting
                    // nothing and wondering why the tool stopped working.
                    const std::int64_t picked = grid.at(cell->x, cell->y);
                    if (picked == kEmptyTile) { return true; }

                    // The brush, and then painting: an eyedropper that left the user still holding
                    // the eyedropper is one they have to put down before they can use what it took.
                    state.paintTile = picked;
                    state.tool = StudioViewportTool::PaintTiles;
                    result.toolChanged = true;
                    return true;
                }

                case StudioViewportTool::FillTiles:
                {
                    if (surface.pressed)
                    {
                        state.fillStart = tileUnder(context, camera, pointer, true);
                        return true;
                    }
                    if (held || !state.fillStart) { return true; }

                    // On the release, so a drag can be adjusted before it commits -- and as one
                    // entry, because a fill is one intention however many cells it covers.
                    const std::optional<TileCoordinate> end =
                        tileUnder(context, camera, pointer, false);
                    const TileCoordinate from = *state.fillStart;
                    state.fillStart.reset();
                    if (!end) { return true; }

                    ++state.paintStroke;
                    state.paintStrokeHasEdited = false;

                    for (int y = std::min(from.y, end->y); y <= std::max(from.y, end->y); ++y)
                    {
                        for (int x = std::min(from.x, end->x); x <= std::max(from.x, end->x); ++x)
                        {
                            if (paintCell(context, state, TileCoordinate{x, y}, state.paintTile))
                            {
                                result.tilesPainted = true;
                            }
                        }
                    }
                    return true;
                }

                case StudioViewportTool::Select: return false;
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

        // --- Paint ------------------------------------------------------------------------------
        //
        // Before the gizmo and before selection. A tool that painted *and* selected would move the
        // inspector out from under the user on every stroke, and the tilemap being painted into is
        // exactly the thing that has to stay selected for the next cell to land.
        if (applyTileTool(frame, context, camera, state, surface, pointer, result))
        {
            return result;
        }

        const std::vector<Uuid>& selection = context.getSelection();
        if (surface.pressed && state.mode != GizmoMode::None && !selection.empty()
            && beginGizmoDrag(context, camera, state, selection, pointer))
        {
            return result;
        }

        // --- Zoom -------------------------------------------------------------------------------
        if (surface.hovered && router.wheelY() != 0.0f)
        {
            // About the pointer, not about the centre. Zooming about the centre makes a user chase
            // the thing they were looking at across the screen.
            const float notches = state.invertZoom ? -router.wheelY() : router.wheelY();
            camera.zoomAt(pointer, std::pow(kZoomPerNotch, notches));
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
                    camera.panByScreenDelta(StudioVector2{delta.x * state.cameraSpeed, delta.y * state.cameraSpeed});
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

    const char* studioViewportViewName(StudioViewportView view)
    {
        switch (view)
        {
            case StudioViewportView::TwoD: return "2D";
            case StudioViewportView::ThreeD: return "3D";
        }
        return "2D";
    }

    StudioViewportResult studioViewportPanel3D(StudioFrame& frame, const UiRect& bounds,
                                               StudioContext& context, StudioCamera3D& camera,
                                               StudioViewportState& state,
                                               const SpriteSizeProvider& sizeProvider)
    {
        StudioViewportResult result;
        if (bounds.isEmpty()) { return result; }

        camera.setViewportSize(StudioVector2{bounds.width, bounds.height});

        // One widget over the whole body, as in 2D and for the same reason: it takes focus so the
        // keyboard can reach the viewport, and it is what stops the panels underneath responding
        // to a drag that began here and wandered off.
        const WidgetId id = frame.ids().make("viewport.surface3d");
        const StudioInteraction surface = frame.interact(id, bounds, /*enabled=*/true);

        StudioInputRouter& router = frame.router();
        const StudioVector2 pointer{router.mouseX() - bounds.left(), router.mouseY() - bounds.top()};

        result.pointerInside = surface.hovered;

        if (!frame.isInputPass()) { return result; }

        const UiInputState& input = frame.input();

        if (surface.hovered && input.wheelY != 0.0f)
        {
            // Geometric, like the 2D zoom and for the same reason: one notch has to feel the same
            // close up and far away. Scrolling up moves the eye towards the pivot, so the exponent
            // is negated.
            constexpr float kDollyPerNotch = 1.15f;
            const float notches = state.invertZoom ? input.wheelY : -input.wheelY;
            camera.dolly(std::pow(kDollyPerNotch, notches));
            result.cameraChanged = true;
        }

        const bool left = router.mouseDown(UiMouseButton::Left);
        const bool middle = router.mouseDown(UiMouseButton::Middle);
        const bool right = router.mouseDown(UiMouseButton::Right);
        const bool anyButton = left || middle || right;

        if (state.navigating && !anyButton)
        {
            state.navigating = false;
            // A release that moved nothing is a click, and a click selects. Tracked rather than
            // read from the interaction because a drag that left the panel and came back must not
            // count as one -- the same rule the 2D viewport applies to its gizmo.
            if (!state.navigationMoved && surface.hovered) { result.clicked3D = true; }
        }
        else if (!state.navigating && anyButton && surface.pressed)
        {
            state.navigating = true;
            state.navigationMoved = false;
            state.navigationX = pointer.x;
            state.navigationY = pointer.y;
        }

        if (state.navigating)
        {
            const StudioVector2 delta{pointer.x - state.navigationX, pointer.y - state.navigationY};
            state.navigationX = pointer.x;
            state.navigationY = pointer.y;

            if (std::abs(delta.x) > 0.0f || std::abs(delta.y) > 0.0f)
            {
                state.navigationMoved = true;

                // Radians per pixel. A full turn across a 900-pixel panel is the rate every 3D
                // editor has converged on, and it is deliberately independent of the panel's size:
                // a rate derived from the width would turn faster in a narrow panel than a wide
                // one, which is the kind of thing nobody reports and everybody notices.
                const float radiansPerPixel = 0.007f * state.cameraSpeed;

                if (middle || input.modifiers.shift)
                {
                    camera.panByScreenDelta(StudioVector2{delta.x * state.cameraSpeed, delta.y * state.cameraSpeed});
                }
                else if (right)
                {
                    // Turning in place rather than about the pivot: the gesture that goes with
                    // flying, and the reason `look()` exists beside `orbit()`.
                    camera.look(-delta.x * radiansPerPixel, delta.y * radiansPerPixel);
                }
                else if (left)
                {
                    camera.orbit(-delta.x * radiansPerPixel, delta.y * radiansPerPixel);
                }
                result.cameraChanged = true;
            }
        }

        // Flying, while the right button is held. The modifier is what keeps W, A, S and D from
        // meaning two things at once: they are the gizmo shortcuts everywhere else.
        if (right)
        {
            // Proportional to the orbit distance, so one press crosses the same fraction of what
            // is on screen whether the camera is inside a room or above a level.
            const float step = std::max(0.05f, camera.getDistance() * 0.04f) * state.cameraSpeed;

            StudioVector3 move;
            if (input.isKeyDown(UiKey::W)) { move.z += step; }
            if (input.isKeyDown(UiKey::S)) { move.z -= step; }
            if (input.isKeyDown(UiKey::D)) { move.x += step; }
            if (input.isKeyDown(UiKey::A)) { move.x -= step; }
            if (input.isKeyDown(UiKey::E)) { move.y += step; }
            if (input.isKeyDown(UiKey::Q)) { move.y -= step; }

            if (!(move == StudioVector3{}))
            {
                camera.moveLocal(move);
                result.cameraChanged = true;
            }
        }

        if (!result.clicked3D) { return result; }

        const Uuid picked = pickEntityAt3D(context.getScene(), camera, pointer, sizeProvider);
        result.picked = picked;

        // The same two selection rules the 2D viewport has, because they are rules about selecting
        // rather than about a projection: Ctrl adds and removes, and Ctrl on empty space leaves a
        // half-assembled selection alone.
        if (input.modifiers.control)
        {
            if (picked.isValid())
            {
                context.toggleSelection(picked);
                result.selectionChanged = true;
            }
            return result;
        }

        context.select(picked);
        result.selectionChanged = true;
        return result;
    }

    void studioViewportToolOverlay(StudioFrame& frame, const UiRect& bounds,
                                   StudioViewportState& state)
    {
        if (bounds.isEmpty() || !studioViewportToolPaints(state.tool)) { return; }

        const StudioTheme& theme = frame.theme();
        const float pad = static_cast<float>(theme.metric(StudioMetric::SpacingSmall));
        const float rowHeight = static_cast<float>(theme.metric(StudioMetric::ControlHeight));

        // Over the image, in the corner, exactly where the prototype puts it. A tool that armed
        // silently would be a viewport where a press does something different from yesterday and
        // nothing on the screen says so.
        UiRect strip{bounds.x + pad, bounds.y + pad,
                     std::min(bounds.width - pad * 2.0f, 260.0f), rowHeight};
        if (strip.width <= 0.0f || strip.height > bounds.height) { return; }

        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(strip, theme.color(StudioColorRole::PopupBackground));
            frame.drawList().strokeRect(strip, theme.color(StudioColorRole::Border),
                                        static_cast<float>(theme.metric(StudioMetric::BorderWidth)));
        }

        UiRect row = strip.inset(UiEdges{pad, 0.0f});
        frame.ids().push("viewport.tool");

        // The tile index only where it means something: the eraser has no index and the eyedropper
        // sets one rather than reading it, so a field beside either would be a control that does
        // nothing.
        const bool needsTile = state.tool == StudioViewportTool::PaintTiles
                            || state.tool == StudioViewportTool::FillTiles;

        if (needsTile)
        {
            const UiRect field = row.splitRight(std::min(row.width, rowHeight * 2.5f));
            row.splitRight(std::min(pad, row.width));

            std::string text = std::to_string(state.paintTile);
            StudioTextFieldOptions options;
            options.font = StudioFontRole::Monospace;
            options.selectAllOnFocus = true;
            options.placeholder = "tile";
            if (studioTextField(frame, frame.ids().make("tile"), field, text, options).committed)
            {
                try
                {
                    const std::int64_t parsed = std::stoll(text);
                    if (parsed >= 0) { state.paintTile = parsed; }
                }
                catch (const std::exception&) { /* left as it was */ }
            }
        }

        if (frame.isDrawPass())
        {
            studioDrawText(frame, row, studioViewportToolName(state.tool), StudioFontRole::Body,
                           theme.color(StudioColorRole::TextPrimary));
        }

        frame.ids().pop();
    }

    bool studioFrameSelection(const StudioContext& context, StudioCamera2D& camera,
                              const SpriteSizeProvider& sizeProvider)
    {
        const std::vector<Uuid>& selection = context.getSelection();
        if (selection.empty()) { return false; }

        std::optional<WorldBounds2D> total;
        for (const Uuid& entityId : selection)
        {
            std::optional<WorldBounds2D> bounds =
                computeHierarchyBounds2D(context.getScene(), entityId, sizeProvider);

            if (!bounds)
            {
                // An entity with no drawable geometry -- a camera, an empty grouping node -- still
                // has a position, and framing it should centre on it rather than do nothing.
                const std::optional<WorldTransform> world =
                    computeWorldTransform(context.getScene(), entityId);
                if (!world) { continue; }

                const StudioVector2 point{world->position.x, world->position.y};
                bounds = WorldBounds2D{point, point};
            }

            total = total ? WorldBounds2D::combine(*total, *bounds) : bounds;
        }

        if (!total) { return false; }
        camera.frame(*total);
        return true;
    }
}
