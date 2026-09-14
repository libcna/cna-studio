// SPDX-License-Identifier: MS-PL
/**
 * @file tests/plugin/TestPlugin.cpp
 * @brief A real shared library, so ED-411's loading is tested by loading rather than by mocking.
 *
 * The point of building this is that nothing about `dlopen`, symbol resolution, cross-runtime
 * allocation or unload order can be checked against a fake. A test double would exercise the
 * host's bookkeeping and none of the things that actually break.
 *
 * What it registers is deliberately observable from outside: a component descriptor whose presence
 * in the registry is the proof that `initialize` ran, and whose absence afterwards is the proof
 * that `shutdown` cleaned up.
 */

#include "CNA/Editor/EditorContext.hpp"
#include "CNA/Editor/Plugins/Plugin.hpp"

namespace
{
    /** @brief The component this plugin adds. Named so a test can look for exactly it. */
    constexpr const char* kComponentTypeId = "Test.PluginComponent";

    class TestPlugin final : public CNA::Editor::EditorPlugin
    {
    public:
        void initialize(CNA::Editor::EditorContext& context) override
        {
            CNA::Editor::ComponentDescriptor descriptor;
            descriptor.typeId = kComponentTypeId;
            descriptor.displayName = "Plugin Component";
            descriptor.category = "Test";

            CNA::Editor::PropertyDescriptor property;
            property.name = "value";
            property.displayName = "Value";
            property.type = CNA::Editor::PropertyType::Integer;
            property.defaultValue = CNA::Editor::PropertyValue{static_cast<std::int64_t>(7)};
            descriptor.properties = {property};

            context.getComponentRegistry().registerComponent(std::move(descriptor));

            // A panel and a menu command (ED-412), so a test can check the two extension points
            // that needed a registry of their own. Both are keyed by this plugin's id, which is
            // how unloading takes exactly these away and nothing else.
            CNA::Editor::PluginPanel panel;
            panel.ownerId = getId();
            panel.title = "Test Plugin Panel";
            panel.preferredSide = CNA::Editor::DockSide::Right;
            panel.draw = [](CNA::Editor::EditorUi& ui, CNA::Editor::EditorContext&)
            { ui.text("Drawn by the test plugin."); };
            context.getPluginExtensions().addPanel(std::move(panel));

            CNA::Editor::PluginMenuCommand command;
            command.ownerId = getId();
            command.menu = "Tools";
            command.label = "Test Plugin Command";
            command.invoke = [](CNA::Editor::EditorContext& target)
            { target.log(CNA::Editor::LogSeverity::Info, "Test plugin command ran."); };
            context.getPluginExtensions().addMenuCommand(std::move(command));
        }

        void shutdown(CNA::Editor::EditorContext& context) override
        {
            // Everything initialize() registered, removed. The host requires this rather than
            // hoping for it: the descriptor holds strings and defaults allocated in *this*
            // library, and leaving it registered past dlclose leaves the registry pointing at
            // unmapped memory.
            context.getComponentRegistry().unregisterComponent(kComponentTypeId);

            // The host removes these too, as a backstop -- but a plugin removing its own is the
            // contract, and a plugin that relied on the backstop would be one that leaked
            // everywhere the backstop does not reach.
            context.getPluginExtensions().removeAllFrom(getId());
        }

        [[nodiscard]] std::string getId() const override { return "org.openeggbert.testplugin"; }
    };
}

extern "C"
{
    CNA::Editor::EditorPlugin* cnaEditorCreatePlugin(int editorApiVersion)
    {
        // The plugin's own refusal, which is a better check than the manifest's: this side knows
        // what it needs, and a manifest can be edited to claim anything.
        if (editorApiVersion != CNA::Editor::kEditorPluginApiVersion) { return nullptr; }
        return new TestPlugin{};
    }

    void cnaEditorDestroyPlugin(CNA::Editor::EditorPlugin* plugin) { delete plugin; }
}
