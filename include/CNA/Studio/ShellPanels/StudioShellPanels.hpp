// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioShellPanels.hpp
 * @brief Binds every ported panel to a shell, in one place.
 *
 * `plan.md` STUDIO-07001, STUDIO-07017.
 *
 * ### Why this exists rather than being written where the shell is created
 *
 * It was written there, and there is exactly one place a running Studio is created — behind a CNA
 * checkout, in `cna-studio-viewport`. That meant the ported panels could only be *seen* in a build
 * with CNA, so the headless shell preview — the one screenshot harness that runs on a machine with
 * no GPU and no display, and the only visual test the project has — showed five empty rectangles
 * where the ported panels are.
 *
 * Nothing about binding a panel needs CNA. Moved here, the same binding serves the real editor and
 * the preview, so what CI photographs is what a user sees rather than a frame of the shell with
 * its content missing.
 *
 * ### It holds the panels' retained state
 *
 * Which tree rows are open, which asset is selected, the Build panel itself: state that belongs to
 * a panel rather than to a frame, and that has to outlive both description passes. Keeping it in
 * one object is also what lets a test drive the whole set the way the editor does.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/ShellPanels/StudioBuildPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioComparisonPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioLayersPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioPreferencesPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioRecovery.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace CNA::Studio
{
    /**
     * @brief Whether a game launched from the editor is running, paused, or not running.
     *
     * Declared here rather than reusing the prototype's `PlayMode`, which lives in the Dear ImGui
     * panel headers that `STUDIO-07030` deletes. The native module must not depend on them, and the
     * alternative -- moving the prototype's enum somewhere shared -- would be rearranging code that
     * is on its way out.
     */
    enum class StudioPlayState
    {
        Stopped,
        Playing,
        Paused
    };

    class StudioContext;

    /** @brief What a host can offer the panels that the panels cannot do themselves. */
    struct StudioShellPanelServices
    {
        /**
         * @brief The editor camera the viewport navigates, and the sprite sizes it picks against.
         *
         * Both live with whoever renders the scene, which is the one thing that has a device.
         * Unset means there is no camera to drive — the viewport then shows its placeholder and
         * does nothing, which is what a build with no device should do.
         */
        StudioCamera2D* camera = nullptr;

        /** @brief Resolves a sprite's texel size for picking. Empty picks at the default size. */
        SpriteSizeProvider spriteSize;

        /**
         * @brief Decodes an image file. Unset means this build cannot read one back.
         *
         * A seam for the same reason the clipboard is: decoding a PNG needs a graphics API and
         * exactly one module may have one (decision D-03). A headless Studio leaves it unset, and
         * the Backends panel then reports that it could not read the captures — which is the
         * honest answer rather than a crash or a silent run that compares nothing.
         */
        ImageReader readImage;

        /** @brief Writes an image file, for the difference images. Unset writes none. */
        ImageWriter writeImage;

        /**
         * @brief Puts text on the system clipboard, returning whether it got there.
         *
         * A seam rather than a direct call, because the clipboard is behind a default-off CNA
         * option (gap G-02) and the CNA-free build has none at all. Unset means "no clipboard",
         * which the Output Log reports rather than silently doing nothing.
         */
        std::function<bool(const std::string&)> setClipboardText;
    };

    /** @brief What the panels reported over the last frame, for diagnostics and smoke tests. */
    struct StudioShellPanelCounts
    {
        std::size_t outlinerRowsDrawn = 0;
        std::size_t outlinerRowsTotal = 0;
        std::size_t detailsRowsDrawn = 0;
        std::size_t contentRowsDrawn = 0;
        std::size_t contentRowsTotal = 0;
        std::size_t logRowsDrawn = 0;
        std::size_t logRowsMatching = 0;
        std::size_t problemRowsDrawn = 0;
        std::size_t historyRowsDrawn = 0;
        std::size_t historyPositions = 0;
        std::size_t diagnosticRowsDrawn = 0;
        std::size_t viewportSelections = 0;
        std::size_t layerRowsDrawn = 0;
        std::size_t comparisonRowsDrawn = 0;
        std::size_t preferenceChanges = 0;
        std::size_t playerMessages = 0;
        std::size_t brokenReferences = 0;
        std::size_t sceneErrors = 0;
        std::size_t sceneWarnings = 0;
    };

    /**
     * @brief The ported panels, bound to a shell and holding their retained state.
     *
     * Construct one beside the shell and it stays bound for the shell's life. It borrows the
     * shell, the context and the log, so all three must outlive it.
     */
    class StudioShellPanels
    {
    public:
        /**
         * @brief Binds every ported panel's content to @p shell.
         * @param shell The shell to bind into.
         * @param context The editor context the panels read and write.
         * @param log The log the Output Log shows and the panels append to.
         * @param services Host facilities the panels cannot reach themselves.
         */
        StudioShellPanels(StudioShell& shell, StudioContext& context, StudioLog& log,
                          StudioShellPanelServices services = {});

        StudioShellPanels(const StudioShellPanels&) = delete;
        StudioShellPanels& operator=(const StudioShellPanels&) = delete;

        /**
         * @brief Advances anything the panels own that runs between frames.
         *
         * The build process, the player and a running backend comparison, all of which must be
         * polled whether or not their panel is the visible tab: work that advanced only while
         * somebody was looking at it would stall the moment they looked away.
         *
         * @param nowSeconds A monotonic clock. Passed in, like every other clock here, so a test
         *        can drive a timeout rather than wait for one.
         */
        void poll(double nowSeconds);

        /** @brief What the panels reported over the last frame. */
        [[nodiscard]] const StudioShellPanelCounts& counts() const { return counts_; }

        /**
         * @brief Hands the viewport its camera once a device exists to own one.
         *
         * Separate from the constructor because the panels are bound before the graphics device is
         * created — that is what lets the headless preview bind the same panels — and the camera
         * belongs to whoever renders the scene.
         *
         * @param camera The editor camera; must outlive this.
         * @param spriteSize Resolves a sprite's texel size for picking.
         */
        void setViewportServices(StudioCamera2D& camera, SpriteSizeProvider spriteSize);

        /** @brief Which manipulator the viewport shows, so the renderer draws the same one. */
        [[nodiscard]] GizmoMode viewportMode() const { return viewportState_.mode; }

        /** @brief Which frame the translate manipulator's arms follow. */
        [[nodiscard]] GizmoSpace viewportSpace() const { return viewportState_.space; }

        /**
         * @brief Tells the shell which player binaries exist beside it.
         *
         * "Run this on Vulkan" means "launch cna-player-vulkan", so what Play can do is decided
         * by what is on disk. One setter rather than two, because the Diagnostics panel reports
         * the same list and two copies would be two chances to disagree.
         *
         * @param builds The discovered players, by renderer.
         */
        void setPlayerBuilds(std::vector<PlayerBuild> builds);

        /** @brief Whether a player is running right now. */
        [[nodiscard]] bool isPlaying() const;

        /** @brief Whether a player is running and paused. */
        [[nodiscard]] StudioPlayState playState() const { return playState_; }

        /**
         * @brief Asks the running game to pause or resume.
         *
         * Follows the player's state only once the request is on the wire: a toolbar that says
         * "Paused" over a game that never got the message is worse than one that did nothing.
         *
         * @param paused True to pause.
         * @return True when the request was sent.
         */
        bool setPlayPaused(bool paused);

        /**
         * @brief Asks a paused game to advance one frame.
         *
         * Only means something while paused; the player ignores it otherwise, and offering it
         * while the game is running would suggest a control that does nothing.
         *
         * @return True when the request was sent.
         */
        bool stepPlayFrame();

        /** @brief Stops the running game and starts it again from the scene as it now stands. */
        void restartPlaying();

        /** @brief The backend comparison this Studio would run, for a caller to report on. */
        [[nodiscard]] const BackendComparison& comparison() const { return comparison_; }

        /**
         * @brief The user's preferences, edited by the Preferences panel.
         *
         * Held here for the same reason the build is: the panel describes them and something else
         * persists them, and two copies would be two answers to what the user decided.
         */
        [[nodiscard]] StudioPreferences& preferences() { return preferences_; }

        /** @brief The user's preferences. */
        [[nodiscard]] const StudioPreferences& preferences() const { return preferences_; }

        /**
         * @brief Crash recovery: the snapshot timer, and whatever a previous session left.
         *
         * Exposed so a host can point it at a directory other than the default -- which is what
         * `--recovery-dir` is for, and the only way a test can have one that is not the user's.
         */
        [[nodiscard]] StudioRecoverySession& recovery() { return recovery_; }

        /** @brief Crash recovery. */
        [[nodiscard]] const StudioRecoverySession& recovery() const { return recovery_; }

        /**
         * @brief Sets the seam through which changed preferences reach disk.
         *
         * Unset means "nothing is persisted", which the preview wants: a shell that refused to
         * change a preference because nobody gave it a file would be worse than one that forgets.
         *
         * @param save Writes the preferences. Receives the reason on failure.
         */
        void setPreferencesSink(std::function<bool(const StudioPreferences&,
                                                   std::string*)> save)
        {
            savePreferences_ = std::move(save);
        }

        /**
         * @brief Applies whatever @ref preferences now says to the shell, then persists it.
         *
         * Public because a host that has just read the file wants exactly this, and because the
         * order matters and should not be repeated: applied *before* it is written, so a save that
         * fails still leaves the user looking at what they chose.
         */
        void applyPreferences();

        /** @brief The build this Studio would run. */
        [[nodiscard]] BuildProcess& build() { return build_; }

        /**
         * @brief What the Diagnostics panel reports, for the host to fill in each frame.
         *
         * A snapshot rather than a live device: the panel is CNA-free and testable because it
         * reads this, and a build with no device leaves it at its honest empty defaults.
         */
        [[nodiscard]] StudioDiagnosticsInfo& diagnostics() { return diagnostics_; }

    private:
        void bind(StudioShell& shell);
        void bindViewport(StudioShell& shell);

        /**
         * @brief Draws the viewport's empty state: what is missing, and what to do about it.
         *
         * @param frame The frame.
         * @param bounds The viewport's body.
         * @param headline What is missing.
         * @param hint What to do next.
         */
        static void sayViewportIsEmpty(StudioFrame& frame, const UiRect& bounds,
                                       std::string_view headline, std::string_view hint);

        /** @brief Writes the project out as a standalone CNA game and says where. */
        void packageProject();

        /** @brief The player binary Play would launch, or nullptr when none was found. */
        [[nodiscard]] const PlayerBuild* choosePlayerBuild() const;

        void startPlaying();
        void stopPlaying();

        /** @brief Runs the open scene on every discovered player build. */
        void startComparison();

        /** @brief The comparison this Studio would run, from the project and the discovered builds. */
        [[nodiscard]] ComparisonRequest makeComparisonRequest() const;

        /** @brief Pumps the bridge once a frame and reports what the player said. */
        void pollPlayer();

        /**
         * @brief Raises @p notification, or logs it when there is no shell to raise it on.
         *
         * The one way these panels announce anything, so a message cannot be lost by being posted
         * somewhere that is not there -- which is what a preview and a headless test both are.
         */
        void notify(StudioNotification notification);

        /**
         * @brief Drives crash recovery: the snapshot timer, and the offer after a project opens.
         * @param nowSeconds The host's clock, which this differences into an interval.
         */
        void pollRecovery(double nowSeconds);

        /** @brief Announces a build that has just finished, either way. */
        void pollBuild();

        /**
         * @brief Announces a comparison that has just finished.
         * @param wasRunning Whether it was still launching or capturing before this poll.
         */
        void reportComparison(bool wasRunning);

        /**
         * @brief Tells the status bar what is open and what is running.
         *
         * Here rather than in the shell, because this is the object that owns the build, the
         * player and the comparison: a status bar that had to be told separately would be a second
         * list of running work to keep in step with the first.
         */
        void publishStatus();

        /** @brief Borrowed so the viewport can be re-bound when a camera arrives. */
        StudioShell* shell_ = nullptr;

        StudioContext& context_;
        StudioLog& log_;
        StudioShellPanelServices services_;

        StudioTreeState outlinerState_;
        StudioTreeState contentState_;
        StudioProblemsState problemsState_;
        StudioTreeState historyState_;
        StudioTreeState layersState_;
        StudioTreeState diagnosticsState_;
        StudioTreeState comparisonState_;
        StudioPreferences preferences_;
        StudioShortcutEditorState shortcutEditor_;

        /** @brief The build's state last poll, so a finish is noticed as a transition. */
        BuildState buildWasState_ = BuildState::Idle;

        /** @brief Whether the running game is playing or paused. */
        StudioPlayState playState_ = StudioPlayState::Stopped;

        /** @brief Snapshots of the open scene, and whatever a previous session left behind. */
        StudioRecoverySession recovery_{context_};

        /** @brief The host clock at the last poll, or negative before the first. */
        double recoveryLastSeconds_ = -1.0;

        /** @brief The project the last scan was for, so opening another triggers a new one. */
        std::string recoveryProject_;
        std::function<bool(const StudioPreferences&, std::string*)> savePreferences_;

        /** @brief Whether the open dialog is this object's Reset confirmation. */
        bool resettingPreferences_ = false;
        StudioViewportState viewportState_;
        Uuid selectedAsset_;

        BuildProcess build_;
        std::unique_ptr<StudioBuildPanel> buildPanel_;

        /**
         * @brief The player this Studio launches, and the builds it can choose from.
         *
         * Owned here for the same reason the build process is: two of either would be two
         * processes racing for one project, and the panel that reports on it must be reporting on
         * the one Play actually started.
         */
        PlayerProcess player_;
        std::vector<PlayerBuild> playerBuilds_;

        /** @brief Whether the player was running when it was last polled. See pollPlayer(). */
        bool playerWasRunning_ = false;

        /**
         * @brief The renderer comparison, and the tolerance the next run uses.
         *
         * Owned here rather than by its panel, so a run started from the panel survives the panel
         * being closed — half an hour of launching several games is not something to abandon
         * because a user switched tabs.
         */
        BackendComparison comparison_;
        int comparisonTolerance_ = kDefaultImageTolerance;

        StudioDiagnosticsInfo diagnostics_;
        StudioShellPanelCounts counts_;
    };
}
