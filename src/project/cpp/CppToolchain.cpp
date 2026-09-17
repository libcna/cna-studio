// SPDX-License-Identifier: MS-PL
/**
 * @file CppToolchain.cpp
 * @brief Driving CMake: finding it, deciding what to pass it, and refusing early when it is absent.
 *
 * `plan.md` STUDIO-02081. Moved here from `src/project/BuildRunner.cpp` unchanged, so that the
 * generic build runner beside it knows nothing about which build system it is running and
 * `STUDIO-02085`'s guard can say so by path rather than by review.
 */

#include "CNA/Studio/Project/Cpp/CppToolchain.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "CNA/Studio/Project/Project.hpp"

namespace CNA::Studio
{
    namespace
    {
#if defined(_WIN32)
        constexpr const char* kCMakeName = "cmake.exe";
        constexpr char kPathSeparator = ';';
#else
        constexpr const char* kCMakeName = "cmake";
        constexpr char kPathSeparator = ':';
#endif
    }

    std::string findCMake()
    {
        const char* path = std::getenv("PATH");
        if (path == nullptr) { return {}; }

        const std::string entries{path};
        std::size_t start = 0;

        while (start <= entries.size())
        {
            const std::size_t end = entries.find(kPathSeparator, start);
            const std::string directory =
                entries.substr(start, end == std::string::npos ? std::string::npos : end - start);

            if (!directory.empty())
            {
                std::error_code errorCode;
                const std::filesystem::path candidate = std::filesystem::path{directory} / kCMakeName;
                if (std::filesystem::is_regular_file(candidate, errorCode))
                {
                    return candidate.generic_string();
                }
            }

            if (end == std::string::npos) { break; }
            start = end + 1;
        }
        return {};
    }

    std::string getDefaultBuildDirectory(const BuildRequest& request)
    {
        if (request.projectRoot.empty()) { return {}; }

        const std::string platform = request.targetPlatform.empty() ? "default" : request.targetPlatform;
        return (std::filesystem::path{request.projectRoot} / "build" / platform).generic_string();
    }

    BuildRequest makeBuildRequest(const Project& project,
                                  std::string targetPlatform,
                                  std::string graphicsBackend)
    {
        BuildRequest request;
        request.projectRoot = project.getRootPath();
        request.targetPlatform = std::move(targetPlatform);
        request.graphicsBackend = std::move(graphicsBackend);
        request.buildDirectory = getDefaultBuildDirectory(request);

        // Deliberately not resolved here. This runs on every frame that draws the build panel, and
        // finding cmake means walking every directory on the PATH; the caller resolves it once and
        // fills it in.
        return request;
    }

    BuildRequest makeBuildRequestFromActiveProfile(const Project& project)
    {
        const StudioTargetProfile& profile = project.getActiveTargetProfile();

        BuildRequest request;
        request.projectRoot = project.getRootPath();
        request.targetPlatform = std::string{studioTargetOsName(profile.os)} + "-"
                               + std::string{studioArchitectureName(profile.architecture)};
        request.configuration = std::string{studioBuildConfigurationName(profile.configuration)};

        // Translated by the profile, not here. It is the one place that knows how Studio's
        // lower-case names map to CNA's upper-case identities, and a second translation would be a
        // second chance to get it wrong.
        for (const std::string& argument : studioTargetProfileCMakeArguments(profile))
        {
            if (argument.rfind("-DCNA_GRAPHICS_RENDERER=", 0) == 0)
            {
                request.graphicsBackend = argument.substr(std::string{"-DCNA_GRAPHICS_RENDERER="}.size());
            }
            else if (argument.rfind("-DCNA_PLATFORM=", 0) == 0)
            {
                request.platform = argument.substr(std::string{"-DCNA_PLATFORM="}.size());
            }
            else if (argument.rfind("-DCMAKE_BUILD_TYPE=", 0) != 0)
            {
                request.extraDefinitions.push_back(argument);
            }
        }

        request.buildDirectory = getDefaultBuildDirectory(request);
        return request;
    }

