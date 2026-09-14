// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/ViewportPanel.hpp"

#include "CNA/Editor/Scene/SceneModels.hpp"
#include "CNA/Editor/Scene/SceneSprites3D.hpp"

#include <algorithm>
#include <array>

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/EditorContext.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/Tilemap.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"

namespace CNA::Editor
{
    const char* toString(EditorTool tool)
    {
        switch (tool)
        {
            case EditorTool::Select: return "Select";
            case EditorTool::PaintTiles: return "Paint Tiles";
            case EditorTool::EraseTiles: return "Erase Tiles";
            case EditorTool::PickTile: return "Pick Tile";
            case EditorTool::FillTiles: return "Fill Tiles";
        }
        return "Select";
    }

    void ViewportPanel::drawPlayToolbar()
    {
        if (actions_.getPlayMode() == PlayMode::Stopped)
        {
            if (ui_.button("Play")) { actions_.startPlay(); }
        }
        else
        {
            if (ui_.button("Stop")) { actions_.stopPlay(); }
            ui_.sameLine();
            if (ui_.button(actions_.getPlayMode() == PlayMode::Paused ? "Resume" : "Pause"))
            {
                actions_.setPlayPaused(actions_.getPlayMode() != PlayMode::Paused);
            }
            ui_.sameLine();
            if (ui_.button("Step")) { actions_.stepPlayFrame(); }
        }

        ui_.sameLine();

        if (actions_.getPlayerBuilds().empty())
        {
            ui_.text("no player build found");
        }
        else if (actions_.getPlayMode() != PlayMode::Stopped)
        {
            ui_.text("running on "
                     + actions_.getPlayerBuilds()[actions_.getSelectedPlayerBuild()].backend
                     + (actions_.getPlayMode() == PlayMode::Paused ? " (paused)" : ""));
        }
        else
        {
            // The choice is an enum over what is installed, not over the fourteen backends CNA
            // knows about: offering a backend with no player binary would be offering a button
            // that cannot work.
            std::vector<std::string> names;
            names.reserve(actions_.getPlayerBuilds().size());
            for (const PlayerBuild& build : actions_.getPlayerBuilds()) { names.push_back(build.backend); }

            // Wide enough for a backend name, narrow enough to leave the toolbar a toolbar.
            ui_.setNextItemWidth(150.0f);

            PropertyValue value{PropertyValue::EnumValue{names[actions_.getSelectedPlayerBuild()]}};
            if (ui_.propertyField("Backend", value, names))
            {
                const std::string chosen = value.get<PropertyValue::EnumValue>().name;
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    if (names[index] == chosen) { actions_.selectPlayerBuild(index); break; }
                }
            }
        }

