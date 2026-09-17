// SPDX-License-Identifier: MS-PL
/**
 * @file ProjectExportTests.cpp
 * @brief The early guard on the invariant the whole product rests on (plan.md STUDIO-02051).
 *
 * > CNA Studio produces CNA games, not CNA Studio games.
 *
 * An exported project is an ordinary CNA C++ project: CMake, a compiler, a CNA checkout, and at no
 * point CNA Studio. The cases here check that property on the *written tree* -- that nothing in it
 * points back at the machine it was exported from, that every include it contains resolves inside
 * it or inside CNA, and that the runtime it carries is byte for byte the one Studio itself
 * compiles.
 *
 * What these cases cannot check is whether the result compiles. That needs a compiler and a CNA
 * checkout, so it is a CTest (`CnaStudioStandaloneExport`) rather than a unit test -- and it is not
 * optional, because every interesting failure of this invariant so far has been one that reading
 * the tree could not have found. Turning CNA's video option on rather than to `AUTO` made an
 * exported game require FFmpeg; CNA building its own tests and examples from a subdirectory made
 * the configure fail outright. Both looked perfectly correct on paper.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/Cpp/CppProjectExport.hpp"
#include "CNA/Studio/Project/Cpp/CppRuntimeSources.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A temporary directory that removes itself, so a failing case leaves nothing behind. */
    class ScopedDirectory
    {
    public:
        explicit ScopedDirectory(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-export-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ~ScopedDirectory()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedDirectory(const ScopedDirectory&) = delete;
        ScopedDirectory& operator=(const ScopedDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return path_; }
        [[nodiscard]] std::string string() const { return path_.generic_string(); }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    /** @brief The repository root, supplied by CMake so the test does not guess. */
    std::filesystem::path sourceRoot()
    {
#ifdef CNA_STUDIO_SOURCE_ROOT
        return std::filesystem::path{CNA_STUDIO_SOURCE_ROOT};
#else
        return std::filesystem::path{};
#endif
    }

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream stream{path, std::ios::binary};
        if (!stream) { return {}; }
        return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    /** @brief Opens the example project, which is the one thing in the tree shaped like a game. */
    Project openExampleProject()
    {
        Project project;
        const std::string path =
            (sourceRoot() / "examples/HelloSprites/HelloSprites.cnaproject").generic_string();
        const ProjectLoadResult loaded = project.loadFromFile(path, &getProjectFormatMigrator());
        CNA_STUDIO_EXPECT(loaded.succeeded);
        return project;
    }

    bool contains(const std::vector<std::string>& values, std::string_view wanted)
    {
        return std::find(values.begin(), values.end(), wanted) != values.end();
    }
}

CNA_STUDIO_TEST(AnExportWritesABuildAGameARuntimeAndTheContent)
{
    const ScopedDirectory output{"tree"};
    const Project project = openExampleProject();

    StudioExportRequest request;
    request.outputDirectory = output.string();

    const StudioExportResult result = exportStandaloneProject(project, request);
    CNA_STUDIO_EXPECT_EQ(result.errorMessage, std::string{});
    CNA_STUDIO_EXPECT(result.succeeded());

    // The four things an exported project is, plus the content. Asserted by name rather than by
    // counting files, so adding one to the export does not silently change what this checks.
    CNA_STUDIO_EXPECT(contains(result.writtenFiles, "CMakeLists.txt"));
    CNA_STUDIO_EXPECT(contains(result.writtenFiles, "README.md"));
    CNA_STUDIO_EXPECT(contains(result.writtenFiles, "Source/Main.cpp"));
    CNA_STUDIO_EXPECT(contains(result.writtenFiles, "Runtime/include/CNA/Studio/Runtime/SceneLoader.hpp"));
    CNA_STUDIO_EXPECT(contains(result.writtenFiles, "Content/Scenes/Level01.cnascene"));
    CNA_STUDIO_EXPECT(contains(result.writtenFiles, "Content/Assets/Textures/player.png"));

    for (const std::string& relative : result.writtenFiles)
    {
        CNA_STUDIO_EXPECT(std::filesystem::exists(output.path() / relative));
    }
}

CNA_STUDIO_TEST(NothingInAnExportedProjectPointsBackAtTheMachineItCameFrom)
{
    // The failure this catches is an absolute path baked into the generated build or source: it
    // works perfectly on the machine that exported it and nowhere else, which is the worst possible
    // way for this invariant to break, because the person who would notice is never the person who
    // ran the export.
    const ScopedDirectory output{"absolute"};
    const Project project = openExampleProject();

    StudioExportRequest request;
    request.outputDirectory = output.string();
    const StudioExportResult result = exportStandaloneProject(project, request);
    CNA_STUDIO_EXPECT(result.succeeded());

    const std::string studioRoot = sourceRoot().generic_string();
    const std::string projectRoot = project.getRootPath();
    CNA_STUDIO_EXPECT(!studioRoot.empty());

    std::size_t leaks = 0;
    for (const std::string& relative : result.writtenFiles)
    {
        // Text only: an asset is bytes, and a PNG that happens to contain the letters of a path is
        // not a dependency on it.
        const std::filesystem::path path = output.path() / relative;
        const std::string extension = path.extension().string();
        if (extension != ".txt" && extension != ".cpp" && extension != ".hpp" && extension != ".md"
            && extension != ".cnascene" && extension != ".cnaproject")
        {
            continue;
        }

        const std::string text = readFile(path);
        if (text.find(studioRoot) != std::string::npos)
        {
            ++leaks;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                relative + " contains the absolute path of the Studio tree it was exported from. "
                           "The exported project would build only on this machine.");
        }
        if (!projectRoot.empty() && text.find(projectRoot) != std::string::npos)
        {
            ++leaks;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                relative + " contains the absolute path of the project it was exported from.");
        }
    }
    CNA_STUDIO_EXPECT_EQ(leaks, std::size_t{0});
}

