// SPDX-License-Identifier: MS-PL
/**
 * @file StudioLanguageAdapterTests.cpp
 * @brief The language seam: what it resolves, and that C++ still does what it did (STUDIO-02086).
 *
 * Two kinds of test here, and they answer different questions.
 *
 * The **registry** cases are about resolution: which adapter a project gets, what an unregistered
 * language does, and what an absent `language` key means. Those rules decide whether a project
 * opens at all, and each of them has an answer that is easy to get wrong in a way nothing would
 * report — an unmarked project silently getting no adapter, or an unknown language silently
 * getting the default one and being built as something it is not.
 *
 * The **adapter** cases are about regression. `STUDIO-02083` moved working code behind an
 * interface and changed nothing else; these assert that by comparing what the adapter answers
 * against what the underlying C++ functions answer directly. An abstraction introduced together
 * with a behaviour change is one whose regressions cannot be attributed, so the proof that nothing
 * changed has to be mechanical rather than a claim in a commit message.
 *
 * This file is allowed to name the C++ adapter's own symbols, which `STUDIO-02085`'s guard forbids
 * in `src/` and `include/`: a test of an implementation has to be able to see it, and the guard
 * scans only the two directories the rule is about.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/Cpp/CppLanguage.hpp"
#include "CNA/Studio/Project/Cpp/CppToolchain.hpp"
#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/Project/Project.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A project rooted at a real directory, so path-dependent answers are real ones. */
    Project projectAt(const std::filesystem::path& root, std::string name = "Probe")
    {
        Project project = Project::createDefault(std::move(name), root.generic_string());
        return project;
    }

    /** @brief A scratch directory that the caller removes. */
    std::filesystem::path scratch(const std::string& tag)
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("cna-studio-language-" + tag);
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }

    /** @brief An adapter that implements nothing, for registry cases that need a second id. */
    class StubLanguageAdapter final : public StudioLanguageAdapter
    {
    public:
        explicit StubLanguageAdapter(std::string id, std::string displayName = "Stub")
        {
            descriptor_.id = std::move(id);
            descriptor_.displayName = std::move(displayName);
            descriptor_.toolchainName = "None";
            descriptor_.sourceDirectory = "Source";
            descriptor_.generatedDirectory = "Generated";
        }

        [[nodiscard]] const StudioLanguageDescriptor& descriptor() const override
        {
            return descriptor_;
        }
        [[nodiscard]] bool supportsProjectKind(ProjectKind kind) const override
        {
            return kind == ProjectKind::CnaNative;
        }
        [[nodiscard]] StudioToolchainReport probeToolchain(std::string_view) const override
        {
            return StudioToolchainReport{};
        }
        [[nodiscard]] std::string describeBuildProblem(const Project&,
                                                       const StudioToolchainReport&) const override
        {
            return "the stub language cannot build anything";
        }
        [[nodiscard]] StudioBuildJob planBuild(const Project&,
                                               const StudioToolchainReport&) const override
        {
            return StudioBuildJob{};
        }
        [[nodiscard]] std::vector<StudioProjectFile> projectFiles(
            const StudioProjectScaffold&) const override
        {
            return {};
        }
        [[nodiscard]] StudioExportResult exportStandalone(const Project&,
                                                          const StudioExportRequest&) const override
        {
            return StudioExportResult{};
        }
        [[nodiscard]] std::vector<std::string> standaloneBuildInstructions(
            std::string_view) const override
        {
            return {};
        }

    private:
        StudioLanguageDescriptor descriptor_;
    };
}

// ------------------------------------------------------------------------------------------------
// The registry
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ThisBuildShipsTheCppAdapterAndNothingItCannotImplement)
{
    const StudioLanguageRegistry registry = studioBuiltInLanguages();

    CNA_STUDIO_EXPECT(!registry.empty());
    CNA_STUDIO_EXPECT_EQ(registry.defaultLanguageId(), std::string{kCppLanguageId});

    const StudioLanguageAdapter* cpp = registry.find(kCppLanguageId);
    CNA_STUDIO_EXPECT(cpp != nullptr);
    if (cpp == nullptr) { return; }

    CNA_STUDIO_EXPECT_EQ(cpp->descriptor().displayName, std::string{"C++"});
    CNA_STUDIO_EXPECT_EQ(cpp->descriptor().toolchainName, std::string{"CMake"});

    // Every registered adapter answers to the id it is registered under. A registry whose find()
    // and descriptor disagreed would resolve projects to the wrong thing with no symptom.
    for (const std::shared_ptr<const StudioLanguageAdapter>& adapter : registry.all())
    {
        CNA_STUDIO_EXPECT(registry.find(adapter->descriptor().id) == adapter.get());
    }
}

