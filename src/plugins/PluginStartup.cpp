// SPDX-License-Identifier: MS-PL
/**
 * @file PluginStartup.cpp
 * @brief Finding and loading the project's plugins when a Studio starts.
 */

#include "CNA/Studio/Plugins/PluginStartup.hpp"

#include "CNA/Studio/Plugins/Plugin.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <filesystem>
#include <system_error>

namespace CNA::Studio
{
    StudioPluginLoad studioLoadPlugins(PluginHost& host, StudioContext& context,
                                       const std::string& pluginDirectory,
                                       const std::string& executablePath)
    {
        StudioPluginLoad result;

        std::string directory = pluginDirectory;
        if (directory.empty())
        {
            // "Beside Studio" needs to know where the editor is. With no executable path -- which
            // is every embedded and test caller -- the same expression would resolve to "plugins"
            // relative to the *working directory*, so an editor started from the wrong folder
            // would load a stranger's plugins and one started from the right one would behave
            // differently for reasons nothing on screen explains. Found by a test that passed from
            // the repository root and failed under ctest.
            if (executablePath.empty()) { return result; }

            directory =
                (std::filesystem::path{executablePath}.parent_path() / "plugins").generic_string();
        }

        std::error_code errorCode;
        if (!std::filesystem::is_directory(directory, errorCode))
        {
            // Silent. Having no plugins is the ordinary case, and an editor that logged a warning
            // about a directory nobody created would train its users to ignore warnings.
            return result;
        }

        result.directory = directory;

        const std::vector<LoadedPlugin> found = host.discover(directory);
        result.discovered = found.size();
        if (found.empty()) { return result; }

        for (const LoadedPlugin& plugin : found)
        {
            if (plugin.loaded) { continue; }

            // Reported per plugin, with the manifest's own words for what is wrong. A single
            // "some plugins failed" is a message a user cannot act on.
            context.log(LogSeverity::Warning,
                        "Plugin '" + plugin.manifest.id + "' was not loaded: " + plugin.error);
        }

        result.active = host.loadAll(context);

        for (const LoadedPlugin& plugin : host.getPlugins())
        {
            if (!plugin.loaded || plugin.active) { continue; }
            context.log(LogSeverity::Warning,
                        "Plugin '" + plugin.manifest.id + "' failed to load: " + plugin.error);
        }

        context.log(LogSeverity::Info,
                    "Plugins: " + std::to_string(result.active) + " of "
                        + std::to_string(found.size()) + " loaded from " + directory + ".");
        return result;
    }

    void studioUnloadPlugins(PluginHost& host, StudioContext& context)
    {
        host.unloadAll(context);
    }
}
