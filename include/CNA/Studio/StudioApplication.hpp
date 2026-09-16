// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/StudioApplication.hpp
 * @brief Ties the context, the UI, the viewport and the panels together and runs the frame loop.
 *
 * What is left here after ED-210 is what no single panel owns: the document lifecycle, the
 * keyboard shortcuts, the play process, and the handful of operations a panel, the menu bar and a
 * shortcut can all trigger. Those reach a panel through StudioActions rather than through a back
 * reference to the whole application, which is what keeps a panel from quietly growing a
 * dependency on the editor's internals.
 */

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Studio/Assets/AssetWatcher.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/StudioOptions.hpp"
#include "CNA/Studio/Plugins/Plugin.hpp"
#include "CNA/Studio/Panels/AssetBrowserPanel.hpp"
#include "CNA/Studio/Panels/BuildPanel.hpp"
#include "CNA/Studio/Core/ImageDiff.hpp"
#include "CNA/Studio/Panels/ComparisonPanel.hpp"
#include "CNA/Studio/Panels/ConsolePanel.hpp"
#include "CNA/Studio/Panels/DiagnosticsPanel.hpp"
#include "CNA/Studio/Panels/StudioPanel.hpp"
#include "CNA/Studio/Panels/HierarchyPanel.hpp"
#include "CNA/Studio/Panels/InspectorPanel.hpp"
#include "CNA/Studio/Panels/MainMenuBar.hpp"
#include "CNA/Studio/Panels/HistoryPanel.hpp"
#include "CNA/Studio/Panels/ValidationPanel.hpp"
#include "CNA/Studio/Panels/ViewportPanel.hpp"
#include "CNA/Studio/Project/RecoveryStore.hpp"
#include "CNA/Studio/StudioRecovery.hpp"
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/Ui/StudioUi.hpp"
#include "CNA/Studio/Viewport/StudioViewport.hpp"

namespace CNA::Studio
{

    /**
     * @brief The editor application.
     *
     * Owns the context, the UI, the viewport, the panels and the play process, and runs the frame
     * loop. It is constructed with an StudioUi and an StudioViewport rather than creating them, so
     * `--headless` and the unit tests build the exact same application over the null
     * implementations -- there is no separate "test mode" path that can drift from the real one.
     *
     * The panels are classes now (plan.md ED-210); what remains here is what is genuinely shared:
     * the document lifecycle, the keyboard shortcuts, the play process, and the operations that a
     * panel, the menu bar and a shortcut can all trigger. It implements StudioActions so that a
     * panel reaches those through one narrow interface rather than through the whole application.
     */
    class StudioApplication final : public StudioActions
    {
    public:
        /**
         * @param ui The UI implementation; must not be null.
         * @param viewport The viewport implementation; must not be null.
         */
        StudioApplication(std::unique_ptr<StudioUi> ui, std::unique_ptr<StudioViewport> viewport);

        /** @brief Applies @p options: opens the project and scene, sets the frame limit. */
        bool initialize(const StudioOptions& options);

        /** @brief Runs frames until exit. Returns the process exit code. */
        int run();

        /**
         * @brief Draws exactly one frame. Exposed so tests can step the editor deterministically.
         *
         * @param deltaSeconds Time since the previous frame, which paces the asset watcher. Tests
         *        pass an explicit value so a poll can be forced without any sleeping.
         */
        void renderFrame(double deltaSeconds = 0.0);

        /** @brief Returns the asset watcher, so a test can shorten its interval. */
        [[nodiscard]] AssetWatcher& getAssetWatcher() { return watcher_; }

        [[nodiscard]] StudioContext& getContext() { return context_; }
        [[nodiscard]] StudioUi& getUi() { return *ui_; }
        [[nodiscard]] StudioViewport& getViewport() override { return *viewport_; }
        [[nodiscard]] StudioAudio& getAudio() override { return *audio_; }

        /**
         * @brief Replaces the audio preview.
         *
         * Installed after construction like the viewport, because the CNA-backed one needs an
         * asset database the application does not have until a project is open.
         */
        void setAudio(std::unique_ptr<StudioAudio> audio);

        /**
         * @brief Replaces the viewport.
         *
         * The CNA-backed viewport needs a graphics device and a UI renderer, neither of which
         * exists when the application is constructed. Swapping it in later keeps every panel
         * written against the abstraction and keeps the construction order honest, rather than
         * having the application reach out for a device it does not own.
         */
        void setViewport(std::unique_ptr<StudioViewport> viewport);

        /**
         * @brief Replaces the discovered player builds.
         *
         * Discovery scans the editor's own directory, which a unit test has no control over. This
         * lets a test state what is installed instead of depending on how the build tree happens to
         * be laid out on the machine running it.
         */
        void setPlayerBuilds(std::vector<PlayerBuild> builds);

        // StudioActions. Each of these is reachable from the menu bar, from a keyboard shortcut and
        // from at least one panel, and must behave identically whichever asked.
        void undo() override;
        void redo() override;
        void newScene() override;
        void saveScene() override;
        void duplicateSelection() override;
        void deleteSelection() override;
        void frameSelection() override;
        void beginRename(const Uuid& entityId) override;

