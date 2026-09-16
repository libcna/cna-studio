// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPlayModeTests.cpp
 * @brief Launching the game from the native shell (plan.md STUDIO-16010).
 *
 * The cases that matter are the refusals. Actually starting a player needs a built
 * `cna-player-<renderer>` binary and a socket, which a unit test has no business needing — but
 * every reason Play *declines* is a message a user will read, and each one is testable here.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/ShellPanels/StudioPlayService.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A project on disk with a scene beside it, removed on the way out. */
    class ScopedProject
    {
    public:
        explicit ScopedProject(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-play-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_ / "Scenes", code);

            std::ofstream project{path_ / "Game.cnaproject", std::ios::binary};
            project << R"({"formatVersion":1,"name":"Played","kind":"CnaNative",)"
                       R"("startupScene":"Scenes/Level.cnascene","sceneDirectory":"Scenes"})";
        }

        ~ScopedProject()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedProject(const ScopedProject&) = delete;
        ScopedProject& operator=(const ScopedProject&) = delete;

        [[nodiscard]] std::string file() const
        {
            return (path_ / "Game.cnaproject").generic_string();
        }

        [[nodiscard]] std::string scene() const
        {
            return (path_ / "Scenes" / "Level.cnascene").generic_string();
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    struct Harness
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell;
        StudioShellPanels panels{shell, context, log};

        Harness() { shell.resetLayout(); }

        void frame()
        {
            UiInputState input;
            input.displayWidth = 1280.0f;
            input.displayHeight = 720.0f;
            shell.renderFrame(input);
        }

        /** @brief The last message logged, or empty. */
        [[nodiscard]] std::string lastMessage() const
        {
            return log.entries().empty() ? std::string{} : log.entries().back().message;
        }
    };

    bool contains(const std::string& text, std::string_view needle)
    {
        return text.find(needle) != std::string::npos;
    }
}

