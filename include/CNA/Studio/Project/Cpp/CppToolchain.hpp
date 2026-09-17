// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/Cpp/CppToolchain.hpp
 * @brief The C++ toolchain: CMake, and everything Studio knows about driving it.
 *
 * `plan.md` STUDIO-02081. Everything under `CNA/Studio/Project/Cpp/` and `src/project/cpp/` belongs
 * to the C++ language adapter, and `STUDIO-02085`'s guard test refuses a reference to CMake — the
 * executable, its cache variables, its `CMakeLists.txt` — anywhere else in Studio. That is the
 * boundary made enforceable: the Project Hub, the Build panel, the export command and the
 * packaging workflow reach a project's toolchain through `StudioLanguageAdapter` and cannot learn
 * from it what a compiler is.
 *
 * These functions were `Project/BuildRunner.hpp` until the seam arrived, and they have not changed:
 * the C++ adapter reproduces what Studio already did, which is the point of moving them rather
 * than rewriting them.
 */

#include <string>
#include <vector>

#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/Project/TargetProfile.hpp"

namespace CNA::Studio
{
    class Project;

    /** @brief What to build, and where to put it. */
    struct BuildRequest
    {
        /** @brief Absolute path to the directory holding the project's `CMakeLists.txt`. */
        std::string projectRoot;

        /** @brief Absolute path for the build tree. Empty means the default beside the project. */
        std::string buildDirectory;

        /** @brief The platform from the project's `targetPlatforms`, e.g. "linux-x64". */
        std::string targetPlatform;

        /**
         * @brief CNA's `CNA_GRAPHICS_RENDERER` value, e.g. `"OPENGLES3"`.
         *
         * The field keeps its name because callers and saved settings use it; what changed is the
         * *variable it sets*. Studio passed `CNA_GRAPHICS_BACKEND`, which current CNA does not
         * define — so every game Studio configured silently took CNA's default renderer instead of
         * the one the user chose, and the build succeeded, which is what made it hard to notice.
         */
        std::string graphicsBackend;

        /** @brief CNA's `CNA_PLATFORM` value, e.g. "SDL3". Empty leaves CNA's default. */
        std::string platform;

        /**
         * @brief Extra `-D` arguments, in order, from the target profile.
         *
         * Everything beyond renderer and platform that the profile decides: the feature options,
         * each passed explicitly on or off. Carried as a list rather than reconstructed here so
         * that the Build panel can show exactly what it is about to run.
         */
        std::vector<std::string> extraDefinitions;

        /** @brief CMake build type, e.g. "Release". */
        std::string configuration = "Release";

        /** @brief The `cmake` executable to run. Empty means look on the PATH. */
        std::string cmakePath;
    };

    /**
     * @brief Returns the default build directory for @p request: `<root>/build/<platform>`.
     *
     * Beside the project rather than in a temporary directory, because a build people iterate on
     * has to be incremental, and because they will want to open it in their own tools. It is not
     * added to `.gitignore` -- Studio does not edit a user's repository configuration -- so the
     * panel says where it went.
     */
    [[nodiscard]] std::string getDefaultBuildDirectory(const BuildRequest& request);

    /**
     * @brief Returns the configure and build commands, in the order they must run.
     *
     * Pure: no filesystem, no process, no clock. That is what makes the *interesting* part of this
     * feature -- which options are passed and in which order -- testable without a compiler on the
     * machine running the tests.
     *
     * Returns an empty list when the request is unusable, which the caller reports; see
     * `describeBuildProblem`.
     */
    [[nodiscard]] std::vector<BuildStep> planBuild(const BuildRequest& request);

    /**
     * @brief Returns why @p request cannot be built, or an empty string when it can.
     *
     * Checked before anything is offered rather than after something fails, because the failure of
     * a missing compiler arrives as a wall of CMake output that says nothing a user can act on.
     */
    [[nodiscard]] std::string describeBuildProblem(const BuildRequest& request);

    /**
     * @brief Returns the full path to a usable `cmake`, or an empty string.
     *
     * Searched on the PATH rather than at a fixed location, since every platform installs it
     * somewhere different and a user may well have several.
     */
    [[nodiscard]] std::string findCMake();

    /** @brief Fills in a request from @p project, leaving the caller's explicit choices alone. */
    [[nodiscard]] BuildRequest makeBuildRequest(const Project& project,
                                                std::string targetPlatform,
                                                std::string graphicsBackend);

    /**
     * @brief Fills in a request from a project's active target profile.
     *
     * Prefer this: the profile is where a project's renderer, platform, configuration and features
     * are decided, and a request assembled from anywhere else is a second opinion.
     *
     * @param project Project to build.
     * @return The request.
     */
    [[nodiscard]] BuildRequest makeBuildRequestFromActiveProfile(const Project& project);

    /**
     * @brief Returns @p profile as CMake command-line arguments, in a stable order.
     *
     * The translation from Studio's own vocabulary — renderer, platform, configuration, features —
     * into one build system's. It lives here rather than beside `StudioTargetProfile` because the
     * profile is a *project* fact that every language shares, and `-DCMAKE_BUILD_TYPE=` is not:
     * a binding with a different build system would answer the same profile with different words.
     *
     * Each feature is passed explicitly on or off rather than omitted when off, so a stale CMake
     * cache cannot leave a feature enabled that the profile turns off.
     *
     * @param profile The profile.
     * @return Arguments, each already in `-DNAME=VALUE` form.
     */
    [[nodiscard]] std::vector<std::string> studioTargetProfileCMakeArguments(
        const StudioTargetProfile& profile);
}
