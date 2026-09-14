// SPDX-License-Identifier: MS-PL
/**
 * @file StudioWorkspaceStore.cpp
 * @brief Reads and writes the workspace layout file.
 */

#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"

#include "CNA/Studio/Core/UserPaths.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace CNA::Studio
{
    std::string StudioWorkspaceStore::defaultPath()
    {
        // Configuration, not state: a workspace arrangement is something the user made, and a
        // machine that swept it would have thrown away work rather than a cache.
        const std::string configDirectory = getStudioConfigDirectory();
        if (configDirectory.empty()) { return {}; }
        return (std::filesystem::path{configDirectory} / kFileName).generic_string();
    }

    bool StudioWorkspaceStore::save(const JsonValue& layout, std::string* outProblem) const
    {
        const auto fail = [&](const std::string& reason) {
            if (outProblem != nullptr) { *outProblem = reason; }
            return false;
        };

        if (path_.empty()) { return fail("there is nowhere on this machine to store the layout"); }

        const std::filesystem::path path{path_};
        std::error_code code;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), code);
            if (code)
            {
                return fail("cannot create '" + path.parent_path().generic_string() + "': "
                            + code.message());
            }
        }

        // Written beside the original and renamed over it. A Studio killed mid-save then leaves the
        // previous layout intact rather than a half-written file that reads as corrupt -- which
        // would cost the user their arrangement for a reason that had nothing to do with it.
        const std::filesystem::path temporary =
            path.parent_path() / (path.filename().string() + ".tmp");
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) { return fail("cannot write '" + temporary.generic_string() + "'"); }

            JsonValue document = JsonValue::makeObject();
            document.set("fileVersion", kFileVersion);
            document.set("layout", layout);
            stream << Json::write(document, true);
            if (!stream)
            {
                return fail("failed while writing '" + temporary.generic_string() + "'");
            }
        }

        std::filesystem::rename(temporary, path, code);
        if (code)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return fail("cannot replace '" + path.generic_string() + "': " + code.message());
        }
        return true;
    }

    StudioWorkspaceDocument StudioWorkspaceStore::load() const
    {
        StudioWorkspaceDocument result;
        if (path_.empty()) { return result; }

        std::ifstream stream{std::filesystem::path{path_}, std::ios::binary};
        if (!stream)
        {
            // A first run, not a fault.
            return result;
        }

        std::ostringstream contents;
        contents << stream.rdbuf();

        const JsonParseResult parsed = Json::parse(contents.str());
        if (!parsed.succeeded)
        {
            result.problem = "the stored workspace layout could not be read (" + parsed.errorMessage
                           + "), so the default arrangement was used";
            return result;
        }

        const int version = parsed.value["fileVersion"].asInt(0);
        if (version > kFileVersion)
        {
            // Refused rather than guessed at. A newer Studio may have written fields whose absence
            // means something, and half-reading a layout is a worse outcome than the default one.
            result.problem = "the stored workspace layout was written by a newer CNA Studio (file "
                             "version " + std::to_string(version) + "), so the default arrangement "
                             "was used";
            return result;
        }

        result.layout = parsed.value["layout"];
        result.found = true;
        return result;
    }

    bool StudioWorkspaceStore::forget() const
    {
        if (path_.empty()) { return false; }
        std::error_code code;
        return std::filesystem::remove(std::filesystem::path{path_}, code) && !code;
    }
}