    std::string describeBuildProblem(const BuildRequest& request)
    {
        if (request.projectRoot.empty()) { return "no project is open"; }

        std::error_code errorCode;
        if (!std::filesystem::is_directory(request.projectRoot, errorCode))
        {
            return "the project directory '" + request.projectRoot + "' does not exist";
        }

        // Checked here rather than left to CMake, because CMake's own message for this is a wall of
        // text about a missing CMakeLists that says nothing about what the user should do.
        if (!std::filesystem::is_regular_file(std::filesystem::path{request.projectRoot} / "CMakeLists.txt",
                                              errorCode))
        {
            return "the project has no CMakeLists.txt, so there is nothing for Studio to build. "
                   "A CNA game's build is the game's own -- Studio only runs it";
        }

        const std::string cmake = request.cmakePath.empty() ? findCMake() : request.cmakePath;
        if (cmake.empty())
        {
            return "cmake was not found on the PATH. Install it, or set the path in the build panel";
        }
        if (!std::filesystem::is_regular_file(cmake, errorCode))
        {
            return "'" + cmake + "' is not an executable file";
        }

        return {};
    }

    std::vector<BuildStep> planBuild(const BuildRequest& request)
    {
        if (!describeBuildProblem(request).empty()) { return {}; }

        const std::string cmake = request.cmakePath.empty() ? findCMake() : request.cmakePath;
        const std::string buildDirectory = request.buildDirectory.empty()
                                               ? getDefaultBuildDirectory(request)
                                               : request.buildDirectory;
        const std::string configuration =
            request.configuration.empty() ? std::string{"Release"} : request.configuration;

        BuildStep configure;
        configure.description = "Configure";
        configure.executable = cmake;
        configure.arguments = {"-S", request.projectRoot,
                               "-B", buildDirectory,
                               "-DCMAKE_BUILD_TYPE=" + configuration};

        // CNA_GRAPHICS_RENDERER, not the CNA_GRAPHICS_BACKEND this used to pass. Current CNA
        // separates renderer from platform and does not define the old name at all, so every game
        // Studio configured silently took CNA's default renderer rather than the one the user
        // chose -- and the build *succeeded*, which is exactly what made it survive this long.
        if (!request.graphicsBackend.empty())
        {
            configure.arguments.push_back("-DCNA_GRAPHICS_RENDERER=" + request.graphicsBackend);
        }
        if (!request.platform.empty())
        {
            configure.arguments.push_back("-DCNA_PLATFORM=" + request.platform);
        }

        // Whatever else the target profile decides. Studio still passes nothing it was not told to:
        // a game's build is the game's business, and guessing at somebody's CMakeLists is how a
        // tool becomes something people work around.
        for (const std::string& definition : request.extraDefinitions)
        {
            configure.arguments.push_back(definition);
        }

        BuildStep build;
        build.description = "Build";
        build.executable = cmake;

        // --config as well as CMAKE_BUILD_TYPE: single-config generators read the first and
        // multi-config ones (Visual Studio, Xcode) read the second, and a build that worked on one
        // developer's machine and produced a Debug binary on another's is the bug this avoids.
        build.arguments = {"--build", buildDirectory, "--config", configuration, "--parallel"};

        return {std::move(configure), std::move(build)};
    }

    std::vector<std::string> studioTargetProfileCMakeArguments(const StudioTargetProfile& profile)
    {
        std::vector<std::string> arguments;
        arguments.push_back("-DCMAKE_BUILD_TYPE="
                            + std::string{studioBuildConfigurationName(profile.configuration)});
        arguments.push_back("-DCNA_GRAPHICS_RENDERER=" + studioRendererCnaIdentity(profile.renderer));
        arguments.push_back("-DCNA_PLATFORM=" + studioPlatformCnaIdentity(profile.platform));

        // Every known feature, on or off explicitly. Passing only the enabled ones would let a
        // stale cache keep a feature the profile turned off -- which is the kind of build that
        // works for whoever configured it and for nobody else.
        for (const StudioFeatureOption& feature : getKnownStudioFeatures())
        {
            arguments.push_back("-D" + std::string{feature.cnaOption} + "="
                                + std::string{profile.hasFeature(feature.name) ? feature.enabledValue
                                                                              : std::string_view{"OFF"}});
        }
        return arguments;
    }
}
