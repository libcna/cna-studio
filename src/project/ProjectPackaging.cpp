// SPDX-License-Identifier: MS-PL
/**
 * @file ProjectPackaging.cpp
 * @brief The parts of exporting that are the same in every language.
 *
 * `plan.md` STUDIO-02082.
 */

#include "CNA/Studio/Project/ProjectPackaging.hpp"

#include <cctype>
#include <string>

namespace CNA::Studio
{
    std::string studioExportTargetName(std::string_view name)
    {
        std::string identifier;
        for (const char character : name)
        {
            const auto value = static_cast<unsigned char>(character);
            identifier.push_back(std::isalnum(value) != 0 ? character : '_');
        }

        // Trim the underscores a name like "Hello Sprites!" leaves on the end, which are legal but
        // make for an executable nobody would choose to type.
        while (!identifier.empty() && identifier.back() == '_') { identifier.pop_back(); }
        while (!identifier.empty() && identifier.front() == '_') { identifier.erase(identifier.begin()); }

        if (identifier.empty()) { return "CnaGame"; }
        if (std::isdigit(static_cast<unsigned char>(identifier.front())) != 0)
        {
            identifier.insert(identifier.begin(), 'G');
        }
        return identifier;
    }
}
