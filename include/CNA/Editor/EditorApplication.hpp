// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/EditorApplication.hpp
 * @brief Ties the context, the UI, the viewport and the panels together and runs the frame loop.
 *
 * What is left here after ED-210 is what no single panel owns: the document lifecycle, the
 * keyboard shortcuts, the play process, and the handful of operations a panel, the menu bar and a
 * shortcut can all trigger. Those reach a panel through EditorActions rather than through a back
 * reference to the whole application, which is what keeps a panel from quietly growing a
 * dependency on the editor's internals.
 */

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/Assets/AssetWatcher.hpp"
#include "CNA/Editor/EditorContext.hpp"
#include "CNA/Editor/Plugins/Plugin.hpp"
#include "CNA/Editor/Panels/AssetBrowserPanel.hpp"
#include "CNA/Editor/Panels/BuildPanel.hpp"
#include "CNA/Editor/Core/ImageDiff.hpp"
#include "CNA/Editor/Panels/ComparisonPanel.hpp"
#include "CNA/Editor/Panels/ConsolePanel.hpp"
#include "CNA/Editor/Panels/DiagnosticsPanel.hpp"
#include "CNA/Editor/Panels/EditorPanel.hpp"
#include "CNA/Editor/Panels/HierarchyPanel.hpp"
#include "CNA/Editor/Panels/InspectorPanel.hpp"
#include "CNA/Editor/Panels/MainMenuBar.hpp"
#include "CNA/Editor/Panels/HistoryPanel.hpp"
#include "CNA/Editor/Panels/ValidationPanel.hpp"
#include "CNA/Editor/Panels/ViewportPanel.hpp"
#include "CNA/Editor/Project/RecoveryStore.hpp"
#include "CNA/Editor/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Editor/Ui/EditorUi.hpp"
#include "CNA/Editor/Viewport/EditorViewport.hpp"

namespace CNA::Editor
{
    /** @brief Parsed command line. */
    struct EditorOptions
    {
        /** @brief Path to a `.cnaproject` to open at start-up. */
        std::string projectPath;

        /** @brief Path to a `.cnascene` to open, overriding the project's startup scene. */
        std::string scenePath;

        /**
         * @brief Requested UI toolkit, e.g. "imgui" or "null".
         *
         * Note that this selects the *UI toolkit*, not the CNA graphics backend. CNA's backend is
         * fixed at compile time (ANALYSIS.md finding F-01), so `--graphics=` on the editor would
         * be a lie -- it appears instead on cna-player, where it chooses which player binary to
         * launch.
         */
        std::string uiBackend = "imgui";

        /** @brief Run with no window, on NullEditorUi. */
        bool headless = false;

        /** @brief Exit after this many frames. Zero means run until the user quits. */
        int frameLimit = 0;

        /**
         * @brief Start with the viewport in its 3D camera (plan.md ED-400).
         *
         * On the command line because it is the only way to *see* the 3D view from a script: the
         * screenshot path takes a picture of whatever the editor is showing, and every UI feature
         * here has been verified by taking one.
         */
        bool threeDimensionalView = false;

        /**
         * @brief Yaw and pitch, in degrees, to orbit the 3D camera to before drawing.
         *
         * The same argument that put `--view=3d` here, taken one step further. That flag makes the
         * 3D view *reachable* from a script; this one makes it reachable from an angle. A 3D
         * feature photographed head-on is photographed in the one pose where it looks like the 2D
         * view -- which is exactly the pose that cannot tell a mesh from the flat rectangle a
         * sprite draws, and so cannot show whether ED-405's models arrived.
         *
         * Empty leaves the camera where start-up put it. Ignored without `--view=3d`, because the
         * 2D camera has no pitch to set.
         */
        std::optional<EditorVector2> orbitDegrees;

        /**
         * @brief A panel to bring to the front before drawing, by its exact title.
         *
         * The third flag on the same argument as `--view=3d` and `--orbit`: a docked panel sharing
         * a tab bar with five others cannot be photographed at all otherwise, because the tab that
         * happens to be in front is whichever docked last. Empty leaves the layout alone.
         */
        std::string focusPanel;

