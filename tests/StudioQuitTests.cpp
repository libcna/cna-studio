// SPDX-License-Identifier: MS-PL
/**
 * @file StudioQuitTests.cpp
 * @brief Quitting, and the question that has to come first (plan.md STUDIO-07020).
 *
 * The inventory listed File > Exit as answered-but-unproven, and the reason was worth the row: the
 * command existed, the CNA host closed the window, and the two had nothing to do with each other.
 * The host watched `invokedActions()` for the string `"studio.file.quit"` and called `Exit()`, so
 * the command's own handler never ran — which meant there was nowhere for "the scene has unsaved
 * changes" to be asked, and no way for any other host to close at all.
 *
 * Losing an afternoon's work to a menu item is the one mistake an editor must not let a user make
 * in a single click, so most of what is here is about the question rather than about closing.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace CNA::Studio;

namespace
{
    UiInputState away()
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 720.0f;
        input.mouseX = -1.0f;
        input.mouseY = -1.0f;
        input.mouseInWindow = false;
        return input;
    }

    /** @brief A project and a scene on disk, so saving can succeed rather than being mocked. */
    class Scratch
    {
    public:
        explicit Scratch(const std::string& name)
        {
            root_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-quit-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(root_, code);
            std::filesystem::create_directories(root_ / "Scenes", code);

            std::ofstream project{root_ / "Game.cnaproject", std::ios::binary};
            project << R"({"formatVersion":1,"name":"Quit","kind":"CnaNative",)"
                       R"("sceneDirectory":"Scenes"})";
        }

        ~Scratch()
        {
            std::error_code code;
            std::filesystem::remove_all(root_, code);
        }

        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;

        [[nodiscard]] std::string project() const
        {
            return (root_ / "Game.cnaproject").generic_string();
        }

        [[nodiscard]] std::string scene() const
        {
            return (root_ / "Scenes" / "Level.cnascene").generic_string();
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path root_;
    };

    struct Fixture
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell;
        StudioCamera2D camera;
        StudioShellPanels panels{shell, context, log};
        int closed = 0;

        Fixture()
        {
            shell.resetLayout();
            (void)bindStudioShellActions(shell, context, log);
            panels.setViewportServices(camera, {});
            shell.setQuitHandler([this] { ++closed; });
        }

        void frame()
        {
            panels.poll(clock_);
            clock_ += 1.0 / 60.0;
            shell.renderFrame(away());
        }

        /** @brief Answers the open dialog by pressing the button at @p index. */
        void answer(std::size_t index)
        {
            const UiRect button = shell.dialogButtonBounds(index);
            UiInputState input = away();
            input.mouseX = button.centerX();
            input.mouseY = button.centerY();
            input.mouseInWindow = true;
            shell.renderFrame(input);
            input.setMouseDown(UiMouseButton::Left, true);
            shell.renderFrame(input);
            input.setMouseDown(UiMouseButton::Left, false);
            shell.renderFrame(input);
        }

        [[nodiscard]] bool logSays(std::string_view needle) const
        {
            for (const StudioLogEntry& entry : log.entries())
            {
                if (entry.message.find(needle) != std::string::npos) { return true; }
            }
            return false;
        }

    private:
        double clock_ = 0.0;
    };
}

CNA_STUDIO_TEST(QuittingWithNothingUnsavedJustCloses)
{
    // No question, because there is nothing to ask about. A confirmation that always appears is one
    // people dismiss without reading, which is how the one that mattered gets dismissed too.
    Fixture fixture;
    fixture.frame();
    fixture.shell.invoke("studio.file.quit");

    CNA_STUDIO_EXPECT(!fixture.shell.isDialogOpen());
    CNA_STUDIO_EXPECT_EQ(fixture.closed, 1);
}

CNA_STUDIO_TEST(QuittingWithUnsavedChangesAsksBeforeAnythingCloses)
{
    Fixture fixture;
    fixture.context.getHistory().markUnsaved();
    fixture.frame();

    fixture.shell.invoke("studio.file.quit");
    CNA_STUDIO_EXPECT(fixture.shell.isDialogOpen());
    CNA_STUDIO_EXPECT_EQ(fixture.closed, 0);
}

CNA_STUDIO_TEST(CancellingTheQuitLeavesStudioOpenAndTheWorkUnsaved)
{
    Fixture fixture;
    fixture.context.getHistory().markUnsaved();
    fixture.frame();
    fixture.shell.invoke("studio.file.quit");

    fixture.answer(0);
    fixture.frame();

    CNA_STUDIO_EXPECT_EQ(fixture.closed, 0);
    CNA_STUDIO_EXPECT(fixture.context.getHistory().isDirty());
    CNA_STUDIO_EXPECT(!fixture.shell.isDialogOpen());
}

