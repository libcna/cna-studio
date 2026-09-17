// SPDX-License-Identifier: MS-PL
/**
 * @file CppLanguage.cpp
 * @brief The C++ adapter: the language seam filled in by CMake, unchanged.
 *
 * `plan.md` STUDIO-02083. Every method here delegates to code that already existed and already had
 * tests. That is the whole design: the seam is introduced without changing what Studio does, so a
 * regression after this commit is a regression in the *wiring* and can be found by reading it.
 */

#include "CNA/Studio/Project/Cpp/CppLanguage.hpp"

#include <memory>
#include <string>
#include <vector>

#include "CNA/Studio/Project/Cpp/CppProjectExport.hpp"
#include "CNA/Studio/Project/Cpp/CppToolchain.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/TargetProfile.hpp"

namespace CNA::Studio
{
    namespace
    {
        class CppLanguageAdapter final : public StudioLanguageAdapter
        {
        public:
            CppLanguageAdapter()
            {
                descriptor_.id = kCppLanguageId;
                descriptor_.displayName = "C++";
                descriptor_.toolchainName = "CMake";

                // Source, not src: the export has written `Source/Main.cpp` since before this seam
                // existed, and a project created by the Hub that used a different directory would
                // be a second convention nobody chose.
                descriptor_.sourceDirectory = "Source";

                // A directory the tool owns outright, separate from the one the user edits.
                // Generation that writes into `Source/` is generation that eventually overwrites
                // something hand-written, and no amount of care at each call site prevents that.
                descriptor_.generatedDirectory = "Generated";

                descriptor_.sourceFileExtensions = {".cpp", ".hpp", ".cc", ".h", ".cxx", ".hxx"};
            }

            [[nodiscard]] const StudioLanguageDescriptor& descriptor() const override
            {
                return descriptor_;
            }

            [[nodiscard]] bool supportsProjectKind(ProjectKind) const override
            {
                // Both, and this is the one adapter that can say so without qualification: CNA's
                // C++ API *is* the XNA-shaped one, so a project with its own
                // Initialize/LoadContent/Update/Draw is a plain CNA C++ program with no entity
                // model, and a CnaNative one is the same program with Studio's scene runtime in it.
                return true;
            }

            [[nodiscard]] StudioToolchainReport probeToolchain(
                std::string_view preferredPath) const override
            {
                StudioToolchainReport report;
                report.toolchainPath =
                    preferredPath.empty() ? findCMake() : std::string{preferredPath};

                if (report.toolchainPath.empty())
                {
                    report.problem =
                        "cmake was not found on the PATH. Install it, or set the path in the build "
                        "panel";
                    return report;
                }

                report.available = true;
                return report;
            }

            [[nodiscard]] std::string describeBuildProblem(
                const Project& project, const StudioToolchainReport& toolchain) const override
            {
                return CNA::Studio::describeBuildProblem(requestFor(project, toolchain));
            }

            [[nodiscard]] StudioBuildJob planBuild(
                const Project& project, const StudioToolchainReport& toolchain) const override
            {
                const BuildRequest request = requestFor(project, toolchain);

                StudioBuildJob job;
                job.buildDirectory = request.buildDirectory.empty()
                                         ? getDefaultBuildDirectory(request)
                                         : request.buildDirectory;
                job.description = request.targetPlatform + ", " + request.configuration;
                job.steps = CNA::Studio::planBuild(request);
                return job;
            }

            [[nodiscard]] std::vector<StudioProjectFile> projectFiles(
                const StudioProjectScaffold& scaffold) const override
            {
                return studioCppProjectFiles(scaffold);
            }

            [[nodiscard]] StudioExportResult exportStandalone(
                const Project& project, const StudioExportRequest& request) const override
            {
                return exportStandaloneProject(project, request);
            }

            [[nodiscard]] std::vector<std::string> standaloneBuildInstructions(
                std::string_view directory) const override
            {
                const std::string path{directory};
                return {"cmake -S " + path + " -B " + path + "/build -DCNA_ROOT=/path/to/cna",
                        "cmake --build " + path + "/build"};
            }

        private:
            /** @brief The C++ request behind a project, with the toolchain that was found. */
            [[nodiscard]] static BuildRequest requestFor(const Project& project,
                                                         const StudioToolchainReport& toolchain)
            {
                BuildRequest request = makeBuildRequestFromActiveProfile(project);
                request.cmakePath = toolchain.toolchainPath;
                return request;
            }

            StudioLanguageDescriptor descriptor_;
        };
    }

    std::shared_ptr<const StudioLanguageAdapter> makeCppLanguageAdapter()
    {
        return std::make_shared<const CppLanguageAdapter>();
    }
}