        /**
         * @brief Where to look for plugins. Defaults to `plugins/` beside the executable.
         *
         * The same convention player builds are discovered by, and for the same reason: an editor
         * and the things that extend it are installed together. On the command line so a test or a
         * developer can point at a build tree without installing anything.
         */
        std::string pluginDirectory;

        /**
         * @brief Write a PNG of the final frame here. Requires a frame limit.
         *
         * A smoke test that only checks the process exited cleanly cannot tell a working editor
         * from one that opened a blank window; an image can.
         */
        std::string screenshotPath;

        /**
         * @brief argv[0], used to find the `cna-player-*` binaries beside the editor.
         *
         * Play mode offers exactly the backends whose player executable is installed, which is a
         * direct consequence of CNA fixing its backend at compile time (ANALYSIS.md finding F-01).
         */
        std::string executablePath;

        /**
         * @brief Seconds between crash-recovery snapshots of an unsaved scene. Zero disables them.
         *
         * The reliable half of crash recovery is the part that runs before the crash: the snapshot
         * is already on disk when the process dies, and needs nothing from the dying process. This
         * is how much work a crash can cost.
         */
        double autosaveSeconds = 30.0;

        /**
         * @brief Where snapshots are kept. Empty means the per-user default.
         *
         * Set it to keep a sandboxed or portable run from writing outside its own directory.
         */
        std::string recoveryDirectory;

        /**
         * @brief Run a backend comparison instead of editing, and exit with its verdict.
         *
         * plan.md ED-511: the same run the Backends panel performs, driven from the command line so
         * a build server can assert that a scene renders identically on every installed backend.
         * The exit code is the assertion -- non-zero when they disagree or when the run could not
         * happen at all.
         *
         * It needs a graphics device, because comparing captures means decoding them, so it does
         * not combine with `--headless`.
         */
        bool compareBackends = false;

        /** @brief Largest per-channel difference `--compare-backends` still counts as identical. */
        int comparisonTolerance = kDefaultImageTolerance;

        /** @brief Print the backend table and exit. */
        bool listBackends = false;

        /** @brief Print usage and exit. */
        bool showHelp = false;

        /** @brief Print the version and exit. */
        bool showVersion = false;

        /** @brief Set when parsing failed; @c errorMessage says why. */
        bool hasError = false;
        std::string errorMessage;

        /** @brief Parses argv. Never throws and never exits the process. */
        static EditorOptions parse(int argc, const char* const* argv);

        /** @brief Returns the usage text. */
        static std::string getUsage();
    };

    /**
     * @brief The editor application.
     *
     * Owns the context, the UI, the viewport, the panels and the play process, and runs the frame
     * loop. It is constructed with an EditorUi and an EditorViewport rather than creating them, so
     * `--headless` and the unit tests build the exact same application over the null
     * implementations -- there is no separate "test mode" path that can drift from the real one.
     *
     * The panels are classes now (plan.md ED-210); what remains here is what is genuinely shared:
     * the document lifecycle, the keyboard shortcuts, the play process, and the operations that a
     * panel, the menu bar and a shortcut can all trigger. It implements EditorActions so that a
     * panel reaches those through one narrow interface rather than through the whole application.
     */
    class EditorApplication final : public EditorActions
    {
    public:
        /**
         * @param ui The UI implementation; must not be null.
         * @param viewport The viewport implementation; must not be null.
         */
        EditorApplication(std::unique_ptr<EditorUi> ui, std::unique_ptr<EditorViewport> viewport);

        /** @brief Applies @p options: opens the project and scene, sets the frame limit. */
        bool initialize(const EditorOptions& options);

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

        [[nodiscard]] EditorContext& getContext() { return context_; }
        [[nodiscard]] EditorUi& getUi() { return *ui_; }
        [[nodiscard]] EditorViewport& getViewport() override { return *viewport_; }
        [[nodiscard]] EditorAudio& getAudio() override { return *audio_; }

        /**
         * @brief Replaces the audio preview.
         *
         * Installed after construction like the viewport, because the CNA-backed one needs an
         * asset database the application does not have until a project is open.
         */
        void setAudio(std::unique_ptr<EditorAudio> audio);

