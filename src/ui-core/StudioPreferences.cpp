// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPreferences.cpp
 * @brief What a user has decided about Studio, and the file it lives in.
 */

#include "CNA/Studio/UiCore/StudioPreferences.hpp"

#include "CNA/Studio/Core/UserPaths.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Reads a string, leaving @p fallback when the field is absent or empty. */
        std::string stringOr(const JsonValue& value, const char* key, std::string fallback)
        {
            const std::string read = value[key].asString();
            return read.empty() ? std::move(fallback) : read;
        }
    }

    std::string_view studioNavigationStyleName(StudioNavigationStyle style)
    {
        switch (style)
        {
            case StudioNavigationStyle::Studio:  return "studio";
            case StudioNavigationStyle::Maya:    return "maya";
            case StudioNavigationStyle::Blender: return "blender";
        }
        return "studio";
    }

    bool parseStudioNavigationStyle(std::string_view name, StudioNavigationStyle& out)
    {
        if (name == "studio")  { out = StudioNavigationStyle::Studio;  return true; }
        if (name == "maya")    { out = StudioNavigationStyle::Maya;    return true; }
        if (name == "blender") { out = StudioNavigationStyle::Blender; return true; }
        return false;
    }

    std::size_t StudioPreferences::applyShortcuts(StudioActionRegistry& registry) const
    {
        std::size_t applied = 0;
        for (const StudioShortcutOverride& override : shortcuts)
        {
            // A rebinding naming a command this build does not have is skipped rather than
            // refused: an upgrade that retired a command must cost the user that one shortcut and
            // not the rest of them.
            if (registry.find(override.actionId) == nullptr) { continue; }
            if (registry.rebind(override.actionId, override.shortcut)) { ++applied; }
        }
        return applied;
    }

    bool operator==(const StudioPreferences& lhs, const StudioPreferences& rhs)
    {
        if (lhs.shortcuts.size() != rhs.shortcuts.size()) { return false; }
        for (std::size_t i = 0; i < lhs.shortcuts.size(); ++i)
        {
            if (lhs.shortcuts[i].actionId != rhs.shortcuts[i].actionId) { return false; }
            if (!(lhs.shortcuts[i].shortcut == rhs.shortcuts[i].shortcut)) { return false; }
        }

        return lhs.theme == rhs.theme && lhs.uiScale == rhs.uiScale
            && lhs.fontSizePoints == rhs.fontSizePoints && lhs.navigation == rhs.navigation
            && lhs.cameraSpeed == rhs.cameraSpeed && lhs.invertZoom == rhs.invertZoom
            && lhs.gridOnGroundPlane == rhs.gridOnGroundPlane
            && lhs.autosaveSeconds == rhs.autosaveSeconds
            && lhs.reopenLastProject == rhs.reopenLastProject
            && lhs.externalEditor == rhs.externalEditor && lhs.cmakePath == rhs.cmakePath
            && lhs.buildJobs == rhs.buildJobs && lhs.defaultLayout == rhs.defaultLayout;
    }

    StudioPreferences studioClampPreferences(StudioPreferences preferences)
    {
        // Every bound here is a value that would otherwise produce a Studio the user cannot
        // recover from without finding and deleting the file: a UI scale of zero has no pixels, a
        // font size of zero has no text, and an autosave of one second saves over their work while
        // they think.
        if (preferences.theme != "light") { preferences.theme = "dark"; }
        preferences.uiScale = std::clamp(preferences.uiScale, 0.5f, 4.0f);
        preferences.fontSizePoints = std::clamp(preferences.fontSizePoints, 8.0f, 32.0f);
        preferences.cameraSpeed = std::clamp(preferences.cameraSpeed, 0.1f, 10.0f);

        // Zero is a real answer -- "do not autosave" -- so it is kept rather than raised, and
        // anything else is given a floor a user can actually work through.
        if (preferences.autosaveSeconds != 0)
        {
            preferences.autosaveSeconds = std::clamp(preferences.autosaveSeconds, 30, 3600);
        }
        preferences.buildJobs = std::clamp(preferences.buildJobs, 0, 256);

        // A rebinding with no command is a row in the preferences UI that names nothing, and a
        // duplicate is two answers to "what is this command bound to".
        std::vector<StudioShortcutOverride> kept;
        for (StudioShortcutOverride& override : preferences.shortcuts)
        {
            if (override.actionId.empty()) { continue; }
            const auto seen = std::find_if(kept.begin(), kept.end(),
                [&](const StudioShortcutOverride& other) {
                    return other.actionId == override.actionId;
                });
            if (seen != kept.end()) { seen->shortcut = override.shortcut; continue; }
            kept.push_back(std::move(override));
        }
        preferences.shortcuts = std::move(kept);

        return preferences;
    }

    JsonValue studioPreferencesToJson(const StudioPreferences& preferences)
    {
        JsonValue document = JsonValue::makeObject();
        document.set("fileVersion", StudioPreferencesStore::kFileVersion);

        JsonValue appearance = JsonValue::makeObject();
        appearance.set("theme", preferences.theme);
        appearance.set("uiScale", static_cast<double>(preferences.uiScale));
        appearance.set("fontSizePoints", static_cast<double>(preferences.fontSizePoints));
        document.set("appearance", std::move(appearance));

        JsonValue viewport = JsonValue::makeObject();
        viewport.set("navigation", std::string{studioNavigationStyleName(preferences.navigation)});
        viewport.set("cameraSpeed", static_cast<double>(preferences.cameraSpeed));
        viewport.set("invertZoom", preferences.invertZoom);
        viewport.set("gridOnGroundPlane", preferences.gridOnGroundPlane);
        document.set("viewport", std::move(viewport));

        JsonValue documents = JsonValue::makeObject();
        documents.set("autosaveSeconds", preferences.autosaveSeconds);
        documents.set("reopenLastProject", preferences.reopenLastProject);
        document.set("documents", std::move(documents));

        JsonValue tools = JsonValue::makeObject();
        tools.set("externalEditor", preferences.externalEditor);
        tools.set("cmakePath", preferences.cmakePath);
        tools.set("buildJobs", preferences.buildJobs);
        document.set("tools", std::move(tools));

        JsonValue workspace = JsonValue::makeObject();
        workspace.set("defaultLayout", preferences.defaultLayout);

        // Written as chord *text*, the same text the menus show. A file a person may open should
        // read as the thing they see in the UI rather than as a key code they would have to look
        // up -- and a shortcut edited by hand is then an ordinary thing to do.
        JsonValue shortcuts = JsonValue::makeArray();
        for (const StudioShortcutOverride& override : preferences.shortcuts)
        {
            JsonValue entry = JsonValue::makeObject();
            entry.set("action", override.actionId);
            entry.set("shortcut", describeStudioShortcut(override.shortcut));
            shortcuts.append(std::move(entry));
        }
        workspace.set("shortcuts", std::move(shortcuts));
        document.set("workspace", std::move(workspace));

        return document;
    }

    StudioPreferences studioPreferencesFromJson(const JsonValue& value)
    {
        StudioPreferences preferences;

        // Every field defaults to what it already is, which *is* the migration mechanism: a file
        // written by an older Studio reads correctly by leaving the fields it never heard of
        // alone, and needs no per-version code to do it.
        const JsonValue& appearance = value["appearance"];
        preferences.theme = stringOr(appearance, "theme", preferences.theme);
        preferences.uiScale = appearance["uiScale"].asFloat(preferences.uiScale);
        preferences.fontSizePoints =
            appearance["fontSizePoints"].asFloat(preferences.fontSizePoints);

        const JsonValue& viewport = value["viewport"];
        (void)parseStudioNavigationStyle(viewport["navigation"].asString(), preferences.navigation);
        preferences.cameraSpeed = viewport["cameraSpeed"].asFloat(preferences.cameraSpeed);
        preferences.invertZoom = viewport["invertZoom"].asBoolean(preferences.invertZoom);
        preferences.gridOnGroundPlane =
            viewport["gridOnGroundPlane"].asBoolean(preferences.gridOnGroundPlane);

        const JsonValue& documents = value["documents"];
        preferences.autosaveSeconds =
            documents["autosaveSeconds"].asInt(preferences.autosaveSeconds);
        preferences.reopenLastProject =
            documents["reopenLastProject"].asBoolean(preferences.reopenLastProject);

        const JsonValue& tools = value["tools"];
        preferences.externalEditor = tools["externalEditor"].asString();
        preferences.cmakePath = tools["cmakePath"].asString();
        preferences.buildJobs = tools["buildJobs"].asInt(preferences.buildJobs);

        const JsonValue& workspace = value["workspace"];
        preferences.defaultLayout = workspace["defaultLayout"].asString();

        for (const JsonValue& entry : workspace["shortcuts"].getElements())
        {
            StudioShortcutOverride override;
            override.actionId = entry["action"].asString();
            if (override.actionId.empty()) { continue; }

            const std::string chord = entry["shortcut"].asString();
            // An empty chord is "unbound deliberately", which is a thing a user can mean; an
            // unparseable one is a line this build cannot honour, and skipping the whole entry is
            // better than binding the command to something the user did not ask for.
            if (!chord.empty() && !parseStudioShortcut(chord, override.shortcut)) { continue; }

            preferences.shortcuts.push_back(std::move(override));
        }

        return studioClampPreferences(std::move(preferences));
    }

    std::string StudioPreferencesStore::defaultPath()
    {
        const std::string configDirectory = getStudioConfigDirectory();
        if (configDirectory.empty()) { return {}; }
        return (std::filesystem::path{configDirectory} / kFileName).generic_string();
    }

    bool StudioPreferencesStore::save(const StudioPreferences& preferences,
                                      std::string* outProblem) const
    {
        const auto fail = [&](const std::string& reason) {
            if (outProblem != nullptr) { *outProblem = reason; }
            return false;
        };

        if (path_.empty()) { return fail("there is nowhere on this machine to store preferences"); }

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

        // Temporary and rename, like the workspace file: a Studio killed mid-save leaves the
        // previous preferences rather than a half-written file that reads as corrupt.
        const std::filesystem::path temporary =
            path.parent_path() / (path.filename().string() + ".tmp");
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) { return fail("cannot write '" + temporary.generic_string() + "'"); }

            stream << Json::write(studioPreferencesToJson(studioClampPreferences(preferences)),
                                  true);
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

    StudioPreferencesDocument StudioPreferencesStore::load() const
    {
        StudioPreferencesDocument result;
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
            result.problem = "the stored preferences could not be read (" + parsed.errorMessage
                           + "), so the defaults were used";
            return result;
        }

        const int version = parsed.value["fileVersion"].asInt(0);
        if (version > kFileVersion)
        {
            // Refused rather than half-read. A newer Studio may write a field whose *absence*
            // means something, and guessing at it is how a preference silently changes.
            result.problem = "the stored preferences were written by a newer CNA Studio (file "
                             "version " + std::to_string(version) + "), so the defaults were used";
            return result;
        }

        result.preferences = studioPreferencesFromJson(parsed.value);
        result.found = true;
        return result;
    }

    bool StudioPreferencesStore::forget() const
    {
        if (path_.empty()) { return false; }
        std::error_code code;
        return std::filesystem::remove(std::filesystem::path{path_}, code) && !code;
    }
}
