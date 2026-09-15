// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPluginMenus.cpp
 * @brief Turning a plugin's menu commands into registry actions and menu rows.
 */

#include "CNA/Studio/ShellPanels/StudioPluginMenus.hpp"

#include "CNA/Studio/Plugins/PluginExtensions.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <set>
#include <string>
#include <vector>

namespace CNA::Studio
{
    namespace
    {
        /** @brief An id-safe form of @p text: letters, digits and dashes. */
        std::string slug(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char character : text)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) != 0) { out.push_back(static_cast<char>(std::tolower(byte))); }
                else if (!out.empty() && out.back() != '-') { out.push_back('-'); }
            }
            while (!out.empty() && out.back() == '-') { out.pop_back(); }
            return out.empty() ? std::string{"command"} : out;
        }
    }

    std::size_t bindStudioPluginMenus(StudioShell& shell, StudioContext& context, StudioLog& log)
    {
        StudioActionRegistry& actions = shell.actions();

        // Everything registered last time, first. A plugin that has been unloaded must leave no row
        // behind: its `invoke` points into a library the host is about to close, and a menu row
        // that called it would be calling code that is no longer mapped.
        std::vector<std::string> stale;
        for (const StudioAction& action : actions.commands())
        {
            if (action.id.rfind(kStudioPluginActionPrefix, 0) == 0) { stale.push_back(action.id); }
        }
        for (const std::string& id : stale) { (void)actions.remove(id); }

        // Copied, because invoking a command may unload the plugin that registered it -- a "Reload
        // Plugin" command does exactly that -- and the vector it lives in would be gone underneath
        // whoever was walking it.
        const std::vector<PluginMenuCommand> commands =
            context.getPluginExtensions().getMenuCommands();

        // One id per command, indexed, because two plugins may each offer "Export" and a slug of
        // the label alone would give them the same id -- at which point one of them silently
        // replaces the other in the registry.
        std::vector<std::pair<std::string, std::string>> rows;   // menu, action id
        rows.reserve(commands.size());

        for (std::size_t i = 0; i < commands.size(); ++i)
        {
            const PluginMenuCommand& command = commands[i];
            if (!command.invoke || command.label.empty()) { continue; }

            StudioAction action;
            action.id = std::string{kStudioPluginActionPrefix} + slug(command.ownerId) + "."
                      + slug(command.label) + "." + std::to_string(i);
            action.label = command.label;
            action.description = "From the '" + command.ownerId + "' plugin.";
            action.category = StudioActionCategory::Tools;

            const std::function<void(StudioContext&)> invoke = command.invoke;
            const std::string label = command.label;
            action.run = [invoke, label, &context, &log] {
                try { invoke(context); }
                catch (const std::exception& thrown)
                {
                    // A plugin command that throws is a plugin problem and is reported as one.
                    // Letting it escape would end the frame inside the menu that invoked it, with
                    // the menu stack half unwound.
                    log.append(LogSeverity::Warning,
                               "Plugin command '" + label + "' failed: " + thrown.what());
                }
                catch (...)
                {
                    log.append(LogSeverity::Warning,
                               "Plugin command '" + label + "' failed with an unknown exception.");
                }
            };

            const std::string id = action.id;

            // `add` reports whether it *replaced* something, which for a freshly built plugin id
            // is always false -- so its return says nothing about success and reading it as such
            // would drop every row.
            (void)actions.add(std::move(action));
            rows.emplace_back(command.menu.empty() ? std::string{"Tools"} : command.menu, id);
        }

        // Studio's own menus from scratch, so a plugin menu cannot outlive the plugin that asked
        // for it -- and so this function is idempotent however many times a plugin is reloaded.
        std::vector<StudioMenuDefinition> menus = StudioShell::defaultMenus();

        const auto findMenu = [&](const std::string& title) -> StudioMenuDefinition* {
            for (StudioMenuDefinition& menu : menus)
            {
                if (menu.title == title) { return &menu; }
            }
            return nullptr;
        };

        std::set<std::string> separated;
        for (const auto& [title, id] : rows)
        {
            StudioMenuDefinition* menu = findMenu(title);
            if (menu == nullptr)
            {
                // A menu only the plugin knows the name of. Inserted before Help, which is last in
                // every application anybody has used, rather than appended after it.
                StudioMenuDefinition added;
                added.title = title;
                const auto help = std::find_if(menus.begin(), menus.end(),
                    [](const StudioMenuDefinition& candidate) { return candidate.title == "Help"; });
                menus.insert(help == menus.end() ? menus.end() : help, std::move(added));
                menu = findMenu(title);
            }

            if (menu == nullptr) { continue; }

            // A separator before the *first* plugin row in a menu Studio already had, so a user can
            // see where the editor's own commands end. Tracked here rather than on the menu model:
            // it is this function's bookkeeping, and a field on the shared definition would be a
            // field every other caller would have to know not to set.
            if (!menu->entries.empty() && separated.insert(title).second)
            {
                menu->entries.emplace_back(std::string{kStudioMenuSeparatorId});
            }
            menu->entries.emplace_back(id);
        }

        shell.setMenus(std::move(menus));
        return rows.size();
    }
}
