// SPDX-License-Identifier: MS-PL
/**
 * @file StudioRecoveryTests.cpp
 * @brief Crash recovery on the native shell (plan.md STUDIO-06014, STUDIO-07020).
 *
 * The inventory listed crash recovery as unanswered natively and it was, in a way that was easy to
 * miss: the snapshot format, the store and the atomic write have always been shared, so everything
 * *about* recovery worked. What did not exist was anyone running it. The Dear ImGui host wrote
 * snapshots and offered what it found; the native host runs a different loop and did neither, so a
 * user on `--ui=studio` had no crash recovery at all and nothing said so.
 *
 * These drive the flow through the panels, which is where the native host drives it: the snapshot
 * appearing on disk, the offer after a project opens, and the two commands being greyed out when
 * there is nothing to answer for -- a Discard that is live when there is nothing to discard is a
 * row a user has to read twice to be sure of.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/StudioRecovery.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace CNA::Studio;

namespace
{
    /** @brief A project and a recovery directory in a scratch place, removed on the way out. */
    class Scratch
    {
    public:
        explicit Scratch(const std::string& name)
        {
            root_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-recovery-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(root_, code);
            std::filesystem::create_directories(root_ / "snapshots", code);
        }

        ~Scratch()
        {
            std::error_code code;
            std::filesystem::remove_all(root_, code);
        }

        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;

        [[nodiscard]] std::string snapshots() const
        {
            return (root_ / "snapshots").generic_string();
        }

        [[nodiscard]] std::string projectPath() const
        {
            return (root_ / "Game.cnaproject").generic_string();
        }

        [[nodiscard]] std::size_t snapshotCount() const
        {
            std::size_t files = 0;
            std::error_code code;
            for (const auto& entry : std::filesystem::directory_iterator{root_ / "snapshots", code})
            {
                if (entry.is_regular_file()) { ++files; }
            }
            return files;
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path root_;
    };


    /**
     * @brief A second Studio over the same project and snapshot directory.
     *
     * Which is what a crash looks like from the next launch, and the only way to test the offer:
     * `scan` reads the disk, so nothing short of a second editor exercises it.
     */
    struct SecondStudio
    {
        StudioContext context;
        StudioLog log;
        std::unique_ptr<StudioShell> shell = std::make_unique<StudioShell>(StudioTheme::dark());
        StudioCamera2D camera;
        std::unique_ptr<StudioShellPanels> panels;

        SecondStudio(const std::string& snapshots, const std::string& project, int autosaveSeconds)
        {
            context.setLogSink([this](LogSeverity severity, const std::string& message) {
                log.append(severity, message);
            });

            shell->resetLayout();
            (void)bindStudioShellActions(*shell, context, log);
            panels = std::make_unique<StudioShellPanels>(*shell, context, log);
            panels->setViewportServices(camera, {});
            panels->recovery().setDirectory(snapshots);
            panels->preferences().autosaveSeconds = autosaveSeconds;
            (void)context.openProject(project);
        }

        void poll(double seconds)
        {
            clock_ += seconds;
            panels->poll(clock_);
        }

    private:
        double clock_ = 0.0;
    };

    /** @brief A context with a project on disk, which is what recovery is keyed by. */
    struct Fixture
    {
        Scratch scratch;
        StudioContext context;
        StudioLog log;
        std::unique_ptr<StudioShell> shell = std::make_unique<StudioShell>(StudioTheme::dark());
        StudioCamera2D camera;
        std::unique_ptr<StudioShellPanels> panels;

        explicit Fixture(const std::string& name) : scratch(name)
        {
            context.setLogSink([this](LogSeverity severity, const std::string& message) {
                log.append(severity, message);
            });
            {
                std::ofstream stream{std::filesystem::path{scratch.projectPath()},
                                     std::ios::binary | std::ios::trunc};
                stream << R"({"formatVersion":1,"name":"Game","kind":"CnaNative",)"
                          R"("assetDirectory":"Assets"})";
            }
            (void)context.openProject(scratch.projectPath());

            shell->resetLayout();
            (void)bindStudioShellActions(*shell, context, log);
            panels = std::make_unique<StudioShellPanels>(*shell, context, log);
            panels->setViewportServices(camera, {});
            panels->recovery().setDirectory(scratch.snapshots());
            panels->preferences().autosaveSeconds = 1;

            // One poll to start the clock. The first interval is always zero -- there is no
            // previous frame to difference against -- which is right, and means a test that wants
            // time to pass has to let a frame happen first, exactly as the editor does.
            poll(0.0);
        }

        /** @brief Polls as the host does: a clock that advances by @p seconds. */
        void poll(double seconds)
        {
            clock_ += seconds;
            panels->poll(clock_);
        }

        [[nodiscard]] bool enabled(const std::string& id) const
        {
            const StudioAction* action = shell->actions().find(id);
            return action != nullptr && (!action->isEnabled || action->isEnabled());
        }

    private:
        double clock_ = 0.0;
    };
}

