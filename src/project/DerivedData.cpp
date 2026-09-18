// SPDX-License-Identifier: MS-PL
/**
 * @file DerivedData.cpp
 * @brief Where regenerable things live (`plan.md` STUDIO-09015).
 */

#include "CNA/Studio/Project/DerivedData.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <sstream>

#include "CNA/Studio/Core/Sha256.hpp"
#include "CNA/Studio/Core/UserPaths.hpp"

namespace CNA::Studio
{
    namespace
    {
        /** @brief @p name with anything that is not a letter, digit, dash or underscore removed. */
        std::string sanitised(std::string_view name)
        {
            std::string out;
            out.reserve(name.size());
            for (const char character : name)
            {
                const unsigned char byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) != 0 || character == '-' || character == '_')
                {
                    out.push_back(character);
                }
            }
            return out;
        }
    }

    std::string studioDerivedDataDirectory(const std::string& projectFilePath)
    {
        if (projectFilePath.empty()) { return {}; }

        const std::string stateDirectory = getStudioStateDirectory();
        if (stateDirectory.empty()) { return {}; }

        const std::filesystem::path project{projectFilePath};
        std::string name = sanitised(project.stem().string());
        if (name.empty()) { name = "project"; }

        // The name *and* a hash of the path: the name is what makes the directory readable by a
        // person who opens it, and the hash is what keeps two projects called `Game` in different
        // places from sharing one. Sixteen hexadecimal digits is plenty for that job and keeps the
        // directory name short enough to read.
        const std::string digest = studioSha256Hex(std::string_view{project.generic_string()});

        return (std::filesystem::path{stateDirectory} / "derived"
                / (name + "-" + digest.substr(0, 16)))
            .generic_string();
    }

    bool studioPathIsInsideProject(const std::string& path, const std::string& projectRoot)
    {
        if (path.empty() || projectRoot.empty()) { return false; }

        // Lexically, and without touching the disk: the paths being compared may not exist yet,
        // and `weakly_canonical` on a path that does not exist is a different question.
        const std::filesystem::path candidate =
            std::filesystem::path{path}.lexically_normal();
        const std::filesystem::path root =
            std::filesystem::path{projectRoot}.lexically_normal();

        const std::string candidateText = candidate.generic_string();
        const std::string rootText = root.generic_string();

        if (candidateText == rootText) { return true; }
        if (candidateText.size() <= rootText.size()) { return false; }
        if (candidateText.compare(0, rootText.size(), rootText) != 0) { return false; }

        // A separator at the boundary, or `/project-backup` would read as inside `/project`.
        const char boundary = candidateText[rootText.size()];
        return boundary == '/' || (rootText.back() == '/' );
    }

}