        ui_.separator();
    }
    void ViewportPanel::draw()
    {
        if (!ui_.beginPanel("Viewport", DockSide::Center)) { ui_.endPanel(); return; }

        // Above the image, so the scene is rendered into whatever is left. Putting the controls in
        // their own panel would let the user dock them away from the thing they control.
        drawPlayToolbar();
        drawToolbar();

        const UiRegion region = ui_.getContentRegion();
        if (region.isEmpty()) { ui_.endPanel(); return; }

        const int width = static_cast<int>(region.width);
        const int height = static_cast<int>(region.height);

        // The scene is rendered at exactly the panel's size. Rendering at a fixed size and
        // stretching would make the grid non-square and, worse, make picking disagree with what
        // is on screen.
        const UiTextureId texture =
            actions_.isThreeDimensionalView()
                ? drawThreeDimensionalView(width, height)
                : actions_.getViewport().render(context_.getScene(), width, height,
                                                context_.getSelection(), actions_.getGizmoMode(),
                                                actions_.getGizmoSpace(),
                                                actions_.getAnimationPreview());

        const UiImageInteraction interaction =
            ui_.image("##viewport", texture, region.width, region.height,
                       actions_.getViewport().isRenderTextureFlippedVertically());

        handleInteraction(interaction);

        // After the editor's own handling, not instead of it: play mode leaves the scene editable,
        // and a drag that moves an entity is also a drag the game may want to know about.
        forwardInputToPlayer(interaction, region);

        ui_.endPanel();
    }

    UiTextureId ViewportPanel::drawThreeDimensionalView(int width, int height)
    {
        EditorCamera3D& camera = actions_.getViewport().getCamera3D();
        camera.setViewportSize(EditorVector2{static_cast<float>(width), static_cast<float>(height)});

        // Built here and drawn there: which lines to draw is a decision, and decisions live in the
        // CNA-free scene module where they are tested (SceneWireframe.hpp). The viewport is handed
        // finished screen-space segments and does nothing but stroke them.
        WireframeOptions options;
        options.gridPlane = actions_.getGridPlane();
        // What turns a `ModelRenderer` from a badge into its own geometry (plan.md ED-405). The
        // cache is in the context rather than the viewport because a mesh needs no CNA, which is
        // what lets the standalone build draw one at all.
        options.meshProvider = context_.makeMeshProvider();

        lastWireframe_ = buildSceneWireframe(context_.getScene(), camera, context_.getSelection(),
                                             actions_.getViewport().makeSizeProvider(), options);

        // The manipulator on top of the scene, in the same currency, so the 3D viewport draws its
        // gizmo through exactly the path it draws everything else through.
        const Uuid subject = getGizmo3DSubject();
        if (subject.isValid())
        {
            const GizmoMode mode = actions_.getGizmoMode();
            std::vector<WireSegment> handles;

            if (mode == GizmoMode::Rotate)
            {
                const GizmoAxis3D active =
                    rotate3DDrag_.isActive() ? rotate3DDrag_.getAxis() : hovered3DAxis_;

                if (const auto layout =
                        computeRotateGizmo3DLayout(context_.getScene(), camera, subject,
                                                   actions_.getGizmoSpace(), getGizmo3DPivot()))
                {
                    handles = buildRotateGizmo3DSegments(*layout, active);
                }
            }
            else if (mode == GizmoMode::Scale)
            {
                const GizmoAxis3D active =
                    scale3DDrag_.isActive() ? scale3DDrag_.getAxis() : hovered3DAxis_;

                if (const auto layout =
                        computeScaleGizmo3DLayout(context_.getScene(), camera, subject, getGizmo3DPivot()))
                {
                    handles = buildScaleGizmo3DSegments(*layout, active);
                }
            }
            else if (mode == GizmoMode::Translate)
            {
                const GizmoAxis3D active =
                    translate3DDrag_.isActive() ? translate3DDrag_.getAxis() : hovered3DAxis_;

                if (const auto layout = getTranslateGizmo3DLayout())
                {
                    handles = buildTranslateGizmo3DSegments(*layout, active);
                }
            }

            lastWireframe_.segments.insert(lastWireframe_.segments.end(), handles.begin(), handles.end());
        }

        // The solid half of the 3D view (ED-402), decided the same way and in the same place as
        // the lines: `buildSceneModelBatch` is CNA-free and tested, and the viewport uploads what
        // it is handed. A build with no CNA ignores the batch and draws the wireframe alone, which
        // is what this view showed before there were models.
        lastModelBatch_ = buildSceneModelBatch(context_.getScene(), camera,
                                               context_.makeMeshProvider(), context_.getSelection(),
                                               context_.makeMaterialProvider());

        // And the sprites, as quads in the scene's own plane. The 3D view showed none until now,
        // because `SpriteBatch` cannot draw the trapezoid a sprite becomes from an angle -- what
        // ED-402 built is the path that can.
        lastSpriteBatch_ = buildSceneSpriteQuads(context_.getScene(), camera,
                                                 actions_.getViewport().makeSizeProvider(),
                                                 actions_.getAnimationPreview(),
                                                 context_.getSelection(),
                                                 &context_.getComponentRegistry());

        return actions_.getViewport().renderScene3D(lastModelBatch_, lastSpriteBatch_,
                                                    lastWireframe_.segments, width, height);
    }

    void ViewportPanel::handleInteraction(const UiImageInteraction& interaction)
    {
        const EditorVector2 cursor{interaction.localMouseX, interaction.localMouseY};

        // The 3D view shares nothing with the 2D one below: no gizmo, no tile brush, and a camera
        // with three more degrees of freedom. Branching here rather than threading a mode through
        // six functions keeps each of them about one thing.
        if (actions_.isThreeDimensionalView())
        {
            handleInteraction3D(interaction, cursor);
            return;
        }

        // Highest priority first, and each rule below is a real one with a reason.
        if (updateActiveDrag(interaction, cursor)) { return; }
        if (!interaction.hovered) { return; }

        // A tool outranks the gizmo. A tilemap's gizmo sits directly over its own first tiles, so
        // without this the opening press of every stroke would drag the map instead of painting it.
        const bool toolActive = actions_.getEditorTool() != EditorTool::Select;
        if (toolActive) { applyToolInput(interaction, cursor); }

        // The gizmo outranks the picker: a press on a handle is a manipulation, and must not also
        // count as a click that reselects whatever is underneath the gizmo.
        else if (interaction.leftPressed && beginGizmoDrag(cursor)) { return; }

        // Zoom and pan work whatever is active. A paint tool you cannot scroll is unusable.
        updateCamera(interaction, cursor);

        // Click-to-select does not, though: while a brush is active the first press would take
        // away the very tilemap being painted into.
        if (interaction.clicked && !toolActive)
        {
            const ScenePickResult pick =
                pickEntityAt(context_.getScene(), actions_.getViewport().getCamera(), cursor,
                             actions_.getViewport().makeSizeProvider());

            // Ctrl adds and removes rather than replacing, which is how a multi-selection is built
            // anywhere else. Ctrl on empty space does nothing at all: clearing a selection the user
            // is halfway through assembling is the one outcome they cannot have meant.
            if (interaction.control)
            {
                if (pick.entityId.isValid()) { context_.toggleSelection(pick.entityId); }
                return;
            }

            // Clicking empty space clears the selection, which is what every editor does and what
            // makes "deselect" reachable without a keyboard.
            context_.select(pick.entityId);
        }
    }

    bool ViewportPanel::updateActiveDrag(const UiImageInteraction& interaction, const EditorVector2& cursor)
    {
        if (isGizmoDragActive())
        {
            if (interaction.leftDown) { updateGizmoDrag(cursor, getSnap(interaction)); }
            else { endGizmoDrag(); }
            return true;
        }

        if (paintStrokeHasEdited_)
        {
            if (interaction.leftDown)
            {
                paintTileAt(cursor, false);
                return true;
            }
            paintStrokeHasEdited_ = false;
        }

        // A fill is drawn by dragging and applied on release, so the drag belongs to it until then
        // -- including the frames where the cursor has left the panel, since the release still
        // decides the rectangle.
        if (fillStart_)
        {
            if (interaction.leftDown) { return true; }

            fillTilesTo(cursor);
            fillStart_.reset();
            return true;
        }

        return false;
    }

    void ViewportPanel::applyToolInput(const UiImageInteraction& interaction, const EditorVector2& cursor)
    {
        switch (actions_.getEditorTool())
        {
            case EditorTool::PaintTiles:
            case EditorTool::EraseTiles:
                if (interaction.leftPressed) { paintTileAt(cursor, true); }
                else if (interaction.leftDown) { paintTileAt(cursor, false); }
                return;

            case EditorTool::PickTile:
                if (interaction.leftPressed) { pickTileAt(cursor); }
                return;

            case EditorTool::FillTiles:
                if (interaction.leftPressed) { fillStart_ = tileUnder(cursor, true); }
                return;

            case EditorTool::Select:
                return;
        }
    }

    void ViewportPanel::updateMulti3DDrag(const EditorVector2& cursor, const GizmoSnap& snap)
    {
        const SceneDocument& scene = context_.getScene();
        const EditorCamera3D& camera = actions_.getViewport().getCamera3D();

        std::vector<EntityTransformEdit> edits;

        if (translate3DDrag_.isActive())
        {
            const std::optional<EditorVector3> delta = translate3DDrag_.getWorldDelta(camera, cursor, snap);
            if (!delta) { return; }
            edits = multi3DDrag_.translate(scene, *delta);
        }
        else if (rotate3DDrag_.isActive())
        {
            const std::optional<float> radians = rotate3DDrag_.getDeltaAngle(camera, cursor, snap);
            if (!radians) { return; }

            // About the ring's own normal, which is the axis the user grabbed, and about the pivot
            // the drag captured at the press -- not a fresh centroid, which would chase the
            // entities as they move and turn a steady drag into a spiral.
            edits = multi3DDrag_.rotate(scene, rotate3DDrag_.getNormal(), *radians);
        }
        else if (scale3DDrag_.isActive())
        {
            const std::optional<ScaleGizmo3DLayout> layout =
                computeScaleGizmo3DLayout(scene, camera, scale3DDrag_.getEntityId(), getGizmo3DPivot());
            if (!layout) { return; }

            const float factor = scale3DDrag_.getFactor(*layout, cursor, snap);
            const GizmoAxis3D axis = scale3DDrag_.getAxis();

            // Only the grabbed axis, or all three for the centre handle: the same rule the single
            // entity follows, applied to a set.
            EditorVector3 factors{1.0f, 1.0f, 1.0f};
            if (axis == GizmoAxis3D::All) { factors = EditorVector3{factor, factor, factor}; }
            else if (axis == GizmoAxis3D::X) { factors.x = factor; }
            else if (axis == GizmoAxis3D::Y) { factors.y = factor; }
            else { factors.z = factor; }

            edits = multi3DDrag_.scale(scene, layout->axes, factors);
        }

        if (edits.empty()) { return; }

        // One command for the whole selection, and one undo entry for the whole drag -- the same
        // rule the 2D multi-drag follows, and for the same reason: undoing one drag one entity at
        // a time walks the scene through arrangements it was never in.
        auto command = std::make_unique<TransformEntitiesCommand>(
            context_.getScene(), std::move(edits), "transform-many:" + std::to_string(multiDragId_));

        context_.execute(std::move(command),
                         gizmoDragHasEdited_ ? MergePolicy::MergeWithPrevious : MergePolicy::NewEntry);
        gizmoDragHasEdited_ = true;
    }

    std::optional<EditorVector3> ViewportPanel::getGizmo3DPivot() const
    {
        // The pivot captured when the drag began, not a fresh centroid: the entities are moving as
        // the drag proceeds, and a centre recomputed from them would chase itself.
        if (multi3DDrag_.isActive()) { return multi3DDrag_.getPivot(); }

        // Nothing for a selection of one, where the gizmo's own origin already is the entity's
        // position and an average of one is a longer way to say the same thing.
        if (context_.getSelection().size() < 2) { return std::nullopt; }
        return computeSelectionPivot3D(context_.getScene(), context_.getSelection());
    }

    bool ViewportPanel::isGizmo3DDragActive() const
    {
        return translate3DDrag_.isActive() || rotate3DDrag_.isActive() || scale3DDrag_.isActive();
    }

    void ViewportPanel::endGizmo3DDrag()
    {
        translate3DDrag_.end();
        rotate3DDrag_.end();
        scale3DDrag_.end();
        multi3DDrag_.end();
        gizmoDragHasEdited_ = false;
    }

    bool ViewportPanel::beginGizmo3DDrag(const UiImageInteraction& interaction,
                                         const EditorVector2& cursor)
    {
        // Cleared first and every frame, including the frames the pointer is elsewhere: a
        // highlight left behind says the cursor is on a handle it left minutes ago.
        hovered3DAxis_ = GizmoAxis3D::None;
        if (!interaction.hovered) { return false; }

        const Uuid subject = getGizmo3DSubject();
        if (!subject.isValid()) { return false; }

        const SceneDocument& scene = context_.getScene();
        const EditorCamera3D& camera = actions_.getViewport().getCamera3D();

        bool began = false;

        // Only the manipulator that is actually drawn is hit-tested, exactly as in 2D: testing the
        // others would let a press land on a handle nobody can see, which from the outside is
        // indistinguishable from a bug.
        switch (actions_.getGizmoMode())
        {
            case GizmoMode::Translate:
            {
                const std::optional<TranslateGizmo3DLayout> layout = getTranslateGizmo3DLayout();
                if (!layout) { break; }

                hovered3DAxis_ = hitTestTranslateGizmo3D(*layout, cursor);
                began = interaction.leftPressed
                        && translate3DDrag_.begin(scene, camera, *layout, subject, cursor);
                break;
            }

            case GizmoMode::Rotate:
            {
                const std::optional<RotateGizmo3DLayout> layout = computeRotateGizmo3DLayout(
                    scene, camera, subject, actions_.getGizmoSpace(), getGizmo3DPivot());
                if (!layout) { break; }

                hovered3DAxis_ = hitTestRotateGizmo3D(*layout, cursor);
                began = interaction.leftPressed
                        && rotate3DDrag_.begin(scene, camera, *layout, subject, cursor);
                break;
            }

            case GizmoMode::Scale:
            {
                const std::optional<ScaleGizmo3DLayout> layout =
                    computeScaleGizmo3DLayout(scene, camera, subject, getGizmo3DPivot());
                if (!layout) { break; }

                hovered3DAxis_ = hitTestScaleGizmo3D(*layout, cursor);
                began = interaction.leftPressed && scale3DDrag_.begin(scene, *layout, subject, cursor);
                break;
            }

            case GizmoMode::None:
                break;
        }

        if (!began) { return false; }

        // The selection-wide half runs beside the single-entity one, and only when there is more
        // than one thing to move: for a selection of one they would compute the same edit twice,
        // and the multi path's command carries a heavier merge key.
        if (const std::optional<EditorVector3> pivot = getGizmo3DPivot())
        {
            if (multi3DDrag_.begin(scene, context_.getSelection(), *pivot)) { ++multiDragId_; }
        }

        gizmoDragHasEdited_ = false;
        return true;
    }

    void ViewportPanel::updateGizmo3DDrag(const EditorVector2& cursor, const GizmoSnap& snap)
    {
        if (multi3DDrag_.isActive())
        {
            updateMulti3DDrag(cursor, snap);
            return;
        }

        const SceneDocument& scene = context_.getScene();
        const EditorCamera3D& camera = actions_.getViewport().getCamera3D();

        if (rotate3DDrag_.isActive())
        {
            if (const auto rotation = rotate3DDrag_.update(scene, camera, cursor, snap))
            {
                commitGizmoEdit(rotate3DDrag_.getEntityId(), "rotation", PropertyValue{*rotation});
            }
            return;
        }

        if (scale3DDrag_.isActive())
        {
            // Recomputed each frame rather than kept from the press, like every other layout here:
            // the entity may be moving for reasons of its own -- a parent animated by the running
            // player -- and the gizmo has to stay on it. What must not be recomputed is the grab,
            // and that lives inside the drag.
            const std::optional<ScaleGizmo3DLayout> layout =
                computeScaleGizmo3DLayout(scene, camera, scale3DDrag_.getEntityId());

            if (!layout) { return; }

            if (const auto scaled = scale3DDrag_.update(*layout, cursor, snap))
            {
                commitGizmoEdit(scale3DDrag_.getEntityId(), "scale", PropertyValue{*scaled});
            }
            return;
        }

        if (const auto position = translate3DDrag_.update(scene, camera, cursor, snap))
        {
            commitGizmoEdit(translate3DDrag_.getEntityId(), "position", PropertyValue{*position});
        }
    }

    Uuid ViewportPanel::getGizmo3DSubject() const
    {
        // The dragged entity while a drag is running, so releasing the pointer over empty space
        // does not make the manipulator vanish mid-gesture; otherwise the primary selection.
        if (translate3DDrag_.isActive()) { return translate3DDrag_.getEntityId(); }
        if (rotate3DDrag_.isActive()) { return rotate3DDrag_.getEntityId(); }
        if (scale3DDrag_.isActive()) { return scale3DDrag_.getEntityId(); }
        return context_.getSelection().empty() ? Uuid{} : context_.getSelection().front();
    }

    std::optional<TranslateGizmo3DLayout> ViewportPanel::getTranslateGizmo3DLayout() const
    {
        const Uuid subject = getGizmo3DSubject();
        if (!subject.isValid()) { return std::nullopt; }

        return computeTranslateGizmo3DLayout(context_.getScene(), actions_.getViewport().getCamera3D(),
                                             subject, actions_.getGizmoSpace(), getGizmo3DPivot());
    }

    void ViewportPanel::handleInteraction3D(const UiImageInteraction& interaction,
                                            const EditorVector2& cursor)
    {
        EditorCamera3D& camera = actions_.getViewport().getCamera3D();

        // A drag in progress outranks everything, and deliberately ignores hover: a drag that
        // wandered off the panel must keep going and end on release, or the entity is dropped
        // wherever the cursor happened to cross the edge. The same rule the 2D viewport has.
        if (isGizmo3DDragActive())
        {
            if (interaction.leftDown)
            {
                updateGizmo3DDrag(cursor, getSnap(interaction));
                return;
            }

            endGizmo3DDrag();
            return;
        }

        // A press on a handle is a manipulation, and must not also count as a click that reselects
        // whatever the handle happens to be drawn over -- which, for a gizmo sitting on its own
        // entity, is that entity's own box.
        if (beginGizmo3DDrag(interaction, cursor)) { return; }

        if (interaction.wheel != 0.0f)
        {
            // Geometric, like the 2D zoom and for the same reason: one notch has to feel the same
            // close up and far away. Scrolling up moves the eye towards the pivot, so the factor
            // is below one.
            constexpr float kDollyPerNotch = 1.15f;
            camera.dolly(std::pow(kDollyPerNotch, -interaction.wheel));
        }

        if (interaction.dragging)
        {
            const EditorVector2 delta{interaction.dragDeltaX, interaction.dragDeltaY};

            // Radians per pixel. A full turn across a 900-pixel panel is the rate every 3D editor
            // has converged on, and it is deliberately independent of the panel's size: a rate
            // derived from the width would turn faster in a narrow panel than a wide one.
            constexpr float kRadiansPerPixel = 0.007f;

            if (interaction.shift) { camera.panByScreenDelta(delta); }
            else if (interaction.rightDown)
            {
                // Right-drag turns the camera in place -- the gesture that goes with flying, and
                // the reason look() exists beside orbit().
                camera.look(-delta.x * kRadiansPerPixel, delta.y * kRadiansPerPixel);
            }
            else { camera.orbit(-delta.x * kRadiansPerPixel, delta.y * kRadiansPerPixel); }
        }

        // Flying, while the right button is held: the modifier is what keeps W, A, S and D from
        // meaning two things at once, since they are the gizmo shortcuts everywhere else.
        if (interaction.rightDown)
        {
            // Proportional to the orbit distance, so one press crosses the same fraction of what
            // is on screen whether the camera is inside a room or above a level.
            const float step = std::max(0.05f, camera.getDistance() * 0.04f);

            EditorVector3 move;
            if (ui_.isKeyDown(UiKey::W)) { move.z += step; }
            if (ui_.isKeyDown(UiKey::S)) { move.z -= step; }
            if (ui_.isKeyDown(UiKey::D)) { move.x += step; }
            if (ui_.isKeyDown(UiKey::A)) { move.x -= step; }
            if (ui_.isKeyDown(UiKey::E)) { move.y += step; }
            if (ui_.isKeyDown(UiKey::Q)) { move.y -= step; }

            if (move != EditorVector3{}) { camera.moveLocal(move); }
        }

        if (!interaction.clicked) { return; }

        const Uuid picked = pickEntityAt3D(context_.getScene(), camera, cursor,
                                           actions_.getViewport().makeSizeProvider());

        // The same two selection rules the 2D viewport has, because they are rules about
        // selecting rather than about a projection: Ctrl adds and removes, and Ctrl on empty space
        // leaves a half-assembled selection alone.
        if (interaction.control)
        {
            if (picked.isValid()) { context_.toggleSelection(picked); }
            return;
        }

        context_.select(picked);
    }

    void ViewportPanel::updateCamera(const UiImageInteraction& interaction, const EditorVector2& cursor)
    {
        EditorCamera2D& camera = actions_.getViewport().getCamera();

        if (interaction.wheel != 0.0f)
        {
            // A constant factor per notch gives geometric zoom, so each notch feels the same
            // whatever the current scale -- a linear step is unusable at both ends of the range.
            constexpr float kZoomPerNotch = 1.15f;
            camera.zoomAt(cursor, std::pow(kZoomPerNotch, interaction.wheel));
        }

        if (interaction.dragging)
        {
            camera.panByScreenDelta(EditorVector2{interaction.dragDeltaX, interaction.dragDeltaY});
        }
    }

    void ViewportPanel::forwardInputToPlayer(const UiImageInteraction& interaction,
                                             const UiRegion& region)
    {
        if (actions_.getPlayMode() == PlayMode::Stopped) { return; }

        // The keys a game plays with, and nothing else. Forwarding every key the editor can name
        // would send Ctrl+S to the game as an S -- which is exactly the sort of thing that gets
        // blamed on the game rather than on the editor that invented the keystroke.
        static constexpr std::array<std::pair<UiKey, const char*>, 12> kForwarded{{
            {UiKey::W, "W"}, {UiKey::A, "A"}, {UiKey::S, "S"}, {UiKey::D, "D"},
            {UiKey::Q, "Q"}, {UiKey::E, "E"}, {UiKey::R, "R"}, {UiKey::F, "F"},
            {UiKey::Space, "Space"}, {UiKey::Enter, "Enter"}, {UiKey::Escape, "Escape"},
            {UiKey::Tab, "Tab"}}};

        PlayerInputSnapshot snapshot;
        for (const auto& [key, name] : kForwarded)
        {
            if (ui_.isKeyDown(key)) { snapshot.keys.emplace_back(name); }
        }

        // The pointer only while it is over the viewport. A cursor resting on the inspector is not
        // hovering the game, and reporting its last position there would leave the game acting on
        // a pointer that has not been near it for minutes.
        if (interaction.hovered)
        {
            // The region the image was drawn into, passed in rather than asked for again: by this
            // point in the frame the panel's remaining content region is what is left *below* the
            // image, which is not the surface the pointer was measured against.
            snapshot.mouseX = interaction.localMouseX;
            snapshot.mouseY = interaction.localMouseY;
            snapshot.surfaceWidth = region.width;
            snapshot.surfaceHeight = region.height;
            snapshot.leftButton = interaction.leftDown;
            snapshot.rightButton = interaction.rightDown;
            snapshot.middleButton = interaction.middleDown;
            snapshot.wheel = interaction.wheel;
        }

        actions_.forwardInputToPlayer(snapshot);
    }

    void ViewportPanel::drawToolbar()
    {
        static const std::array<EditorTool, 5> kOrder{EditorTool::Select, EditorTool::PaintTiles,
                                                      EditorTool::EraseTiles, EditorTool::PickTile,
                                                      EditorTool::FillTiles};

        std::vector<std::string> names;
        names.reserve(kOrder.size());
        for (const EditorTool tool : kOrder) { names.emplace_back(toString(tool)); }

        ui_.setNextItemWidth(130.0f);
        PropertyValue chosen{PropertyValue::EnumValue{toString(actions_.getEditorTool())}};
        if (ui_.propertyField("##tool", chosen, names))
        {
            const std::string name = chosen.get<PropertyValue::EnumValue>().name;
            for (std::size_t index = 0; index < kOrder.size(); ++index)
            {
                if (names[index] == name) { actions_.setEditorTool(kOrder[index]); }
            }
        }

        // The manipulator and the space it works in, beside the tool. They have keys (W/E/R and X)
        // and menu items already; what neither of those does is *show* which one is active, and a
        // user who cannot see the state has to press a key to find out what it was.
        static const std::array<GizmoMode, 3> kModes{GizmoMode::Translate, GizmoMode::Rotate,
                                                     GizmoMode::Scale};

        std::vector<std::string> modeNames;
        modeNames.reserve(kModes.size());
        for (const GizmoMode mode : kModes) { modeNames.emplace_back(toString(mode)); }

        ui_.sameLine();
        ui_.setNextItemWidth(110.0f);
        PropertyValue mode{PropertyValue::EnumValue{toString(actions_.getGizmoMode())}};
        if (ui_.propertyField("##gizmo", mode, modeNames))
        {
            const std::string name = mode.get<PropertyValue::EnumValue>().name;
            for (std::size_t index = 0; index < kModes.size(); ++index)
            {
                if (modeNames[index] == name) { actions_.setGizmoMode(kModes[index]); }
            }
        }

        // Labelled with the space it is *in*, not the one pressing it selects: a toolbar reports
        // state, and a button that named the other space would read as a claim about the current
        // one. The menu says "Use Local Space" instead, because a menu item is an instruction.
        ui_.sameLine();
        const GizmoSpace space = actions_.getGizmoSpace();
        if (ui_.button(std::string{toString(space)} + "##space"))
        {
            actions_.setGizmoSpace(space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World);
        }

        // The view the user is in, on the same "a toolbar reports state" rule as the space button
        // beside it. It comes last of the three because it is the one changed least often.
        ui_.sameLine();
        const bool threeDimensional = actions_.isThreeDimensionalView();
        if (ui_.button(threeDimensional ? "3D##view" : "2D##view"))
        {
            actions_.setThreeDimensionalView(!threeDimensional);
        }

        if (threeDimensional)
        {
            // A wireframe that ran out of room looks exactly like a scene missing half its
            // entities, so the one place it can be seen says so.
            if (lastWireframe_.truncated)
            {
                ui_.sameLine();
                ui_.text("(too much to draw; showing part of the scene)");
            }

            // What ED-402 draws, and -- deliberately -- what it does not. The owner's decision was
            // that models render in the editor and not yet in the running game, and a
            // ModelRenderer that is there while authoring and gone when the game runs is exactly
            // the silent difference this editor exists to prevent. So it is said here, beside the
            // models, rather than left to be discovered by pressing Play.
            if (!lastModelBatch_.draws.empty())
            {
                ui_.sameLine();
                ui_.text("| " + std::to_string(lastModelBatch_.draws.size())
                         + " model(s), editor view only -- the player does not draw them yet");
            }

            // A model still importing looks identical to an entity that never had one, and only
            // one of the two is worth waiting for.
            if (lastModelBatch_.pendingMeshes > 0)
            {
                ui_.sameLine();
                ui_.text("| " + std::to_string(lastModelBatch_.pendingMeshes) + " still loading");
            }
            return;
        }

        // Only where it means something. A tile index beside the Select tool is a control that
        // does nothing, which is worse than one that is not there. The eraser has no index either;
        // the eyedropper sets one rather than reading it.
        const EditorTool active = actions_.getEditorTool();
        if (active != EditorTool::PaintTiles && active != EditorTool::FillTiles) { return; }

        ui_.sameLine();
        ui_.setNextItemWidth(90.0f);
        PropertyValue tile{actions_.getPaintTile()};
        if (ui_.propertyField("Tile", tile))
        {
            actions_.setPaintTile(std::max<std::int64_t>(0, tile.get<std::int64_t>(0)));
        }
    }

    std::optional<TileCoordinate> ViewportPanel::tileUnder(const EditorVector2& cursor,
                                                           bool reportWhenMissing)
    {
        const Uuid selectedId = context_.getPrimarySelection();
        const EditorEntity* entity = context_.getScene().findEntity(selectedId);
        const EditorComponent* tilemap =
            entity != nullptr ? entity->findComponent(BuiltinComponentIds::kTilemap) : nullptr;

        if (tilemap == nullptr)
        {
            // Said once per press rather than per frame: a brush over a sprite is a near miss, and
            // sixty lines a second about it is how a console stops being read.
            if (reportWhenMissing)
            {
                context_.log(LogSeverity::Warning,
                             "Select an entity with a Tilemap component to paint into.");
            }
            return std::nullopt;
        }

        const std::optional<WorldTransform> transform =
            computeWorldTransform(context_.getScene(), selectedId);
        if (!transform) { return std::nullopt; }

        const ComponentDescriptor* descriptor =
            context_.getComponentRegistry().find(BuiltinComponentIds::kTilemap);

        const EditorVector2 world = actions_.getViewport().getCamera().screenToWorld(cursor);
        return worldToTile(
            *transform,
            static_cast<int>(tilemap->getPropertyOrDefault(TilemapKeys::kTileWidth, descriptor)
                                 .get<std::int64_t>(0)),
            static_cast<int>(tilemap->getPropertyOrDefault(TilemapKeys::kTileHeight, descriptor)
                                 .get<std::int64_t>(0)),
            world);
    }

    void ViewportPanel::paintTileAt(const EditorVector2& cursor, bool startStroke)
    {
        const std::optional<TileCoordinate> cell = tileUnder(cursor, startStroke);
        if (!cell) { return; }

        if (startStroke)
        {
            ++paintStroke_;
            paintStrokeHasEdited_ = false;
        }

        const std::int64_t value =
            actions_.getEditorTool() == EditorTool::EraseTiles ? kEmptyTile : actions_.getPaintTile();

        auto command = std::make_unique<PaintTilesCommand>(context_.getScene(),
                                                           context_.getComponentRegistry(),
                                                           context_.getPrimarySelection(), paintStroke_);
        if (!command->paint(cell->x, cell->y, value)) { return; }

        // The first cell of a stroke opens a new entry and every later one merges into it, which is
        // what makes a drag across forty tiles one Ctrl+Z.
        const MergePolicy policy =
            paintStrokeHasEdited_ ? MergePolicy::MergeWithPrevious : MergePolicy::NewEntry;
        paintStrokeHasEdited_ = true;
        context_.execute(std::move(command), policy);
    }

    void ViewportPanel::pickTileAt(const EditorVector2& cursor)
    {
        const std::optional<TileCoordinate> cell = tileUnder(cursor, true);
        if (!cell) { return; }

        const Uuid selectedId = context_.getPrimarySelection();
        const EditorComponent* tilemap =
            context_.getScene().findEntity(selectedId)->findComponent(BuiltinComponentIds::kTilemap);

        const TilemapGrid grid = readTilemapGrid(
            *tilemap, context_.getComponentRegistry().find(BuiltinComponentIds::kTilemap));

        const std::int64_t picked = grid.at(cell->x, cell->y);
        if (picked < 0)
        {
            // An empty cell is not a tile. Taking -1 as the brush would silently turn the
            // eyedropper into an eraser, which is a different tool the user did not choose.
            context_.log(LogSeverity::Info, "That cell is empty; the brush was left alone.");
            return;
        }

        actions_.setPaintTile(picked);

        // Straight back to painting, which is what every editor does and what makes the eyedropper
        // worth reaching for: picking a tile is never the goal, painting with it is.
        actions_.setEditorTool(EditorTool::PaintTiles);
        context_.log(LogSeverity::Info, "Brush set to tile " + std::to_string(picked) + ".");
    }

    void ViewportPanel::fillTilesTo(const EditorVector2& cursor)
    {
        if (!fillStart_) { return; }

        const std::optional<TileCoordinate> end = tileUnder(cursor, false);
        if (!end) { return; }

        const int minX = std::min(fillStart_->x, end->x);
        const int maxX = std::max(fillStart_->x, end->x);
        const int minY = std::min(fillStart_->y, end->y);
        const int maxY = std::max(fillStart_->y, end->y);

        ++paintStroke_;

        auto command = std::make_unique<PaintTilesCommand>(context_.getScene(),
                                                           context_.getComponentRegistry(),
                                                           context_.getPrimarySelection(), paintStroke_);

        // Every cell into one command, so a fill is one undo entry however large the rectangle.
        // Cells outside the map are refused by the command itself, so dragging past the edge fills
        // what exists rather than nothing.
        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x) { command->paint(x, y, actions_.getPaintTile()); }
        }

        if (!command->isValid()) { return; }

        const std::size_t cells = command->getCells().size();
        context_.execute(std::move(command));
        context_.log(LogSeverity::Info, "Filled " + std::to_string(cells) + " tile(s).");
    }

    bool ViewportPanel::isGizmoDragActive() const
    {
        return translateDrag_.isActive() || rotateDrag_.isActive() || scaleDrag_.isActive();
    }

    void ViewportPanel::endGizmoDrag()
    {
        translateDrag_.end();
        rotateDrag_.end();
        scaleDrag_.end();
        multiDrag_.end();
    }

    std::optional<EditorVector2> ViewportPanel::getGizmoPivot() const
    {
        const std::vector<Uuid>& selection = context_.getSelection();
        if (selection.empty()) { return std::nullopt; }
        if (selection.size() == 1)
        {
            const std::optional<WorldTransform> world = computeWorldTransform(context_.getScene(), selection.front());
            if (!world) { return std::nullopt; }
            return EditorVector2{world->position.x, world->position.y};
        }
        return computeSelectionPivot(context_.getScene(), selection);
    }

    bool ViewportPanel::beginGizmoDrag(const EditorVector2& cursor)
    {
        const Uuid selectedId = context_.getPrimarySelection();
        const SceneDocument& scene = context_.getScene();
        const EditorCamera2D& camera = actions_.getViewport().getCamera();

        // The shared pivot, when several entities are selected. Where the gizmo is *drawn* is where
        // it must be grabbed, and the renderer places it the same way.
        const std::optional<EditorVector2> pivot =
            context_.getSelection().size() > 1 ? getGizmoPivot() : std::nullopt;

        // Only the manipulator that is actually drawn can be grabbed. Hit-testing the others would
        // let a press land on a handle nobody can see, which is indistinguishable from a bug.
        switch (actions_.getGizmoMode())
        {
            case GizmoMode::Translate:
            {
                auto layout =
                    computeTranslateGizmoLayout(scene, camera, selectedId, actions_.getGizmoSpace());
                if (!layout) { return false; }
                if (pivot) { placeGizmoAt(*layout, camera, *pivot); }

                const GizmoHandle handle = hitTestTranslateGizmo(*layout, cursor);
                if (!translateDrag_.begin(scene, camera, selectedId, handle, cursor,
                                          actions_.getGizmoSpace()))
                {
                    return false;
                }
                break;
            }

            case GizmoMode::Rotate:
            {
                auto layout = computeRotateGizmoLayout(scene, camera, selectedId);
                if (!layout) { return false; }
                if (pivot) { placeGizmoAt(*layout, camera, *pivot); }

                if (hitTestRotateGizmo(*layout, cursor) == GizmoHandle::None) { return false; }
                if (!rotateDrag_.begin(scene, *layout, selectedId, cursor)) { return false; }
                break;
            }

            case GizmoMode::Scale:
            {
                auto layout = computeScaleGizmoLayout(scene, camera, selectedId);
                if (!layout) { return false; }
                if (pivot) { placeGizmoAt(*layout, camera, *pivot); }

                const GizmoHandle handle = hitTestScaleGizmo(*layout, cursor);
                if (!scaleDrag_.begin(scene, *layout, selectedId, handle, cursor)) { return false; }
                break;
            }

            case GizmoMode::None:
                return false;
        }

        gizmoDragHasEdited_ = false;

        // A multi-selection drag runs beside the single-entity one: that computes the gesture, this
        // turns it into the edits a whole selection needs. Started only when there is more than one
        // entity, so a single selection takes exactly the path it always did.
        multiDrag_.end();
        if (context_.getSelection().size() > 1)
        {
            if (const std::optional<EditorVector2> pivot = getGizmoPivot())
            {
                multiDrag_.begin(scene, context_.getSelection(), *pivot);
                ++multiDragId_;
            }
        }
        return true;
    }

    void ViewportPanel::commitGizmoEdit(const Uuid& entityId, const std::string& property,
                                        const PropertyValue& value)
    {
        const EditorEntity* entity = context_.getScene().findEntity(entityId);
        if (entity == nullptr) { return; }

        const EditorComponent* transform = entity->findComponent(BuiltinComponentIds::kTransform);
        if (transform == nullptr) { return; }

        // A drag that has not moved yet must not push anything: a press and release on a handle is
        // not an edit, and an undo entry that restores the value it already had is worse than no
        // entry -- it costs the user an undo to get back to a change they can actually see.
        if (transform->getProperty(property) == value) { return; }

        // The first edit opens a new undo entry; every later one folds into it. The result is one
        // entry per drag that undoes to where the drag started -- not one per mouse-move event, and
        // not one shared with the previous drag of the same entity.
        context_.execute(std::make_unique<SetPropertyCommand>(context_.getScene(), entityId,
                                                              BuiltinComponentIds::kTransform,
                                                              property, value),
                         gizmoDragHasEdited_ ? MergePolicy::MergeWithPrevious : MergePolicy::NewEntry);
        gizmoDragHasEdited_ = true;
    }

    GizmoSnap ViewportPanel::getSnap(const UiImageInteraction& interaction) const
    {
        if (!interaction.control) { return {}; }

        GizmoSnap snap;

        // The project's step when it has one, and the visible grid when it does not. A project laid
        // out on a 16-pixel tile grid says so once; everything else keeps the old behaviour, which
        // is to snap to the lines the user can actually see.
        const float projectStep =
            context_.hasProject() ? context_.getProject().getGridSnap() : 0.0f;

        snap.translate = projectStep > 0.0f
                             ? projectStep
                             : chooseGridSpacing(actions_.getViewport().getCamera().getZoom(),
                                                 kGridTargetPixels);
        snap.rotate = kDefaultRotationSnap;
        snap.scale = kDefaultScaleSnap;
        return snap;
    }

    void ViewportPanel::updateMultiDrag(const EditorVector2& cursor, const GizmoSnap& snap)
    {
        const SceneDocument& scene = context_.getScene();
        const EditorCamera2D& camera = actions_.getViewport().getCamera();

        std::vector<EntityTransformEdit> edits;

        if (translateDrag_.isActive())
        {
            edits = multiDrag_.translate(scene, translateDrag_.getWorldDelta(camera, cursor, snap));
        }
        else if (rotateDrag_.isActive())
        {
            auto layout = computeRotateGizmoLayout(scene, camera, rotateDrag_.getEntityId());
            if (!layout) { return; }

            // The pivot captured when the drag began, not a fresh centroid: the entities are moving
            // as the drag proceeds, and a centre recomputed from them would chase itself.
            placeGizmoAt(*layout, camera, multiDrag_.getPivot());
            edits = multiDrag_.rotate(scene, rotateDrag_.getDeltaAngle(*layout, cursor, snap));
        }
        else if (scaleDrag_.isActive())
        {
            auto layout = computeScaleGizmoLayout(scene, camera, scaleDrag_.getEntityId());
            if (!layout) { return; }
            placeGizmoAt(*layout, camera, multiDrag_.getPivot());

            const float factor = scaleDrag_.getFactor(*layout, cursor, snap);
            const GizmoHandle handle = scaleDrag_.getHandle();
            edits = multiDrag_.scale(scene,
                                     EditorVector2{handle == GizmoHandle::YAxis ? 1.0f : factor,
                                                   handle == GizmoHandle::XAxis ? 1.0f : factor});
        }

        if (edits.empty()) { return; }

        // One command for the whole selection, and one undo entry for the whole drag. A command per
        // entity would make undoing one drag several presses of Ctrl+Z -- and would undo them one
        // at a time, through arrangements the scene was never in.
        auto command = std::make_unique<TransformEntitiesCommand>(
            context_.getScene(), std::move(edits),
            "transform-many:" + std::to_string(multiDragId_));

        context_.execute(std::move(command),
                         gizmoDragHasEdited_ ? MergePolicy::MergeWithPrevious : MergePolicy::NewEntry);
        gizmoDragHasEdited_ = true;
    }

    void ViewportPanel::updateGizmoDrag(const EditorVector2& cursor, const GizmoSnap& snap)
    {
        if (multiDrag_.isActive())
        {
            updateMultiDrag(cursor, snap);
            return;
        }

        const SceneDocument& scene = context_.getScene();
        const EditorCamera2D& camera = actions_.getViewport().getCamera();

        if (translateDrag_.isActive())
        {
            if (const auto position = translateDrag_.update(scene, camera, cursor, snap))
            {
                commitGizmoEdit(translateDrag_.getEntityId(), "position", PropertyValue{*position});
            }
            return;
        }

        if (rotateDrag_.isActive())
        {
            // The layout is recomputed each frame rather than kept from the press, so the ring
            // stays under the entity if something else moves it -- a parent being animated by the
            // running player, say. The drag's own start angle is what must not be recomputed, and
            // that lives in the drag.
            const auto layout = computeRotateGizmoLayout(scene, camera, rotateDrag_.getEntityId());
            if (!layout) { return; }

            if (const auto rotation = rotateDrag_.update(*layout, cursor, snap))
            {
                commitGizmoEdit(rotateDrag_.getEntityId(), "rotation", PropertyValue{*rotation});
            }
            return;
        }

        if (scaleDrag_.isActive())
        {
            const auto layout = computeScaleGizmoLayout(scene, camera, scaleDrag_.getEntityId());
            if (!layout) { return; }

            if (const auto scale = scaleDrag_.update(*layout, cursor, snap))
            {
                commitGizmoEdit(scaleDrag_.getEntityId(), "scale", PropertyValue{*scale});
            }
        }
    }
}