CNA_STUDIO_TEST(TheNativeShellWritesSnapshotsWhileTheSceneIsUnsaved)
{
    // The half that has to work before the crash. Without it the offer below has nothing to offer.
    Fixture fixture{"writes"};
    CNA_STUDIO_EXPECT_EQ(fixture.scratch.snapshotCount(), std::size_t{0});

    // Dirty, which is the only state a snapshot could rescue.
    fixture.context.getScene().setName("Level01");
    fixture.context.getHistory().markUnsaved();
    CNA_STUDIO_EXPECT(fixture.context.getHistory().isDirty());

    fixture.poll(2.0);
    CNA_STUDIO_EXPECT_EQ(fixture.scratch.snapshotCount(), std::size_t{1});
}

CNA_STUDIO_TEST(ASnapshotIsDroppedOnceTheDocumentMatchesItsFile)
{
    // Otherwise the next start-up offers to recover work that is already saved, which teaches users
    // to dismiss the offer without reading it -- and then to dismiss the one that mattered.
    Fixture fixture{"drops"};
    fixture.context.getHistory().markUnsaved();
    fixture.poll(2.0);
    CNA_STUDIO_EXPECT_EQ(fixture.scratch.snapshotCount(), std::size_t{1});

    fixture.context.getHistory().markSaved();
    fixture.poll(0.1);
    CNA_STUDIO_EXPECT_EQ(fixture.scratch.snapshotCount(), std::size_t{0});
}

CNA_STUDIO_TEST(AnAutosaveIntervalOfZeroWritesNothing)
{
    // The Preferences panel offers zero and says what it means. A setting that says "0 for none"
    // and snapshots anyway is worse than one that is not offered.
    Fixture fixture{"off"};
    fixture.panels->preferences().autosaveSeconds = 0;
    fixture.context.getHistory().markUnsaved();

    for (int i = 0; i < 100; ++i) { fixture.poll(1.0); }
    CNA_STUDIO_EXPECT_EQ(fixture.scratch.snapshotCount(), std::size_t{0});
}

CNA_STUDIO_TEST(WorkFromAPreviousSessionIsOfferedRatherThanFound)
{
    // The case the whole feature is for: Studio died, and the next one has to *say so* rather than
    // leaving a file for somebody to notice.
    Fixture first{"offer"};
    first.context.getScene().setName("Level01");
    first.context.getHistory().markUnsaved();
    first.poll(2.0);
    CNA_STUDIO_EXPECT_EQ(first.scratch.snapshotCount(), std::size_t{1});

    // A second Studio over the same project and the same snapshot directory.
    SecondStudio second{first.scratch.snapshots(), first.scratch.projectPath(), 0};

    CNA_STUDIO_EXPECT(!second.panels->recovery().hasRecoverable());
    second.poll(0.0);
    CNA_STUDIO_EXPECT(second.panels->recovery().hasRecoverable());

    // Announced, and stickily: it arrives at start-up, when the log has just filled with everything
    // else start-up says, and it is the one message whose point is that nobody was there.
    const std::vector<StudioNotification> showing = second.shell->notifications().showing();
    CNA_STUDIO_EXPECT(showing.size() == 1);
    if (!showing.empty())
    {
        CNA_STUDIO_EXPECT(showing.front().isSticky());
        CNA_STUDIO_EXPECT(showing.front().actionId == "studio.file.recoverScene");
        CNA_STUDIO_EXPECT(showing.front().detail.find("Level01") != std::string::npos);
    }

    // And the scene is not silently replaced: the user decides.
    CNA_STUDIO_EXPECT(second.context.getScene().getName() != "Level01");
}