CNA_STUDIO_TEST(AProjectThatNamesNoLanguageGetsTheDefaultRatherThanNothing)
{
    // Every `.cnaproject` written before the key existed is in this state, and every one of them is
    // C++ -- it is the only language Studio has ever authored. Resolving it to nothing would make
    // the seam's arrival break every existing project, which is the failure mode of adding a
    // required field to a format people already have files in.
    const StudioLanguageRegistry registry = studioBuiltInLanguages();

    Project project;
    CNA_STUDIO_EXPECT(project.getLanguage().empty());

    const StudioLanguageAdapter* adapter = registry.forProject(project);
    CNA_STUDIO_EXPECT(adapter != nullptr);
    if (adapter != nullptr)
    {
        CNA_STUDIO_EXPECT_EQ(adapter->descriptor().id, std::string{kCppLanguageId});
    }
}

CNA_STUDIO_TEST(AProjectInALanguageThisBuildDoesNotHaveResolvesToNothing)
{
    // The other half, and the more important one: an unknown language must *not* fall back to the
    // default. Building a project as a language it is not would produce a failure a long way from
    // its cause, and the honest answer -- an editor that opens it and says it cannot build it -- is
    // what the null return is for.
    const StudioLanguageRegistry registry = studioBuiltInLanguages();

    Project project;
    project.setLanguage("a-binding-this-build-has-never-heard-of");

    CNA_STUDIO_EXPECT(registry.forProject(project) == nullptr);
}

CNA_STUDIO_TEST(RegisteringTwoAdaptersUnderOneIdKeepsTheSecondRatherThanBoth)
{
    // A list with two entries under one id has a first one that silently wins, which is the shape
    // that makes a substituted adapter in a test look as though it did not take effect.
    StudioLanguageRegistry registry;
    registry.add(std::make_shared<StubLanguageAdapter>("probe", "First"));
    registry.add(std::make_shared<StubLanguageAdapter>("probe", "Second"));

    CNA_STUDIO_EXPECT_EQ(registry.all().size(), std::size_t{1});
    const StudioLanguageAdapter* found = registry.find("probe");
    CNA_STUDIO_EXPECT(found != nullptr);
    if (found != nullptr)
    {
        CNA_STUDIO_EXPECT_EQ(found->descriptor().displayName, std::string{"Second"});
    }

    // And a null adapter is ignored rather than stored, so nothing downstream has to null-check a
    // registry entry.
    registry.add(nullptr);
    CNA_STUDIO_EXPECT_EQ(registry.all().size(), std::size_t{1});
}

CNA_STUDIO_TEST(AnEmptyRegistryHasNoDefaultAndSaysSo)
{
    // The default is the first registered rather than a hard-coded "cpp", so a build assembled
    // without the C++ adapter reports no default rather than one it does not have.
    const StudioLanguageRegistry registry;
    CNA_STUDIO_EXPECT(registry.defaultLanguageId().empty());
    CNA_STUDIO_EXPECT(registry.find(kCppLanguageId) == nullptr);

    Project project;
    CNA_STUDIO_EXPECT(registry.forProject(project) == nullptr);
}

CNA_STUDIO_TEST(TheLanguageIsAnAdditiveFieldOlderProjectFilesSimplyLack)
{
    // Written only when set, for the reason gridSnap is: a key that appeared in every file the
    // moment Studio touched it would make the first save of every existing project a diff nobody
    // asked for.
    Project untouched;
    CNA_STUDIO_EXPECT(!untouched.toJson().contains("language"));

    Project declared;
    declared.setLanguage(kCppLanguageId);
    CNA_STUDIO_EXPECT(declared.toJson().contains("language"));

    Project reloaded;
    const ProjectLoadResult loaded = reloaded.loadFromJson(declared.toJson());
    CNA_STUDIO_EXPECT(loaded.succeeded);
    CNA_STUDIO_EXPECT_EQ(reloaded.getLanguage(), std::string{kCppLanguageId});

    // An absent key loads as absent rather than as a guess, so the registry stays the one place
    // that decides what unspecified means.
    Project fromOldFile;
    CNA_STUDIO_EXPECT(fromOldFile.loadFromJson(untouched.toJson()).succeeded);
    CNA_STUDIO_EXPECT(fromOldFile.getLanguage().empty());
}

