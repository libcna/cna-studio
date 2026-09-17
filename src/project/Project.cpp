// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Project/Project.hpp"

#include "CNA/Studio/Project/RendererCatalog.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace CNA::Studio
{
    const char* toString(ProjectKind kind)
    {
        return kind == ProjectKind::XnaCompatible ? "XnaCompatible" : "CnaNative";
    }

    ProjectKind parseProjectKind(std::string_view text)
    {
        return text == "XnaCompatible" ? ProjectKind::XnaCompatible : ProjectKind::CnaNative;
    }


    bool Project::setTargetProfiles(std::vector<StudioTargetProfile> profiles)
    {
        // Refused rather than accepted. A project with no target cannot be built, and taking an
        // empty list here defers the error to the moment somebody presses Build.
        if (profiles.empty()) { return false; }

        targetProfiles_ = std::move(profiles);
        if (activeTargetProfile_ >= targetProfiles_.size()) { activeTargetProfile_ = 0; }
        defaultGraphicsBackend_ = targetProfiles_[activeTargetProfile_].renderer;
        return true;
    }

    bool Project::setActiveTargetProfileIndex(std::size_t index)
    {
        if (index >= targetProfiles_.size()) { return false; }
        activeTargetProfile_ = index;
        // Kept in step deliberately: `defaultGraphicsBackend` is a serialized contract that the
        // player's discovery and existing project files depend on, so it follows the active
        // profile rather than becoming a second place the renderer is decided.
        defaultGraphicsBackend_ = targetProfiles_[index].renderer;
        return true;
    }

    const StudioTargetProfile& Project::getActiveTargetProfile() const
    {
        return targetProfiles_[std::min(activeTargetProfile_, targetProfiles_.size() - 1)];
    }

    JsonValue Project::toJson() const
    {
        JsonValue json = JsonValue::makeObject();
        json.set("formatVersion", JsonValue{kFormatVersion});
        json.set("name", JsonValue{name_});
        json.set("kind", JsonValue{toString(kind_)});

        // Written only when set, for the reason `gridSnap` is: an additive key that appeared in
        // every file the moment the editor touched it would make the first save of every existing
        // project a diff nobody asked for. A project that names no language is C++ -- see
        // Project::getLanguage -- so the absent key and "cpp" mean the same thing today, and the
        // key exists so that they stop meaning the same thing without a migration when they must.
        if (!language_.empty()) { json.set("language", JsonValue{language_}); }

        json.set("startupScene", JsonValue{startupScene_});
        json.set("assetDirectory", JsonValue{assetDirectory_});
        json.set("sceneDirectory", JsonValue{sceneDirectory_});
        json.set("defaultGraphicsBackend", JsonValue{defaultGraphicsBackend_});

        JsonValue profiles = JsonValue::makeArray();
        for (const StudioTargetProfile& profile : targetProfiles_)
        {
            profiles.append(studioTargetProfileToJson(profile));
        }
        json.set("targetProfiles", std::move(profiles));
        json.set("activeTargetProfile", static_cast<int>(activeTargetProfile_));

        JsonValue platforms = JsonValue::makeArray();
        for (const std::string& platform : targetPlatforms_) { platforms.append(JsonValue{platform}); }
        json.set("targetPlatforms", std::move(platforms));

        JsonValue layers = JsonValue::makeArray();
        for (const std::string& layer : layers_) { layers.append(JsonValue{layer}); }
        json.set("layers", std::move(layers));

        // Written only when set. An additive field that appears in every file the moment the
        // editor touches it would make the first save of every existing project a diff -- and
        // zero is exactly the behaviour of a project that has never heard of the setting.
        if (gridSnap_ > 0.0f) { json.set("gridSnap", JsonValue{static_cast<double>(gridSnap_)}); }

        JsonValue modules = JsonValue::makeArray();
        for (const std::string& module : modules_) { modules.append(JsonValue{module}); }
        json.set("modules", std::move(modules));

        if (!plugins_.empty())
        {
            JsonValue plugins = JsonValue::makeArray();
            for (const std::string& plugin : plugins_) { plugins.append(JsonValue{plugin}); }
            json.set("plugins", std::move(plugins));
        }
        return json;
    }

    const FormatMigrator& getProjectFormatMigrator()
    {
        static const FormatMigrator migrator{"project", Project::kFormatVersion};
        return migrator;
    }

    void Project::setGridSnap(float step)
    {
        // Negative is refused rather than clamped: it is not a smaller step, it is a value with no
        // meaning, and rounding to it would move an entity to a coordinate nothing else agrees on.
        if (step < 0.0f) { return; }
        gridSnap_ = step;
    }

    void Project::setLayers(std::vector<std::string> layers)
    {
        // Refused rather than accepted and repaired, so a caller that computed an empty list finds
        // out here instead of discovering later that its change did not take.
        if (layers.empty()) { return; }
        layers_ = std::move(layers);
    }

    ProjectLoadResult Project::loadFromJson(const JsonValue& json, const FormatMigrator* migrator)
    {
        ProjectLoadResult result;

        if (!json.isObject())
        {
            result.errorMessage = "project root is not a JSON object";
            return result;
        }

        const FormatMigrator& chain = migrator != nullptr ? *migrator : getProjectFormatMigrator();

        JsonValue upgraded;
        const JsonValue* source = &json;

        if (json["formatVersion"].asInt(0) != chain.getCurrentVersion())
        {
            upgraded = json;

            const FormatMigrationResult migration = chain.migrate(upgraded);
            if (!migration.succeeded)
            {
                result.errorMessage = migration.errorMessage;
                return result;
            }
            for (const std::string& step : migration.applied)
            {
                result.warnings.push_back("upgraded from an older project format: " + step);
            }

            source = &upgraded;
        }

        const JsonValue& document = *source;

        name_ = document["name"].asString("Untitled");
        kind_ = parseProjectKind(document["kind"].asString("CnaNative"));

        // Left empty when the key is absent rather than defaulted to a name this file has no
        // business knowing. Which language an unmarked project is written in is a question for the
        // registry that knows what this build implements, and answering it here would put a second
        // opinion in the loader.
        language_ = document["language"].asString();

        startupScene_ = document["startupScene"].asString();
        assetDirectory_ = document["assetDirectory"].asString("Assets");
        sceneDirectory_ = document["sceneDirectory"].asString("Scenes");
        // The JSON key stays `defaultGraphicsBackend` -- it is a serialized contract that existing
        // project files already carry. The *value* it takes is a CNA renderer identity, and CNA's
        // renderer registry has been reorganised since the prototype wrote these files.
        defaultGraphicsBackend_ = document["defaultGraphicsBackend"].asString(kDefaultRenderer);

        if (findRenderer(defaultGraphicsBackend_) == nullptr)
        {
            const RendererAlias* alias = findLegacyRendererAlias(defaultGraphicsBackend_);
            if (alias != nullptr && !alias->replacement.empty())
            {
                // Migrated, and reported. Silently substituting would change which renderer the
                // user's game ships on without telling them.
                result.warnings.push_back(
                    "defaultGraphicsBackend '" + defaultGraphicsBackend_ + "' is no longer a CNA "
                    "renderer; using '" + std::string{alias->replacement} + "' instead. "
                    + std::string{alias->reason});
                defaultGraphicsBackend_ = std::string{alias->replacement};
            }
            else if (alias != nullptr)
            {
                result.warnings.push_back(
                    "defaultGraphicsBackend '" + defaultGraphicsBackend_ + "' has been removed "
                    "from CNA and has no replacement. " + std::string{alias->reason}
                    + " Choose a renderer before building.");
            }
            else
            {
                result.warnings.push_back("unknown defaultGraphicsBackend '" + defaultGraphicsBackend_
                                          + "'; the Play button will need one chosen explicitly");
            }
        }

        // --- Target profiles ---------------------------------------------------------------------
        targetProfiles_.clear();
        for (const JsonValue& profile : document["targetProfiles"].getElements())
        {
            StudioTargetProfile parsed = studioTargetProfileFromJson(profile);
            const StudioProfileValidation validation = validateStudioTargetProfile(parsed);
            for (const StudioProfileProblem& problem : validation.problems)
            {
                result.warnings.push_back("target profile '" + parsed.name + "': " + problem.message);
            }
            targetProfiles_.push_back(std::move(parsed));
        }

        if (targetProfiles_.empty())
        {
            // A project written before profiles existed -- which is every project the prototype
            // wrote. Its one renderer string becomes one profile, so the build targets a user
            // already had keep working and the richer model arrives without an import step.
            StudioTargetProfile migrated = StudioTargetProfile::defaults();
            migrated.name = "Default";
            migrated.renderer = defaultGraphicsBackend_;
            (void) validateStudioTargetProfile(migrated);
            targetProfiles_.push_back(std::move(migrated));

            if (document.contains("defaultGraphicsBackend"))
            {
                result.warnings.push_back(
                    "this project predates build target profiles; its renderer '"
                    + defaultGraphicsBackend_ + "' became the profile 'Default'. Saving the project "
                    "writes the profile and keeps the old field in step.");
            }
        }

        activeTargetProfile_ = 0;
        const int requested = document["activeTargetProfile"].asInt(0);
        if (requested > 0 && static_cast<std::size_t>(requested) < targetProfiles_.size())
        {
            activeTargetProfile_ = static_cast<std::size_t>(requested);
        }
        else if (requested != 0)
        {
            result.warnings.push_back(
                "activeTargetProfile " + std::to_string(requested) + " is out of range; using the "
                "first profile.");
        }

        targetPlatforms_.clear();
        for (const JsonValue& platform : document["targetPlatforms"].getElements())
        {
            targetPlatforms_.push_back(platform.asString());
        }
        if (targetPlatforms_.empty()) { targetPlatforms_.push_back("linux-x64"); }

        // Absent in every project written before the setting existed, and absent again in any
        // project that never sets it, so the fallback is the behaviour those files already had.
        gridSnap_ = std::max(0.0f, document["gridSnap"].asFloat(0.0f));

        layers_.clear();
        for (const JsonValue& layer : document["layers"].getElements())
        {
            // A blank name would be a layer nothing could refer to and everything could be
            // mistaken for, so it is dropped rather than kept as an unnameable entry.
            const std::string name = layer.asString();
            if (!name.empty()) { layers_.push_back(name); }
        }
        if (layers_.empty())
        {
            // Both the "written by a build before layers existed" case and the "hand-edited to
            // nothing" case. A project with no layers has no valid layer for an entity to be on.
            layers_.push_back(kDefaultLayer);
        }

        modules_.clear();
        for (const JsonValue& module : document["modules"].getElements()) { modules_.push_back(module.asString()); }
        if (modules_.empty()) { modules_.push_back("cna-core"); }

        plugins_.clear();
        for (const JsonValue& plugin : document["plugins"].getElements()) { plugins_.push_back(plugin.asString()); }

        if (kind_ == ProjectKind::XnaCompatible && !startupScene_.empty())
        {
            result.warnings.push_back("an XnaCompatible project declares a startupScene; Studio will "
                                      "not use it, because scene loading is the game's own responsibility");
        }

        result.succeeded = true;
        return result;
    }

    ProjectLoadResult Project::loadFromFile(const std::string& path, const FormatMigrator* migrator)
    {
        ProjectLoadResult result;

        std::ifstream stream{path, std::ios::binary};
        if (!stream)
        {
            result.errorMessage = "cannot open '" + path + "'";
            return result;
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        const JsonParseResult parsed = Json::parse(buffer.str());
        if (!parsed.succeeded)
        {
            result.errorMessage = "'" + path + "': " + parsed.errorMessage + " at offset "
                                + std::to_string(parsed.errorOffset);
            return result;
        }

        result = loadFromJson(parsed.value, migrator);
        if (!result.succeeded) { return result; }

        std::error_code errorCode;
        const std::filesystem::path absolute = std::filesystem::absolute(path, errorCode);
        const std::filesystem::path resolved = errorCode ? std::filesystem::path{path} : absolute;
        filePath_ = resolved.generic_string();
        rootPath_ = resolved.parent_path().generic_string();
        return result;
    }

    bool Project::saveToFile(const std::string& path, std::string* errorMessage)
    {
        const std::string target = path.empty() ? filePath_ : path;
        if (target.empty())
        {
            if (errorMessage != nullptr) { *errorMessage = "no project file path set"; }
            return false;
        }

        std::error_code errorCode;
        const std::filesystem::path filePath{target};
        if (filePath.has_parent_path())
        {
            std::filesystem::create_directories(filePath.parent_path(), errorCode);
            if (errorCode)
            {
                if (errorMessage != nullptr) { *errorMessage = "cannot create directory: " + errorCode.message(); }
                return false;
            }
        }

        std::ofstream stream{target, std::ios::binary | std::ios::trunc};
        if (!stream)
        {
            if (errorMessage != nullptr) { *errorMessage = "cannot open '" + target + "' for writing"; }
            return false;
        }

        stream << Json::write(toJson(), true);
        if (!stream)
        {
            if (errorMessage != nullptr) { *errorMessage = "write to '" + target + "' failed"; }
            return false;
        }

        const std::filesystem::path absolute = std::filesystem::absolute(target, errorCode);
        const std::filesystem::path resolved = errorCode ? filePath : absolute;
        filePath_ = resolved.generic_string();
        rootPath_ = resolved.parent_path().generic_string();
        return true;
    }

    std::string Project::resolvePath(std::string_view relativePath) const
    {
        if (rootPath_.empty()) { return std::string{relativePath}; }
        return (std::filesystem::path{rootPath_} / std::filesystem::path{relativePath}).generic_string();
    }

    Project Project::createDefault(std::string name, std::string rootPath)
    {
        Project project;
        project.name_ = std::move(name);
        project.rootPath_ = rootPath;
        project.filePath_ = (std::filesystem::path{rootPath} / (project.name_ + kFileExtension)).generic_string();
        project.startupScene_ = "Scenes/MainMenu.cnascene";
        return project;
    }
}
