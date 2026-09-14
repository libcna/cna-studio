// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Plugins/PluginExtensions.hpp"

#include <algorithm>

namespace CNA::Editor
{
    void PluginExtensionRegistry::addPanel(PluginPanel panel)
    {
        // Replaced rather than appended, so a hot-reload does not leave two panels with one title
        // fighting over the same dock node -- and the second of which draws through an unmapped
        // library. The same rule ComponentRegistry follows for a re-registered descriptor.
        for (PluginPanel& existing : panels_)
        {
            if (existing.title == panel.title)
            {
                existing = std::move(panel);
                return;
            }
        }

        panels_.push_back(std::move(panel));
    }

    void PluginExtensionRegistry::addMenuCommand(PluginMenuCommand command)
    {
        // Appended, unlike a panel: two commands with the same label under one menu are unusual but
        // not wrong -- two plugins may each offer "Export" -- while two panels with one title
        // cannot both exist, because the title *is* the window's identity.
        commands_.push_back(std::move(command));
    }

    std::size_t PluginExtensionRegistry::removeAllFrom(std::string_view ownerId)
    {
        const std::size_t before = panels_.size() + commands_.size();

        panels_.erase(std::remove_if(panels_.begin(), panels_.end(),
                                     [&](const PluginPanel& panel)
                                     { return panel.ownerId == ownerId; }),
                      panels_.end());

        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
                                       [&](const PluginMenuCommand& command)
                                       { return command.ownerId == ownerId; }),
                        commands_.end());

        return before - (panels_.size() + commands_.size());
    }

    std::vector<std::string> PluginExtensionRegistry::getMenuNames() const
    {
        // First-seen order rather than sorted: the menu bar reads left to right and a plugin's
        // menus appearing in the order they registered is at least predictable from something the
        // user can see, which alphabetical order is not.
        std::vector<std::string> names;
        for (const PluginMenuCommand& command : commands_)
        {
            if (std::find(names.begin(), names.end(), command.menu) == names.end())
            {
                names.push_back(command.menu);
            }
        }
        return names;
    }
}
