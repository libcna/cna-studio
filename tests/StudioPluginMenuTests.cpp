// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPluginMenuTests.cpp
 * @brief A plugin's menu commands, as commands (plan.md STUDIO-07002).
 *
 * The last unanswered row of the migration inventory's menu table, and the one that was genuinely
 * architectural rather than a binding. The prototype *draws* plugin menus: it walks the extension
 * registry every frame and calls `beginMenu` and `menuItem`. That works, and it is exactly why a
 * plugin command there can never have a shortcut, never be greyed out, never appear on a toolbar
 * and never show up in the shortcut editor — it is not a command, it is a row.
 *
 * So the cases worth having are about the difference: that a plugin command *is* in the registry,
 * that it goes away when its plugin does, and that a menu a plugin asks for by a name Studio
 * already uses is the same menu rather than a second one beside it.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Plugins/PluginExtensions.hpp"
#include "CNA/Studio/Plugins/Plugin.hpp"
#include "CNA/Studio/ShellPanels/StudioPluginMenus.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <string>
#include <vector>

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

    PluginMenuCommand command(std::string owner, std::string menu, std::string label, int& ran)
    {
        PluginMenuCommand entry;
        entry.ownerId = std::move(owner);
        entry.menu = std::move(menu);
        entry.label = std::move(label);
        entry.invoke = [&ran](StudioContext&) { ++ran; };
        return entry;
    }

    struct Fixture
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell;
        StudioCamera2D camera;
        StudioCamera3D camera3D;
        StudioShellPanels panels{shell, context, log};

        Fixture()
        {
            shell.resetLayout();
            panels.setViewportServices(camera, camera3D, {});
        }

        void poll()
        {
            panels.poll(clock_);
            clock_ += 1.0 / 60.0;
            shell.renderFrame(away());
        }

        /** @brief The rows of the menu titled @p title, as action ids. */
        [[nodiscard]] std::vector<std::string> rowsOf(const std::string& title) const
        {
            for (const StudioMenuDefinition& menu : shell.menus())
            {
                if (menu.title != title) { continue; }

                std::vector<std::string> ids;
                for (const StudioMenuEntry& entry : menu.entries) { ids.push_back(entry.id); }
                return ids;
            }
            return {};
        }

        [[nodiscard]] std::size_t menusTitled(const std::string& title) const
        {
            std::size_t count = 0;
            for (const StudioMenuDefinition& menu : shell.menus())
            {
                if (menu.title == title) { ++count; }
            }
            return count;
        }

        /** @brief The plugin actions the registry now carries. */
        [[nodiscard]] std::vector<std::string> pluginActions() const
        {
            std::vector<std::string> ids;
            for (const StudioAction& action : shell.actions().commands())
            {
                if (action.id.rfind(kStudioPluginActionPrefix, 0) == 0) { ids.push_back(action.id); }
            }
            return ids;
        }

    private:
        double clock_ = 0.0;
    };
}

CNA_STUDIO_TEST(APluginCommandBecomesACommandRatherThanARow)
{
    // The whole difference. In the registry it has an id, so everything the registry offers -- a
    // shortcut, enablement, the shortcut editor, a toolbar -- applies to it without plugin menus
    // being a special case anywhere.
    Fixture fixture;
    int ran = 0;
    fixture.context.getPluginExtensions().addMenuCommand(
        command("demo.plugin", "Tools", "Generate Level", ran));

    fixture.poll();

    const std::vector<std::string> actions = fixture.pluginActions();
    CNA_STUDIO_EXPECT(actions.size() == 1);
    if (actions.empty()) { return; }

    const StudioAction* action = fixture.shell.actions().find(actions.front());
    CNA_STUDIO_EXPECT(action != nullptr);
    if (action != nullptr)
    {
        CNA_STUDIO_EXPECT_EQ(action->label, std::string{"Generate Level"});
        CNA_STUDIO_EXPECT(action->description.find("demo.plugin") != std::string::npos);
    }

    fixture.shell.invoke(actions.front());
    CNA_STUDIO_EXPECT_EQ(ran, 1);
}

CNA_STUDIO_TEST(AMenuThePluginAsksForByAnExistingNameIsThatMenu)
{
    // Not a second one beside it, which is what the prototype produces because Dear ImGui's
    // BeginMenu has no opinion about a title it has already seen. Two menus called Tools is a bar
    // a user has to open twice to find out which one holds what.
    Fixture fixture;
    int ran = 0;
    fixture.context.getPluginExtensions().addMenuCommand(
        command("demo.plugin", "Tools", "Generate Level", ran));
    fixture.poll();

    CNA_STUDIO_EXPECT_EQ(fixture.menusTitled("Tools"), std::size_t{1});

    const std::vector<std::string> rows = fixture.rowsOf("Tools");
    CNA_STUDIO_EXPECT(!rows.empty());
    if (!rows.empty())
    {
        CNA_STUDIO_EXPECT(rows.back().rfind(kStudioPluginActionPrefix, 0) == 0);
    }
}

