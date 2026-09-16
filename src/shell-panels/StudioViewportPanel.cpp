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
            // A drag that has not actually changed anything writes nothing (STUDIO-07047). A press
            // on an arm followed by a release without movement is an ordinary thing to do -- a user
            // grabs a handle and thinks better of it -- and it used to leave an undo entry behind
            // that undid nothing, so the next Ctrl+Z appeared to do nothing at all.
            //
            // Compared against the document rather than against the pointer: a drag *can* return to
            // where it started, and the entry for that gesture should go too.
            const StudioEntity* entity = context.getScene().findEntity(entityId);
            const StudioComponent* transform =
                entity != nullptr ? entity->findComponent(BuiltinComponentIds::kTransform) : nullptr;
            if (transform != nullptr && transform->getProperty(property) == value) { return; }

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

            // Drop the edits that change nothing, for the reason `commitDragEdit` gives: a gesture
            // that moved no entity must not leave an undo entry that moves none back.
            const auto changesSomething = [&scene](const EntityTransformEdit& edit) {
                const StudioEntity* entity = scene.findEntity(edit.entityId);
                const StudioComponent* transform =
                    entity != nullptr ? entity->findComponent(BuiltinComponentIds::kTransform)
                                      : nullptr;
                if (transform == nullptr) { return false; }
                if (edit.position.has_value()
                    && transform->getProperty("position") != PropertyValue{*edit.position})
                {
                    return true;
                }
                if (edit.rotation.has_value()
                    && transform->getProperty("rotation") != PropertyValue{*edit.rotation})
                {
                    return true;
                }
                return edit.scale.has_value()
                    && transform->getProperty("scale") != PropertyValue{*edit.scale};
            };
            if (std::none_of(edits.begin(), edits.end(), changesSomething)) { return false; }

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

        // -----------------------------------------------------------------------------------
        // The same three manipulators, over the 3D view (STUDIO-07050)
        //
        // `studioViewportPanel3D` picked and did not manipulate: `TransformGizmos3D.hpp` has
        // carried the layout, hit-test and drag maths for all three since the 2D panel showed
        // how a panel drives one, unit-tested in `SceneTests.cpp`, and reached by nothing.
        // What follows is wiring, in the same shape as the three functions above it -- reusing
        // `commitDragEdit` verbatim, since it takes no 2D type at all.
        // -----------------------------------------------------------------------------------

        /** @brief How much a 3D drag rounds by while the snap modifier is held. */
        GizmoSnap snapFor3D(const StudioContext& context, bool held)
        {
            if (!held) { return {}; }

            GizmoSnap snap;
            // The project's own step when it declares one. Unlike the 2D gizmo, a 3D translate
            // has no on-screen grid to fall back to -- the visible floor is a decision of
            // STUDIO-35051, not of this one -- so an undeclared step lands on one world unit,
            // which is the increment every one of this project's own example scenes is authored
            // on.
            const float projectStep = context.hasProject() ? context.getProject().getGridSnap() : 0.0f;
            snap.translate = projectStep > 0.0f ? projectStep : 1.0f;
            snap.rotate = kDefaultRotationSnap;
            snap.scale = kDefaultScaleSnap;
            return snap;
        }

        /**
         * @brief Starts a 3D manipulator drag when the press landed on one of its handles.
         * @return True when a drag began, meaning the press must not also orbit or select.
         */
        bool beginGizmoDrag3D(StudioContext& context, const StudioCamera3D& camera,
                              StudioViewportState& state, const std::vector<Uuid>& selection,
                              const StudioVector2& pointer)
        {
            const SceneDocument& scene = context.getScene();
            const Uuid entityId = selection.back();

            // The 3D layout functions take the pivot directly rather than being relocated after
            // the fact, unlike the 2D ones: there is no equivalent of `placeGizmoAt` here because
            // none is needed.
            const std::optional<StudioVector3> pivot =
                selection.size() > 1 ? computeSelectionPivot3D(scene, selection) : std::nullopt;

            bool began = false;
            switch (state.mode)
            {
                case GizmoMode::Translate:
                {
                    auto layout =
                        computeTranslateGizmo3DLayout(scene, camera, entityId, state.space, pivot);
                    if (!layout) { return false; }
                    if (hitTestTranslateGizmo3D(*layout, pointer) == GizmoAxis3D::None)
                    {
                        return false;
                    }
                    began = state.translate3D.begin(scene, camera, *layout, entityId, pointer);
                    break;
                }
                case GizmoMode::Rotate:
                {
                    auto layout =
                        computeRotateGizmo3DLayout(scene, camera, entityId, state.space, pivot);
                    if (!layout) { return false; }
                    if (hitTestRotateGizmo3D(*layout, pointer) == GizmoAxis3D::None)
                    {
                        return false;
                    }
                    began = state.rotate3D.begin(scene, camera, *layout, entityId, pointer);
                    break;
                }
                case GizmoMode::Scale:
                {
                    auto layout = computeScaleGizmo3DLayout(scene, camera, entityId, pivot);
                    if (!layout) { return false; }
                    if (hitTestScaleGizmo3D(*layout, pointer) == GizmoAxis3D::None) { return false; }
                    began = state.scale3D.begin(scene, *layout, entityId, pointer);
                    break;
                }
                case GizmoMode::None:
                    break;
            }

            if (began && pivot)
            {
                // The roots only: a child moves when its parent does, and applying a drag to
                // both would move it twice.
                state.multi3D.begin(scene, selection, *pivot);
                ++state.multiDragId;
            }
            return began;
        }

        /** @brief Applies one frame of a 3D multi-selection drag. @return True when it moved. */
        bool updateMultiDrag3D(StudioContext& context, const StudioCamera3D& camera,
                               StudioViewportState& state, const StudioVector2& pointer,
                               const GizmoSnap& snap)
        {
            const SceneDocument& scene = context.getScene();
            std::vector<EntityTransformEdit> edits;

            if (state.translate3D.isActive())
            {
                const std::optional<StudioVector3> delta =
                    state.translate3D.getWorldDelta(camera, pointer, snap);
                if (!delta) { return false; }
                edits = state.multi3D.translate(scene, *delta);
            }
            else if (state.rotate3D.isActive())
            {
                const std::optional<float> angle =
                    state.rotate3D.getDeltaAngle(camera, pointer, snap);
                if (!angle) { return false; }
                edits = state.multi3D.rotate(scene, state.rotate3D.getNormal(), *angle);
            }
            else if (state.scale3D.isActive())
            {
                auto layout = computeScaleGizmo3DLayout(scene, camera, state.scale3D.getEntityId(),
                                                         state.multi3D.getPivot());
                if (!layout) { return false; }

                const float factor = state.scale3D.getFactor(*layout, pointer, snap);
                const GizmoAxis3D axis = state.scale3D.getAxis();
                // The grabbed arm's own factor, one on the other two -- exactly the 2D scale
                // gizmo's rule, generalised from two axes to three.
                const StudioVector3 perAxis{axis == GizmoAxis3D::X ? factor : 1.0f,
                                            axis == GizmoAxis3D::Y ? factor : 1.0f,
                                            axis == GizmoAxis3D::Z ? factor : 1.0f};
                edits = state.multi3D.scale(scene, layout->axes, perAxis);
            }

            if (edits.empty()) { return false; }

            const auto changesSomething = [&scene](const EntityTransformEdit& edit) {
                const StudioEntity* entity = scene.findEntity(edit.entityId);
                const StudioComponent* transform =
                    entity != nullptr ? entity->findComponent(BuiltinComponentIds::kTransform)
                                      : nullptr;
                if (transform == nullptr) { return false; }
                if (edit.position.has_value()
                    && transform->getProperty("position") != PropertyValue{*edit.position})
                {
                    return true;
                }
                if (edit.rotation.has_value()
                    && transform->getProperty("rotation") != PropertyValue{*edit.rotation})
                {
                    return true;
                }
                return edit.scale.has_value()
                    && transform->getProperty("scale") != PropertyValue{*edit.scale};
            };
            if (std::none_of(edits.begin(), edits.end(), changesSomething)) { return false; }

            context.execute(
                std::make_unique<TransformEntitiesCommand>(
                    context.getScene(), std::move(edits),
                    "transform-many:" + std::to_string(state.multiDragId)),
                state.dragHasEdited ? MergePolicy::MergeWithPrevious : MergePolicy::NewEntry);
            state.dragHasEdited = true;
            return true;
        }

        /** @brief Applies one frame of whichever 3D drag is in flight. @return True when moved. */
        bool updateGizmoDrag3D(StudioContext& context, const StudioCamera3D& camera,
                               StudioViewportState& state, const StudioVector2& pointer,
                               const GizmoSnap& snap)
        {
            if (state.multi3D.isActive())
            {
                return updateMultiDrag3D(context, camera, state, pointer, snap);
            }

            const SceneDocument& scene = context.getScene();

            if (state.translate3D.isActive())
            {
                if (const auto position = state.translate3D.update(scene, camera, pointer, snap))
                {
                    commitDragEdit(context, state, state.translate3D.getEntityId(), "position",
                                   PropertyValue{*position});
                    return true;
                }
                return false;
            }
            if (state.rotate3D.isActive())
            {
                if (const auto rotation = state.rotate3D.update(scene, camera, pointer, snap))
                {
                    commitDragEdit(context, state, state.rotate3D.getEntityId(), "rotation",
                                   PropertyValue{*rotation});
                    return true;
                }
                return false;
            }
            if (state.scale3D.isActive())
            {
                auto layout = computeScaleGizmo3DLayout(scene, camera, state.scale3D.getEntityId());
                if (!layout) { return false; }
                if (const auto scale = state.scale3D.update(*layout, pointer, snap))
                {
                    commitDragEdit(context, state, state.scale3D.getEntityId(), "scale",
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

    std::string_view studioViewportGestureName(StudioViewportGesture gesture)
    {
        switch (gesture)
        {
            case StudioViewportGesture::None:  return "none";
            case StudioViewportGesture::Orbit: return "orbit";
            case StudioViewportGesture::Pan:   return "pan";
            case StudioViewportGesture::Dolly: return "dolly";
            case StudioViewportGesture::Look:  return "look";
        }
        return "";
    }

    StudioViewportGesture studioViewportGestureFor(StudioNavigationStyle style,
                                                   const StudioViewportChord& chord)
    {
        switch (style)
        {
            case StudioNavigationStyle::Maya:
                // Everything behind Alt, which is the whole of Maya's arrangement: an unmodified
                // drag is *always* a selection, in every viewport, whatever else is going on. A
                // scheme that let one unmodified button navigate would be the thing a Maya user
                // finds by moving the camera when they meant to pick something.
                if (!chord.alt) { return StudioViewportGesture::None; }
                if (chord.left)   { return StudioViewportGesture::Orbit; }
                if (chord.middle) { return StudioViewportGesture::Pan; }
                if (chord.right)  { return StudioViewportGesture::Dolly; }
                return StudioViewportGesture::None;

            case StudioNavigationStyle::Blender:
                // Everything on the middle button, with Shift and Control as the modifiers, which
                // leaves left free for selection for the same reason Maya's Alt does. Order
                // matters: Shift+Control+middle is a zoom in Blender, so Control is tested first.
                if (!chord.middle) { return StudioViewportGesture::None; }
                if (chord.control) { return StudioViewportGesture::Dolly; }
                if (chord.shift)   { return StudioViewportGesture::Pan; }
                return StudioViewportGesture::Orbit;

            case StudioNavigationStyle::Studio:
                break;
        }

        // Studio's own, which is what this editor shipped with and is deliberately unchanged:
        // middle or Shift pans, right turns the eye in place and flies, left orbits.
        if (chord.middle || chord.shift) { return StudioViewportGesture::Pan; }
        if (chord.right) { return StudioViewportGesture::Look; }
        if (chord.left) { return StudioViewportGesture::Orbit; }
        return StudioViewportGesture::None;
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
        // Under Studio's own scheme: middle or right. A trackpad has no middle button, and a
        // viewport a laptop cannot pan is a viewport half the users cannot use.
        //
        // Under Maya and Blender, whatever their schemes call a pan -- which for a 2D view is the
        // only camera gesture there is beside the wheel, so an Orbit resolves to a pan here. A user
        // who set the scheme for the 3D view and found the 2D one unchanged would have half a
        // preference, which is the defect this whole task exists to fix.
        StudioViewportChord chord;
        chord.left = router.mouseDown(UiMouseButton::Left);
        chord.middle = router.mouseDown(UiMouseButton::Middle);
        chord.right = router.mouseDown(UiMouseButton::Right);
        chord.alt = frame.input().modifiers.alt;
        chord.shift = frame.input().modifiers.shift;
        chord.control = frame.input().modifiers.control;

        const StudioViewportGesture gesture =
            studioViewportGestureFor(state.navigation, chord);
        const bool panning = state.navigation == StudioNavigationStyle::Studio
            ? (chord.middle || chord.right)
            : (gesture == StudioViewportGesture::Pan
               || gesture == StudioViewportGesture::Orbit);
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

        StudioViewportChord chord;
        chord.left = router.mouseDown(UiMouseButton::Left);
        chord.middle = router.mouseDown(UiMouseButton::Middle);
        chord.right = router.mouseDown(UiMouseButton::Right);
        chord.alt = input.modifiers.alt;
        chord.shift = input.modifiers.shift;
        chord.control = input.modifiers.control;

        const bool anyButton = chord.left || chord.middle || chord.right;

        // A press of *any* button over the panel, not only the left one. `StudioInteraction::pressed`
        // is deliberately left-only -- it is what decides focus and clicks -- so asking it alone
        // meant the middle and right gestures could never begin: Studio's own right-drag fly and
        // middle-drag pan did nothing, and the whole of the Blender scheme, which lives on the
        // middle button, was unreachable (STUDIO-07047). The 2D viewport had always read the
        // buttons directly for exactly this reason.
        const bool pressedHere =
            surface.pressed
            || (surface.hovered
                && (router.mousePressed(UiMouseButton::Middle)
                    || router.mousePressed(UiMouseButton::Right)));

        // --- Manipulate (STUDIO-07050) ----------------------------------------------------------
        //
        // Checked first and, while active, exclusively: a manipulator drag owns the pointer for
        // its whole gesture, the same rule the 2D viewport applies to its own gizmo. Left alone,
        // Studio's own navigation scheme puts an orbit on the plain left button -- see
        // `studioViewportGestureFor` below -- so a gizmo handle under the cursor has to be tried
        // *before* a press is allowed to arm one, or an object could never be dragged without
        // first switching schemes.
        const GizmoSnap snap3D = snapFor3D(context, chord.control);
        if (state.dragging3D())
        {
            if (!chord.left)
            {
                state.endDrag();
            }
            else
            {
                if (updateGizmoDrag3D(context, camera, state, pointer, snap3D))
                {
                    result.transformed = true;
                }
                return result;
            }
        }

        if (state.navigating && !anyButton)
        {
            state.navigating = false;
            state.navigationGesture = StudioViewportGesture::None;
            // A release that moved nothing is a click, and a click selects. Tracked rather than
            // read from the interaction because a drag that left the panel and came back must not
            // count as one -- the same rule the 2D viewport applies to its gizmo.
            if (!state.navigationMoved && surface.hovered) { result.clicked3D = true; }
        }
        else if (!state.navigating && anyButton && pressedHere)
        {
            // A handle under the cursor takes the press before anything else is allowed to: a
            // gizmo grab and an orbit are both, in Studio's own scheme, an unmodified left press,
            // and the grab has to win the race or an object could never be dragged without first
            // switching schemes.
            const bool grabbedGizmo =
                chord.left && state.mode != GizmoMode::None && !context.getSelection().empty()
                && beginGizmoDrag3D(context, camera, state, context.getSelection(), pointer);

            // Resolved once, at the press, and kept for the length of the drag. Asked every frame,
            // a user who released Shift halfway through a pan would find the camera orbiting from
            // wherever the pan had got to -- and the gesture a drag *started* as is the one the
            // user is still making.
            const StudioViewportGesture gesture = grabbedGizmo
                ? StudioViewportGesture::None
                : studioViewportGestureFor(state.navigation, chord);
            if (gesture != StudioViewportGesture::None)
            {
                state.navigating = true;
                state.navigationGesture = gesture;
                state.navigationMoved = false;
                state.navigationX = pointer.x;
                state.navigationY = pointer.y;
            }
            else if (chord.left && !grabbedGizmo)
            {
                // Under Maya and Blender an unmodified left drag is a selection rather than a
                // gesture, and the release below has to report it as one. Tracked through the same
                // state so a press that wanders off the panel still does not select. Excluded here
                // as everywhere else in this block: a press the gizmo already took must not also
                // arm a pending click-to-select.
                state.navigating = true;
                state.navigationGesture = StudioViewportGesture::None;
                state.navigationMoved = false;
                state.navigationX = pointer.x;
                state.navigationY = pointer.y;
            }
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

                switch (state.navigationGesture)
                {
                    case StudioViewportGesture::Pan:
                        camera.panByScreenDelta(StudioVector2{delta.x * state.cameraSpeed,
                                                             delta.y * state.cameraSpeed});
                        result.cameraChanged = true;
                        break;
                    case StudioViewportGesture::Look:
                        // Turning in place rather than about the pivot: the gesture that goes with
                        // flying, and the reason `look()` exists beside `orbit()`.
                        camera.look(-delta.x * radiansPerPixel, delta.y * radiansPerPixel);
                        result.cameraChanged = true;
                        break;
                    case StudioViewportGesture::Orbit:
                        camera.orbit(-delta.x * radiansPerPixel, delta.y * radiansPerPixel);
                        result.cameraChanged = true;
                        break;
                    case StudioViewportGesture::Dolly:
                        // Vertical drag, as Maya does it: a horizontal one would fight the pan
                        // that shares the same hand. Geometric like the wheel, so one inch of
                        // travel feels the same close up and far away.
                        camera.dolly(std::pow(1.01f, -delta.y * state.cameraSpeed));
                        result.cameraChanged = true;
                        break;
                    case StudioViewportGesture::None:
                        // A drag that is a selection rather than a gesture. It still counts as
                        // movement, so the release does not select whatever the pointer stopped
                        // over -- a drag is not a click in any scheme.
                        break;
                }
            }
        }

        // Flying, while the right button is held. The button is what keeps W, A, S and D from
        // meaning two things at once: they are the gizmo shortcuts everywhere else.
        //
        // Only under Studio's own scheme. Maya puts a dolly on Alt with the right button and
        // Blender puts nothing there, so a right-drag under either is not a fly and binding the
        // keys to it would be this editor's habit leaking into somebody else's vocabulary.
        if (chord.right && state.navigation == StudioNavigationStyle::Studio)
        {
            // The keys are this gesture's for as long as it lasts. Without this the shell's
            // shortcut dispatch also sees them, and W and E are Translate and Rotate -- so flying
            // forwards switched the manipulator on the way (STUDIO-07047). The comment above used
            // to say the right button kept them apart; the button is what *arms* the gesture, and
            // this is what tells the rest of the shell about it.
            frame.router().setWantsKeyboardGesture(true);

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

    const std::vector<StudioViewportToolbarItem>& studioViewportToolbarItems()
    {
        // Ordered as the work is: which projection, then what a drag does, then what it does it in,
        // then whether it snaps. That is the order a user answers those questions in, and a toolbar
        // grouped any other way is one whose next button is always somewhere else.
        //
        // Deliberately short. A viewport toolbar is chrome over the thing the viewport exists to
        // show, and every control on it is a control that is also on a menu -- these four are the
        // ones whose *state* a user needs to see while dragging, which is the only reason to spend
        // viewport on them.
        static const std::vector<StudioViewportToolbarItem> items = {
            // Sprite and Entity for the two views: a picture frame is a flat thing and a cube is
            // a solid one, which is the difference between the projections. Grid is *not* used
            // here, deliberately -- it is the snap toggle at the other end of the same strip, and
            // two identical pictures on one toolbar is worse than one unfamiliar picture.
            {"studio.view.2d", StudioIcon::Sprite},
            {"studio.view.3d", StudioIcon::Entity},
            {{}, StudioIcon::None},
            {"studio.view.translate", StudioIcon::Translate},
            {"studio.view.rotate", StudioIcon::Rotate},
            {"studio.view.scale", StudioIcon::Scale},
            {{}, StudioIcon::None},
            {"studio.view.toggleGizmoSpace", StudioIcon::SpaceWorld, StudioIcon::SpaceLocal},
            {"studio.view.toggleGrid", StudioIcon::Grid},
        };
        return items;
    }

    UiRect studioViewportToolbar(StudioFrame& frame, const UiRect& bounds,
                                 StudioActionRegistry& actions)
    {
        if (bounds.isEmpty()) { return UiRect{}; }

        const StudioTheme& theme = frame.theme();
        const float pad = static_cast<float>(theme.metric(StudioMetric::SpacingSmall));
        const float button = static_cast<float>(theme.metric(StudioMetric::ControlHeight));
        const float gap = static_cast<float>(theme.metric(StudioMetric::SpacingXSmall));
        const float separator = static_cast<float>(theme.metric(StudioMetric::SpacingMedium));

        // Measured before anything is drawn, so a viewport too small for the strip gets none
        // rather than a clipped one -- a half-drawn toolbar over a scene is worse than none,
        // because the buttons it did draw are still clickable.
        float width = pad;
        std::size_t drawn = 0;
        for (const StudioViewportToolbarItem& item : studioViewportToolbarItems())
        {
            if (item.actionId.empty()) { width += separator; continue; }
            if (actions.find(item.actionId) == nullptr) { continue; }
            width += button + gap;
            ++drawn;
        }
        width += pad - gap;

        if (drawn == 0) { return UiRect{}; }

        const UiRect strip{bounds.x + pad, bounds.y + pad,
                           std::min(width, bounds.width - pad * 2.0f), button + pad * 2.0f};
        if (strip.width < button + pad * 2.0f || strip.height > bounds.height * 0.5f)
        {
            return UiRect{};
        }

        if (frame.isDrawPass())
        {
            // Rounded and raised, so it reads as floating over the scene rather than as painted
            // onto it. The popup surface rather than the panel's: this is above the image, and the
            // image is whatever colour the user's level happens to be.
            frame.drawList().fillRoundedRect(strip, theme.color(StudioColorRole::PopupBackground),
                                             static_cast<float>(theme.metric(StudioMetric::CornerRadius)));
            frame.drawList().strokeRect(strip, theme.color(StudioColorRole::Border),
                                        static_cast<float>(theme.metric(StudioMetric::BorderWidth)));
        }

        UiRect cursor = strip.inset(UiEdges{pad, pad});
        frame.ids().push("viewport.toolbar");

        for (const StudioViewportToolbarItem& item : studioViewportToolbarItems())
        {
            if (cursor.width <= 0.0f) { break; }

            if (item.actionId.empty())
            {
                const UiRect space = cursor.splitLeft(std::min(separator, cursor.width));
                if (frame.isDrawPass())
                {
                    frame.drawList().fillRect(
                        UiRect{std::round(space.centerX()), space.top() + gap,
                               static_cast<float>(theme.metric(StudioMetric::SeparatorThickness)),
                               std::max(0.0f, space.height - gap * 2.0f)},
                        theme.color(StudioColorRole::Border));
                }
                continue;
            }

            const StudioAction* action = actions.find(item.actionId);
            if (action == nullptr) { continue; }

            const UiRect box = cursor.splitLeft(std::min(button, cursor.width));
            cursor.splitLeft(std::min(gap, cursor.width));

            // The registry's own text, its shortcut and its sentence of help. Written here
            // rather than taken from a field, because the toolbar at the top of the window builds
            // it the same way and neither of them should own the format -- but a tooltip composed
            // in this file from the action's own parts cannot go stale the way a second copy of
            // every label would.
            std::string tooltip{action->label};
            {
                const std::string chord = describeStudioShortcut(action->shortcut);
                if (!chord.empty()) { tooltip += "  (" + chord + ")"; }
                if (!action->description.empty()) { tooltip += "\n" + action->description; }
            }

            const bool checked = actions.isChecked(item.actionId);

            StudioButtonOptions options;
            options.icon = (checked && item.checkedIcon != StudioIcon::None) ? item.checkedIcon
                                                                            : item.icon;
            options.iconOnly = true;
            options.enabled = actions.isEnabled(item.actionId);
            options.selected = checked;
            options.tooltip = tooltip;

            if (studioButton(frame, frame.ids().make(item.actionId), box, action->label, options)
                    .activated)
            {
                actions.invoke(item.actionId);
            }
        }

        frame.ids().pop();
        return strip;
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