        void setGizmoMode(GizmoMode mode) override;
        [[nodiscard]] GizmoMode getGizmoMode() const override { return gizmoMode_; }

        /**
         * @brief Starts a backend comparison, as pressing Compare in the Backends panel does.
         *
         * Exposed so `--compare-backends` drives exactly the same code a user does, rather than a
         * parallel path that could pass while the panel was broken.
         */
        void startBackendComparison();

        /** @brief Drives a `--compare-backends` run: start it, wait for it, report, exit. */
        void updateBackendComparison();

        /** @brief Returns the comparison the panel owns, for a caller that has to report on it. */
        [[nodiscard]] const BackendComparison& getBackendComparison() const;

        /** @brief Called once when a `--compare-backends` run reaches a verdict. */
        using ComparisonReport = std::function<void(const BackendComparison&)>;

        /**
         * @brief Sets who to tell when a comparison run finishes.
         *
         * A callback rather than a value the caller reads afterwards, because in a windowed run the
         * application is owned by the host and is gone by the time `runStudioInWindow` returns.
         */
        void setComparisonReport(ComparisonReport report) { comparisonReport_ = std::move(report); }

        void setGizmoSpace(GizmoSpace space) override;
        [[nodiscard]] GizmoSpace getGizmoSpace() const override { return gizmoSpace_; }

        void forwardInputToPlayer(const PlayerInputSnapshot& snapshot) override;
        [[nodiscard]] const PlayerInputSnapshot& getPlayerInput() const override { return playerInput_; }

        void setThreeDimensionalView(bool enabled) override;

        /**
         * @brief Points the 3D camera at the whole scene.
         *
         * Called the first time the 3D view is entered, and only then. A default camera shows an
         * empty grid of any scene laid out away from the origin, and "it is pointing the wrong
         * way" is not a conclusion a user reaches -- they conclude the view is broken. Doing it on
         * every toggle instead would throw away an orbit the user still wanted; after the first
         * time, Frame Selected is how the camera is aimed, as it is in the 2D view.
         */
        void frameSceneInThreeDimensions();
        [[nodiscard]] bool isThreeDimensionalView() const override { return threeDimensionalView_; }

        void setGridPlane(GridPlane plane) override { gridPlane_ = plane; }
        [[nodiscard]] GridPlane getGridPlane() const override { return gridPlane_; }

        void startPlay() override;
        void stopPlay() override;
        void setPlayPaused(bool paused) override;
        void stepPlayFrame() override;

        [[nodiscard]] PlayMode getPlayMode() const override { return playMode_; }
        [[nodiscard]] const std::vector<PlayerBuild>& getPlayerBuilds() const override { return playerBuilds_; }
        [[nodiscard]] std::size_t getSelectedPlayerBuild() const override { return selectedBuild_; }
        void selectPlayerBuild(std::size_t index) override { selectedBuild_ = index; }

        [[nodiscard]] const RecoverySnapshot* getRecoverableScene() const override
        {
            return recovery_.recoverable();
        }
        void recoverScene() override;
        void discardRecoveredScene() override;

        void setStudioTool(StudioTool tool) override;
        [[nodiscard]] StudioTool getStudioTool() const override { return tool_; }

        void setPaintTile(std::int64_t tile) override { paintTile_ = tile; }
        [[nodiscard]] std::int64_t getPaintTile() const override { return paintTile_; }

        void setAnimationPreview(const AnimationPreview& preview) override { animationPreview_ = preview; }
        [[nodiscard]] const AnimationPreview& getAnimationPreview() const override
        {
            return animationPreview_;
        }

        /** @brief Returns the snapshot store, so a test can point it at a scratch directory. */
        [[nodiscard]] RecoveryStore& getRecoveryStore() { return recovery_.store(); }

    private:
        /**
         * @brief Applies this frame's keyboard shortcuts.
         *
         * Runs before the panels, so a shortcut and the menu item bound to the same operation both
         * take effect on the frame they are triggered.
         */
        void handleShortcuts();

        /**
         * @brief Reads whatever the player sent this frame and routes it into the console.
         *
         * Also notices the player going away on its own -- the user closing the game window is a
         * perfectly normal way to end play mode, and the toolbar has to follow it back to Stopped
         * rather than keep offering Pause for a process that is gone.
         */
        void pollPlayer();

        /**
         * @brief Notices assets changed outside the editor and reloads what they affect.
         *
         * A texture edited in another program is the common case, and without this the editor goes
         * on showing the old art until it is restarted.
         */
        void pollAssets(double deltaSeconds);

        /**
         * @brief Writes a crash-recovery snapshot when one is due, or drops a stale one.
         *
         * Only while the document differs from its file: a snapshot of a scene that matches disk
         * protects nothing and would offer a pointless recovery on the next start-up.
         */
        void updateAutosave(double deltaSeconds);

        /** @brief Looks for unsaved work from a previous session and reports what it finds. */
        void findRecoverableScene();

