// SPDX-License-Identifier: MS-PL
/**
 * @file TargetProfile.cpp
 * @brief The target-profile model, its validation, and its translation to CMake arguments.
 */

#include "CNA/Studio/Project/TargetProfile.hpp"

#include "CNA/Studio/Project/RendererCatalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Lower-cases a name for case-insensitive parsing. */
        std::string lowered(std::string_view text)
        {
            std::string result{text};
            for (char& character : result)
            {
                character = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            }
            return result;
        }

        /** @brief Upper-cases a name for CNA's CMake variables, which are upper case. */
        std::string uppered(std::string_view text)
        {
            std::string result{text};
            for (char& character : result)
            {
                character = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(character)));
            }
            return result;
        }

        /**
         * @brief A renderer CNA refuses to configure outside one set of operating systems.
         *
         * Transcribed from `cmake/RendererSelection.cmake` in the audited CNA commit, where each is
         * a `FATAL_ERROR`. Studio holds this only because CNA exposes it nowhere else (gap G-08);
         * `STUDIO-29007` re-reads CNA and fails when the transcription drifts.
         */
        struct RendererOsGate
        {
            /** @brief Lower-case renderer names the gate applies to. */
            std::vector<std::string_view> renderers;
            /** @brief The systems CNA will configure them for. */
            std::vector<StudioTargetOs> allowed;
            /** @brief CNA's own reason, shortened. */
            std::string_view reason;
        };

        const std::vector<RendererOsGate>& rendererOsGates()
        {
            static const std::vector<RendererOsGate> gates = {
                {{"directx1", "directx2", "directx3", "directx5", "directx6", "directx7",
                  "directx8", "directx9", "directx10", "directx11", "directx12", "direct2d",
                  "glide", "gdi"},
                 {StudioTargetOs::Windows},
                 "d3d and ddraw headers exist only on Windows; CNA makes this a hard configure "
                 "error rather than failing later"},

                {{"canvas", "html_dom", "svg_dom", "pixijs", "webgl1", "webgl2"},
                 {StudioTargetOs::Web},
                 "these render through browser APIs -- the DOM, HTML canvas or WebGL -- which do "
                 "not exist outside a browser"},

                {{"opengles2", "opengles3", "opengl33", "magnum"},
                 {StudioTargetOs::Linux, StudioTargetOs::Windows, StudioTargetOs::MacOs,
                  StudioTargetOs::Android, StudioTargetOs::IOs},
                 "a native GL renderer; for a browser build CNA wants WEBGL1 or WEBGL2"},

                {{"nanovg", "rlgl"},
                 {StudioTargetOs::Linux, StudioTargetOs::Windows, StudioTargetOs::MacOs},
                 "needs a real desktop OpenGL context, which CNA supports on desktop only"},
            };
            return gates;
        }

        /** @brief Appends a problem. */
        void add(StudioProfileValidation& validation, StudioProfileSeverity severity,
                 std::string axis, std::string message)
        {
            validation.problems.push_back(
                StudioProfileProblem{severity, std::move(axis), std::move(message)});
        }
    } // namespace

    std::string_view studioTargetOsName(StudioTargetOs os)
    {
        switch (os)
        {
            case StudioTargetOs::Linux:   return "linux";
            case StudioTargetOs::Windows: return "windows";
            case StudioTargetOs::MacOs:   return "macos";
            case StudioTargetOs::Android: return "android";
            case StudioTargetOs::IOs:     return "ios";
            case StudioTargetOs::Web:     return "web";
            case StudioTargetOs::Count:   break;
        }
        return "";
    }

    std::string_view studioTargetOsDisplayName(StudioTargetOs os)
    {
        switch (os)
        {
            case StudioTargetOs::Linux:   return "Linux";
            case StudioTargetOs::Windows: return "Windows";
            case StudioTargetOs::MacOs:   return "macOS";
            case StudioTargetOs::Android: return "Android";
            case StudioTargetOs::IOs:     return "iOS";
            case StudioTargetOs::Web:     return "Web";
            case StudioTargetOs::Count:   break;
        }
        return "";
    }

    bool parseStudioTargetOs(std::string_view name, StudioTargetOs& out)
    {
        const std::string key = lowered(name);
        for (int i = 0; i < static_cast<int>(StudioTargetOs::Count); ++i)
        {
            const auto candidate = static_cast<StudioTargetOs>(i);
            if (studioTargetOsName(candidate) == key) { out = candidate; return true; }
        }
        return false;
    }

    std::string_view studioArchitectureName(StudioArchitecture architecture)
    {
        switch (architecture)
        {
            case StudioArchitecture::X86_64: return "x86_64";
            case StudioArchitecture::X86:    return "x86";
            case StudioArchitecture::Arm64:  return "arm64";
            case StudioArchitecture::Arm32:  return "arm32";
            case StudioArchitecture::Wasm32: return "wasm32";
            case StudioArchitecture::Count:  break;
        }
        return "";
    }

    bool parseStudioArchitecture(std::string_view name, StudioArchitecture& out)
    {
        const std::string key = lowered(name);
        for (int i = 0; i < static_cast<int>(StudioArchitecture::Count); ++i)
        {
            const auto candidate = static_cast<StudioArchitecture>(i);
            if (studioArchitectureName(candidate) == key) { out = candidate; return true; }
        }
        return false;
    }

    std::string_view studioBuildConfigurationName(StudioBuildConfiguration value)
    {
        switch (value)
        {
            case StudioBuildConfiguration::Debug:          return "Debug";
            case StudioBuildConfiguration::Release:        return "Release";
            case StudioBuildConfiguration::RelWithDebInfo: return "RelWithDebInfo";
            case StudioBuildConfiguration::MinSizeRel:     return "MinSizeRel";
            case StudioBuildConfiguration::Count:          break;
        }
        return "";
    }

    bool parseStudioBuildConfiguration(std::string_view name, StudioBuildConfiguration& out)
    {
        const std::string key = lowered(name);
        for (int i = 0; i < static_cast<int>(StudioBuildConfiguration::Count); ++i)
        {
            const auto candidate = static_cast<StudioBuildConfiguration>(i);
            if (lowered(studioBuildConfigurationName(candidate)) == key)
            {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    const std::vector<StudioFeatureOption>& getKnownStudioFeatures()
    {
        // CNA's optional subsystems, by the option that switches each one. A game that needs none
        // of them should not pay for them: the whole point of a target profile is that shipping
        // decisions are the project's, not the tool's.
        static const std::vector<StudioFeatureOption> features = {
            {"net", "CNA_ENABLE_NET", "Networking",
             "ENet-based networking. Off by default; a single-player game needs none of it.", false},
            {"video", "CNA_ENABLE_VIDEO", "Video playback",
             "FFmpeg-backed video, used when the build machine has FFmpeg and quietly left out when "
             "it does not. A project that must have it should set CNA_ENABLE_VIDEO=ON itself, which "
             "turns a missing FFmpeg into a failed configure rather than a game without video.",
             true, "AUTO"},
            {"draco", "CNA_ENABLE_DRACO", "Draco mesh compression",
             "Compressed glTF meshes. Off unless the project's content actually uses them.", false},
            {"devices", "CNA_DEVICES", "Device services",
             "Clipboard and related host services. Off in CNA by default, which is why Studio's own "
             "clipboard degrades visibly rather than silently (CNA gap G-02).", false},
            {"cnaext", "CNA_CNAEXT", "Modern graphics extensions",
             "PBR materials, shadows, post-processing and compute. A modern-looking game needs it; "
             "an XNA-compatible one does not.", true},
        };
        return features;
    }

    const StudioFeatureOption* findStudioFeature(std::string_view name)
    {
        const std::string key = lowered(name);
        for (const StudioFeatureOption& feature : getKnownStudioFeatures())
        {
            if (feature.name == key) { return &feature; }
        }
        return nullptr;
    }

    bool StudioTargetProfile::hasFeature(std::string_view feature) const
    {
        return std::find(features.begin(), features.end(), feature) != features.end();
    }

    void StudioTargetProfile::setFeature(std::string_view feature, bool enabled)
    {
        const auto found = std::find(features.begin(), features.end(), feature);
        if (enabled)
        {
            if (found == features.end()) { features.emplace_back(feature); }
            // Sorted, so that two profiles with the same features serialize identically and a
            // project file does not churn in version control because somebody toggled one twice.
            std::sort(features.begin(), features.end());
            return;
        }
        if (found != features.end()) { features.erase(found); }
    }

    StudioTargetProfile StudioTargetProfile::defaults()
    {
        StudioTargetProfile profile;
        profile.name = "Default";
        profile.os = StudioTargetOs::Linux;
        profile.architecture = StudioArchitecture::X86_64;
        profile.platform = "sdl3";
        // The default renderer a new project targets, lower case as a project file spells it. Not
        // taken from Project.hpp's kDefaultRenderer, which is the *upper-case* CNA identity and
        // belongs to the older single-string model this replaces.
        profile.renderer = "opengles3";
        profile.configuration = StudioBuildConfiguration::Release;

        for (const StudioFeatureOption& feature : getKnownStudioFeatures())
        {
            if (feature.defaultEnabled) { profile.setFeature(feature.name, true); }
        }
        return profile;
    }

    bool StudioProfileValidation::isBuildable() const
    {
        return std::none_of(problems.begin(), problems.end(),
                            [](const StudioProfileProblem& problem) {
                                return problem.severity == StudioProfileSeverity::Error;
                            });
    }

    std::string StudioProfileValidation::report() const
    {
        std::string text;
        for (const StudioProfileProblem& problem : problems)
        {
            switch (problem.severity)
            {
                case StudioProfileSeverity::Error:     text += "  error   "; break;
                case StudioProfileSeverity::Warning:   text += "  warning "; break;
                case StudioProfileSeverity::Migration: text += "  changed "; break;
            }
            text += problem.axis + ": " + problem.message + "\n";
        }
        return text;
    }

    bool isRendererAvailableOn(std::string_view renderer, StudioTargetOs os, std::string* outReason)
    {
        const std::string key = lowered(renderer);
        for (const RendererOsGate& gate : rendererOsGates())
        {
            if (std::find(gate.renderers.begin(), gate.renderers.end(), key)
                == gate.renderers.end())
            {
                continue;
            }
            if (std::find(gate.allowed.begin(), gate.allowed.end(), os) != gate.allowed.end())
            {
                return true;
            }
            if (outReason != nullptr) { *outReason = std::string{gate.reason}; }
            return false;
        }
        // No gate: CNA configures it anywhere its dependencies are present. Studio does not invent
        // a restriction CNA does not state.
        return true;
    }

    StudioProfileValidation validateStudioTargetProfile(StudioTargetProfile& profile)
    {
        StudioProfileValidation validation;

        // --- Renderer ---------------------------------------------------------------------------
        if (const RendererAlias* alias = findLegacyRendererAlias(profile.renderer))
        {
            // Migrated and *told*. Silently substituting would change which renderer the user's
            // game ships on; silently failing would make an old project look corrupt.
            add(validation, StudioProfileSeverity::Migration, "renderer",
                "'" + profile.renderer + "' no longer exists in CNA and has been changed to '"
                + std::string{alias->replacement} + "'. " + std::string{alias->reason});
            profile.renderer = std::string{alias->replacement};
        }

        const RendererInfo* renderer = findRenderer(profile.renderer);
        if (renderer == nullptr)
        {
            add(validation, StudioProfileSeverity::Error, "renderer",
                "CNA has no renderer called '" + profile.renderer + "'.");
        }
        else
        {
            // Normalised to the catalogue's own lower-case spelling, silently and without a
            // warning: nothing about the target changed, only how it is written down. A project
            // that predates profiles carries CNA's upper-case identity in
            // `defaultGraphicsBackend`, and leaving it as written made every renderer comparison
            // in Studio a case-insensitive one -- which is the kind of rule that holds until the
            // one place that forgets it, where it shows up as a Build panel with a blank renderer.
            profile.renderer = std::string{renderer->commandLineName};

            std::string reason;
            if (!isRendererAvailableOn(profile.renderer, profile.os, &reason))
            {
                add(validation, StudioProfileSeverity::Error, "renderer",
                    "CNA cannot build the " + std::string{renderer->displayName} + " renderer for "
                    + std::string{studioTargetOsDisplayName(profile.os)} + ": " + reason + ".");
            }
            if (profile.renderer == "glide" && profile.architecture != StudioArchitecture::X86)
            {
                add(validation, StudioProfileSeverity::Error, "architecture",
                    "The Glide renderer targets the 32-bit x86 Glide ABI; CNA refuses to build it "
                    "for " + std::string{studioArchitectureName(profile.architecture)} + ".");
            }
        }

        // --- Platform ---------------------------------------------------------------------------
        const PlatformInfo* platform = findPlatform(profile.platform);
        if (platform == nullptr)
        {
            add(validation, StudioProfileSeverity::Error, "platform",
                "CNA has no platform called '" + profile.platform + "'.");
        }
        else if (profile.platform = std::string{platform->commandLineName};  // normalised, as above
                 platform->status == PlatformStatus::Reserved)
        {
            add(validation, StudioProfileSeverity::Error, "platform",
                "CNA reserves the name '" + std::string{platform->cnaIdentity}
                + "' but does not implement it yet; selecting it is a hard configure error.");
        }

        // --- Architecture -----------------------------------------------------------------------
        const bool wantsWasm = profile.architecture == StudioArchitecture::Wasm32;
        if (wantsWasm != (profile.os == StudioTargetOs::Web))
        {
            add(validation, StudioProfileSeverity::Error, "architecture",
                "WebAssembly is the architecture of a Web build and of nothing else; '"
                + std::string{studioArchitectureName(profile.architecture)} + "' and '"
                + std::string{studioTargetOsDisplayName(profile.os)} + "' cannot be combined.");
        }

        // --- Features ---------------------------------------------------------------------------
        for (const std::string& feature : profile.features)
        {
            if (findStudioFeature(feature) == nullptr)
            {
                add(validation, StudioProfileSeverity::Warning, "features",
                    "This build of Studio does not know a feature called '" + feature
                    + "'; it will be passed to CMake unchanged.");
            }
        }

        // --- Name -------------------------------------------------------------------------------
        if (profile.name.empty())
        {
            add(validation, StudioProfileSeverity::Warning, "name",
                "The profile has no name, so it cannot be told apart from another in the "
                "build target list.");
        }

        std::stable_sort(validation.problems.begin(), validation.problems.end(),
                         [](const StudioProfileProblem& a, const StudioProfileProblem& b) {
                             return static_cast<int>(a.severity) < static_cast<int>(b.severity);
                         });
        return validation;
    }

    std::string studioTargetProfileSummary(const StudioTargetProfile& profile)
    {
        std::string summary{studioTargetOsDisplayName(profile.os)};
        summary += " ";
        summary += studioArchitectureName(profile.architecture);
        summary += " - " + profile.renderer;
        summary += " - ";
        summary += studioBuildConfigurationName(profile.configuration);
        return summary;
    }

    std::string studioRendererCnaIdentity(std::string_view renderer)
    {
        // Upper case, because that is how CNA spells its renderer and platform identities. Studio
        // stores them lower case because that is how a user types them and how a project file
        // reads; the conversion happens here, once, rather than at every call site.
        const RendererInfo* known = findRenderer(renderer);
        return known != nullptr ? std::string{known->cnaIdentity} : uppered(renderer);
    }

    std::string studioPlatformCnaIdentity(std::string_view platform)
    {
        const PlatformInfo* known = findPlatform(platform);
        return known != nullptr ? std::string{known->cnaIdentity} : uppered(platform);
    }

    JsonValue studioTargetProfileToJson(const StudioTargetProfile& profile)
    {
        JsonValue value = JsonValue::makeObject();
        value.set("name", profile.name);
        value.set("os", std::string{studioTargetOsName(profile.os)});
        value.set("architecture", std::string{studioArchitectureName(profile.architecture)});
        value.set("platform", profile.platform);
        value.set("renderer", profile.renderer);
        value.set("configuration", std::string{studioBuildConfigurationName(profile.configuration)});

        JsonValue features = JsonValue::makeArray();
        for (const std::string& feature : profile.features) { features.append(JsonValue{feature}); }
        value.set("features", std::move(features));
        return value;
    }

    StudioTargetProfile studioTargetProfileFromJson(const JsonValue& value)
    {
        StudioTargetProfile profile = StudioTargetProfile::defaults();
        if (!value.isObject()) { return profile; }

        if (value.contains("name")) { profile.name = value["name"].asString(profile.name); }
        if (value.contains("platform"))
        {
            profile.platform = value["platform"].asString(profile.platform);
        }
        if (value.contains("renderer"))
        {
            profile.renderer = value["renderer"].asString(profile.renderer);
        }

        // An axis this build does not recognise keeps its default rather than failing the load.
        // A project must open; validation is what tells the user what was wrong with it.
        StudioTargetOs os = profile.os;
        if (parseStudioTargetOs(value["os"].asString(), os)) { profile.os = os; }

        StudioArchitecture architecture = profile.architecture;
        if (parseStudioArchitecture(value["architecture"].asString(), architecture))
        {
            profile.architecture = architecture;
        }

        StudioBuildConfiguration configuration = profile.configuration;
        if (parseStudioBuildConfiguration(value["configuration"].asString(), configuration))
        {
            profile.configuration = configuration;
        }

        if (value.contains("features"))
        {
            profile.features.clear();
            for (const JsonValue& feature : value["features"].getElements())
            {
                const std::string name = feature.asString();
                if (!name.empty()) { profile.setFeature(name, true); }
            }
        }
        return profile;
    }
} // namespace CNA::Studio
