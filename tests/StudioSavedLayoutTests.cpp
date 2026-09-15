// SPDX-License-Identifier: MS-PL
/**
 * @file StudioSavedLayoutTests.cpp
 * @brief Arrangements saved under a name (plan.md STUDIO-05010).
 *
 * The file half and the menu half fail differently. The file has to survive a second Studio
 * writing beside it and a document written before named layouts existed; the menu has to stop
 * naming a layout the moment it is deleted, and has to ask before deleting one at all — a
 * workspace somebody spent ten minutes on is not undoable.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 1280.0f;
    constexpr float kHeight = 720.0f;

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A workspace file in a temporary directory, removed on the way out. */
    class ScopedWorkspace
    {
    public:
        explicit ScopedWorkspace(const std::string& name)
        {
            directory_ = std::filesystem::temp_directory_path()
                       / ("cna-studio-layouts-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(directory_, code);
            std::filesystem::create_directories(directory_, code);
        }

        ~ScopedWorkspace()
        {
            std::error_code code;
            std::filesystem::remove_all(directory_, code);
        }

        ScopedWorkspace(const ScopedWorkspace&) = delete;
        ScopedWorkspace& operator=(const ScopedWorkspace&) = delete;

        [[nodiscard]] std::string path() const
        {
            return (directory_ / StudioWorkspaceStore::kFileName).generic_string();
        }

        void write(const std::string& contents) const
        {
            std::ofstream stream{std::filesystem::path{path()}, std::ios::binary | std::ios::trunc};
            stream << contents;
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path directory_;
    };

    std::unique_ptr<StudioShell> defaultShell()
    {
        auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
        shell->resetLayout();
        shell->renderFrame(at(-1.0f, -1.0f));
        return shell;
    }

    /** @brief A layout document that is not the default one. */
    JsonValue someLayout(StudioShell& shell)
    {
        (void)shell.closePanel("history");
        return shell.saveLayout();
    }

    void press(StudioShell& shell, UiKey key)
    {
        UiInputState input = at(-1.0f, -1.0f);
        input.setKeyDown(key, true);
        shell.renderFrame(input);
        shell.renderFrame(at(-1.0f, -1.0f));
    }
}

// --- The file ----------------------------------------------------------------------------------

CNA_STUDIO_TEST(ASavedLayoutSurvivesBeingWrittenAndReadBack)
{
    const ScopedWorkspace workspace{"roundtrip"};
    const StudioWorkspaceStore store{workspace.path()};

    const std::unique_ptr<StudioShell> shell = defaultShell();
    const JsonValue current = shell->saveLayout();
    const JsonValue animation = someLayout(*shell);

    std::string problem;
    CNA_STUDIO_EXPECT(store.save(current, &problem));
    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT(store.saveNamed("Animation", animation, &problem));
    CNA_STUDIO_EXPECT(problem.empty());

    const StudioWorkspaceDocument stored = store.load();
    CNA_STUDIO_EXPECT(stored.found);
    CNA_STUDIO_EXPECT(stored.problem.empty());
    CNA_STUDIO_EXPECT_EQ(stored.named.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(stored.named.front().name, std::string{"Animation"});

    // And the current arrangement is still the current one: saving a *named* layout must not
    // replace what the next start restores.
    CNA_STUDIO_EXPECT_EQ(Json::write(stored.layout), Json::write(current));
}

CNA_STUDIO_TEST(SavingTheCurrentArrangementKeepsTheNamedOnes)
{
    // This runs on exit, and a Studio that rewrote the file from what it happened to hold in
    // memory would throw away a layout saved by a second Studio running beside it.
    const ScopedWorkspace workspace{"keep"};
    const StudioWorkspaceStore store{workspace.path()};
    const std::unique_ptr<StudioShell> shell = defaultShell();

    std::string problem;
    CNA_STUDIO_EXPECT(store.saveNamed("Debug", shell->saveLayout(), &problem));
    CNA_STUDIO_EXPECT(store.save(someLayout(*shell), &problem));

    const StudioWorkspaceDocument stored = store.load();
    CNA_STUDIO_EXPECT_EQ(stored.named.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(stored.named.front().name, std::string{"Debug"});
}

CNA_STUDIO_TEST(SavingUnderANameThatExistsReplacesItRatherThanAddingASecond)
{
    // Two rows with the same name is a menu where one of them is unreachable.
    const ScopedWorkspace workspace{"replace"};
    const StudioWorkspaceStore store{workspace.path()};
    const std::unique_ptr<StudioShell> shell = defaultShell();

    std::string problem;
    CNA_STUDIO_EXPECT(store.saveNamed("Debug", shell->saveLayout(), &problem));
    CNA_STUDIO_EXPECT(store.saveNamed("Debug", someLayout(*shell), &problem));

    CNA_STUDIO_EXPECT_EQ(store.load().named.size(), std::size_t{1});
}

CNA_STUDIO_TEST(SavedLayoutsComeBackInNameOrderHoweverTheyWereWritten)
{
    // A menu whose rows move when one is added is a menu people read every time instead of
    // reaching straight for the one they want.
    const ScopedWorkspace workspace{"order"};
    const StudioWorkspaceStore store{workspace.path()};
    const std::unique_ptr<StudioShell> shell = defaultShell();
    const JsonValue layout = shell->saveLayout();

    std::string problem;
    CNA_STUDIO_EXPECT(store.saveNamed("Zoom", layout, &problem));
    CNA_STUDIO_EXPECT(store.saveNamed("Animation", layout, &problem));
    CNA_STUDIO_EXPECT(store.saveNamed("Modelling", layout, &problem));

    const std::vector<StudioNamedLayout> named = store.load().named;
    CNA_STUDIO_EXPECT_EQ(named.size(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(named[0].name, std::string{"Animation"});
    CNA_STUDIO_EXPECT_EQ(named[1].name, std::string{"Modelling"});
    CNA_STUDIO_EXPECT_EQ(named[2].name, std::string{"Zoom"});
}

CNA_STUDIO_TEST(AWorkspaceFileWrittenBeforeNamedLayoutsExistedStillOpens)
{
    const ScopedWorkspace workspace{"old"};
    workspace.write(R"({"fileVersion":1,"layout":{"version":1,"root":)"
                    R"({"kind":"leaf","panels":["viewport"],"active":0}}})");

    const StudioWorkspaceStore store{workspace.path()};
    const StudioWorkspaceDocument stored = store.load();

    CNA_STUDIO_EXPECT(stored.found);
    CNA_STUDIO_EXPECT(stored.problem.empty());
    CNA_STUDIO_EXPECT(stored.named.empty());

    // And saving into it upgrades the envelope rather than refusing.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    std::string problem;
    CNA_STUDIO_EXPECT(store.saveNamed("Animation", shell->saveLayout(), &problem));
    CNA_STUDIO_EXPECT_EQ(store.load().named.size(), std::size_t{1});
}

CNA_STUDIO_TEST(ANameThatCouldNotBeAMenuRowIsRefusedOnceRatherThanTwice)
{
    // One place decides, because a name the menu accepts and the file rejects is a save that
    // appears to work and is gone at the next start.
    CNA_STUDIO_EXPECT(StudioWorkspaceStore::sanitizeName("").empty());
    CNA_STUDIO_EXPECT(StudioWorkspaceStore::sanitizeName("   ").empty());
    CNA_STUDIO_EXPECT(StudioWorkspaceStore::sanitizeName("a\nb").empty());
    CNA_STUDIO_EXPECT(StudioWorkspaceStore::sanitizeName(
        std::string(StudioWorkspaceStore::kMaximumNameLength + 1, 'x')).empty());

    CNA_STUDIO_EXPECT_EQ(StudioWorkspaceStore::sanitizeName("  Animation  "),
                         std::string{"Animation"});

    const ScopedWorkspace workspace{"names"};
    const StudioWorkspaceStore store{workspace.path()};
    const std::unique_ptr<StudioShell> shell = defaultShell();

    std::string problem;
    CNA_STUDIO_EXPECT(!store.saveNamed("   ", shell->saveLayout(), &problem));
    CNA_STUDIO_EXPECT(!problem.empty());
    CNA_STUDIO_EXPECT(store.load().named.empty());
}

CNA_STUDIO_TEST(ALayoutFileWithARubbishNameIsReadWithoutIt)
{
    // Plain JSON in the user's configuration directory, so a name that never went through
    // saveNamed is an ordinary thing to find rather than a corruption.
    const ScopedWorkspace workspace{"rubbish"};
    workspace.write(R"({"fileVersion":2,"layout":{"version":1,"root":)"
                    R"({"kind":"leaf","panels":["viewport"],"active":0}},)"
                    R"("named":[{"name":"   ","layout":{}},)"
                    R"({"name":"Real","layout":{"version":1,"root":)"
                    R"({"kind":"leaf","panels":["viewport"],"active":0}}}]})");

    const StudioWorkspaceDocument stored = StudioWorkspaceStore{workspace.path()}.load();
    CNA_STUDIO_EXPECT(stored.found);
    CNA_STUDIO_EXPECT_EQ(stored.named.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(stored.named.front().name, std::string{"Real"});
}

CNA_STUDIO_TEST(RemovingASavedLayoutRemovesOnlyThatOne)
{
    const ScopedWorkspace workspace{"remove"};
    const StudioWorkspaceStore store{workspace.path()};
    const std::unique_ptr<StudioShell> shell = defaultShell();
    const JsonValue layout = shell->saveLayout();

    std::string problem;
    CNA_STUDIO_EXPECT(store.saveNamed("Animation", layout, &problem));
    CNA_STUDIO_EXPECT(store.saveNamed("Debug", layout, &problem));

    CNA_STUDIO_EXPECT(store.removeNamed("Animation", &problem));
    CNA_STUDIO_EXPECT(!store.removeNamed("Nothing", &problem));

    const std::vector<StudioNamedLayout> named = store.load().named;
    CNA_STUDIO_EXPECT_EQ(named.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(named.front().name, std::string{"Debug"});
}

// --- The menu ----------------------------------------------------------------------------------

CNA_STUDIO_TEST(EverySavedLayoutIsACommandAndAMenuRow)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    const JsonValue layout = shell->saveLayout();

    shell->setSavedLayouts({StudioNamedLayout{"Zoom", layout},
                            StudioNamedLayout{"Animation", layout}});

    CNA_STUDIO_EXPECT_EQ(shell->savedLayouts().size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(shell->savedLayouts().front().name, std::string{"Animation"});

    CNA_STUDIO_EXPECT(shell->actions().find(StudioShell::applyLayoutActionId("Animation"))
                      != nullptr);
    CNA_STUDIO_EXPECT(shell->actions().find(StudioShell::deleteLayoutActionId("Zoom")) != nullptr);
    CNA_STUDIO_EXPECT(shell->actions().find(std::string{kStudioSaveLayoutAsActionId}) != nullptr);
}

CNA_STUDIO_TEST(AFreshStudioCanStillReachSaveLayoutAs)
{
    // A submenu with nothing in it draws greyed out, so filling Layouts only once a layout exists
    // would leave a fresh Studio unable to reach the command that creates the first one.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->savedLayouts().empty());

    shell->setOpenMenu(-1);
    const std::vector<StudioMenuDefinition>& menus = shell->menus();
    const auto window = std::find_if(menus.begin(), menus.end(),
        [](const StudioMenuDefinition& menu) { return menu.title == "Window"; });
    CNA_STUDIO_EXPECT(window != menus.end());

    const auto layouts = std::find_if(window->entries.begin(), window->entries.end(),
        [](const StudioMenuEntry& entry) {
            return entry.isSubmenu() && entry.label == kStudioLayoutMenuLabel;
        });
    CNA_STUDIO_EXPECT(layouts != window->entries.end());
    CNA_STUDIO_EXPECT(!layouts->rows.empty());
    CNA_STUDIO_EXPECT_EQ(layouts->rows.front().id, std::string{kStudioSaveLayoutAsActionId});

    // And replacing the menus refills both submenus rather than emptying them, or a host that
    // customised its File menu would silently lose its panel list.
    shell->setMenus(shell->menus());
    const auto again = std::find_if(shell->menus().begin(), shell->menus().end(),
        [](const StudioMenuDefinition& menu) { return menu.title == "Window"; });
    const auto refilled = std::find_if(again->entries.begin(), again->entries.end(),
        [](const StudioMenuEntry& entry) {
            return entry.isSubmenu() && entry.label == kStudioPanelMenuLabel;
        });
    CNA_STUDIO_EXPECT(refilled != again->entries.end());
    CNA_STUDIO_EXPECT(!refilled->rows.empty());
}

CNA_STUDIO_TEST(ApplyingASavedLayoutRearrangesTheWorkspace)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();

    // An arrangement with one fewer panel, so "it was applied" and "nothing happened" differ.
    CNA_STUDIO_EXPECT(shell->isPanelOpen("history"));
    const JsonValue without = someLayout(*shell);
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->isPanelOpen("history"));

    shell->setSavedLayouts({StudioNamedLayout{"Trimmed", without}});
    shell->invoke(StudioShell::applyLayoutActionId("Trimmed"));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(!shell->isPanelOpen("history"));
    CNA_STUDIO_EXPECT(shell->dockTree().isWellFormed());
}

CNA_STUDIO_TEST(SaveLayoutAsAsksForANameAndSavesItThroughTheSeam)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();

    std::string savedName;
    StudioWorkspaceServices services;
    services.saveNamed = [&](const std::string& name, const JsonValue&, std::string*) {
        savedName = name;
        return true;
    };
    shell->setWorkspaceServices(std::move(services));

    shell->invoke(std::string{kStudioSaveLayoutAsActionId});
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->isDialogOpen());
    CNA_STUDIO_EXPECT(shell->dialog().hasTextField);

    // Typed, then Enter -- which is the whole gesture for a name prompt.
    UiInputState typing = at(-1.0f, -1.0f);
    typing.characters = {u'D', u'e', u'b', u'u', u'g'};
    shell->renderFrame(typing);
    press(*shell, UiKey::Enter);

    CNA_STUDIO_EXPECT(!shell->isDialogOpen());
    CNA_STUDIO_EXPECT_EQ(savedName, std::string{"Debug"});
    CNA_STUDIO_EXPECT_EQ(shell->savedLayouts().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(shell->actions().find(StudioShell::applyLayoutActionId("Debug")) != nullptr);
}

CNA_STUDIO_TEST(CancellingSaveLayoutAsSavesNothing)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();

    bool reached = false;
    StudioWorkspaceServices services;
    services.saveNamed = [&](const std::string&, const JsonValue&, std::string*) {
        reached = true;
        return true;
    };
    shell->setWorkspaceServices(std::move(services));

    shell->invoke(std::string{kStudioSaveLayoutAsActionId});
    shell->renderFrame(at(-1.0f, -1.0f));
    press(*shell, UiKey::Escape);

    CNA_STUDIO_EXPECT(!shell->isDialogOpen());
    CNA_STUDIO_EXPECT(!reached);
    CNA_STUDIO_EXPECT(shell->savedLayouts().empty());
}

CNA_STUDIO_TEST(DeletingASavedLayoutAsksFirstAndACancelKeepsIt)
{
    // A workspace somebody spent ten minutes on is not undoable, and a menu row one place lower
    // than expected is exactly how it would go.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    shell->setSavedLayouts({StudioNamedLayout{"Animation", shell->saveLayout()}});

    std::vector<std::string> removed;
    StudioWorkspaceServices services;
    services.removeNamed = [&](const std::string& name, std::string*) {
        removed.push_back(name);
        return true;
    };
    shell->setWorkspaceServices(std::move(services));

    shell->invoke(StudioShell::deleteLayoutActionId("Animation"));
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->isDialogOpen());

    press(*shell, UiKey::Escape);
    CNA_STUDIO_EXPECT(removed.empty());
    CNA_STUDIO_EXPECT_EQ(shell->savedLayouts().size(), std::size_t{1});

    // And confirming it does remove it -- along with the commands that named it, or the menu
    // would keep a row naming nothing.
    shell->invoke(StudioShell::deleteLayoutActionId("Animation"));
    shell->renderFrame(at(-1.0f, -1.0f));
    const UiRect bounds = shell->dialogBounds();
    shell->renderFrame(at(bounds.right() - 40.0f, bounds.bottom() - 24.0f));
    shell->renderFrame(at(bounds.right() - 40.0f, bounds.bottom() - 24.0f, true));
    shell->renderFrame(at(bounds.right() - 40.0f, bounds.bottom() - 24.0f, false));

    CNA_STUDIO_EXPECT_EQ(removed.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(shell->savedLayouts().empty());
    CNA_STUDIO_EXPECT(shell->actions().find(StudioShell::applyLayoutActionId("Animation"))
                      == nullptr);
    CNA_STUDIO_EXPECT(shell->actions().find(StudioShell::deleteLayoutActionId("Animation"))
                      == nullptr);
}

CNA_STUDIO_TEST(AShellWithNoWorkspaceFileStillArrangesItselfForThisRun)
{
    // A shell that refused to save a layout because nobody gave it a file would be worse than one
    // that forgets on exit -- the preview has no file and still has to work.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->saveLayoutAs("Scratch"));
    CNA_STUDIO_EXPECT_EQ(shell->savedLayouts().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(shell->deleteSavedLayout("Scratch"));
    CNA_STUDIO_EXPECT(shell->savedLayouts().empty());
}

CNA_STUDIO_TEST(ASaveThatTheFileRefusesDoesNotAppearInTheMenu)
{
    // A menu listing a layout the file never received would offer it again after a restart and
    // find nothing there.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    StudioWorkspaceServices services;
    services.saveNamed = [](const std::string&, const JsonValue&, std::string* problem) {
        if (problem != nullptr) { *problem = "the disk is full"; }
        return false;
    };
    shell->setWorkspaceServices(std::move(services));

    CNA_STUDIO_EXPECT(!shell->saveLayoutAs("Debug"));
    CNA_STUDIO_EXPECT(shell->savedLayouts().empty());
    CNA_STUDIO_EXPECT(shell->actions().find(StudioShell::applyLayoutActionId("Debug")) == nullptr);

    // And it says so, rather than the command quietly doing nothing.
    CNA_STUDIO_EXPECT(!shell->refusedActions().empty());
}
