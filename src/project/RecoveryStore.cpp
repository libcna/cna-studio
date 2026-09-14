// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Project/RecoveryStore.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace CNA::Editor
{
    namespace
    {
        /** @brief Reads a whole file, or returns an empty optional. */
        std::optional<std::string> readWholeFile(const std::filesystem::path& path)
        {
            std::ifstream stream{path, std::ios::binary};
            if (!stream) { return std::nullopt; }

            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }

        /** @brief Parses one snapshot file, or returns an empty optional when it is unusable. */
        std::optional<RecoverySnapshot> readSnapshot(const std::filesystem::path& path)
        {
            const std::optional<std::string> text = readWholeFile(path);
            if (!text) { return std::nullopt; }

            const JsonParseResult parsed = Json::parse(*text);
            if (!parsed.succeeded || !parsed.value.isObject()) { return std::nullopt; }

            // A snapshot from a newer build is skipped rather than guessed at. Restoring a document
            // this build cannot fully understand would quietly discard whatever it did not read,
            // and the user would have no way to know which parts.
            if (parsed.value["formatVersion"].asInt(0) > RecoveryStore::kFormatVersion)
            {
                return std::nullopt;
            }

            RecoverySnapshot snapshot;
            snapshot.filePath = path.generic_string();
            snapshot.projectPath = parsed.value["projectPath"].asString();
            snapshot.scenePath = parsed.value["scenePath"].asString();
            snapshot.sceneName = parsed.value["sceneName"].asString();
            snapshot.sceneId = Uuid::parse(parsed.value["sceneId"].asString());
            snapshot.savedAtSeconds = static_cast<std::int64_t>(parsed.value["savedAt"].asNumber(0.0));
            snapshot.scene = parsed.value["scene"];

            if (!snapshot.scene.isObject()) { return std::nullopt; }
            return snapshot;
        }
    }

    bool RecoveryStore::write(const RecoverySnapshot& snapshot, std::string* errorMessage) const
    {
        const auto fail = [&](std::string reason) {
            if (errorMessage != nullptr) { *errorMessage = std::move(reason); }
            return false;
        };

        if (directory_.empty()) { return fail("no recovery directory is configured"); }
        if (!snapshot.sceneId.isValid()) { return fail("the scene has no id to file the snapshot under"); }

        std::error_code errorCode;
        const std::filesystem::path directory{directory_};
        std::filesystem::create_directories(directory, errorCode);
        if (errorCode) { return fail("cannot create '" + directory_ + "': " + errorCode.message()); }

        JsonValue document = JsonValue::makeObject();
        document.set("formatVersion", JsonValue{kFormatVersion});
        document.set("projectPath", JsonValue{snapshot.projectPath});
        document.set("scenePath", JsonValue{snapshot.scenePath});
        document.set("sceneName", JsonValue{snapshot.sceneName});
        document.set("sceneId", JsonValue{snapshot.sceneId.toString()});
        document.set("savedAt", JsonValue{snapshot.savedAtSeconds});
        document.set("scene", snapshot.scene);

        const std::filesystem::path target = directory / (snapshot.sceneId.toString() + kExtension);
        const std::filesystem::path temporary = target.string() + ".tmp";

        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) { return fail("cannot open '" + temporary.generic_string() + "' for writing"); }

            stream << Json::write(document, true);
            if (!stream) { return fail("write to '" + temporary.generic_string() + "' failed"); }
        }

        // Rename over the old snapshot rather than truncating it in place. A crash during a
        // snapshot then leaves the *previous* one intact, which is the whole point: a half-written
        // recovery file fails to load at the one moment it is needed, having already convinced the
        // user their work was safe.
        std::filesystem::rename(temporary, target, errorCode);
        if (errorCode)
        {
            std::filesystem::remove(temporary, errorCode);
            return fail("cannot replace '" + target.generic_string() + "'");
        }

        return true;
    }

    bool RecoveryStore::discard(const Uuid& sceneId) const
    {
        if (directory_.empty() || !sceneId.isValid()) { return false; }

        std::error_code errorCode;
        const std::filesystem::path target =
            std::filesystem::path{directory_} / (sceneId.toString() + kExtension);
        return std::filesystem::remove(target, errorCode) && !errorCode;
    }

    std::vector<RecoverySnapshot> RecoveryStore::list() const
    {
        std::vector<RecoverySnapshot> snapshots;
        if (directory_.empty()) { return snapshots; }

        std::error_code errorCode;
        std::filesystem::directory_iterator entries{directory_, errorCode};
        if (errorCode) { return snapshots; }

        for (const std::filesystem::directory_entry& entry : entries)
        {
            if (!entry.is_regular_file(errorCode) || errorCode) { continue; }
            if (entry.path().extension() != kExtension) { continue; }

            // An unreadable file is skipped, not reported as an error. Recovery is a best-effort
            // path by construction; one corrupt snapshot must not hide the others.
            if (std::optional<RecoverySnapshot> snapshot = readSnapshot(entry.path()))
            {
                snapshots.push_back(std::move(*snapshot));
            }
        }

        std::sort(snapshots.begin(), snapshots.end(),
                  [](const RecoverySnapshot& lhs, const RecoverySnapshot& rhs) {
                      if (lhs.savedAtSeconds != rhs.savedAtSeconds)
                      {
                          return lhs.savedAtSeconds > rhs.savedAtSeconds;
                      }
                      // Ties broken by file name so the order is total and testable: two snapshots
                      // written in the same second are otherwise ordered by whatever the directory
                      // iterator happened to hand back.
                      return lhs.filePath < rhs.filePath;
                  });
        return snapshots;
    }

    std::optional<RecoverySnapshot> RecoveryStore::findForProject(const std::string& projectPath) const
    {
        for (RecoverySnapshot& snapshot : list())
        {
            if (snapshot.projectPath == projectPath) { return std::move(snapshot); }
        }
        return std::nullopt;
    }

    std::string getDefaultRecoveryDirectory()
    {
        const auto environment = [](const char* name) -> std::string {
            const char* value = std::getenv(name);
            return value != nullptr ? std::string{value} : std::string{};
        };

        std::filesystem::path base;

        const std::string stateHome = environment("XDG_STATE_HOME");
        const std::string localAppData = environment("LOCALAPPDATA");
        const std::string appData = environment("APPDATA");
        const std::string home = environment("HOME");

        if (!stateHome.empty()) { base = stateHome; }
        else if (!localAppData.empty()) { base = localAppData; }
        else if (!appData.empty()) { base = appData; }
        else if (!home.empty()) { base = std::filesystem::path{home} / ".local" / "state"; }
        else
        {
            // Worse than the others -- a temporary directory can be swept between reboots -- but
            // never nothing. A user with no home directory still deserves an autosave.
            std::error_code errorCode;
            base = std::filesystem::temp_directory_path(errorCode);
            if (errorCode) { return {}; }
        }

        return (base / "cna-editor" / "recovery").generic_string();
    }
}

namespace CNA::Editor
{
    std::string formatRecoveryTime(std::int64_t unixSeconds)
    {
        const std::time_t stamp = static_cast<std::time_t>(unixSeconds);

        std::tm broken{};
#if defined(_WIN32)
        if (localtime_s(&broken, &stamp) != 0) { return "an unknown time"; }
#else
        if (localtime_r(&stamp, &broken) == nullptr) { return "an unknown time"; }
#endif

        char text[32] = {};
        if (std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &broken) == 0)
        {
            return "an unknown time";
        }
        return text;
    }
}