// ------------------------------------------------------------------------------------------------
// The C++ adapter still does what it did
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheCppAdapterPlansExactlyTheBuildTheToolchainFunctionsDo)
{
    // The regression proof for STUDIO-02083. The adapter is a facade over functions that already
    // had tests; this compares the two so that a change to either without the other fails here
    // rather than in somebody's build.
    const std::filesystem::path root = scratch("plan");
    {
        std::ofstream lists{root / "CMakeLists.txt", std::ios::binary};
        lists << "cmake_minimum_required(VERSION 3.20)\nproject(Probe NONE)\n";
    }

    const Project project = projectAt(root);
    const std::shared_ptr<const StudioLanguageAdapter> adapter = makeCppLanguageAdapter();

    const StudioToolchainReport toolchain = adapter->probeToolchain(std::string_view{});

    BuildRequest expected = makeBuildRequestFromActiveProfile(project);
    expected.cmakePath = toolchain.toolchainPath;

    const StudioBuildJob job = adapter->planBuild(project, toolchain);
    const std::vector<BuildStep> steps = planBuild(expected);

    CNA_STUDIO_EXPECT_EQ(job.buildDirectory, getDefaultBuildDirectory(expected));
    CNA_STUDIO_EXPECT_EQ(job.steps.size(), steps.size());
    for (std::size_t index = 0; index < std::min(job.steps.size(), steps.size()); ++index)
    {
        CNA_STUDIO_EXPECT_EQ(job.steps[index].toCommandLine(), steps[index].toCommandLine());
    }

    // And the problem it reports is the same sentence, so the panel's message did not change shape
    // when the call behind it did.
    CNA_STUDIO_EXPECT_EQ(adapter->describeBuildProblem(project, toolchain),
                         describeBuildProblem(expected));

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ProbingTheToolchainPrefersAnExplicitPathAndExplainsAnAbsentOne)
{
    const std::shared_ptr<const StudioLanguageAdapter> adapter = makeCppLanguageAdapter();

    // An explicit path is taken as given rather than searched for. Whether it *works* is
    // describeBuildProblem's answer, on the project: a preference pointing at a file that is not
    // there should say so at the build, not silently fall back to a different cmake.
    const StudioToolchainReport explicitPath = adapter->probeToolchain("/opt/somewhere/cmake");
    CNA_STUDIO_EXPECT_EQ(explicitPath.toolchainPath, std::string{"/opt/somewhere/cmake"});
    CNA_STUDIO_EXPECT(explicitPath.available);

    const StudioToolchainReport searched = adapter->probeToolchain(std::string_view{});
    if (searched.available)
    {
        CNA_STUDIO_EXPECT(searched.problem.empty());
        CNA_STUDIO_EXPECT(!searched.toolchainPath.empty());
    }
    else
    {
        // The machine has no cmake, which is a legitimate state. What is not legitimate is
        // reporting it with nothing to act on.
        CNA_STUDIO_EXPECT(!searched.problem.empty());
    }
}

CNA_STUDIO_TEST(ANewCppProjectGetsABuildFileAnEntryPointAndItsOwnRuntime)
{
    const std::shared_ptr<const StudioLanguageAdapter> adapter = makeCppLanguageAdapter();

    StudioProjectScaffold scaffold;
    scaffold.projectName = "Hello Sprites";
    scaffold.rootPath = "/tmp/does-not-need-to-exist";
    scaffold.startupScene = "Scenes/Main.cnascene";
    scaffold.renderer = "software";

    const std::vector<StudioProjectFile> files = adapter->projectFiles(scaffold);
    CNA_STUDIO_EXPECT(files.size() >= 4);

    std::set<std::string> paths;
    for (const StudioProjectFile& file : files) { paths.insert(file.pathInProject); }

    CNA_STUDIO_EXPECT(paths.count("CMakeLists.txt") == 1);
    CNA_STUDIO_EXPECT(paths.count("Source/Main.cpp") == 1);
    CNA_STUDIO_EXPECT(paths.count("README.md") == 1);

    // The runtime travels inside the project as ordinary source it compiles itself. That is the
    // difference between a project that builds on a clean machine with a CNA checkout and one that
    // quietly requires Studio to be installed -- and the difference is invisible on the machine
    // Studio was built on.
    bool carriesRuntime = false;
    for (const std::string& path : paths)
    {
        if (path.rfind("Runtime/", 0) == 0) { carriesRuntime = true; }
    }
    CNA_STUDIO_EXPECT(carriesRuntime);

    // Nothing is written twice, and nothing is empty: either would produce a project tree whose
    // failure appears at the user's first build rather than here.
    CNA_STUDIO_EXPECT_EQ(paths.size(), files.size());
    for (const StudioProjectFile& file : files) { CNA_STUDIO_EXPECT(!file.contents.empty()); }

    // The build file names the executable after the project, with the spaces taken out: a CMake
    // target cannot be called `Hello Sprites`.
    for (const StudioProjectFile& file : files)
    {
        if (file.pathInProject != "CMakeLists.txt") { continue; }
        CNA_STUDIO_EXPECT(file.contents.find("project(Hello_Sprites LANGUAGES") != std::string::npos);

        // And it stages the project's own directories beside the executable, so the game finds its
        // content from its own tree rather than only from an export.
        CNA_STUDIO_EXPECT(file.contents.find("/Scenes\"") != std::string::npos);
        CNA_STUDIO_EXPECT(file.contents.find("/Assets\"") != std::string::npos);
    }
}