CNA_STUDIO_TEST(RecoveringTakesTheOfferAwayAndLeavesTheWorkUnsaved)
{
    Fixture first{"recover"};
    first.context.getScene().setName("Level01");
    first.context.getHistory().markUnsaved();
    first.poll(2.0);

    SecondStudio second{first.scratch.snapshots(), first.scratch.projectPath(), 0};
    second.poll(0.0);

    CNA_STUDIO_EXPECT(second.shell->actions().find("studio.file.recoverScene") != nullptr);
    second.shell->invoke("studio.file.recoverScene");

    CNA_STUDIO_EXPECT(second.context.getScene().getName() == "Level01");
    CNA_STUDIO_EXPECT(!second.panels->recovery().hasRecoverable());
    CNA_STUDIO_EXPECT(second.shell->notifications().empty());

    // Never saved anywhere, so the history must not claim otherwise -- that is how a user closes
    // the editor believing the work is on disk.
    CNA_STUDIO_EXPECT(second.context.getHistory().isDirty());
}

CNA_STUDIO_TEST(DiscardingRemovesTheSnapshotAndTheOffer)
{
    Fixture first{"discard"};
    first.context.getHistory().markUnsaved();
    first.poll(2.0);
    CNA_STUDIO_EXPECT_EQ(first.scratch.snapshotCount(), std::size_t{1});

    SecondStudio second{first.scratch.snapshots(), first.scratch.projectPath(), 0};
    second.poll(0.0);
    CNA_STUDIO_EXPECT(second.panels->recovery().hasRecoverable());

    second.shell->invoke("studio.file.discardRecovered");
    CNA_STUDIO_EXPECT(!second.panels->recovery().hasRecoverable());
    CNA_STUDIO_EXPECT(second.shell->notifications().empty());
    CNA_STUDIO_EXPECT_EQ(first.scratch.snapshotCount(), std::size_t{0});
}

CNA_STUDIO_TEST(BothCommandsAreGreyedOutWhenThereIsNothingToAnswerFor)
{
    // Which is nearly always. A Discard that is live when there is nothing to discard is a row the
    // user has to read twice, every time they open the File menu.
    Fixture fixture{"greyed"};

    CNA_STUDIO_EXPECT(!fixture.enabled("studio.file.recoverScene"));
    CNA_STUDIO_EXPECT(!fixture.enabled("studio.file.discardRecovered"));

    // And invoking one anyway changes nothing, because the shell refuses a disabled command.
    fixture.shell->invoke("studio.file.discardRecovered");
    CNA_STUDIO_EXPECT(!fixture.panels->recovery().hasRecoverable());
}

CNA_STUDIO_TEST(BothCommandsAreOnTheFileMenuWhereTheLogSaysTheyAre)
{
    // The log's own message names them: "File > Recover Unsaved Scene restores them". A message
    // naming a menu row that is not there is worse than no message.
    StudioShell shell{StudioTheme::dark()};

    bool recover = false;
    bool discard = false;
    for (const StudioMenuDefinition& menu : shell.menus())
    {
        if (menu.title != "File") { continue; }
        for (const StudioMenuEntry& entry : menu.entries)
        {
            if (entry.id == "studio.file.recoverScene") { recover = true; }
            if (entry.id == "studio.file.discardRecovered") { discard = true; }
        }
    }
    CNA_STUDIO_EXPECT(recover);
    CNA_STUDIO_EXPECT(discard);
}

