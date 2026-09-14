// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Project/Project.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace CNA::Editor
{
    const char* toString(ProjectKind kind)
    {
        return kind == ProjectKind::XnaCompatible ? "XnaCompatible" : "CnaNative";
    }

    ProjectKind parseProjectKind(std::string_view text)
    {
        return text == "XnaCompatible" ? ProjectKind::XnaCompatible : ProjectKind::CnaNative;
    }

    const std::vector<BackendInfo>& getKnownBackends()
    {
        // Mirrors CNA's cmake/BackendSelection.cmake as of the CNA revision this editor was
        // written against. The support column is the editor's own judgement, not CNA's: it
        // records whether a backend can host a *docked editor UI*, which is a stricter
        // requirement than being able to run a game.
        static const std::vector<BackendInfo> backends{
            {"EASYGL", "easygl", "EasyGL (OpenGL ES)", BackendEditorSupport::EditorSupported,
             "CNA's default on Linux and Emscripten. The reference target for the editor UI."},
            {"VULKAN", "vulkan", "Vulkan", BackendEditorSupport::EditorSupported,
             "Full editor UI support."},
            {"SDL_RENDERER", "sdlrenderer", "SDL_Renderer (2D only)", BackendEditorSupport::EditorSupported,
             "CNA's default off Linux. 2D-only, which the editor UI itself does not mind."},
            {"BGFX", "bgfx", "bgfx", BackendEditorSupport::EditorSupported, "Full editor UI support."},
            {"SDL_GPU", "sdlgpu", "SDL_GPU", BackendEditorSupport::EditorSupported, "Full editor UI support."},
            {"D3D11", "d3d11", "Direct3D 11", BackendEditorSupport::EditorSupported,
             "Windows only. Full editor UI support there."},
            {"D3D12", "d3d12", "Direct3D 12", BackendEditorSupport::EditorSupported,
             "Windows only. Full editor UI support there."},
            {"WEBGPU", "webgpu", "WebGPU", BackendEditorSupport::PreviewOnly,
             "Experimental in CNA. Useful for previewing a browser build's rendering."},
            {"D3D9", "d3d9", "Direct3D 9", BackendEditorSupport::PreviewOnly,
             "Fixed-function-era feature set. Suitable for a player process, not for the editor UI."},
            {"SOFTWARE", "software", "Software (CPU rasterizer)", BackendEditorSupport::PreviewOnly,
             "Correct but slow. Ideal as a comparison reference, unusable as an interactive UI host."},
            {"CANVAS", "canvas", "HTML Canvas 2D", BackendEditorSupport::RuntimeOnly,
             "Emscripten only; there is no desktop editor process to host."},
            {"ASCII", "ascii", "ASCII glyph grid", BackendEditorSupport::RuntimeOnly,
             "A deliberately lossy presentation filter. Meaningful for a game, not for a UI."},
            {"DX3", "dx3", "DirectX 3 (DirectDraw)", BackendEditorSupport::RuntimeOnly,
             "Historical backend. Exactly the case the separate player process exists for."},
            {"HEADLESS", "headless", "Headless (no GPU or window)", BackendEditorSupport::RuntimeOnly,
             "No window by definition. Used by the editor's own automated tests."},
        };
        return backends;
    }

    const BackendInfo* findBackend(std::string_view name)
    {
        const std::vector<BackendInfo>& backends = getKnownBackends();
        const auto found = std::find_if(backends.begin(), backends.end(),
                                        [&](const BackendInfo& backend) {
                                            return backend.commandLineName == name || backend.cmakeName == name;
                                        });
        return found == backends.end() ? nullptr : &*found;
    }

    JsonValue Project::toJson() const
    {
        JsonValue json = JsonValue::makeObject();
        json.set("formatVersion", JsonValue{kFormatVersion});
        json.set("name", JsonValue{name_});
        json.set("kind", JsonValue{toString(kind_)});
        json.set("startupScene", JsonValue{startupScene_});
        json.set("assetDirectory", JsonValue{assetDirectory_});
        json.set("sceneDirectory", JsonValue{sceneDirectory_});
        json.set("defaultGraphicsBackend", JsonValue{defaultGraphicsBackend_});

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
        startupScene_ = document["startupScene"].asString();
        assetDirectory_ = document["assetDirectory"].asString("Assets");
        sceneDirectory_ = document["sceneDirectory"].asString("Scenes");
        defaultGraphicsBackend_ = document["defaultGraphicsBackend"].asString("easygl");

        if (findBackend(defaultGraphicsBackend_) == nullptr)
        {
            result.warnings.push_back("unknown defaultGraphicsBackend '" + defaultGraphicsBackend_
                                      + "'; the Play button will need one chosen explicitly");
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
            result.warnings.push_back("an XnaCompatible project declares a startupScene; the editor will "
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