CNA_STUDIO_TEST(GeneratingAProjectTwiceProducesTheSameBytes)
{
    // Authored output is deterministic, which is what makes a generated project's first commit
    // reviewable and its second generation a no-op diff.
    const std::shared_ptr<const StudioLanguageAdapter> adapter = makeCppLanguageAdapter();

    StudioProjectScaffold scaffold;
    scaffold.projectName = "Deterministic";
    scaffold.rootPath = "/tmp/anywhere";
    scaffold.startupScene = "Scenes/Main.cnascene";

    const std::vector<StudioProjectFile> first = adapter->projectFiles(scaffold);
    const std::vector<StudioProjectFile> second = adapter->projectFiles(scaffold);

    CNA_STUDIO_EXPECT_EQ(first.size(), second.size());
    for (std::size_t index = 0; index < std::min(first.size(), second.size()); ++index)
    {
        CNA_STUDIO_EXPECT_EQ(first[index].pathInProject, second[index].pathInProject);
        CNA_STUDIO_EXPECT_EQ(first[index].contents, second[index].contents);
    }
}

CNA_STUDIO_TEST(GeneratedCodeAndHandWrittenCodeLiveInDifferentDirectories)
{
    // The mechanical form of "hand-written source is never overwritten". A generator pointed at the
    // directory the user edits is one that eventually overwrites something in it, and no amount of
    // care at each call site prevents that -- a subtree the tool owns outright does.
    // The registry is held rather than used inline: `studioBuiltInLanguages()` returns by value --
    // it is a factory, not an accessor -- so `studioBuiltInLanguages().all()` would bind a
    // reference into a temporary that dies before the loop body runs.
    const StudioLanguageRegistry registry = studioBuiltInLanguages();
    for (const std::shared_ptr<const StudioLanguageAdapter>& adapter : registry.all())
    {
        const StudioLanguageDescriptor& descriptor = adapter->descriptor();
        CNA_STUDIO_EXPECT(!descriptor.sourceDirectory.empty());
        CNA_STUDIO_EXPECT(!descriptor.generatedDirectory.empty());
        CNA_STUDIO_EXPECT(descriptor.sourceDirectory != descriptor.generatedDirectory);

        // Not nested either: `Source/Generated` would put generated files inside the tree a user
        // selects when they say "all my code", which is how one gets committed and then edited.
        CNA_STUDIO_EXPECT(descriptor.generatedDirectory.rfind(descriptor.sourceDirectory + "/", 0)
                          != 0);

        // And a project that has just been created writes nothing into the generated directory --
        // there is nothing to generate yet, and a directory full of placeholders is noise.
        StudioProjectScaffold scaffold;
        scaffold.projectName = "Ownership";
        scaffold.rootPath = "/tmp/anywhere";
        for (const StudioProjectFile& file : adapter->projectFiles(scaffold))
        {
            CNA_STUDIO_EXPECT(file.pathInProject.rfind(descriptor.generatedDirectory + "/", 0) != 0);
        }
    }
}

CNA_STUDIO_TEST(TheStandaloneBuildInstructionsNameTheDirectoryTheyApplyTo)
{
    // What `--export` prints, written by whoever knows what the exported build file says rather
    // than by the command-line parser. A set of instructions that named the wrong directory would
    // be worse than none: it looks authoritative and fails a minute later.
    const std::shared_ptr<const StudioLanguageAdapter> adapter = makeCppLanguageAdapter();
    const std::vector<std::string> lines = adapter->standaloneBuildInstructions("/tmp/exported");

    CNA_STUDIO_EXPECT(!lines.empty());
    for (const std::string& line : lines)
    {
        CNA_STUDIO_EXPECT(line.find("/tmp/exported") != std::string::npos);
    }
}
