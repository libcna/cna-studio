// SPDX-License-Identifier: MS-PL
/**
 * @file UserPaths.cpp
 * @brief Resolves the user's configuration and state directories from the environment.
 */

#include "CNA/Studio/Core/UserPaths.hpp"

#include <cstdlib>
#include <filesystem>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Reads an environment variable, treating unset and empty as the same thing. */
        std::string environment(const char* name)
        {
            const char* value = std::getenv(name);
            return value != nullptr ? std::string{value} : std::string{};
        }

        /**
         * @brief The last resort, for a machine with no home directory of any kind.
         *
         * Worse than the alternatives -- a temporary directory can be swept between reboots -- but
         * never nothing: a user with no home directory still deserves an autosave and a remembered
         * layout, and returning an empty path would silently disable both.
         */
        std::filesystem::path temporaryFallback()
        {
            std::error_code code;
            const std::filesystem::path path = std::filesystem::temp_directory_path(code);
            return code ? std::filesystem::path{} : path;
        }
    }

    std::string getStudioConfigDirectory()
    {
        std::filesystem::path base;

        const std::string configHome = environment("XDG_CONFIG_HOME");
        const std::string appData = environment("APPDATA");
        const std::string home = environment("HOME");

        if (!configHome.empty()) { base = configHome; }
        else if (!appData.empty()) { base = appData; }
        else if (!home.empty()) { base = std::filesystem::path{home} / ".config"; }
        else { base = temporaryFallback(); }

        if (base.empty()) { return {}; }
        return (base / "cna-studio").generic_string();
    }

    std::string getStudioStateDirectory()
    {
        std::filesystem::path base;

        const std::string stateHome = environment("XDG_STATE_HOME");
        const std::string localAppData = environment("LOCALAPPDATA");
        const std::string appData = environment("APPDATA");
        const std::string home = environment("HOME");

        if (!stateHome.empty()) { base = stateHome; }
        else if (!localAppData.empty()) { base = localAppData; }
        else if (!appData.empty()) { base = appData; }
        else if (!home.empty()) { base = std::filesystem::path{home} / ".local" / "state"; }
        else { base = temporaryFallback(); }

        if (base.empty()) { return {}; }
        return (base / "cna-studio").generic_string();
    }
}
