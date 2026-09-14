// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/EditorPanel.hpp
 * @brief The base a panel derives from, and the editor operations it may invoke.
 *
 * Panels were methods on EditorApplication through Phase 0, which was right while there was barely
 * a panel to speak of. They are classes now (plan.md ED-210): each owns its own view state --
 * which component the inspector's Add picker is showing, which row the hierarchy is renaming,
 * how the console is filtered -- and none of it is visible to the others.
 *
 * What a panel may *do* to the editor goes through EditorActions rather than through a back
 * reference to the application. That keeps the surface deliberately narrow, and it keeps one rule
 * enforceable: an operation reachable from a panel, from the menu bar and from a keyboard shortcut
 * must be the same operation. A panel that reimplemented "delete the selection" slightly
 * differently is a bug users report as "undo is broken".
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Project/RecoveryStore.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"
#include "CNA/Editor/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Editor/Ui/EditorUi.hpp"
#include "CNA/Editor/Viewport/EditorAudio.hpp"
#include "CNA/Editor/Viewport/EditorViewport.hpp"

namespace CNA::Editor
{
    class EditorContext;

    /** @brief What the editor believes the player process is doing. */
    enum class PlayMode
    {
        Stopped,
        Playing,
        Paused
    };

    /**
     * @brief What a press in the viewport means.
     *
     * Kept apart from GizmoMode on purpose. A gizmo mode picks *which manipulator* acts on the
     * selection; a tool decides whether a press manipulates anything at all. Folding a paint brush
     * into the gizmo enumeration would make "no gizmo" and "painting" the same state.
     */
    enum class EditorTool
    {
        /** @brief Pick entities and drag the gizmo. The default. */
        Select,
        /** @brief Set the tile under the cursor on the selected tilemap. */
        PaintTiles,
        /** @brief Clear the tile under the cursor. */
        EraseTiles,
        /** @brief Take the tile under the cursor as the brush, then go back to painting. */
        PickTile,
        /** @brief Fill the rectangle a drag encloses, applied on release. */
        FillTiles
    };

    /** @brief Returns the display name of @p tool. */
    const char* toString(EditorTool tool);

    /**
     * @brief The editor operations a panel can invoke.
     *
     * Narrow on purpose. Everything here is something the menu bar, a keyboard shortcut and at
     * least one panel all trigger, so it lives in one place and behaves identically whichever
     * asked for it. An operation only one panel ever performs belongs to that panel instead.
     */
    class EditorActions
    {
    public:
        virtual ~EditorActions() = default;

        /**
         * @brief Returns the current viewport.
         *
         * Asked for per use rather than held, because the CNA-backed viewport is installed after
         * construction (EditorApplication::setViewport) and a panel holding the old one would go
         * on drawing into a viewport nobody displays.
         */
        [[nodiscard]] virtual EditorViewport& getViewport() = 0;

        /**
         * @brief Returns the audio preview.
         *
         * Asked for per use rather than held, for the same reason the viewport is: the CNA-backed
         * one is installed after construction, and a panel holding the old one would play into
         * nothing.
         */
        [[nodiscard]] virtual EditorAudio& getAudio() = 0;

        virtual void undo() = 0;
        virtual void redo() = 0;
        virtual void newScene() = 0;
        virtual void saveScene() = 0;

        virtual void duplicateSelection() = 0;
        virtual void deleteSelection() = 0;

        /** @brief Moves and zooms the camera so the selection fills the viewport. */
        virtual void frameSelection() = 0;

        /** @brief Puts @p entityId's hierarchy row into rename mode. */
        virtual void beginRename(const Uuid& entityId) = 0;

        virtual void setGizmoMode(GizmoMode mode) = 0;
        [[nodiscard]] virtual GizmoMode getGizmoMode() const = 0;

        /**
         * @brief Sets which frame the gizmo's arms point along.
         *
         * One setting shared by every manipulator rather than one per gizmo, because it is a way
         * of working rather than a property of a tool: a user who thinks in an object's own axes
         * thinks that way whether they are moving it or turning it.
         */
        virtual void setGizmoSpace(GizmoSpace space) = 0;
        [[nodiscard]] virtual GizmoSpace getGizmoSpace() const = 0;