        /**
         * @brief Sends a document change to the running player, when it is one the wire can carry.
         *
         * Every document change goes through a command (D-06), so this one hook sees all of them.
         * Only property edits are mirrored today; anything else is left alone rather than guessed
         * at, because a partially applied scene in the player would be worse than a stale one.
         */
        void mirrorToPlayer(const StudioCommand& command);

        /** @brief Tells the running player that one asset changed on disk. */
        void reloadAssetInPlayer(const Uuid& assetId);

    public:
        /**
         * @brief Unloads every plugin before anything they registered into goes away.
         *
         * The only reason this class needs a destructor at all. A plugin's `shutdown` is handed
         * the `StudioContext`, so it has to run while the context is still alive -- and member
         * destruction alone would release the library handles without ever calling it, leaving
         * the registries pointing into unmapped code.
         */
        ~StudioApplication();

    private:
        /** @brief Draws every panel a plugin registered (ED-412). */
        void drawPluginPanels();

        /** @brief Discovers and loads plugins, reporting each failure by name (ED-411). */
        void loadPlugins(const StudioOptions& options);

        /** @brief Unloads every plugin while the context they were given is still alive. */
        void unloadPlugins();

        StudioContext context_;

        /**
         * @brief The plugins this editor loaded (ED-411).
         *
         * Declared *after* the context and therefore destroyed *before* it, which is the ordering
         * that matters: a plugin's `shutdown` is handed the context, so the context has to still
         * be there when the host unloads. Getting this the other way round is a crash at exit and
         * only at exit.
         */
        PluginHost plugins_;
        std::unique_ptr<StudioUi> ui_;
        std::unique_ptr<StudioViewport> viewport_;
        std::unique_ptr<StudioAudio> audio_ = std::make_unique<NullStudioAudio>();

        MainMenuBar menuBar_;
        HierarchyPanel hierarchyPanel_;
        ViewportPanel viewportPanel_;
        InspectorPanel inspectorPanel_;
        HistoryPanel historyPanel_;
        AssetBrowserPanel assetBrowserPanel_;
        ValidationPanel validationPanel_;
        ConsolePanel consolePanel_;
        DiagnosticsPanel diagnosticsPanel_;
        BuildPanel buildPanel_;
        ComparisonPanel comparisonPanel_;

        /**
         * @brief Seconds since the application started, accumulated from the frame delta.
         *
         * The comparison's timeout is measured against this rather than against a wall clock, for
         * the reason every clock in this editor is passed in: a test drives it by handing over
         * frame deltas, and never has to sleep.
         */
        double elapsedSeconds_ = 0.0;

        /** @brief `--compare-backends`: run a comparison instead of editing, then exit. */
        bool comparisonMode_ = false;
        bool comparisonStarted_ = false;
        ComparisonReport comparisonReport_;

        GizmoMode gizmoMode_ = GizmoMode::Translate;
        GizmoSpace gizmoSpace_ = GizmoSpace::World;

        /**
         * @brief Whether the viewport looks through the 3D camera. Never serialised (D-07).
         *
         * Off by default: every scene this editor can currently draw is a 2D one, and a 3D
         * wireframe is the right first sight of a scene with models in it, not of a tilemap.
         */
        bool threeDimensionalView_ = false;

        /**
         * @brief Which plane the 3D grid is drawn on. Never serialised (D-07), for the same reason.
         *
         * The scene's own plane by default, because that is where everything this editor can place
         * today lives; a floor is the useful one once ED-402 brings models with height.
         */
        GridPlane gridPlane_ = GridPlane::SceneXY;

        /**
         * @brief What the player reported it makes of the input last forwarded to it.
         *
         * The player's own view, in the player's own window coordinates, rather than a copy of
         * what was sent: the two differ by exactly the mapping the player applied, and the
         * mapping is the part worth showing.
         */
        PlayerInputSnapshot playerInput_;

        /** @brief The last snapshot actually sent, so an unchanged one is not sent again. */
        PlayerInputSnapshot lastForwardedInput_;

        /** @brief Whether the 3D camera has been aimed at the scene yet. See the method above. */
        bool threeDimensionalCameraPlaced_ = false;
        StudioTool tool_ = StudioTool::Select;
        std::int64_t paintTile_ = 0;
        AnimationPreview animationPreview_;

        /**
         * @brief The player builds installed beside Studio, and which one Play will launch.
         *
         * A list rather than a single choice because CNA fixes its backend at compile time
         * (ANALYSIS.md finding F-01): "play this on Vulkan" means "launch `cna-player-vulkan`", so
         * the set of available backends is the set of binaries actually on disk.
         */
        std::vector<PlayerBuild> playerBuilds_;
        std::size_t selectedBuild_ = 0;

        AssetWatcher watcher_;

        std::unique_ptr<PlayerProcess> player_;
        PlayMode playMode_ = PlayMode::Stopped;

        int frameLimit_ = 0;
        int framesRendered_ = 0;

        /**
         * @brief Snapshots, and whatever a previous session left behind.
         *
         * The flow rather than a copy of it: the native shell runs the same object through its own
         * host, and two hosts each deciding when work is safe is one decision too many.
         */
        StudioRecoverySession recovery_{context_};
    };
}