CNA_STUDIO_TEST(AutosaveIsSuspendedWhileWorkFromAPreviousSessionIsWaiting)
{
    // The snapshot file is keyed by scene id, so writing this session's would overwrite the one the
    // user has not answered for yet -- trading their unsaved hours for our unsaved seconds.
    Fixture first{"suspend"};
    first.context.getScene().setName("Level01");
    first.context.getHistory().markUnsaved();
    first.poll(2.0);

    SecondStudio second{first.scratch.snapshots(), first.scratch.projectPath(), 1};
    second.poll(0.0);
    CNA_STUDIO_EXPECT(second.panels->recovery().hasRecoverable());

    // The recovered scene's id, so this is the collision rather than a different scene.
    second.context.getScene().setSceneId(second.panels->recovery().recoverable()->sceneId);
    second.context.getHistory().markUnsaved();
    for (int i = 1; i <= 10; ++i) { second.poll(static_cast<double>(i) * 2.0); }

    bool said = false;
    for (const StudioLogEntry& entry : second.log.entries())
    {
        if (entry.message.find("Autosave is suspended") != std::string::npos) { said = true; }
    }
    CNA_STUDIO_EXPECT(said);

    // Said once, however long it goes on: an editor that repeats a warning every interval is one
    // whose console nobody reads.
    for (const StudioLogEntry& entry : second.log.entries())
    {
        if (entry.message.find("Autosave is suspended") != std::string::npos)
        {
            CNA_STUDIO_EXPECT_EQ(entry.repeats, std::size_t{1});
        }
    }
}

CNA_STUDIO_TEST(TheOfferIsMadeAgainWhenAnotherProjectIsOpened)
{
    // The scan is driven by the project path changing rather than by whoever opened it, so it
    // happens however a project arrives: the command, the command line, or a host that opened one
    // before these panels existed.
    Fixture first{"reopen"};
    first.context.getHistory().markUnsaved();
    first.poll(2.0);

    Fixture second{"reopen-other"};
    CNA_STUDIO_EXPECT(!second.panels->recovery().hasRecoverable());
    second.poll(0.0);
    CNA_STUDIO_EXPECT(!second.panels->recovery().hasRecoverable());

    second.panels->recovery().setDirectory(first.scratch.snapshots());
    (void)second.context.openProject(first.scratch.projectPath());
    second.poll(0.1);
    CNA_STUDIO_EXPECT(second.panels->recovery().hasRecoverable());
}

CNA_STUDIO_TEST(TurningAutosaveOffDoesNotHideWorkThatIsAlreadyOnDisk)
{
    // A change from what the Dear ImGui host did, found by moving the flow somewhere both UIs
    // could use it. That host skipped the whole scan when the interval was zero, which is right for
    // *writing* and wrong for finding: a user who turns autosave off after a crash would have lost
    // the work for ever, with the snapshot sitting in the directory.
    Fixture first{"off-after"};
    first.context.getScene().setName("Level01");
    first.context.getHistory().markUnsaved();
    first.poll(2.0);
    CNA_STUDIO_EXPECT_EQ(first.scratch.snapshotCount(), std::size_t{1});

    SecondStudio second{first.scratch.snapshots(), first.scratch.projectPath(), 0};
    second.poll(0.0);

    CNA_STUDIO_EXPECT(second.panels->recovery().hasRecoverable());
    second.shell->invoke("studio.file.recoverScene");
    CNA_STUDIO_EXPECT(second.context.getScene().getName() == "Level01");

    // And still nothing new is written, which is what the setting actually says. Under a fresh
    // scene id, so a write would add a second file rather than rewriting the one already there --
    // the recovered snapshot is deliberately kept until the work is saved somewhere.
    second.context.getScene().setSceneId(Uuid::generate());
    second.context.getHistory().markUnsaved();
    for (int i = 1; i <= 20; ++i) { second.poll(static_cast<double>(i) * 2.0); }
    CNA_STUDIO_EXPECT_EQ(first.scratch.snapshotCount(), std::size_t{1});
}