        /**
         * @brief Sends the pointer and keyboard to the running player, if one is running.
         *
         * Sent only when something changed, which is decided by the implementation rather than by
         * every caller. A panel forwarding sixty identical snapshots a second would be a panel
         * that had to remember the previous one.
         */
        virtual void forwardInputToPlayer(const PlayerInputSnapshot& snapshot) = 0;

        /** @brief Returns what the player last reported it makes of that input. */
        [[nodiscard]] virtual const PlayerInputSnapshot& getPlayerInput() const = 0;

        /**
         * @brief Switches the viewport between the 2D camera and the 3D one (plan.md ED-400).
         *
         * Application state rather than the viewport panel's own, for the same reason the gizmo
         * mode is: the View menu offers it, the toolbar shows it, and a second copy of "which
         * camera is the user looking through" would be a second thing to keep in step.
         */
        virtual void setThreeDimensionalView(bool enabled) = 0;
        [[nodiscard]] virtual bool isThreeDimensionalView() const = 0;

        /**
         * @brief Chooses which plane the 3D grid is drawn on, and returns it.
         *
         * Application state for the same reason, and a *view* setting rather than a document one:
         * which plane a user wants under a scene is about what they are doing to it this minute,
         * not about the scene, so nothing here is written to a file.
         */
        virtual void setGridPlane(GridPlane plane) = 0;
        [[nodiscard]] virtual GridPlane getGridPlane() const = 0;

        virtual void startPlay() = 0;
        virtual void stopPlay() = 0;
        virtual void setPlayPaused(bool paused) = 0;
        virtual void stepPlayFrame() = 0;

        [[nodiscard]] virtual PlayMode getPlayMode() const = 0;
        [[nodiscard]] virtual const std::vector<PlayerBuild>& getPlayerBuilds() const = 0;
        [[nodiscard]] virtual std::size_t getSelectedPlayerBuild() const = 0;
        virtual void selectPlayerBuild(std::size_t index) = 0;

        /**
         * @brief Returns unsaved work found from a previous session, or nullptr when there is none.
         *
         * Offered rather than restored automatically. Silently replacing what the user opened with
         * something they cannot see the provenance of is how an editor turns a crash into two
         * losses instead of one.
         */
        [[nodiscard]] virtual const RecoverySnapshot* getRecoverableScene() const = 0;

        /** @brief Loads the recovered scene into the editor, leaving the file on disk untouched. */
        virtual void recoverScene() = 0;

        /** @brief Deletes the snapshot, accepting the file on disk as the truth. */
        virtual void discardRecoveredScene() = 0;

        virtual void setEditorTool(EditorTool tool) = 0;
        [[nodiscard]] virtual EditorTool getEditorTool() const = 0;

        /** @brief The tile index the paint tool writes. Ignored by every other tool. */
        virtual void setPaintTile(std::int64_t tile) = 0;
        [[nodiscard]] virtual std::int64_t getPaintTile() const = 0;

        /**
         * @brief Publishes which animation frame the viewport should draw.
         *
         * The panel that owns the playback keeps owning it -- this carries only the *result*, so
         * the viewport draws the frame the preview shows without either side knowing about the
         * other. Editor state throughout: none of it reaches the document (D-07).
         */
        virtual void setAnimationPreview(const AnimationPreview& preview) = 0;
        [[nodiscard]] virtual const AnimationPreview& getAnimationPreview() const = 0;
    };

    /**
     * @brief One dockable panel.
     *
     * Holds references rather than pointers: a panel cannot outlive the application that owns it,
     * and a null check on every use would be noise guarding against a state that cannot arise.
     */
    class EditorPanel
    {
    public:
        EditorPanel(EditorContext& context, EditorUi& ui, EditorActions& actions)
            : context_(context), ui_(ui), actions_(actions)
        {
        }

        virtual ~EditorPanel() = default;

        EditorPanel(const EditorPanel&) = delete;
        EditorPanel& operator=(const EditorPanel&) = delete;

        /** @brief Draws the panel for one frame. */
        virtual void draw() = 0;

    protected:
        EditorContext& context_;
        EditorUi& ui_;
        EditorActions& actions_;
    };
}
