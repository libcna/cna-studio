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
    CNA_STUDIO_EXPECT(harness.panels.isPlaying());
    CNA_STUDIO_EXPECT(harness.shell.actions().isEnabled("studio.play.stop"));

    // Reaping a child is asynchronous -- the fork returns before the child has even reached its
    // first instruction -- so this waits the way the editor does, across frames, rather than
    // spinning. Waiting for the *report* rather than for isPlaying() to go false is deliberate:
    // asking whether it is playing is itself what notices the exit, so a loop that stopped there
    // would stop one poll before the poll that says so. Bounded, so a player that somehow lives
    // cannot hang the test.
    for (int attempt = 0;
         attempt < 400 && !contains(harness.lastMessage(), "Player exited");
         ++attempt)
    {
        harness.panels.poll();
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
