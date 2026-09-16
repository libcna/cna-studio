// SPDX-License-Identifier: MS-PL
/**
 * @file PluginHostTests.cpp
 * @brief Loading a real shared library, and unloading it cleanly.
 *
 * `plan.md` ED-411, and `STUDIO-07047` for why these are here rather than in
 * `ApplicationTests.cpp`: they construct a `PluginHost` and a `StudioContext` directly and never
 * touch the prototype's application, so they were only in that file by habit — and would have been
 * deleted with it.
 *
 * Note what they do **not** cover: nothing in the native shell ever calls `discover` or `loadAll`,
 * so a plugin is never loaded on `--ui=studio` and the plugin menus are always empty
 * (`STUDIO-07052`). That is a gap in the editor rather than in this file.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Plugins/Plugin.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

#ifdef CNA_STUDIO_TEST_PLUGIN_DIR
/**
 * @brief ED-411: a real shared library is opened, initialised, and closed in the right order.
 *
 * Against a *real* `.so` rather than a double, because nothing that actually breaks in a plugin
 * system -- `dlopen`, symbol resolution, allocating in one runtime and freeing in another, unload
 * order -- can be checked against a fake. A double would exercise the host's bookkeeping and none
 * of it.
 *
 * The observable proof is a component descriptor: present in the registry only while the plugin is
 * active, which makes "initialize ran" and "shutdown cleaned up" the same assertion read twice.
 */
CNA_STUDIO_TEST(APluginIsLoadedFromARealLibraryAndUnloadsCleanly)
{
    StudioContext context;
    PluginHost host;

    const std::vector<LoadedPlugin> found = host.discover(CNA_STUDIO_TEST_PLUGIN_DIR);
    CNA_STUDIO_EXPECT_EQ(found.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(found[0].manifest.id, std::string{"org.openeggbert.testplugin"});
    CNA_STUDIO_EXPECT(found[0].loaded);

    // Discovered is not active: the manifest passed its checks, and nothing has been opened.
    CNA_STUDIO_EXPECT(!found[0].active);
    CNA_STUDIO_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") == nullptr);

    CNA_STUDIO_EXPECT_EQ(host.loadAll(context), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(host.getActiveCount(), std::size_t{1});
    CNA_STUDIO_EXPECT(host.getPlugins()[0].active);
    CNA_STUDIO_EXPECT(host.getPlugins()[0].error.empty());

    // The plugin's own component is in the editor's registry -- and it is the *editor's*, which is
    // the thing a plugin linking the editor statically would silently fail at.
    const ComponentDescriptor* descriptor =
        context.getComponentRegistry().find("Test.PluginComponent");
    CNA_STUDIO_EXPECT(descriptor != nullptr);
    CNA_STUDIO_EXPECT_EQ(descriptor->displayName, std::string{"Plugin Component"});

    // ED-412's two extension points that needed a registry: a panel and a menu command.
    CNA_STUDIO_EXPECT_EQ(context.getPluginExtensions().getPanels().size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(context.getPluginExtensions().getPanels()[0].title,
                         std::string{"Test Plugin Panel"});
    CNA_STUDIO_EXPECT_EQ(context.getPluginExtensions().getMenuCommands().size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(context.getPluginExtensions().getMenuNames().size(), std::size_t{1});

    host.unloadAll(context);
    CNA_STUDIO_EXPECT_EQ(host.getActiveCount(), std::size_t{0});

    // Gone. A descriptor left registered past dlclose points into unmapped memory, which is why
    // shutdown() removing everything is a requirement rather than a courtesy.
    CNA_STUDIO_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") == nullptr);

    // And so are the panel and the command -- a `std::function` outliving its library is the same
    // failure wearing different clothes, and it would not show until the next frame drew it.
    CNA_STUDIO_EXPECT(context.getPluginExtensions().getPanels().empty());
    CNA_STUDIO_EXPECT(context.getPluginExtensions().getMenuCommands().empty());
}

/**
 * @brief The host removes a plugin's registrations even when the plugin forgets to (ED-412).
 *
 * A backstop rather than the contract: `shutdown` is required to clean up, and a plugin author who
 * relies on this would leak everywhere the backstop does not reach. It exists because the failure
 * it prevents is not an error message -- it is a `std::function` pointing into unmapped code,
 * which fails on the next frame that draws it rather than at the moment of the mistake.
 */
CNA_STUDIO_TEST(TheHostRemovesRegistrationsAPluginForgotToRemove)
{
    StudioContext context;
    PluginHost host;

    host.discover(CNA_STUDIO_TEST_PLUGIN_DIR);
    CNA_STUDIO_EXPECT_EQ(host.loadAll(context), std::size_t{1});

    // Something the plugin never registered and will never clean up, under its id.
    PluginPanel stray;
    stray.ownerId = "org.openeggbert.testplugin";
    stray.title = "Forgotten Panel";
    stray.draw = [](StudioUi&, StudioContext&) {};
    context.getPluginExtensions().addPanel(std::move(stray));
    CNA_STUDIO_EXPECT_EQ(context.getPluginExtensions().getPanels().size(), std::size_t{2});

    host.unloadAll(context);
    CNA_STUDIO_EXPECT(context.getPluginExtensions().getPanels().empty());
}

/**
 * @brief Hot-reload is an unload and a load, and says so: the plugin comes back registered afresh.
 *
 * What the test pins is that reload leaves the editor in the state a fresh load would -- one copy
 * of the component, not two, and not none. A reload that re-registered without unregistering, or
 * unregistered without coming back, both look fine for one cycle.
 */
CNA_STUDIO_TEST(ReloadingAPluginLeavesItRegisteredExactlyOnce)
{
    StudioContext context;
    PluginHost host;

    host.discover(CNA_STUDIO_TEST_PLUGIN_DIR);
    CNA_STUDIO_EXPECT_EQ(host.loadAll(context), std::size_t{1});

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        CNA_STUDIO_EXPECT(host.reload(context, "org.openeggbert.testplugin"));
        CNA_STUDIO_EXPECT_EQ(host.getActiveCount(), std::size_t{1});
        CNA_STUDIO_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") != nullptr);
    }

    // An id nobody has is a failure rather than a silent no-op: a user who typed it wrong would
    // otherwise be told nothing and believe the reload happened.
    CNA_STUDIO_EXPECT(!host.reload(context, "org.openeggbert.nosuchplugin"));

    host.unloadAll(context);
    CNA_STUDIO_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") == nullptr);
}
#endif