CNA_STUDIO_TEST(AMenuOnlyThePluginKnowsTheNameOfIsCreatedBeforeHelp)
{
    // Help is last in every application anybody has used, and a plugin menu appended after it
    // would move the one row a lost user goes looking for.
    Fixture fixture;
    int ran = 0;
    fixture.context.getPluginExtensions().addMenuCommand(
        command("demo.plugin", "Levels", "Generate", ran));
    fixture.poll();

    CNA_STUDIO_EXPECT_EQ(fixture.menusTitled("Levels"), std::size_t{1});

    std::size_t levels = 0;
    std::size_t help = 0;
    const std::vector<StudioMenuDefinition>& menus = fixture.shell.menus();
    for (std::size_t i = 0; i < menus.size(); ++i)
    {
        if (menus[i].title == "Levels") { levels = i; }
        if (menus[i].title == "Help") { help = i; }
    }
    CNA_STUDIO_EXPECT(levels < help);
}

CNA_STUDIO_TEST(StudiosOwnRowsAreSeparatedFromThePluginsOnce)
{
    // One separator however many rows a plugin adds, so the editor's own commands are visibly a
    // group rather than a list with a rule through the middle of it.
    Fixture fixture;
    int ran = 0;
    PluginExtensionRegistry& extensions = fixture.context.getPluginExtensions();
    extensions.addMenuCommand(command("demo.plugin", "Tools", "One", ran));
    extensions.addMenuCommand(command("demo.plugin", "Tools", "Two", ran));
    fixture.poll();

    std::size_t separators = 0;
    for (const std::string& id : fixture.rowsOf("Tools"))
    {
        if (id == kStudioMenuSeparatorId) { ++separators; }
    }
    CNA_STUDIO_EXPECT_EQ(separators, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(fixture.pluginActions().size(), std::size_t{2});
}

CNA_STUDIO_TEST(UnloadingAPluginTakesItsRowsAndItsCommandsWithIt)
{
    // The failure this prevents is the worst kind: the command's `invoke` points into a library the
    // host is about to close, so a row left behind is a row that calls code no longer mapped.
    Fixture fixture;
    int ran = 0;
    PluginExtensionRegistry& extensions = fixture.context.getPluginExtensions();
    extensions.addMenuCommand(command("demo.plugin", "Levels", "Generate", ran));
    fixture.poll();
    CNA_STUDIO_EXPECT_EQ(fixture.pluginActions().size(), std::size_t{1});

    CNA_STUDIO_EXPECT(extensions.removeAllFrom("demo.plugin") == 1);
    fixture.poll();

    CNA_STUDIO_EXPECT(fixture.pluginActions().empty());
    CNA_STUDIO_EXPECT_EQ(fixture.menusTitled("Levels"), std::size_t{0});
}

CNA_STUDIO_TEST(ReloadingAPluginDoesNotLeaveTwoOfEverything)
{
    // A hot reload removes and re-adds, and a rebuild that only appended would double the rows
    // every time -- which is a bug nobody sees until the fifth reload.
    Fixture fixture;
    int ran = 0;
    PluginExtensionRegistry& extensions = fixture.context.getPluginExtensions();

    for (int reload = 0; reload < 3; ++reload)
    {
        (void)extensions.removeAllFrom("demo.plugin");
        extensions.addMenuCommand(command("demo.plugin", "Tools", "Generate", ran));
        fixture.poll();
    }

    CNA_STUDIO_EXPECT_EQ(fixture.pluginActions().size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(fixture.menusTitled("Tools"), std::size_t{1});
}

CNA_STUDIO_TEST(TwoPluginsOfferingTheSameLabelGetTwoCommands)
{
    // "Export" is the obvious one. Two ids derived from the label alone would collide, and the
    // registry would silently keep one of them -- so one plugin's command would disappear because
    // another plugin happened to choose the same word.
    Fixture fixture;
    int ran = 0;
    PluginExtensionRegistry& extensions = fixture.context.getPluginExtensions();
    extensions.addMenuCommand(command("one.plugin", "Tools", "Export", ran));
    extensions.addMenuCommand(command("two.plugin", "Tools", "Export", ran));
    fixture.poll();

    CNA_STUDIO_EXPECT_EQ(fixture.pluginActions().size(), std::size_t{2});
}

CNA_STUDIO_TEST(APluginCommandThatThrowsIsReportedRatherThanEndingTheFrame)
{
    // Inside the menu that invoked it, with the menu stack half unwound, is where an escaping
    // exception would leave the frame.
    Fixture fixture;
    PluginMenuCommand thrower;
    thrower.ownerId = "bad.plugin";
    thrower.menu = "Tools";
    thrower.label = "Explode";
    thrower.invoke = [](StudioContext&) { throw std::runtime_error("boom"); };
    fixture.context.getPluginExtensions().addMenuCommand(std::move(thrower));
    fixture.poll();

    const std::vector<std::string> actions = fixture.pluginActions();
    CNA_STUDIO_EXPECT(actions.size() == 1);
    if (actions.empty()) { return; }

    fixture.shell.invoke(actions.front());

    bool said = false;
    for (const StudioLogEntry& entry : fixture.log.entries())
    {
        if (entry.message.find("Explode") != std::string::npos
            && entry.message.find("boom") != std::string::npos)
        {
            said = true;
        }
    }
    CNA_STUDIO_EXPECT(said);

    // And the shell is still usable, which is the part that matters.
    fixture.poll();
    CNA_STUDIO_EXPECT_EQ(fixture.shell.frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(AStudioWithNoPluginsHasTheMenusItShipsWith)
{
    // The common case, and the one a rebuild could quietly damage: nothing to add must mean nothing
    // to change, not "the default menus, reassembled, nearly the same".
    Fixture fixture;
    const std::vector<StudioMenuDefinition> before = fixture.shell.menus();
    fixture.poll();

    const std::vector<StudioMenuDefinition>& after = fixture.shell.menus();
    CNA_STUDIO_EXPECT_EQ(after.size(), before.size());
    for (std::size_t i = 0; i < after.size() && i < before.size(); ++i)
    {
        CNA_STUDIO_EXPECT_EQ(after[i].title, before[i].title);
        CNA_STUDIO_EXPECT_EQ(after[i].entries.size(), before[i].entries.size());
    }
}

#ifdef CNA_STUDIO_TEST_PLUGIN_DIR
CNA_STUDIO_TEST(ARealPluginsCommandsLeaveTheRegistryBeforeItsLibraryIsClosed)
{
    // Every case above uses a hand-built `PluginMenuCommand`, whose `invoke` is a lambda in this
    // translation unit. That is the right shape for asking what the menus do with a command -- and
    // it is precisely the wrong shape for the failure this case exists for, because a lambda from
    // this binary stays mapped no matter what the host does with the library.
    //
    // A registered action holds a *copy* of the plugin's `std::function`, and destroying that copy
    // calls a manager function that lives in the plugin's library. So clearing the registry after
    // `dlclose` does not fail to find a command: it jumps into unmapped memory. It crashes in
    // `~StudioShell`, which is the hardest place to read a backtrace from and the last place
    // anybody looks for a plugin bug -- and it stayed invisible for as long as it did because no
    // shell had ever loaded a plugin to begin with (STUDIO-07052).
    //
    // The assertion is the order, and the way this test fails is by crashing. That is not a defect
    // in the test: a segmentation fault is a failed test, and the alternative -- asserting on a
    // pointer that has already been freed -- is a test that reads memory it must not touch.
    Fixture fixture;
    PluginHost host;

    const std::vector<LoadedPlugin> found = host.discover(CNA_STUDIO_TEST_PLUGIN_DIR);
    CNA_STUDIO_EXPECT(!found.empty());
    CNA_STUDIO_EXPECT(host.loadAll(fixture.context) >= 1);

    fixture.poll();
    const std::size_t rows = fixture.pluginActions().size();

    // A plugin that registered no command would make everything below vacuous -- and the test
    // plugin registering one is the reason it is the fixture rather than a stub.
    CNA_STUDIO_EXPECT(rows >= 1);

    // The registry first, while the library is still mapped.
    CNA_STUDIO_EXPECT_EQ(studioClearPluginMenus(fixture.shell), rows);
    CNA_STUDIO_EXPECT(fixture.pluginActions().empty());

    // Then the unload, which runs each plugin's shutdown and closes its library.
    host.unloadAll(fixture.context);
    CNA_STUDIO_EXPECT(fixture.pluginActions().empty());

    // And the menus rebuild around nothing rather than keeping a row whose command has gone.
    fixture.poll();
    CNA_STUDIO_EXPECT(fixture.pluginActions().empty());
}
#endif
