// SPDX-License-Identifier: MS-PL
/**
 * @file RecentProjects.cpp
 * @brief The recent-projects list, and the answer to "is that one still there".
 *
 * `plan.md` STUDIO-08002.
 */

#include "CNA/Studio/Project/RecentProjects.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/UserPaths.hpp"
#include "CNA/Studio/Project/Project.hpp"

namespace CNA::Studio
{
    std::string StudioRecentProject::directory() const
    {
        return std::filesystem::path{path}.parent_path().generic_string();
    }

    std::string describeStudioProjectAvailability(const std::string& projectFilePath)
    {
        if (projectFilePath.empty()) { return "no project file was named"; }

        const std::filesystem::path path{projectFilePath};
        std::error_code code;

        if (!std::filesystem::exists(path, code))
        {
            // The directory is mentioned separately because the two have different answers: a
            // project whose directory is gone was deleted or is on an unmounted drive, and one
            // whose directory is there but whose file is not was renamed.
            return std::filesystem::is_directory(path.parent_path(), code)
                ? "the project file is no longer in '" + path.parent_path().generic_string() + "'"
                : "'" + path.parent_path().generic_string() + "' is not there any more";
        }
        if (!std::filesystem::is_regular_file(path, code))
        {
            return "'" + projectFilePath + "' is not a file";
        }

        std::ifstream stream{path, std::ios::binary};
        if (!stream) { return "'" + projectFilePath + "' cannot be read"; }

        return {};
    }

    std::string StudioRecentProjectsStore::defaultPath()
    {
        const std::string configDirectory = getStudioConfigDirectory();
        if (configDirectory.empty()) { return {}; }
        return (std::filesystem::path{configDirectory} / kFileName).generic_string();
    }

    std::vector<StudioRecentProject> StudioRecentProjectsStore::load() const
    {
        std::vector<StudioRecentProject> entries;
        if (path_.empty()) { return entries; }

        std::ifstream stream{path_, std::ios::binary};
        if (!stream) { return entries; }

        const std::string text{std::istreambuf_iterator<char>{stream},
                               std::istreambuf_iterator<char>{}};

        // A corrupt convenience file reads as an empty list rather than as a failure. Nothing about
        // this list is worth refusing to open Studio over.
        const JsonParseResult parsed = Json::parse(text);
        if (!parsed.succeeded || !parsed.value.isObject()) { return entries; }

        for (const JsonValue& entry : parsed.value["projects"].getElements())
        {
            if (!entry.isObject()) { continue; }

            StudioRecentProject recent;
            recent.path = entry["path"].asString();
            if (recent.path.empty()) { continue; }

            recent.name = entry["name"].asString();
            recent.openedAt = static_cast<std::int64_t>(entry["openedAt"].asNumber(0.0));

            // Answered now, not stored. The filesystem changes while Studio is not running, which
            // is precisely the case this list exists to handle gracefully.
            recent.problem = describeStudioProjectAvailability(recent.path);
            recent.available = recent.problem.empty();

            entries.push_back(std::move(recent));
        }

        // Newest first. Sorted on read rather than trusted from the file, so an entry written by
        // hand or by an older Studio still lands where it belongs.
        std::stable_sort(entries.begin(), entries.end(),
            [](const StudioRecentProject& lhs, const StudioRecentProject& rhs) {
                return lhs.openedAt > rhs.openedAt;
            });
        return entries;
    }

    bool StudioRecentProjectsStore::write(const std::vector<StudioRecentProject>& entries,
                                          std::string* outProblem) const
    {
        const auto fail = [&](const std::string& reason) {
            if (outProblem != nullptr) { *outProblem = reason; }
            return false;
        };

        if (path_.empty()) { return fail("there is nowhere on this machine to store the list"); }

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

        JsonValue document = JsonValue::makeObject();
        document.set("formatVersion", JsonValue{1});

        JsonValue projects = JsonValue::makeArray();
        for (const StudioRecentProject& entry : entries)
        {
            JsonValue value = JsonValue::makeObject();
            value.set("path", JsonValue{entry.path});
            value.set("name", JsonValue{entry.name});
            value.set("openedAt", JsonValue{entry.openedAt});
            projects.append(std::move(value));
        }
        document.set("projects", std::move(projects));

        // Temporary and rename, like the preferences and the workspace: a Studio killed mid-save
        // leaves the previous list rather than a half-written file that reads as corrupt.
        const std::filesystem::path temporary =
            path.parent_path() / (path.filename().string() + ".tmp");
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) { return fail("cannot write '" + temporary.generic_string() + "'"); }
            stream << Json::write(document, true);
            if (!stream) { return fail("failed while writing '" + temporary.generic_string() + "'"); }
        }

        std::filesystem::rename(temporary, path, code);
        if (code)
        {
            std::filesystem::remove(temporary, code);
            return fail("cannot replace '" + path.generic_string() + "'");
        }
        return true;
    }

    bool StudioRecentProjectsStore::remember(const std::string& projectFilePath,
                                             const std::string& name,
                                             std::int64_t now,
                                             std::string* outProblem) const
    {
        if (projectFilePath.empty())
        {
            if (outProblem != nullptr) { *outProblem = "no project file was named"; }
            return false;
        }

        // Absolute and normalised, so the same project opened as `./Game/Game.cnaproject` and as
        // `/home/me/Game/Game.cnaproject` is one row rather than two.
        std::error_code code;
        const std::string canonical =
            std::filesystem::absolute(std::filesystem::path{projectFilePath}, code)
                .lexically_normal().generic_string();
        const std::string key = code ? projectFilePath : canonical;

        std::vector<StudioRecentProject> entries = load();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const StudioRecentProject& entry) {
                                         return entry.path == key;
                                     }),
                      entries.end());

        StudioRecentProject recent;
        recent.path = key;
        recent.name = name;
        recent.openedAt = now;
        entries.insert(entries.begin(), std::move(recent));

        if (entries.size() > kMaximumEntries) { entries.resize(kMaximumEntries); }
        return write(entries, outProblem);
    }

    bool StudioRecentProjectsStore::forget(const std::string& projectFilePath,
                                           std::string* outProblem) const
    {
        std::vector<StudioRecentProject> entries = load();
        const std::size_t before = entries.size();

        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const StudioRecentProject& entry) {
                                         return entry.path == projectFilePath;
                                     }),
                      entries.end());

        // Writing anyway when nothing matched would rewrite the file for a no-op, which is one
        // more chance to lose it to a full disk for no benefit.
        if (entries.size() == before) { return true; }
        return write(entries, outProblem);
    }

    bool StudioRecentProjectsStore::clear() const
    {
        if (path_.empty()) { return false; }
        std::error_code code;
        return std::filesystem::remove(std::filesystem::path{path_}, code);
    }
}
