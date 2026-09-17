// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioShellPanels.hpp
 * @brief Binds every ported panel to a shell, in one place.
 *
 * `plan.md` STUDIO-07017, over the panel content seam of STUDIO-07016.
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

#include "CNA/Studio/Assets/AssetDocumentCache.hpp"
#include "CNA/Studio/Assets/AssetWatcher.hpp"
#include "CNA/Studio/Core/StudioJobs.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/ShellPanels/StudioBuildPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioComparisonPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioComparisonService.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioLayersPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioBuildService.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioPlayService.hpp"
#include "CNA/Studio/ShellPanels/StudioPreferencesPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioPreferencesService.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioProjectHubPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioRecovery.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace CNA::Studio
{
    class StudioAudio;
    class StudioCommand;
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

        /**
         * @brief The 3D editor camera, for the 3D view. Unset leaves that view unreachable.
         *
         * A second camera rather than one that switches projection, exactly as the prototype
         * keeps them: the 2D view's pan and zoom and the 3D view's orbit and distance are
         * different state, and a user who switches to 3D, looks around and switches back expects
         * to find the 2D view where they left it.
         */
        StudioCamera3D* camera3D = nullptr;

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
         * @brief Plays one clip at a time, for the Details panel's preview.
         *
         * `STUDIO-07044`. A seam for the same reason the clipboard and the image decoder are:
         * playing a sound needs CNA, exactly one module may link CNA (decision D-03), and the
         * panels have to keep working in a build that has neither. Unset draws the preview
         * disabled and says why, which is what a headless Studio can honestly offer.
         */
        StudioAudio* audio = nullptr;

        /**
         * @brief Resolves an image asset to a UI texture for a preview, or `kUiTextureNone`.
         *
         * `STUDIO-07043`. The sprite animation preview needs the sheet's pixels, and only the
         * module with a device can turn a file into a texture. Unset means this build shows the
         * frame's box and its texel range rather than the picture.
         */
        std::function<UiTextureId(const Uuid&)> assetThumbnail;

        /**
         * @brief Names the effect this build's model pass draws through.
         *
         * `STUDIO-07046`. Which effect a build got decides whether a material's metallic and
         * roughness reach the screen at all (CNA gap G-05), so the material editor says which one
         * rather than leaving a user wondering why a field does nothing. Unset leaves the line off.
         */
        std::function<std::string()> modelEffectName;

        /**
         * @brief Puts text on the system clipboard, returning whether it got there.
         *
         * A seam rather than a direct call, because the clipboard is behind a default-off CNA
         * option (gap G-02) and the CNA-free build has none at all. Unset means "no clipboard",
         * which the Output Log reports rather than silently doing nothing.
         */
        std::function<bool(const std::string&)> setClipboardText;

        /**
         * @brief Drops a rendered texture, so the next frame fetches the new one.
         *
         * `plan.md` STUDIO-30012. The asset watcher lives here rather than in the CNA-backed host,
         * so that "a file changed outside Studio is noticed within half a second" is true of *every*
         * build — including the headless preview and the tests, which is where the presence cache's
         * invalidation is actually checked. What the watcher cannot do itself is drop a texture,
         * because only the module with a device has one; unset means this build has none to drop.
         */
        std::function<void(const Uuid&)> invalidateRenderedAsset;
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
        std::size_t hubRecentRowsDrawn = 0;
        std::size_t hubTemplateRowsDrawn = 0;
        std::size_t comparisonRowsDrawn = 0;
        std::size_t preferenceChanges = 0;
        std::size_t playerMessages = 0;
        std::size_t brokenReferences = 0;
        std::size_t sceneErrors = 0;
        std::size_t sceneWarnings = 0;

        /** @brief How many plugin commands are on the menus. */
        std::size_t pluginMenuRows = 0;

        /** @brief How many audio preview controls the Details panel drew. */
        std::size_t audioPreviews = 0;

        /** @brief How many frames the sprite clip being previewed has, or zero. */
        std::size_t animationFrames = 0;

        /** @brief How many editable fields the Material panel drew. */
        std::size_t materialFields = 0;

        /** @brief How many ways the selected prefab instance differs from its prefab. */
        std::size_t prefabOverrides = 0;
    };

    /**
     * @brief The ported panels, bound to a shell and holding their retained state.
     *
     * Construct one beside the shell and it stays bound for the shell's life. It borrows the
     * shell, the context and the log, so all three must outlive it.
     */
    /**
     * @brief The pointer and the keys a running game is told about.
     *
     * **Only the keys a game plays with.** Forwarding every key the editor can name would send
     * Ctrl+S to the game as an S — which is exactly the sort of thing that gets blamed on the game
     * rather than on the editor that invented the keystroke.
     *
     * **The pointer only while it is over the viewport.** A cursor resting on the inspector is not
     * hovering the game, and reporting its last position there would leave the game acting on a
     * pointer that has not been near it for minutes. The snapshot then carries keys alone, which
     * is what a zero surface means on the wire.
     *
     * Free rather than a member, because it is the part with the rules in it and a function is
     * what a test can ask about without starting a process.
     *
     * @param frame The frame whose input is being read.
     * @param bounds The viewport panel's body, which the pointer is measured against.
     * @param pointerInside Whether the pointer is over that body at all.
     * @return The snapshot to send.
     */
    [[nodiscard]] PlayerInputSnapshot studioPlayerInputFrom(const StudioFrame& frame,
                                                            const UiRect& bounds,
                                                            bool pointerInside);

    class StudioShellPanels;

    /**
     * @brief Gives @p panels the templates this build ships and the user's recent-projects list.
     *
     * One function rather than two call sites' worth of setup, because there are two hosts — the
     * CNA-backed editor and the headless shell preview — and a Hub that offered different
     * templates in the one CI photographs would make the picture a lie.
     *
     * A Studio whose templates were not installed still gets a Hub: the catalogue comes back empty
     * and the panel says so, which is a diagnosis rather than a missing panel.
     *
     * @param panels The panels to configure.
     * @param executablePath `argv[0]`, for the install-relative template search paths.
     * @param log Where a manifest that could not be read is reported.
     */
    void bindStudioProjectHub(StudioShellPanels& panels, std::string_view executablePath,
                              StudioLog& log);

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
         * @brief The background job system (`plan.md` STUDIO-30001).
         *
         * Handed to whatever needs to do work off the frame, as a constructor argument rather than
         * found through a locator (`docs/ARCHITECTURE.md` §10.1). Drained once per @ref poll.
         */
        [[nodiscard]] StudioJobSystem& jobs() { return jobs_; }
        [[nodiscard]] const StudioJobSystem& jobs() const { return jobs_; }

        /**
         * @brief Hands the viewport its camera once a device exists to own one.
         *
         * Separate from the constructor because the panels are bound before the graphics device is
         * created — that is what lets the headless preview bind the same panels — and the camera
         * belongs to whoever renders the scene.
         *
         * @param camera The 2D editor camera; must outlive this.
         * @param camera3D The 3D editor camera; must outlive this.
         * @param spriteSize Resolves a sprite's texel size for picking.
         */
        void setViewportServices(StudioCamera2D& camera, StudioCamera3D& camera3D,
                                 SpriteSizeProvider spriteSize);

        /**
         * @brief Hands the Project Hub the templates it offers and the list it remembers.
         *
         * Separate from the constructor because both depend on where the executable is, which the
         * panels do not know and should not go looking for — the same argument the viewport's
         * camera is handed in on.
         *
         * A Studio that is never given these still draws a Hub: it says there are no templates,
         * which is the honest picture of a build whose templates were not installed, rather than a
         * panel that is missing.
         *
         * @param templates The templates on offer. Copied; the Hub outlives whoever built it.
         * @param recentProjectsPath Where the recent list is stored. Empty remembers nothing.
         */
        void setProjectHubServices(StudioTemplateCatalogue templates,
                                   std::string recentProjectsPath);

        /** @brief The Hub's retained state: the page, the selection and the half-typed form. */
        [[nodiscard]] StudioProjectHubState& projectHubState() { return hubState_; }

        /** @brief The Hub's retained state. */
        [[nodiscard]] const StudioProjectHubState& projectHubState() const { return hubState_; }

        /** @brief The templates the Hub offers. */
        [[nodiscard]] const StudioTemplateCatalogue& projectTemplates() const { return templates_; }

        /**
         * @brief Records that a project was opened, so the Hub lists it next time.
         *
         * Called by whoever actually opens one — the Hub itself, `--project`, and the Open Project
         * command — rather than from inside `StudioContext::openProject`, because the context is
         * the CNA-free document model and a list of recently opened files is not its business.
         *
         * @param projectFilePath Absolute path of the `.cnaproject`.
         * @param nowSeconds Seconds since the epoch, passed in like every other clock here.
         */
        void rememberProject(const std::string& projectFilePath, std::int64_t nowSeconds);

        /**
         * @brief Switches the viewport to the view the open project asks to open in.
         *
         * `plan.md` STUDIO-11014. Public because a host that opened a project with `--project`
         * has to make the same switch the Hub does, and a project's answer must not depend on
         * which of the two routes opened it.
         *
         * @param shell The shell whose view commands are invoked.
         */
        void applyProjectDefaultViewOnOpen(StudioShell& shell) { applyProjectDefaultView(shell); }

        /**
         * @brief Hands a running game the pointer and the keys.
         *
         * Sent only when something changed, because the player answers every snapshot: sixty
         * identical ones a second would be sixty round trips that told it nothing, doubled.
         *
         * @param snapshot What the pointer and the forwarded keys are doing.
         * @return True when a message went on the wire.
         */
        bool forwardInputToPlayer(const PlayerInputSnapshot& snapshot)
        {
            return play_.forwardInput(snapshot);
        }

        /** @brief The last snapshot actually sent, for tests and for the deduplication above. */
        [[nodiscard]] const PlayerInputSnapshot& lastForwardedInput() const
        {
            return play_.lastForwardedInput();
        }

        /**
         * @brief Which sprite frame the Details panel is previewing, for the host to draw.
         *
         * `STUDIO-07043`. A snapshot the panel produced, not playback state this object owns: the
         * Details panel keeps deciding when time passes, and the viewport only has to know what to
         * draw. Inactive when nothing is being previewed, which is what makes a sprite go back to
         * its own frame the moment the selection changes.
         */
        [[nodiscard]] const AnimationPreview& animationPreview() const { return animation_; }

        /** @brief Which projection the viewport is showing, so the host renders the same one. */
        [[nodiscard]] StudioViewportView viewportView() const { return viewportState_.view; }

        /** @brief Which manipulator the viewport shows, so the renderer draws the same one. */
        [[nodiscard]] GizmoMode viewportMode() const { return viewportState_.mode; }

        /** @brief Which frame the translate manipulator's arms follow. */
        [[nodiscard]] GizmoSpace viewportSpace() const { return viewportState_.space; }

        /**
         * @brief Whether the 3D grid lies on the ground plane rather than the scene's own.
         *
         * `STUDIO-07056`. Read by the host that builds the wireframe, which cannot read the
         * preference directly: the preference is a boolean in `cna-studio-ui-core` and `GridPlane`
         * lives in `cna-studio-scene`, and this is the object that already sees both.
         */
        [[nodiscard]] bool viewportGridOnGroundPlane() const
        {
            return viewportState_.gridOnGroundPlane;
        }

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

        /**
         * @brief The game this Studio plays: the process, its state and its rules.
         *
         * `STUDIO-02054`. Handed out rather than wrapped method by method — a wrapper per operation
         * would be six functions that do nothing, which is the failure mode of a decomposition
         * rather than the point of one. The few members still forwarded below are the ones with a
         * second responsibility here: `setPlayerBuilds` also feeds the Diagnostics panel, and
         * `isPlaying`, `playState` and the three commands are what the bound actions and the
         * existing tests already call.
         */
        [[nodiscard]] StudioPlayService& play() { return play_; }
        /** @brief The game this Studio plays. */
        [[nodiscard]] const StudioPlayService& play() const { return play_; }

        /** @brief Whether a player is running right now. */
        [[nodiscard]] bool isPlaying() const { return play_.isRunning(); }

        /**
         * @brief Chooses the renderer Play launches on, for this session.
         *
         * @param backend An installed build's backend name, or empty to go back to whatever the
         *        project's target profile names.
         * @return True when the choice was applied; false when no such build is installed.
         */
        bool selectPlayerBuild(const std::string& backend) { return play_.selectBuild(backend); }

        /** @brief The session's renderer override, or empty when the project decides. */
        [[nodiscard]] const std::string& playerBuildOverride() const { return play_.buildOverride(); }

        /** @brief The player builds installed beside this Studio. */
        [[nodiscard]] const std::vector<PlayerBuild>& playerBuilds() const { return play_.builds(); }

        /** @brief Whether a player is running and paused. */
        [[nodiscard]] StudioPlayState playState() const { return play_.state(); }

        /**
         * @brief Asks the running game to pause or resume.
         *
         * Follows the player's state only once the request is on the wire: a toolbar that says
         * "Paused" over a game that never got the message is worse than one that did nothing.
         *
         * @param paused True to pause.
         * @return True when the request was sent.
         */
        bool setPlayPaused(bool paused) { return play_.setPaused(paused); }

        /**
         * @brief Asks a paused game to advance one frame.
         *
         * Only means something while paused; the player ignores it otherwise, and offering it
         * while the game is running would suggest a control that does nothing.
         *
         * @return True when the request was sent.
         */
        bool stepPlayFrame() { return play_.stepFrame(); }

        /** @brief Stops the running game and starts it again from the scene as it now stands. */
        void restartPlaying() { play_.restart(); }

        /**
         * @brief The renderer comparison, whole.
         *
         * Handed out rather than forwarded to, like `play()` and `builds()`: a method per
         * operation would be six that do nothing, which is what `STUDIO-02050` forbids.
         */
        [[nodiscard]] StudioComparisonService& comparisons() { return comparison_; }
        /** @brief The renderer comparison. */
        [[nodiscard]] const StudioComparisonService& comparisons() const { return comparison_; }

        /**
         * @brief The user's preferences, edited by the Preferences panel.
         *
         * The one forwarder the preferences service keeps, for the reason `build()` keeps one onto
         * `BuildProcess`: this is the thing every panel and every test already spells, and the
         * service exists to own the *rules* around it rather than to be spelled instead of it.
         */
        [[nodiscard]] StudioPreferences& preferences() { return preferences_.model(); }

        /** @brief The user's preferences. */
        [[nodiscard]] const StudioPreferences& preferences() const { return preferences_.model(); }

        /**
         * @brief The preferences, their sink and the rules for applying them.
         *
         * Handed out whole rather than forwarded to, like `play()`, `builds()` and
         * `comparisons()`: a method per operation would be three that do nothing.
         */
        [[nodiscard]] StudioPreferencesService& userPreferences() { return preferences_; }
        /** @brief The preferences service. */
        [[nodiscard]] const StudioPreferencesService& userPreferences() const { return preferences_; }

        /**
         * @brief Crash recovery: the snapshot timer, and whatever a previous session left.
         *
         * Exposed so a host can point it at a directory other than the default -- which is what
         * `--recovery-dir` is for, and the only way a test can have one that is not the user's.
         */
        [[nodiscard]] StudioRecoverySession& recovery() { return recovery_; }

        /** @brief Crash recovery. */
        [[nodiscard]] const StudioRecoverySession& recovery() const { return recovery_; }


        /** @brief The build this Studio would run. */
        [[nodiscard]] BuildProcess& build() { return build_.process(); }

        /**
         * @brief Building the user's game and packaging it. `STUDIO-02055`.
         *
         * Handed out for the same reason the play service is: a forwarder per operation would be
         * code that does nothing. `build()` stays as it was because every panel and test already
         * spells it that way and it names the process rather than the service.
         */
        [[nodiscard]] StudioBuildService& builds() { return build_; }
        /** @brief Building the user's game and packaging it. */
        [[nodiscard]] const StudioBuildService& builds() const { return build_; }

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

        /** @brief The player binary Play would launch, or nullptr when none was found. */


        /** @brief Pumps the bridge once a frame and reports what the player said. */

        /**
         * @brief Raises @p notification, or logs it when there is no shell to raise it on.
         *
         * The one way these panels announce anything, so a message cannot be lost by being posted
         * somewhere that is not there -- which is what a preview and a headless test both are.
         */
        void notify(StudioNotification notification);

        /**
         * @brief Mirrors a property edit to a running game, if @p command is one and one is.
         *
         * The document's context registers this once, in the constructor, against every command
         * that lands -- an inspector edit, a gizmo drag, an undo -- so a running game reflects
         * what the editor shows without every editing surface having to remember to say so.
         */
        void mirrorCommandToPlayer(const StudioCommand& command);

        /**
         * @brief Drives crash recovery: the snapshot timer, and the offer after a project opens.
         * @param nowSeconds The host's clock, which this differences into an interval.
         */
        void pollRecovery(double nowSeconds);

        /**
         * @brief Starts quitting: asks about unsaved changes, or closes.
         *
         * Here rather than in `bindStudioShellActions` because the answer arrives a frame later,
         * and this is the object with a poll to read it in.
         */
        void requestQuit();

        /** @brief Asks the shell's host to close, and says so when nothing can. */
        void closeStudio();

        /**
         * @brief Opens a project the Hub named, and brings the viewport forward when it works.
         *
         * The last two steps of the user story the Project Hub exists for: a project that opens
         * has a world in it, and leaving the Hub in front of it would make the next thing the user
         * does be closing a tab. A project that does *not* open leaves the Hub where it is, with
         * the reason in the log, because sending somebody to an empty viewport to be told nothing
         * is the worse answer.
         *
         * @param shell The shell, for the tab to raise.
         * @param projectFilePath Absolute path of the `.cnaproject`.
         */
        void openProjectFromHub(StudioShell& shell, const std::string& projectFilePath);

        /**
         * @brief Switches the viewport to the view the open project asks to open in.
         *
         * `plan.md` STUDIO-11014. Through the view command rather than by assigning the state, so
         * the camera is framed on the scene and the log says which view is on -- the difference
         * between opening a 3D project and opening a 3D view of nothing.
         */
        void applyProjectDefaultView(StudioShell& shell);

        /** @brief Rebuilds the plugin menus when the extension registry has moved on. */
        /** @brief Closes the undo merge chain on the first frame nothing is being dragged. */
        void pollInteractionEnd();

        /** @brief Polls the asset watcher, which also refreshes the presence cache. */
        void pollAssetChanges(double nowSeconds);

        void pollPlugins();


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

        /** @brief The frame the Details panel is previewing, for the host's scene render. */
        AnimationPreview animation_;

        /**
         * @brief Whether the 3D camera has been placed over the scene.
         *
         * The first switch to 3D frames the scene and no later one does: the default camera looks
         * straight down an axis, and framing every time would throw away an angle the user set up.
         */
        bool framedIn3D_ = false;

        StudioTreeState outlinerState_;
        StudioContentBrowserState contentState_;

        /**
         * @brief The asset reference graph the Details panel's dependency section reads
         *        (`plan.md` STUDIO-09012).
         *
         * Built lazily and kept, because building it reads every scene, prefab and material in the
         * project. `dependenciesStale_` is set by any command and by a project opening; the rebuild
         * happens the next time an asset is inspected, which is the only moment the answer is
         * looked at. `STUDIO-30001`'s background jobs are what will let it be rebuilt without the
         * pause; until then the pause is paid once per change, by the user who asked.
         */
        AssetDependencyIndex dependencies_;
        bool dependenciesStale_ = true;

        /**
         * @brief Materials and prefabs the Details panel shows (`plan.md` STUDIO-30016).
         *
         * Owned here rather than by the panel, because when a file is re-read is a policy and the
         * function that draws a section is the wrong place for one. Dropped on any command, which
         * is coarse on purpose: too much costs one reload of what is on screen, and too little is
         * an editor showing a file it has already overwritten.
         */
        StudioAssetDocumentCache documents_;

        /** @brief Returns the index, rebuilding it first when something has changed. */
        [[nodiscard]] const AssetDependencyIndex* dependencyIndex();

        /**
         * @brief The one place background work happens (`plan.md` STUDIO-30001).
         *
         * Owned here rather than by each subsystem that wants it, which is the whole point: threads
         * scattered across subsystems agree about nothing, are joined by nobody, and are one
         * careless line from touching the document while the UI draws it. Its completions run on
         * the main thread from @ref poll, so a job's result reaches the document between frames.
         */
        StudioJobSystem jobs_;

        /**
         * @brief Notices assets changed outside Studio (`plan.md` STUDIO-07051, STUDIO-30012).
         *
         * Here rather than in the CNA-backed host, where it used to be: it is what refreshes the
         * asset database's presence cache, and a cache whose invalidation only ran in one of the
         * two builds would be a cache that is right in one of them.
         */
        AssetWatcher watcher_;

        /** @brief The previous `poll()` timestamp, so the watcher gets a delta. Negative until one. */
        double lastPollSeconds_ = -1.0;

        StudioProblemsState problemsState_;
        StudioTreeState historyState_;
        StudioTreeState layersState_;
        StudioTreeState diagnosticsState_;
        StudioTreeState comparisonState_;
        StudioShortcutEditorState shortcutEditor_;
        StudioProjectHubState hubState_;

        /** @brief The templates the Hub offers, and where the recent list is kept. */
        StudioTemplateCatalogue templates_;
        std::string recentProjectsPath_;

        /** @brief Snapshots of the open scene, and whatever a previous session left behind. */
        StudioRecoverySession recovery_{context_};

        /** @brief The host clock at the last poll, or negative before the first. */
        double recoveryLastSeconds_ = -1.0;

        /** @brief The project the last scan was for, so opening another triggers a new one. */
        std::string recoveryProject_;

        /**
         * @brief The plugin registry's revision when the menus were last built.
         *
         * Starts at zero, which is also a registry nobody has touched -- so a Studio with no
         * plugins never rebuilds, and one whose plugins loaded before these panels existed rebuilds
         * on its first poll.
         */
        std::uint64_t pluginRevision_ = 0;

        StudioViewportState viewportState_;

        StudioBuildService build_;
        std::unique_ptr<StudioBuildPanel> buildPanel_;

        /**
         * @brief The game this Studio plays.
         *
         * Owned here for the same reason the build process is: two of either would be two
         * processes racing for one project, and the panel that reports on it must be reporting on
         * the one Play actually started.
         */
        StudioPlayService play_;

        /**
         * @brief The renderer comparison.
         *
         * Owned here rather than by its panel, so a run started from the panel survives the panel
         * being closed — half an hour of launching several games is not something to abandon
         * because a user switched tabs.
         */
        StudioComparisonService comparison_;

        /**
         * @brief What the user decided Studio should be like.
         *
         * Held here for the same reason the build is: the panel describes them and something else
         * persists them, and two copies would be two answers to what the user decided.
         */
        StudioPreferencesService preferences_;

        StudioDiagnosticsInfo diagnostics_;
        StudioShellPanelCounts counts_;
    };
}