CNA_STUDIO_TEST(EveryIncludeInAnExportedProjectResolvesInsideItOrInsideCna)
{
    // The strong form of "does not need Studio". A generated source that included a Studio header
    // it did not carry would compile on a developer's machine, where the header is on the include
    // path anyway, and fail on any other -- so the check is that every quoted include resolves to
    // a file the export actually wrote, or to CNA's own public headers, which come from CNA_ROOT.
    const ScopedDirectory output{"includes"};
    const Project project = openExampleProject();

    StudioExportRequest request;
    request.outputDirectory = output.string();
    const StudioExportResult result = exportStandaloneProject(project, request);
    CNA_STUDIO_EXPECT(result.succeeded());

    std::size_t checked = 0;
    std::size_t unresolved = 0;

    for (const std::string& relative : result.writtenFiles)
    {
        const std::filesystem::path path = output.path() / relative;
        const std::string extension = path.extension().string();
        if (extension != ".cpp" && extension != ".hpp") { continue; }

        const std::string text = readFile(path);
        std::size_t position = 0;
        while ((position = text.find("#include \"", position)) != std::string::npos)
        {
            const std::size_t start = position + std::string{"#include \""}.size();
            const std::size_t end = text.find('"', start);
            if (end == std::string::npos) { break; }

            const std::string header = text.substr(start, end - start);
            position = end;
            ++checked;

            // CNA's own public headers. They come from the CNA checkout the generated CMakeLists
            // asks for, which is the one external dependency an exported project is allowed.
            if (header.rfind("Microsoft/Xna/", 0) == 0) { continue; }

            // Anything else must be a file this export wrote, found on the include path the
            // generated CMakeLists sets up, or beside the file that included it.
            const bool inRuntime =
                std::filesystem::exists(output.path() / "Runtime/include" / header);
            const bool beside = std::filesystem::exists(path.parent_path() / header);
            if (inRuntime || beside) { continue; }

            ++unresolved;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                relative + " includes \"" + header + "\", which the export did not write. It would "
                           "compile only where that header is already on the include path.");
        }
    }

    // A scan that found no includes would report perfection.
    CNA_STUDIO_EXPECT(checked > 5);
    CNA_STUDIO_EXPECT_EQ(unresolved, std::size_t{0});
}

CNA_STUDIO_TEST(TheExportedRuntimeIsTheSameCodeStudioItselfCompiles)
{
    // The scene format is Studio's, and a runtime that had drifted from Studio's own reader would
    // load an authored scene differently from the way the editor drew it -- a bug that appears only
    // in the shipped game. Embedding at build time is what prevents the drift; this is what proves
    // the embedding is wired to the files it claims.
    std::size_t compared = 0;
    for (const StudioRuntimeSource& source : studioRuntimeSources())
    {
        // "Runtime/include/CNA/Studio/Core/Json.hpp" -> "include/CNA/Studio/Core/Json.hpp", and
        // "Runtime/src/Json.cpp" -> "src/core/Json.cpp": the runtime is flattened on the way out,
        // so the mapping back is by file name for the sources and by suffix for the headers.
        const std::filesystem::path exported{std::string{source.pathInProject}};
        std::filesystem::path original;
        if (source.compiled)
        {
            original = sourceRoot() / "src/core" / exported.filename();
        }
        else
        {
            const std::string path{source.pathInProject};
            original = sourceRoot() / path.substr(std::string{"Runtime/"}.size());
        }

        const std::string fromDisk = readFile(original);
        CNA_STUDIO_EXPECT(!fromDisk.empty());
        CNA_STUDIO_EXPECT_EQ(std::string{source.contents}, fromDisk);
        ++compared;
    }
    CNA_STUDIO_EXPECT_EQ(compared, std::size_t{5});
}