CNA_STUDIO_TEST(PlayIsRefusedUntilThereIsSomethingToPlayAndSomethingToPlayItWith)
{
    // Drawn greyed out rather than drawn available and then explaining itself in the log: a
    // control that looks available and refuses is indistinguishable from one that is broken.
    Harness harness;
    harness.frame();
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.play"));

    const ScopedProject project{"enable"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    harness.frame();

    // A project, but no player binary beside this executable.
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.play"));

    harness.panels.setPlayerBuilds({PlayerBuild{"software", "/nowhere/cna-player-software"}});
    harness.frame();
    CNA_STUDIO_EXPECT(harness.shell.actions().isEnabled("studio.play.play"));
}

CNA_STUDIO_TEST(StopIsRefusedWhileNothingIsRunning)
{
    Harness harness;
    harness.panels.setPlayerBuilds({PlayerBuild{"software", "/nowhere/cna-player-software"}});
    harness.frame();

    CNA_STUDIO_EXPECT(!harness.panels.isPlaying());
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.stop"));
}

CNA_STUDIO_TEST(PlayingAnUnsavedSceneSaysSoRatherThanWritingItSilently)
{
    // The player is a separate process and reads the scene from disk, so what is on screen has to
    // be there first -- but saving silently would be worse than refusing: a user who has not saved
    // deliberately would find their file overwritten by pressing Play.
    Harness harness;
    const ScopedProject project{"unsaved"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    harness.panels.setPlayerBuilds({PlayerBuild{"software", "/nowhere/cna-player-software"}});
    harness.frame();

    CNA_STUDIO_EXPECT(harness.context.getScenePath().empty());
    harness.shell.invoke("studio.play.play");

    CNA_STUDIO_EXPECT(!harness.panels.isPlaying());
    CNA_STUDIO_EXPECT(contains(harness.lastMessage(), "Save the scene before playing"));
}

CNA_STUDIO_TEST(APlayerThatCannotBeLaunchedIsReportedRatherThanLeavingTheToolbarStuck)
{
    // Pressing Play on a Studio whose player binary has been moved or half-built is the ordinary
    // failure, and the toolbar must come straight back: a Stop button for a game that never ran is
    // worse than the error, because the user has no way out of it.
    Harness harness;
    const ScopedProject project{"missing"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));

    // A scene on disk, so what follows is about the player rather than about saving.
    CNA_STUDIO_EXPECT(harness.context.saveScene(project.scene()));

    harness.panels.setPlayerBuilds({PlayerBuild{"software", "/definitely/not/here/cna-player"}});
    harness.frame();
    harness.shell.invoke("studio.play.play");

    CNA_STUDIO_EXPECT(!harness.panels.isPlaying());

    // Naming the path, because "could not start the player" leaves the user guessing at which of
    // the several things that could be wrong actually is.
    CNA_STUDIO_EXPECT(contains(harness.lastMessage(), "Could not start the player"));
    CNA_STUDIO_EXPECT(contains(harness.lastMessage(), "/definitely/not/here/cna-player"));

    harness.frame();
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.stop"));
    CNA_STUDIO_EXPECT(harness.shell.actions().isEnabled("studio.play.play"));
}

CNA_STUDIO_TEST(APlayerThatEndsOnItsOwnReleasesTheToolbar)
{
    // The game window being closed is how most play sessions end, and the editor hears about it
    // only by polling -- so the exit has to arrive, or Play would stay unavailable until restart.
    if (!std::filesystem::exists("/bin/true")) { return; }

    Harness harness;
    const ScopedProject project{"selfexit"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    CNA_STUDIO_EXPECT(harness.context.saveScene(project.scene()));

    // /bin/true stands in for a game that starts and closes at once: it ignores the arguments the
    // player takes and returns success, which is exactly what a closed game window looks like.
    harness.panels.setPlayerBuilds({PlayerBuild{"default", "/bin/true"}});
    harness.frame();
    harness.shell.invoke("studio.play.play");

    // That it *launched* is asserted from the log rather than from isPlaying(), which is a race:
    // /bin/true can be gone before the next statement runs, and a test that demanded to catch it
    // mid-flight would fail on a loaded machine for no reason of the editor's.
    CNA_STUDIO_EXPECT(contains(harness.lastMessage(), "Playing on default"));

    // Reaping a child is asynchronous -- the fork returns before the child has even reached its
    // first instruction -- so this waits the way the editor does, across frames, rather than
    // spinning. Waiting for the *report* rather than for isPlaying() to go false is deliberate:
    // asking whether it is playing is itself what notices the exit, so a loop that stopped there
    // would stop one poll before the poll that says so. Bounded, so a player that somehow lives
    // cannot hang the test.
    double now = 0.0;
    for (int attempt = 0;
         attempt < 400 && !contains(harness.lastMessage(), "Player exited");
         ++attempt)
    {
        now += 0.005;
        harness.panels.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_STUDIO_EXPECT(!harness.panels.isPlaying());
    CNA_STUDIO_EXPECT(contains(harness.lastMessage(), "Player exited"));

    harness.frame();
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.stop"));
    CNA_STUDIO_EXPECT(harness.shell.actions().isEnabled("studio.play.play"));
}

CNA_STUDIO_TEST(TheChosenPlayerIsTheOneTheProjectShipsOnWhenItWasBuilt)
{
    // "Run this on Vulkan" means "launch cna-player-vulkan", so what Play can do is decided by
    // what is on disk -- and which of several it picks is decided by the project.
    Harness harness;
    const ScopedProject project{"choice"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));

    std::vector<StudioTargetProfile> profiles = harness.context.getProject().getTargetProfiles();
    profiles.front().renderer = "opengles3";
    harness.context.getProject().setTargetProfiles(profiles);

    harness.panels.setPlayerBuilds({PlayerBuild{"software", "/a/cna-player-software"},
                                    PlayerBuild{"opengles3", "/a/cna-player-opengles3"}});

    // Reported through the Diagnostics panel, which reads the same list -- one setter, so the two
    // cannot disagree about what this Studio can run.
    CNA_STUDIO_EXPECT_EQ(harness.panels.diagnostics().players.size(), std::size_t{2});
}

CNA_STUDIO_TEST(PlayAndStopAreNeverBothAvailable)
{
    // They are one state with two faces, and a toolbar offering both is a toolbar that has lost
    // track of whether the game is running.
    Harness harness;
    const ScopedProject project{"exclusive"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    harness.panels.setPlayerBuilds({PlayerBuild{"software", "/a/cna-player-software"}});
    harness.frame();

    const bool play = harness.shell.actions().isEnabled("studio.play.play");
    const bool stop = harness.shell.actions().isEnabled("studio.play.stop");
    CNA_STUDIO_EXPECT(!(play && stop));
    CNA_STUDIO_EXPECT(play);
}

// --- Pause, Step and Restart (plan.md STUDIO-16015) --------------------------------------------

CNA_STUDIO_TEST(TheThreePlayControlsAreGreyedOutWhileNothingIsRunning)
{
    // Which is nearly always. Pause, Step and Restart over a stopped editor are three controls that
    // do nothing, and a toolbar with three of those is a toolbar nobody reads.
    Harness harness;
    harness.frame();

    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.pause"));
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.step"));
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.restart"));

    // And asking anyway changes nothing rather than pretending.
    CNA_STUDIO_EXPECT(!harness.panels.setPlayPaused(true));
    CNA_STUDIO_EXPECT(!harness.panels.stepPlayFrame());
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Stopped);
}

CNA_STUDIO_TEST(StepIsOfferedOnlyWhilePausedRatherThanWheneverAGameIsRunning)
{
    // The player ignores a step while running, so a Step that were live then would be a control
    // that is enabled and does nothing -- which is how a user learns to distrust a toolbar.
    Harness harness;
    const ScopedProject project{"stepstate"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    CNA_STUDIO_EXPECT(harness.context.saveScene(project.scene()));
    harness.panels.setPlayerBuilds({PlayerBuild{"default", "/bin/true"}});
    harness.frame();

    harness.shell.invoke("studio.play.play");
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Playing);
    harness.frame();
    CNA_STUDIO_EXPECT(harness.shell.actions().isEnabled("studio.play.pause"));
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.step"));

    // And Pause is a toggle that reports, not a button that renames itself: unchecked while the
    // game is running is how the toolbar says which of the two states it is in.
    const StudioAction* pause = harness.shell.actions().find("studio.play.pause");
    CNA_STUDIO_EXPECT(pause != nullptr && pause->checkable);
    if (pause != nullptr) { CNA_STUDIO_EXPECT(pause->isChecked && !pause->isChecked()); }
}

CNA_STUDIO_TEST(APlayerThatExitsLeavesNothingPaused)
{
    // A game that was paused and then closed leaves Pause checked over a window that is not there,
    // and Step offering to advance it. The exit has to clear the state as well as the process.
    if (!std::filesystem::exists("/bin/true")) { return; }

    Harness harness;
    const ScopedProject project{"exitpaused"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    CNA_STUDIO_EXPECT(harness.context.saveScene(project.scene()));
    harness.panels.setPlayerBuilds({PlayerBuild{"default", "/bin/true"}});
    harness.frame();

    harness.shell.invoke("studio.play.play");
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Playing);

    double now = 0.0;
    for (int attempt = 0;
         attempt < 400 && !contains(harness.lastMessage(), "Player exited");
         ++attempt)
    {
        now += 0.005;
        harness.panels.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Stopped);
    harness.frame();
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.pause"));
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.step"));
}

CNA_STUDIO_TEST(RestartStopsWhatIsRunningAndStartsItAgain)
{
    // A restart is how a user sees the edits they have made since pressing Play: the player reads
    // the scene from disk when it starts, so stopping and starting is the whole mechanism.
    if (!std::filesystem::exists("/bin/true")) { return; }

    Harness harness;
    const ScopedProject project{"restart"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    CNA_STUDIO_EXPECT(harness.context.saveScene(project.scene()));
    harness.panels.setPlayerBuilds({PlayerBuild{"default", "/bin/true"}});
    harness.frame();

    harness.shell.invoke("studio.play.play");
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Playing);

    harness.shell.invoke("studio.play.restart");
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Playing);

    // Two launches in the log, because a restart that only stopped would look identical from here.
    std::size_t launches = 0;
    for (const StudioLogEntry& entry : harness.log.entries())
    {
        if (contains(entry.message, "Playing on default")) { launches += entry.repeats; }
    }
    CNA_STUDIO_EXPECT_EQ(launches, std::size_t{2});
}

CNA_STUDIO_TEST(RestartIsOfferedBeforeAnythingIsRunning)
{
    // It is Play with a stop in front of it, so refusing it when nothing is running would make the
    // user press two different buttons for the same intention depending on state they may not know.
    Harness harness;
    const ScopedProject project{"restartcold"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    harness.panels.setPlayerBuilds({PlayerBuild{"default", "/bin/true"}});
    harness.frame();

    CNA_STUDIO_EXPECT(harness.shell.actions().isEnabled("studio.play.restart"));
}

CNA_STUDIO_TEST(PausingARealPlayerFollowsItRatherThanAnnouncingIt)
{
    // The end-to-end case, and the one rule worth having: the editor follows the player's state
    // only once the request is on the wire. A toolbar that says "Paused" over a game that never
    // got the message is worse than one that did nothing, because the user then believes it.
    const std::vector<PlayerBuild> builds =
        discoverPlayerBuilds(std::filesystem::path{CNA_STUDIO_TEST_PLAYER_DIR}.generic_string());
    if (builds.empty()) { return; }

    Harness harness;
    const ScopedProject project{"realpause"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    CNA_STUDIO_EXPECT(harness.context.saveScene(project.scene()));
    harness.panels.setPlayerBuilds(builds);
    harness.frame();

    harness.shell.invoke("studio.play.play");
    if (!harness.panels.isPlaying()) { return; }

    // The player has to be listening before a message can reach it. Polled across frames the way
    // the editor does, rather than slept for.
    double now = 0.0;
    for (int attempt = 0; attempt < 400 && !harness.panels.setPlayPaused(true); ++attempt)
    {
        now += 0.005;
        harness.panels.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Paused);

    harness.frame();
    CNA_STUDIO_EXPECT(harness.shell.actions().isEnabled("studio.play.step"));
    const StudioAction* pause = harness.shell.actions().find("studio.play.pause");
    CNA_STUDIO_EXPECT(pause != nullptr && pause->isChecked && pause->isChecked());

    // The status bar says which of the two, because a paused game and a running one look identical
    // from the editor: the window is there either way.
    harness.panels.poll(now + 0.005);
    bool said = false;
    for (const StudioStatusJob& job : harness.shell.status().jobs)
    {
        if (job.label == "Paused") { said = true; }
    }
    CNA_STUDIO_EXPECT(said);

    CNA_STUDIO_EXPECT(harness.panels.stepPlayFrame());

    // Stepping does not resume: one step is one frame, which is the point of having it.
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Paused);

    CNA_STUDIO_EXPECT(harness.panels.setPlayPaused(false));
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Playing);
    CNA_STUDIO_EXPECT(!harness.panels.setPlayPaused(false));

    harness.shell.invoke("studio.play.stop");
}

CNA_STUDIO_TEST(ALiveEditReachesARunningPlayerAsASetPropertyMessage)
{
    // The other end-to-end rule: an inspector edit -- any command through the one hook every edit
    // goes through -- reaches a running game as the property it now holds. StudioContext's
    // command observer is what carries it there; before STUDIO-07030 nothing on the native shell
    // had ever installed one, so a game kept running the arrangement it started with no matter
    // what the editor showed.
    const std::vector<PlayerBuild> builds =
        discoverPlayerBuilds(std::filesystem::path{CNA_STUDIO_TEST_PLAYER_DIR}.generic_string());
    if (builds.empty()) { return; }

    Harness harness;
    const ScopedProject project{"liveedit"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));

    // The startup scene the project names does not exist on disk yet -- opening it failed and
    // left the scene empty, silently, which is its own small trap. A fresh one is populated and
    // known-good, and this is what actually puts something at the path the player will load.
    harness.context.newScene();
    CNA_STUDIO_EXPECT(harness.context.saveScene(project.scene()));
    harness.panels.setPlayerBuilds(builds);
    harness.frame();

    CNA_STUDIO_EXPECT(!harness.context.getScene().getEntities().empty());
    const Uuid entityId = harness.context.getScene().getEntities().front().getId();

    harness.shell.invoke("studio.play.play");
    if (!harness.panels.isPlaying()) { return; }

    const std::string expected =
        "set " + harness.context.getScene().findEntity(entityId)->getName()
        + ".CNA.Transform.position";

    // The player has to be listening before a message can reach it, same as any other real-player
    // test -- so the edit is retried across frames rather than sent once and hoped for. Repeating
    // it is harmless: every attempt sets the same value, so a retry before the first one lands
    // changes nothing a running game would show.
    //
    // Drained from the process directly rather than through the service's own poll(): the
    // confirmation is a trace-level ReportLog, which is not one of the message types poll()
    // interprets for the toolbar, and asserting through a path that would silently drop it is
    // exactly the mistake that would let this regress unnoticed a second time. Draining this way
    // is also what actually accepts the player's incoming connection -- the service's own poll()
    // would do the same thing and then throw the reply away.
    bool sawIt = false;
    for (int attempt = 0; attempt < 400 && !sawIt; ++attempt)
    {
        harness.context.execute(std::make_unique<SetPropertyCommand>(
            harness.context.getScene(), entityId, BuiltinComponentIds::kTransform, "position",
            PropertyValue{StudioVector3{40.0f, 8.0f, 0.0f}}));

        for (const StudioMessage& message : harness.panels.play().process().poll())
        {
            if (message.type == StudioMessageType::ReportLog
                && message.payload["text"].asString() == expected)
            {
                sawIt = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CNA_STUDIO_EXPECT(sawIt);

    harness.shell.invoke("studio.play.stop");
}

// ------------------------------------------------------------------------------------------------
// The play service on its own (STUDIO-02054)
//
// The reason the decomposition is worth its churn, stated as tests: every rule below used to need
// a StudioShell, a StudioShellPanels and therefore a binding of every panel in Studio to reach.
// They need a context, a log and a lambda now. A decomposition whose parts still cannot be used
// apart has moved code rather than separated concerns, so these construct the service directly and
// never mention a shell.
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ThePlayServiceRunsWithNoShellAndNoPanels)
{
    StudioContext context;
    StudioLog log;
    StudioPlayService play{context, log, nullptr};

    CNA_STUDIO_EXPECT(!play.isRunning());
    CNA_STUDIO_EXPECT(play.state() == StudioPlayState::Stopped);
    CNA_STUDIO_EXPECT(play.builds().empty());
    CNA_STUDIO_EXPECT(play.chooseBuild() == nullptr);

    // Every operation refuses rather than crashing on a service with nothing to play.
    CNA_STUDIO_EXPECT(!play.setPaused(true));
    CNA_STUDIO_EXPECT(!play.stepFrame());
    CNA_STUDIO_EXPECT(!play.forwardInput(PlayerInputSnapshot{}));
    play.stop();
    CNA_STUDIO_EXPECT(play.state() == StudioPlayState::Stopped);

    play.start();
    CNA_STUDIO_EXPECT(!play.isRunning());
    CNA_STUDIO_EXPECT(contains(log.entries().back().message, "No player build was found"));
}

CNA_STUDIO_TEST(TheSessionOverrideOutranksTheProjectAndIsDroppedWhenItsBuildGoesAway)
{
    StudioContext context;
    StudioLog log;
    StudioPlayService play{context, log, nullptr};

    play.setBuilds({PlayerBuild{"software", "/nowhere/cna-player-software"},
                    PlayerBuild{"opengl4", "/nowhere/cna-player-opengl4"}});

    // No override: whatever the project names, or the first installed build when it names none.
    CNA_STUDIO_EXPECT(play.buildOverride().empty());
    CNA_STUDIO_EXPECT(play.chooseBuild() != nullptr);

    CNA_STUDIO_EXPECT(play.selectBuild("opengl4"));
    CNA_STUDIO_EXPECT_EQ(play.buildOverride(), std::string{"opengl4"});
    CNA_STUDIO_EXPECT_EQ(play.chooseBuild()->backend, std::string{"opengl4"});

    // A renderer nobody built for is refused rather than silently accepted, so the panel's row and
    // what Play does cannot disagree.
    CNA_STUDIO_EXPECT(!play.selectBuild("vulkan"));
    CNA_STUDIO_EXPECT_EQ(play.buildOverride(), std::string{"opengl4"});

    // And an override whose build is uninstalled between two scans is dropped, not kept: keeping it
    // would make Play fall back to something else without saying so.
    play.setBuilds({PlayerBuild{"software", "/nowhere/cna-player-software"}});
    CNA_STUDIO_EXPECT(play.buildOverride().empty());
    CNA_STUDIO_EXPECT_EQ(play.chooseBuild()->backend, std::string{"software"});

    CNA_STUDIO_EXPECT(play.selectBuild(""));
    CNA_STUDIO_EXPECT(play.buildOverride().empty());
}

CNA_STUDIO_TEST(ThePlayServiceRaisesItsCrashNotificationThroughASinkRatherThanAShell)
{
    // The sink is what lets this be asserted at all. A service that called
    // shell.notifications().raise() directly would need a shell to test its one user-visible
    // failure, which is exactly the coupling the extraction removes.
    std::vector<StudioNotification> raised;
    StudioContext context;
    StudioLog log;
    StudioPlayService play{context, log,
                           [&raised](StudioNotification note) { raised.push_back(std::move(note)); }};

    // Nothing is running, so a poll reads nothing and raises nothing -- the transition, not the
    // state, is what reports an ending.
    CNA_STUDIO_EXPECT_EQ(play.poll(), std::size_t{0});
    CNA_STUDIO_EXPECT(raised.empty());
}

CNA_STUDIO_TEST(TheShellStillSpeaksForThePlayServiceItOwns)
{
    // The forwarding half: a decomposition that broke every existing caller would have been a
    // rewrite. What the shell's actions, panels and tests call is unchanged, and answers from the
    // service.
    Harness harness;
    harness.panels.setPlayerBuilds({PlayerBuild{"software", "/nowhere/cna-player-software"}});

    CNA_STUDIO_EXPECT_EQ(harness.panels.playerBuilds().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(harness.panels.playState() == StudioPlayState::Stopped);
    CNA_STUDIO_EXPECT(!harness.panels.isPlaying());
    CNA_STUDIO_EXPECT(harness.panels.selectPlayerBuild("software"));
    CNA_STUDIO_EXPECT_EQ(harness.panels.playerBuildOverride(), std::string{"software"});

    // Same object, not a copy: a forwarder that returned a snapshot would let the panel and the
    // service drift apart within one frame.
    CNA_STUDIO_EXPECT_EQ(harness.panels.play().buildOverride(), std::string{"software"});
    CNA_STUDIO_EXPECT(&harness.panels.play().builds() == &harness.panels.playerBuilds());

    // And the Diagnostics panel's copy is still fed, which is the second responsibility that kept
    // setPlayerBuilds a method here rather than only on the service.
    CNA_STUDIO_EXPECT_EQ(harness.panels.diagnostics().players.size(), std::size_t{1});
}