        /**
         * @brief Replaces the viewport.
         *
         * The CNA-backed viewport needs a graphics device and a UI renderer, neither of which
         * exists when the application is constructed. Swapping it in later keeps every panel
         * written against the abstraction and keeps the construction order honest, rather than
         * having the application reach out for a device it does not own.
         */
        void setViewport(std::unique_ptr<EditorViewport> viewport);

        /**
         * @brief Replaces the discovered player builds.
         *
         * Discovery scans the editor's own directory, which a unit test has no control over. This
         * lets a test state what is installed instead of depending on how the build tree happens to
         * be laid out on the machine running it.
         */
        void setPlayerBuilds(std::vector<PlayerBuild> builds);

        // EditorActions. Each of these is reachable from the menu bar, from a keyboard shortcut and
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
         * application is owned by the host and is gone by the time `runEditorInWindow` returns.
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
            return recoverable_ ? &*recoverable_ : nullptr;
        }
        void recoverScene() override;
        void discardRecoveredScene() override;

        void setEditorTool(EditorTool tool) override;
        [[nodiscard]] EditorTool getEditorTool() const override { return tool_; }

        void setPaintTile(std::int64_t tile) override { paintTile_ = tile; }
        [[nodiscard]] std::int64_t getPaintTile() const override { return paintTile_; }

        void setAnimationPreview(const AnimationPreview& preview) override { animationPreview_ = preview; }
        [[nodiscard]] const AnimationPreview& getAnimationPreview() const override
        {
            return animationPreview_;
        }

        /** @brief Returns the snapshot store, so a test can point it at a scratch directory. */
        [[nodiscard]] RecoveryStore& getRecoveryStore() { return recovery_; }

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
        void mirrorToPlayer(const EditorCommand& command);

        /** @brief Tells the running player that one asset changed on disk. */
        void reloadAssetInPlayer(const Uuid& assetId);

    public:
        /**
         * @brief Unloads every plugin before anything they registered into goes away.
         *
         * The only reason this class needs a destructor at all. A plugin's `shutdown` is handed
         * the `EditorContext`, so it has to run while the context is still alive -- and member
         * destruction alone would release the library handles without ever calling it, leaving
         * the registries pointing into unmapped code.
         */
        ~EditorApplication();

    private:
        /** @brief Draws every panel a plugin registered (ED-412). */
        void drawPluginPanels();

        /** @brief Discovers and loads plugins, reporting each failure by name (ED-411). */
        void loadPlugins(const EditorOptions& options);

        /** @brief Unloads every plugin while the context they were given is still alive. */
        void unloadPlugins();

        EditorContext context_;

        /**
         * @brief The plugins this editor loaded (ED-411).
         *
         * Declared *after* the context and therefore destroyed *before* it, which is the ordering
         * that matters: a plugin's `shutdown` is handed the context, so the context has to still
         * be there when the host unloads. Getting this the other way round is a crash at exit and
         * only at exit.
         */
        PluginHost plugins_;
        std::unique_ptr<EditorUi> ui_;
        std::unique_ptr<EditorViewport> viewport_;
        std::unique_ptr<EditorAudio> audio_ = std::make_unique<NullEditorAudio>();

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
        EditorTool tool_ = EditorTool::Select;
        std::int64_t paintTile_ = 0;
        AnimationPreview animationPreview_;

        /**
         * @brief The player builds installed beside the editor, and which one Play will launch.
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

        RecoveryStore recovery_{getDefaultRecoveryDirectory()};
        double autosaveInterval_ = 30.0;
        double autosaveElapsed_ = 0.0;

        /** @brief Unsaved work from a previous session, waiting for the user to accept or drop it. */
        std::optional<RecoverySnapshot> recoverable_;

        /** @brief Whether a snapshot for the open scene is currently on disk. */
        bool autosaveWritten_ = false;

        /** @brief Whether the "autosave is suspended" warning has already been said. */
        bool autosaveSuspensionReported_ = false;

        /** @brief Whether the "cannot write a snapshot" error has already been said. */
        bool autosaveFailureReported_ = false;
    };
}
