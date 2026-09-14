// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/ViewportPanel.hpp
 * @brief The scene preview, its play toolbar, and every pointer interaction over it.
 */

#include "CNA/Editor/Panels/EditorPanel.hpp"
#include <cstdint>
#include <optional>

#include "CNA/Editor/Scene/SceneModels.hpp"
#include "CNA/Editor/Scene/SceneSprites3D.hpp"
#include "CNA/Editor/Scene/SceneWireframe.hpp"
#include "CNA/Editor/Scene/Tilemap.hpp"
#include "CNA/Editor/Scene/TransformGizmos.hpp"
#include "CNA/Editor/Scene/TransformGizmos3D.hpp"

namespace CNA::Editor
{
    /** @brief Draws the rendered scene, the play controls above it, and drives the gizmo. */
    class ViewportPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

    private:
        /** @brief Draws the play controls at the top of the panel. */
        void drawPlayToolbar();

        /**
         * @brief Turns one frame of pointer input into camera moves, selection, gizmos and tools.
         *
         * Split into the pieces below rather than written as one function, because the *ordering*
         * between them is load-bearing and was becoming impossible to see: a drag in progress
         * outranks everything, a tool outranks the gizmo, and the gizmo outranks the picker. Each
         * of those is a real rule with a reason, and each is now one line here.
         */
        void handleInteraction(const UiImageInteraction& interaction);

        /**
         * @brief Draws the scene through the 3D camera, as a wireframe, and returns its texture.
         *
         * A separate path rather than a branch inside the 2D one: the two share no drawing at all
         * -- one composes sprites, the other projects line segments -- and the only thing they have
         * in common is the panel they land in.
         */
        UiTextureId drawThreeDimensionalView(int width, int height);

        /**
         * @brief Turns one frame of pointer and key input into 3D camera moves and selection.
         *
         * The bindings are the ones every 3D editor has settled on, and each avoids a collision
         * rather than being chosen for its own sake: middle-drag orbits, right-drag turns the
         * camera in place, Shift with either pans, the wheel dollies, and W/A/S/D with Q and E fly
         * while the right button is held. The left button belongs to the gizmo and the picker,
         * which is why none of the navigation bindings uses it.
         */
        void handleInteraction3D(const UiImageInteraction& interaction, const EditorVector2& cursor);

        /**
         * @brief Returns the 3D manipulator's layout for whatever it is currently acting on.
         *
         * The dragged entity while a drag runs, the primary selection otherwise. Asking for the
         * selection mid-drag would make the manipulator vanish the moment a drag carried the
         * pointer over something else.
         */
        /** @brief Returns the entity the 3D manipulator acts on: the dragged one, else the primary. */
        [[nodiscard]] Uuid getGizmo3DSubject() const;

        [[nodiscard]] std::optional<TranslateGizmo3DLayout> getTranslateGizmo3DLayout() const;

        /** @brief Applies one frame of an in-progress 3D drag to a whole selection, as one command. */
        void updateMulti3DDrag(const EditorVector2& cursor, const GizmoSnap& snap);

        /**
         * @brief Returns where the 3D manipulator sits, when that is not the entity's own position.
         *
         * Nothing for a selection of one. For several, their shared pivot -- and while a drag is
         * running, the one it captured at the press rather than a fresh average, which would chase
         * the entities as they move.
         */
        [[nodiscard]] std::optional<EditorVector3> getGizmo3DPivot() const;

        /**
         * @brief Hit-tests the 3D manipulator under @p cursor and starts a drag on a press.
         *
         * Hover and grab together because they ask the same question of the same layout, and the
         * layout is not cheap: a rotate ring is a hundred and fifty projected points. Computing it
         * once per frame rather than once to highlight and again to grab also removes the chance
         * that the two disagree about what is under the cursor.
         *
         * @return True when a drag began, in which case the press must not also reach the picker.
         */
        bool beginGizmo3DDrag(const UiImageInteraction& interaction, const EditorVector2& cursor);

        /** @brief Returns true while any of the three 3D manipulators is being dragged. */
        [[nodiscard]] bool isGizmo3DDragActive() const;

        /** @brief Ends whichever 3D drag is in progress, and its selection-wide half. */
        void endGizmo3DDrag();

        /** @brief Applies one frame of whichever 3D drag is in progress. */
        void updateGizmo3DDrag(const EditorVector2& cursor, const GizmoSnap& snap);

        /**
         * @brief Continues a gizmo drag or a paint stroke already in progress.
         *
         * @return True when one was active, in which case the pointer belongs to it and nothing
         *         else this frame may look at it. Deliberately independent of hover: a drag that
         *         wandered off the panel must keep going and end only on release, or the entity is
         *         dropped wherever the cursor happened to cross the edge.
         */
        bool updateActiveDrag(const UiImageInteraction& interaction, const EditorVector2& cursor);

        /** @brief Applies this frame's input to the active tool. */
        void applyToolInput(const UiImageInteraction& interaction, const EditorVector2& cursor);

        /** @brief Applies wheel zoom and drag panning, which every tool leaves alone. */
        void updateCamera(const UiImageInteraction& interaction, const EditorVector2& cursor);

        /**
         * @brief Starts a gizmo drag if @p cursor is over a handle of the selected entity's gizmo.
         *
         * Dispatches on the active mode: exactly one manipulator is on screen, so exactly one can
         * be grabbed, and each knows only about its own handles.
         *
         * @return True when a drag began, in which case the press must not also reach the picker.
         */
        bool beginGizmoDrag(const EditorVector2& cursor);

        /** @brief Returns true while any of the three manipulators is being dragged. */
        [[nodiscard]] bool isGizmoDragActive() const;