CNA_STUDIO_TEST(TheGeneratedBuildAsksForCnaAndNothingElse)
{
    const ScopedDirectory output{"cmake"};
    const Project project = openExampleProject();

    StudioExportRequest request;
    request.outputDirectory = output.string();
    const StudioExportResult result = exportStandaloneProject(project, request);
    CNA_STUDIO_EXPECT(result.succeeded());

    const std::string text = readFile(output.path() / "CMakeLists.txt");
    CNA_STUDIO_EXPECT(text.find("CNA_ROOT") != std::string::npos);
    CNA_STUDIO_EXPECT(text.find("add_subdirectory(\"${CNA_ROOT}\"") != std::string::npos);
    CNA_STUDIO_EXPECT(text.find("target_link_libraries(HelloSprites PRIVATE CNA)") != std::string::npos);

    // The two CNA options a consumer must turn off to get a working configure at all, and which
    // nothing in CNA tells them about: its tests need an initialised googletest submodule, and its
    // examples resolve a helper script through CMAKE_SOURCE_DIR -- which, from a subdirectory, is
    // the consuming project's root. Recorded as CNA gap G-09.
    CNA_STUDIO_EXPECT(text.find("set(CNA_BUILD_TESTS OFF CACHE BOOL") != std::string::npos);
    CNA_STUDIO_EXPECT(text.find("set(CNA_BUILD_EXAMPLES OFF CACHE BOOL") != std::string::npos);

    // Written as cache entries, so `-DCNA_GRAPHICS_RENDERER=...` on the command line wins. A game
    // builds for whatever its builder selects, not for whatever the machine that exported it had.
    CNA_STUDIO_EXPECT(text.find("set(CNA_GRAPHICS_RENDERER \"OPENGLES3\" CACHE STRING")
                      != std::string::npos);
    CNA_STUDIO_EXPECT(text.find("set(CNA_PLATFORM \"SDL3\" CACHE STRING") != std::string::npos);

    // Nothing here may name a Studio target. Linking one would be the invariant broken outright.
    CNA_STUDIO_EXPECT(text.find("cna-studio") == std::string::npos);
}

CNA_STUDIO_TEST(ExportRefusesToWriteOverADirectoryThatAlreadyHasSomethingInIt)
{
    // Export writes a whole tree. Over a directory somebody picked by mistake from a file dialog,
    // that is not something an undo can help with -- so it is opt-in rather than opt-out.
    const ScopedDirectory output{"occupied"};
    std::filesystem::create_directories(output.path());
    {
        std::ofstream existing{output.path() / "important.txt"};
        existing << "do not lose me";
    }

    const Project project = openExampleProject();
    StudioExportRequest request;
    request.outputDirectory = output.string();

    const StudioExportResult refused = exportStandaloneProject(project, request);
    CNA_STUDIO_EXPECT(!refused.succeeded());
    CNA_STUDIO_EXPECT(refused.writtenFiles.empty());
    CNA_STUDIO_EXPECT(std::filesystem::exists(output.path() / "important.txt"));

    request.overwrite = true;
    const StudioExportResult allowed = exportStandaloneProject(project, request);
    CNA_STUDIO_EXPECT(allowed.succeeded());
    CNA_STUDIO_EXPECT(std::filesystem::exists(output.path() / "CMakeLists.txt"));
}

CNA_STUDIO_TEST(AProjectNameBecomesSomethingCMakeAndAShellWillAccept)
{
    // A project is named by a person and a CMake target is not, so the two cannot be the same
    // string. Exposed and tested rather than inlined, because the export test asserts on the
    // executable's name and a second copy of this rule would be free to disagree with the first.
    CNA_STUDIO_EXPECT_EQ(studioExportTargetName("HelloSprites"), std::string{"HelloSprites"});
    CNA_STUDIO_EXPECT_EQ(studioExportTargetName("Hello Sprites"), std::string{"Hello_Sprites"});
    CNA_STUDIO_EXPECT_EQ(studioExportTargetName("Hello Sprites!"), std::string{"Hello_Sprites"});
    CNA_STUDIO_EXPECT_EQ(studioExportTargetName("2048"), std::string{"G2048"});
    CNA_STUDIO_EXPECT_EQ(studioExportTargetName("...."), std::string{"CnaGame"});
    CNA_STUDIO_EXPECT_EQ(studioExportTargetName(""), std::string{"CnaGame"});
}

CNA_STUDIO_TEST(AProjectWithNoStartupSceneStillExportsAndSaysWhy)
{
    // Refusing would be the wrong call: a project mid-authoring has every right to have no startup
    // scene yet, and an export that failed on it would be one more reason not to try exporting
    // until the end -- which is exactly when this invariant is most expensive to discover broken.
    const ScopedDirectory output{"nostartup"};
    Project project = openExampleProject();
    project.setStartupScene("");

    StudioExportRequest request;
    request.outputDirectory = output.string();

    const StudioExportResult result = exportStandaloneProject(project, request);
    CNA_STUDIO_EXPECT(result.succeeded());
    CNA_STUDIO_EXPECT(!result.warnings.empty());

    bool mentioned = false;
    for (const std::string& warning : result.warnings)
    {
        if (warning.find("startup scene") != std::string::npos) { mentioned = true; }
    }
    CNA_STUDIO_EXPECT(mentioned);
}