CNA_STUDIO_TEST(DiscardingQuitsWithoutWriting)
{
    Fixture fixture;
    const Scratch scratch{"discard"};
    CNA_STUDIO_EXPECT(fixture.context.openProject(scratch.project()));
    CNA_STUDIO_EXPECT(fixture.context.saveScene(scratch.scene()));
    fixture.context.getHistory().markUnsaved();
    fixture.frame();

    const auto written = std::filesystem::last_write_time(std::filesystem::path{scratch.scene()});
    fixture.shell.invoke("studio.file.quit");
    fixture.answer(1);
    fixture.frame();

    CNA_STUDIO_EXPECT_EQ(fixture.closed, 1);
    CNA_STUDIO_EXPECT(std::filesystem::last_write_time(std::filesystem::path{scratch.scene()})
                      == written);
}

CNA_STUDIO_TEST(SaveAndQuitWritesTheSceneAndThenCloses)
{
    Fixture fixture;
    const Scratch scratch{"save"};
    CNA_STUDIO_EXPECT(fixture.context.openProject(scratch.project()));
    CNA_STUDIO_EXPECT(fixture.context.saveScene(scratch.scene()));

    fixture.context.getScene().setName("Renamed");
    fixture.context.getHistory().markUnsaved();
    fixture.frame();

    fixture.shell.invoke("studio.file.quit");
    fixture.answer(2);
    fixture.frame();

    CNA_STUDIO_EXPECT_EQ(fixture.closed, 1);
    CNA_STUDIO_EXPECT(!fixture.context.getHistory().isDirty());
}

CNA_STUDIO_TEST(ASaveThatFailsLeavesStudioOpenRatherThanQuittingAnyway)
{
    // "Save and Quit" is one intention with two halves. Doing the second after failing the first is
    // the worst possible reading of it, and it is the reading that loses the work.
    Fixture fixture;
    fixture.context.getHistory().markUnsaved();
    fixture.frame();

    // No project and no scene path, so there is nowhere to save to.
    fixture.shell.invoke("studio.file.quit");
    fixture.answer(2);
    fixture.frame();

    CNA_STUDIO_EXPECT_EQ(fixture.closed, 0);
    CNA_STUDIO_EXPECT(fixture.logSays("still open"));
}

CNA_STUDIO_TEST(DismissingTheQuestionIsCancelRatherThanAnAnswer)
{
    // Escape withdraws the question. The one answer that must never be inferred is the one that
    // discards work, and "they pressed Escape" is not somebody saying discard it.
    Fixture fixture;
    fixture.context.getHistory().markUnsaved();
    fixture.frame();
    fixture.shell.invoke("studio.file.quit");

    UiInputState escape = away();
    escape.setKeyDown(UiKey::Escape, true);
    fixture.shell.renderFrame(escape);
    fixture.shell.renderFrame(away());
    fixture.frame();

    CNA_STUDIO_EXPECT_EQ(fixture.closed, 0);
    CNA_STUDIO_EXPECT(fixture.context.getHistory().isDirty());
}

CNA_STUDIO_TEST(AShellWithNoHostSaysSoRatherThanDoingNothing)
{
    // The preview and every test are exactly this, and so is a host that forgot the seam. A Quit
    // that silently does nothing reads as a broken menu.
    StudioContext context;
    StudioLog log;
    StudioShell shell;
    shell.resetLayout();
    (void)bindStudioShellActions(shell, context, log);
    StudioCamera2D camera;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, {});

    CNA_STUDIO_EXPECT(!shell.canQuit());
    shell.invoke("studio.file.quit");

    bool said = false;
    for (const StudioLogEntry& entry : log.entries())
    {
        if (entry.message.find("no window to close") != std::string::npos) { said = true; }
    }
    CNA_STUDIO_EXPECT(said);
}

CNA_STUDIO_TEST(QuitIsACommandRatherThanAnIdTheHostWatchesFor)
{
    // What this task actually fixed. A host matching on a command's name is a host reimplementing
    // its behaviour outside the registry, and the two drift: this one closed the window without
    // asking about unsaved changes, because the command it named never ran.
    Fixture fixture;
    const StudioAction* quit = fixture.shell.actions().find("studio.file.quit");
    CNA_STUDIO_EXPECT(quit != nullptr);
    if (quit != nullptr) { CNA_STUDIO_EXPECT(static_cast<bool>(quit->run)); }
}