        /** @brief Ends whichever drag is in progress. */
        void endGizmoDrag();

        /** @brief Applies the in-progress drag to the entity, as one merged command per drag. */
        void updateGizmoDrag(const EditorVector2& cursor, const GizmoSnap& snap);

        /**
         * @brief Returns what a drag should round to this frame.
         *
         * Translation snaps to the *visible* grid rather than to a constant: the lines the user can
         * see are the ones they are lining things up with, and the two are the same function
         * (`chooseGridSpacing`) so a snapped entity always lands on a line that is drawn.
         */
        [[nodiscard]] GizmoSnap getSnap(const UiImageInteraction& interaction) const;

        /**
         * @brief Pushes @p value into the selected entity's transform, merging within one drag.
         *
         * Shared by all three manipulators because the undo rule is theirs jointly: the first edit
         * of a drag opens an entry and every later one folds into it, and a drag that has not
         * changed anything must push nothing at all -- an undo entry restoring a value the entity
         * already had costs the user an undo to reach a change they can actually see.
         */
        void commitGizmoEdit(const Uuid& entityId, const std::string& property,
                             const PropertyValue& value);

        /**
         * @brief Sends the pointer and keyboard to the running player.
         *
         * Built from the same interaction the editor's own tools read, so what the game is told
         * about the pointer is what the editor did with it -- and only while the pointer is over
         * the viewport, because a click on the hierarchy is not a click in the game.
         */
        void forwardInputToPlayer(const UiImageInteraction& interaction, const UiRegion& region);

        /** @brief Draws the tool picker and the tile index the paint tool writes. */
        void drawToolbar();

        /** @brief Takes the tile under @p cursor as the brush and returns to painting. */
        void pickTileAt(const EditorVector2& cursor);

        /** @brief Fills the rectangle between the press cell and @p cursor, as one undo entry. */
        void fillTilesTo(const EditorVector2& cursor);

        /**
         * @brief Returns the tilemap cell under @p cursor, or nothing when there is none to paint.
         *
         * Shared by every tile tool, because "which cell is this, on which entity" is the same
         * question for all of them and answering it four times is four chances to differ.
         */
        [[nodiscard]] std::optional<TileCoordinate> tileUnder(const EditorVector2& cursor,
                                                              bool reportWhenMissing);

        /**
         * @brief Paints or erases the tile under @p cursor on the selected tilemap.
         *
         * @param startStroke True on the frame the button went down, which begins a new undo entry.
         *        Every frame after it merges into that entry, so one drag is one Ctrl+Z.
         */
        void paintTileAt(const EditorVector2& cursor, bool startStroke);

        /**
         * @brief Returns the world point the gizmo sits on, and whether it is a shared pivot.
         *
         * One entity selected: its own position. Several: their average, and the drag then moves
         * all of them about it.
         */
        [[nodiscard]] std::optional<EditorVector2> getGizmoPivot() const;

        /** @brief Applies an in-progress multi-selection drag as one command. */
        void updateMultiDrag(const EditorVector2& cursor, const GizmoSnap& snap);

        TranslateGizmoDrag translateDrag_;
        RotateGizmoDrag rotateDrag_;
        ScaleGizmoDrag scaleDrag_;

        /**
         * @brief The selection-wide half of a drag, active only when several entities are selected.
         *
         * Runs *beside* the single-entity drags rather than instead of them: those still compute
         * the gesture -- how far, how much, by what angle -- and this turns one gesture into the
         * edits a whole selection needs. One quantity, many entities, so they cannot drift apart.
         */
        MultiTransformDrag multiDrag_;

        /** @brief Distinguishes one multi-selection drag from the next, for merging. */
        std::uint64_t multiDragId_ = 0;

        /**
         * @brief Whether the current drag has already pushed a command.
         *
         * The first edit of a drag is a *new* undo entry and every later one merges into it.
         * Without this, a second drag of the same entity would merge into the first -- the merge
         * key is entity + component + property and has no notion of where one interaction ends --
         * and the two moves would undo together as if they had been one.
         */
        bool gizmoDragHasEdited_ = false;

        /**
         * @brief Which paint stroke is in progress, and whether it has pushed a command yet.
         *
         * The stroke id is part of the merge key, which is what keeps two separate drags from
         * collapsing into one undo entry -- the property alone cannot tell them apart.
         */
        std::uint64_t paintStroke_ = 0;
        bool paintStrokeHasEdited_ = false;

        /** @brief Where a fill drag began, while one is in progress. */
        std::optional<TileCoordinate> fillStart_;

        /** @brief What the last 3D frame drew, so the toolbar can report a truncated wireframe. */
        WireframeResult lastWireframe_;

        /** @brief What the 3D view drew solid last frame, kept for the status line and the tests. */
        SceneModelBatch lastModelBatch_;

        /** @brief The sprite quads the 3D view drew last frame, for the same reasons. */
        SceneSpriteBatch3D lastSpriteBatch_;

        /** @brief The in-progress 3D translate drag, if any. */
        TranslateGizmo3DDrag translate3DDrag_;

        /** @brief The selection-wide half of any 3D drag, active only when several are selected. */
        MultiTransform3D multi3DDrag_;

        /** @brief The in-progress 3D rotate drag, if any. */
        RotateGizmo3DDrag rotate3DDrag_;

        /** @brief The in-progress 3D scale drag, if any. */
        ScaleGizmo3DDrag scale3DDrag_;

        /** @brief Which 3D arm the cursor is over, so the drawn gizmo can say so before it is grabbed. */
        GizmoAxis3D hovered3DAxis_ = GizmoAxis3D::None;
    };
}
