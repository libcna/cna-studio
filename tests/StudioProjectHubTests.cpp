// SPDX-License-Identifier: MS-PL
/**
 * @file StudioProjectHubTests.cpp
 * @brief The Project Hub: templates, refusals, the recent list and what creation writes.
 *
 * `plan.md` STUDIO-08001 … STUDIO-08012, and STUDIO-11014.
 *
 * The one thing these cases deliberately do **not** do is build anything. Whether a created
 * project compiles is `STUDIO-08011`'s question and it is answered by a CTest case that actually
 * compiles one, because that is the only kind of evidence the central invariant accepts. What is
 * here is everything that can be settled without a compiler: which templates are offered, what is
 * refused and why, what lands on disk, and whether creating the same project twice produces the
 * same bytes.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/ProjectCreation.hpp"
#include "CNA/Studio/Project/ProjectTemplate.hpp"
#include "CNA/Studio/Project/ProjectValidation.hpp"
#include "CNA/Studio/Project/RecentProjects.hpp"
#include "CNA/Studio/ShellPanels/StudioProjectHubPanel.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    std::filesystem::path sourceRoot() { return std::filesystem::path{CNA_STUDIO_SOURCE_ROOT}; }

    /** @brief A scratch directory, emptied first. The caller removes it. */
    std::filesystem::path scratch(const std::string& tag)
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("cna-studio-hub-" + tag);
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }

    /** @brief The templates this repository ships, read from the tree rather than from a build. */
    StudioTemplateCatalogue shippedTemplates()
    {
        StudioTemplateCatalogue catalogue;
        const std::vector<std::string> problems =
            catalogue.addSearchPath((sourceRoot() / "templates").generic_string());

        // A manifest that will not read is a template that does not work, and the suite should say
        // so here rather than in the CTest case that tries to build it twenty minutes later.
        for (const std::string& problem : problems)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__, "templates/: " + problem);
        }
        return catalogue;
    }

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream stream{path, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    }
}

// ------------------------------------------------------------------------------------------------
// The template model (STUDIO-08005 … STUDIO-08010)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheShippedTemplatesAreDataAndEveryOneOfThemReads)
{
    const StudioTemplateCatalogue catalogue = shippedTemplates();

    // The four `STUDIO-08006`–`STUDIO-08009` name. A count rather than "at least one", because the
    // failure this catches is a template directory that stopped being found, and a catalogue with
    // three in it passes every other check in this file.
    CNA_STUDIO_EXPECT_EQ(catalogue.all().size(), std::size_t{4});

    for (const char* id : {"empty-3d", "empty-2d", "basic-sample", "xna-compatible"})
    {
        const StudioProjectTemplate* value = catalogue.find(id);
        if (value == nullptr)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"no template called '"} + id + "' was found under templates/.");
            continue;
        }

        // Everything the Hub shows and everything creation needs. A template with no description
        // is a row in a list that says nothing; one with no renderer would silently take whatever
        // `StudioTargetProfile::defaults()` happens to name, which is `STUDIO-08010`'s whole point.
        CNA_STUDIO_EXPECT(!value->name.empty());
        CNA_STUDIO_EXPECT(!value->description.empty());
        CNA_STUDIO_EXPECT(!value->renderer.empty());
        CNA_STUDIO_EXPECT(!value->platform.empty());
    }
}

CNA_STUDIO_TEST(AddingATemplateIsAddingADirectoryRatherThanChangingStudio)
{
    // `STUDIO-08005`'s acceptance, checked by doing it: a directory with a manifest and a content
    // tree, dropped on a search path, is a template. No registration, no code, no rebuild.
    const std::filesystem::path root = scratch("addtemplate");
    const std::filesystem::path directory = root / "my-own";
    std::filesystem::create_directories(directory / "content" / "Scenes");

    {
        std::ofstream manifest{directory / "template.json", std::ios::binary};
        manifest << R"({"name":"My Own","description":"Mine.","renderer":"software",)"
                    R"("platform":"sdl3","view":"3d","startupScene":"Scenes/Main.cnascene"})";
    }
    {
        std::ofstream scene{directory / "content" / "Scenes" / "Main.cnascene", std::ios::binary};
        scene << R"({"formatVersion":1,"name":"Main","entities":[]})";
    }

    StudioTemplateCatalogue catalogue;
    CNA_STUDIO_EXPECT(catalogue.addSearchPath(root.generic_string()).empty());

    const StudioProjectTemplate* found = catalogue.find("my-own");
    CNA_STUDIO_EXPECT(found != nullptr);
    if (found != nullptr)
    {
        // The id defaults to the directory name, so the two cannot disagree by accident.
        CNA_STUDIO_EXPECT_EQ(found->id, std::string{"my-own"});
        CNA_STUDIO_EXPECT_EQ(found->name, std::string{"My Own"});
        CNA_STUDIO_EXPECT(found->view == StudioTemplateView::ThreeD);
    }

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ADirectoryWithNoManifestIsSkippedAndABrokenManifestIsReported)
{
    // Two different failures with two different right answers. A search path is somewhere people
    // put things, so a stray directory must not stop Studio; a manifest that will not read is
    // somebody's template not working, and saying nothing about that is how it stays broken.
    const std::filesystem::path root = scratch("badtemplate");
    std::filesystem::create_directories(root / "not-a-template" / "whatever");

    const std::filesystem::path broken = root / "broken";
    std::filesystem::create_directories(broken / "content");
    {
        std::ofstream manifest{broken / "template.json", std::ios::binary};
        manifest << "{ this is not json";
    }

    // A manifest that parses but describes no content tree: the Hub offering it would produce an
    // empty directory, which is worse than not offering it.
    const std::filesystem::path contentless = root / "contentless";
    std::filesystem::create_directories(contentless);
    {
        std::ofstream manifest{contentless / "template.json", std::ios::binary};
        manifest << R"({"name":"Contentless"})";
    }

    StudioTemplateCatalogue catalogue;
    const std::vector<std::string> problems = catalogue.addSearchPath(root.generic_string());

    CNA_STUDIO_EXPECT_EQ(problems.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(catalogue.empty());

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(TheFirstSearchPathWinsSoAUsersOwnTemplateBeatsTheShippedOne)
{
    // What "most specific first" has to mean if it is to mean anything: a template a user drops in
    // under an id Studio already ships replaces it rather than being silently ignored.
    const std::filesystem::path mine = scratch("override-mine");
    const std::filesystem::path theirs = scratch("override-theirs");

    for (const auto& [root, name] : {std::pair{mine, "Mine"}, std::pair{theirs, "Theirs"}})
    {
        const std::filesystem::path directory = root / "empty-3d";
        std::filesystem::create_directories(directory / "content");
        std::ofstream manifest{directory / "template.json", std::ios::binary};
        manifest << R"({"name":")" << name << R"(","renderer":"software"})";
    }

    StudioTemplateCatalogue catalogue;
    (void)catalogue.addSearchPath(mine.generic_string());
    (void)catalogue.addSearchPath(theirs.generic_string());

    CNA_STUDIO_EXPECT_EQ(catalogue.all().size(), std::size_t{1});
    const StudioProjectTemplate* found = catalogue.find("empty-3d");
    CNA_STUDIO_EXPECT(found != nullptr);
    if (found != nullptr) { CNA_STUDIO_EXPECT_EQ(found->name, std::string{"Mine"}); }

    std::filesystem::remove_all(mine);
    std::filesystem::remove_all(theirs);
}

// ------------------------------------------------------------------------------------------------
// New Project: what is refused (STUDIO-08004)
// ------------------------------------------------------------------------------------------------

namespace
{
    /** @brief Whether @p problems holds one about @p field. */
    bool refusedOn(const std::vector<StudioNewProjectProblem>& problems, std::string_view field)
    {
        return std::any_of(problems.begin(), problems.end(),
                           [&](const StudioNewProjectProblem& problem) {
                               return problem.field == field;
                           });
    }
}

CNA_STUDIO_TEST(EveryRefusalOfANewProjectNamesItsFieldAndSaysWhat)
{
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("refusals");

    const auto refuse = [&](StudioNewProjectRequest request) {
        return validateStudioNewProject(request, templates, languages);
    };

    StudioNewProjectRequest good;
    good.name = "Good";
    good.directory = (root / "Good").generic_string();
    good.templateId = "empty-2d";

    // The control. A refusal check whose "good" case is also refused proves nothing.
    CNA_STUDIO_EXPECT(refuse(good).empty());

    {
        StudioNewProjectRequest noName = good;
        noName.name.clear();
        CNA_STUDIO_EXPECT(refusedOn(refuse(noName), "name"));
    }
    {
        // A name that is nothing but path separators leaves no directory name at all.
        StudioNewProjectRequest slashes = good;
        slashes.name = "///";
        CNA_STUDIO_EXPECT(refusedOn(refuse(slashes), "name"));
    }
    {
        // Reserved on Windows whatever the extension. A project created on Linux that cannot be
        // checked out on Windows is a trap that springs on somebody else's machine.
        StudioNewProjectRequest reserved = good;
        reserved.name = "con";
        CNA_STUDIO_EXPECT(refusedOn(refuse(reserved), "name"));
    }
    {
        StudioNewProjectRequest noTemplate = good;
        noTemplate.templateId.clear();
        CNA_STUDIO_EXPECT(refusedOn(refuse(noTemplate), "template"));
    }
    {
        StudioNewProjectRequest unknownTemplate = good;
        unknownTemplate.templateId = "no-such-template";
        CNA_STUDIO_EXPECT(refusedOn(refuse(unknownTemplate), "template"));
    }
    {
        StudioNewProjectRequest unknownLanguage = good;
        unknownLanguage.languageId = "a-binding-this-build-does-not-have";
        CNA_STUDIO_EXPECT(refusedOn(refuse(unknownLanguage), "language"));
    }
    {
        StudioNewProjectRequest relative = good;
        relative.directory = "Somewhere/Relative";
        CNA_STUDIO_EXPECT(refusedOn(refuse(relative), "directory"));
    }
    {
        // A non-empty directory. Refused rather than merged, because a new project written over a
        // directory somebody picked by mistake is not something an undo helps with.
        const std::filesystem::path occupied = root / "Occupied";
        std::filesystem::create_directories(occupied);
        { std::ofstream file{occupied / "something.txt", std::ios::binary}; file << "mine"; }

        StudioNewProjectRequest inUse = good;
        inUse.directory = occupied.generic_string();
        CNA_STUDIO_EXPECT(refusedOn(refuse(inUse), "directory"));
    }
    {
        // A path that is a file. The message has to distinguish it from a full directory, because
        // the two have different fixes.
        const std::filesystem::path file = root / "AFile";
        { std::ofstream stream{file, std::ios::binary}; stream << "not a directory"; }

        StudioNewProjectRequest onAFile = good;
        onAFile.directory = file.generic_string();
        CNA_STUDIO_EXPECT(refusedOn(refuse(onAFile), "directory"));
    }

    // Every message is a sentence rather than a code, because "invalid project" is the message
    // that makes somebody try the same thing again.
    StudioNewProjectRequest empty;
    for (const StudioNewProjectProblem& problem : refuse(empty))
    {
        CNA_STUDIO_EXPECT(!problem.field.empty());
        CNA_STUDIO_EXPECT(problem.message.size() > 10);
    }

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ValidatingANewProjectLeavesNothingBehindOnDisk)
{
    // The Hub validates as the user types, so a check that created the directory it was asked
    // about would create one per keystroke -- and then refuse the next keystroke because the
    // directory it just made is in the way.
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("notouch");

    StudioNewProjectRequest request;
    request.name = "Typed";
    request.directory = (root / "Typed").generic_string();
    request.templateId = "empty-2d";

    CNA_STUDIO_EXPECT(validateStudioNewProject(request, templates, languages).empty());
    CNA_STUDIO_EXPECT(!std::filesystem::exists(request.directory));

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ARefusedNewProjectWritesNothingAtAll)
{
    // A creation that failed halfway leaves a directory that is neither a project nor empty, and
    // the user is left working out which files were theirs.
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("refused");

    StudioNewProjectRequest request;
    request.name = "Refused";
    request.directory = (root / "Refused").generic_string();
    request.templateId = "no-such-template";

    const StudioNewProjectResult result = createStudioProject(request, templates, languages);

    CNA_STUDIO_EXPECT(!result.succeeded());
    CNA_STUDIO_EXPECT(result.writtenFiles.empty());
    CNA_STUDIO_EXPECT(!std::filesystem::exists(request.directory));

    std::filesystem::remove_all(root);
}

// ------------------------------------------------------------------------------------------------
// New Project: what is written (STUDIO-08006 … STUDIO-08010, STUDIO-11014)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryShippedTemplateProducesAProjectStudioCanOpenAgain)
{
    // The round trip, for all four: created, then loaded back through the same reader the editor
    // uses. A generator that wrote a `.cnaproject` Studio cannot read would pass every other check
    // in this file, and would fail at the one moment a user is watching.
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("roundtrip");

    for (const StudioProjectTemplate& value : templates.all())
    {
        StudioNewProjectRequest request;
        request.name = "Round " + value.id;
        request.directory = (root / value.id).generic_string();
        request.templateId = value.id;

        const StudioNewProjectResult created = createStudioProject(request, templates, languages);
        if (!created.succeeded())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "template '" + value.id + "' could not be created: "
                + (created.problems.empty() ? std::string{"no reason given"}
                                            : created.problems.front().message));
            continue;
        }

        Project project;
        const ProjectLoadResult loaded =
            project.loadFromFile(created.projectFilePath, &getProjectFormatMigrator());
        CNA_STUDIO_EXPECT(loaded.succeeded);

        CNA_STUDIO_EXPECT_EQ(project.getName(), request.name);
        CNA_STUDIO_EXPECT(project.getKind() == value.kind);

        // STUDIO-08010: the renderer and the platform the template was authored for, on the
        // profile Build and Play actually read.
        CNA_STUDIO_EXPECT_EQ(project.getActiveTargetProfile().renderer, value.renderer);
        CNA_STUDIO_EXPECT_EQ(project.getActiveTargetProfile().platform, value.platform);

        // STUDIO-11014: which viewport it opens in, carried from the template.
        CNA_STUDIO_EXPECT_EQ(project.getDefaultView(), std::string{toString(value.view)});

        // STUDIO-02084: the language it was created in, declared rather than assumed.
        CNA_STUDIO_EXPECT_EQ(project.getLanguage(), languages.defaultLanguageId());

        // And it opens clean. A project Studio just created that reports an error on open is a
        // generator bug shown to the user as their fault.
        for (const StudioProjectDiagnostic& diagnostic : validateStudioProject(project, languages))
        {
            if (diagnostic.severity != StudioProjectDiagnosticSeverity::Error) { continue; }
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "a fresh '" + value.id + "' project reports an error on open: "
                + diagnostic.toLine());
        }
    }

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(AnEmpty3DProjectCarriesItsSceneItsBuildFileAndItsRuntime)
{
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("empty3d");

    StudioNewProjectRequest request;
    request.name = "Empty Three D";
    request.directory = (root / "EmptyThreeD").generic_string();
    request.templateId = "empty-3d";

    const StudioNewProjectResult created = createStudioProject(request, templates, languages);
    CNA_STUDIO_EXPECT(created.succeeded());

    const std::set<std::string> written{created.writtenFiles.begin(), created.writtenFiles.end()};

    // The template's half.
    CNA_STUDIO_EXPECT(written.count("Scenes/Main.cnascene") == 1);

    // The language's half. Neither is the other's business, and a template that carried a build
    // file would be a template secretly about C++ (STUDIO-02080).
    CNA_STUDIO_EXPECT(written.count("Source/Main.cpp") == 1);
    CNA_STUDIO_EXPECT(written.count("CMakeLists.txt") == 1);

    // The runtime travels inside the project as ordinary source it compiles itself, which is the
    // difference between a project that builds on a clean machine and one that quietly needs
    // Studio installed.
    CNA_STUDIO_EXPECT(std::any_of(written.begin(), written.end(), [](const std::string& path) {
        return path.rfind("Runtime/", 0) == 0;
    }));

    // Nothing was written outside the project directory, and nothing was written twice.
    CNA_STUDIO_EXPECT_EQ(written.size(), created.writtenFiles.size());
    for (const std::string& path : created.writtenFiles)
    {
        CNA_STUDIO_EXPECT(path.find("..") == std::string::npos);
        CNA_STUDIO_EXPECT(std::filesystem::exists(std::filesystem::path{request.directory} / path));
    }

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(AnXnaCompatibleProjectGetsItsOwnGameLoopAndNoEntityModel)
{
    // `STUDIO-08008`'s acceptance, and the one place `ProjectKind` has to reach all the way into
    // generated source: a port coming from XNA must never be pushed through the entity model to
    // use the tooling, and a generated `main` that loaded a scene would be exactly that push,
    // applied on the project's first line.
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("xna");

    StudioNewProjectRequest request;
    request.name = "Ported";
    request.directory = (root / "Ported").generic_string();
    request.templateId = "xna-compatible";

    const StudioNewProjectResult created = createStudioProject(request, templates, languages);
    CNA_STUDIO_EXPECT(created.succeeded());

    const std::string main = readFile(std::filesystem::path{request.directory} / "Source/Main.cpp");
    CNA_STUDIO_EXPECT(!main.empty());

    for (const char* member : {"void Initialize()", "void LoadContent()", "void Update(",
                               "void Draw("})
    {
        if (main.find(member) == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"an XNA-compatible project's Main.cpp has no '"} + member + "'.");
        }
    }

    // And nothing of Studio's scene model in it, nor the runtime that would read one.
    CNA_STUDIO_EXPECT(main.find("SceneLoader") == std::string::npos);
    CNA_STUDIO_EXPECT(main.find("loadScene") == std::string::npos);
    CNA_STUDIO_EXPECT(!std::filesystem::exists(std::filesystem::path{request.directory} / "Runtime"));

    // Its build file stages its assets and does not try to stage scenes it does not have: a copy
    // of a directory that is not there is a build failure that says nothing about project kinds.
    const std::string lists = readFile(std::filesystem::path{request.directory} / "CMakeLists.txt");
    CNA_STUDIO_EXPECT(lists.find("/Assets\"") != std::string::npos);
    CNA_STUDIO_EXPECT(lists.find("/Scenes\"") == std::string::npos);
    CNA_STUDIO_EXPECT(lists.find("Runtime/include") == std::string::npos);

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(CreatingTheSameProjectTwiceProducesTheSameBytes)
{
    // Authored output is deterministic, which is what makes a generated project's first commit
    // reviewable. A generator that stamped a date or a fresh uuid into the tree would make every
    // regeneration an unreadable diff -- and would make STUDIO-08011's build unreproducible.
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("deterministic");

    std::vector<std::vector<std::pair<std::string, std::string>>> runs;
    for (const char* tag : {"first", "second"})
    {
        StudioNewProjectRequest request;
        request.name = "Same";
        request.directory = (root / tag).generic_string();
        request.templateId = "basic-sample";

        const StudioNewProjectResult created = createStudioProject(request, templates, languages);
        CNA_STUDIO_EXPECT(created.succeeded());

        std::vector<std::pair<std::string, std::string>> files;
        for (const std::string& relative : created.writtenFiles)
        {
            files.emplace_back(relative,
                               readFile(std::filesystem::path{request.directory} / relative));
        }
        std::sort(files.begin(), files.end());
        runs.push_back(std::move(files));
    }

    CNA_STUDIO_EXPECT_EQ(runs[0].size(), runs[1].size());
    for (std::size_t index = 0; index < std::min(runs[0].size(), runs[1].size()); ++index)
    {
        CNA_STUDIO_EXPECT_EQ(runs[0][index].first, runs[1][index].first);
        if (runs[0][index].second != runs[1][index].second)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "'" + runs[0][index].first + "' differs between two creations of the same project.");
        }
    }

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ATemplatesOwnFileWinsOverTheOneTheLanguageWouldWrite)
{
    // A template is the more specific answer. Silently overwriting what it provided would make a
    // template unable to customise its own entry point, which is the first thing a template that
    // is more than an empty world needs to do.
    const std::filesystem::path root = scratch("templatewins");
    const std::filesystem::path directory = root / "opinionated";
    std::filesystem::create_directories(directory / "content" / "Source");
    {
        std::ofstream manifest{directory / "template.json", std::ios::binary};
        manifest << R"({"name":"Opinionated","renderer":"software","platform":"sdl3"})";
    }
    {
        std::ofstream main{directory / "content" / "Source" / "Main.cpp", std::ios::binary};
        main << "// the template's own entry point\n";
    }

    StudioTemplateCatalogue templates;
    CNA_STUDIO_EXPECT(templates.addSearchPath(root.generic_string()).empty());

    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path into = scratch("templatewins-out");

    StudioNewProjectRequest request;
    request.name = "Opinionated";
    request.directory = (into / "Opinionated").generic_string();
    request.templateId = "opinionated";

    const StudioNewProjectResult created = createStudioProject(request, templates, languages);
    CNA_STUDIO_EXPECT(created.succeeded());

    CNA_STUDIO_EXPECT_EQ(readFile(std::filesystem::path{request.directory} / "Source/Main.cpp"),
                         std::string{"// the template's own entry point\n"});

    // And it is said out loud rather than done silently, because a file that did not appear is
    // the hardest kind of absence to notice.
    CNA_STUDIO_EXPECT(!created.warnings.empty());

    std::filesystem::remove_all(root);
    std::filesystem::remove_all(into);
}

// ------------------------------------------------------------------------------------------------
// Recent projects (STUDIO-08002)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AMovedProjectIsShownAsUnavailableRatherThanDroppedFromTheList)
{
    // `STUDIO-08002`'s acceptance, and the obvious implementation gets it wrong: filtering missing
    // entries out on read makes a project on an unmounted drive *disappear*, so somebody who
    // unplugs a disk loses their history rather than seeing it greyed out until they plug it back
    // in. A present-and-unavailable row is also the only place there is to say why.
    const std::filesystem::path root = scratch("recent");
    const StudioRecentProjectsStore store{(root / "recent-projects.json").generic_string()};

    const std::filesystem::path here = root / "Here" / "Here.cnaproject";
    std::filesystem::create_directories(here.parent_path());
    { std::ofstream file{here, std::ios::binary}; file << "{}"; }

    const std::filesystem::path gone = root / "Gone" / "Gone.cnaproject";

    CNA_STUDIO_EXPECT(store.remember(here.generic_string(), "Here", 200));
    CNA_STUDIO_EXPECT(store.remember(gone.generic_string(), "Gone", 100));

    const std::vector<StudioRecentProject> entries = store.load();
    CNA_STUDIO_EXPECT_EQ(entries.size(), std::size_t{2});

    // Newest first, so the one somebody just closed is the one at the top.
    CNA_STUDIO_EXPECT_EQ(entries[0].name, std::string{"Here"});
    CNA_STUDIO_EXPECT(entries[0].available);
    CNA_STUDIO_EXPECT(entries[0].problem.empty());

    CNA_STUDIO_EXPECT_EQ(entries[1].name, std::string{"Gone"});
    CNA_STUDIO_EXPECT(!entries[1].available);
    CNA_STUDIO_EXPECT(!entries[1].problem.empty());

    // And the name survives its file: a row with nothing but a path is a row nobody recognises.
    CNA_STUDIO_EXPECT_EQ(entries[1].name, std::string{"Gone"});

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(OpeningAProjectTwiceIsOneRowAndTheListIsBounded)
{
    const std::filesystem::path root = scratch("recent-bounds");
    const StudioRecentProjectsStore store{(root / "recent.json").generic_string()};

    const std::filesystem::path project = root / "One" / "One.cnaproject";
    std::filesystem::create_directories(project.parent_path());
    { std::ofstream file{project, std::ios::binary}; file << "{}"; }

    CNA_STUDIO_EXPECT(store.remember(project.generic_string(), "One", 1));
    CNA_STUDIO_EXPECT(store.remember(project.generic_string(), "One", 2));
    CNA_STUDIO_EXPECT_EQ(store.load().size(), std::size_t{1});

    // Bounded, because this list is a convenience and not a history: a hub showing two hundred
    // rows is one nobody scrolls, and the oldest of them point at directories that are gone.
    for (int index = 0; index < 40; ++index)
    {
        CNA_STUDIO_EXPECT(store.remember(
            (root / ("P" + std::to_string(index)) / "P.cnaproject").generic_string(),
            "P" + std::to_string(index), 10 + index));
    }
    CNA_STUDIO_EXPECT_EQ(store.load().size(), StudioRecentProjectsStore::kMaximumEntries);

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ACorruptRecentListReadsAsEmptyRatherThanStoppingStudio)
{
    // Nothing about this list is worth refusing to open Studio over.
    const std::filesystem::path root = scratch("recent-corrupt");
    const std::filesystem::path path = root / "recent.json";
    { std::ofstream file{path, std::ios::binary}; file << "{ not json at all"; }

    const StudioRecentProjectsStore store{path.generic_string()};
    CNA_STUDIO_EXPECT(store.load().empty());

    // And a store with nowhere to write reports failure rather than throwing.
    const StudioRecentProjectsStore nowhere{""};
    CNA_STUDIO_EXPECT(nowhere.load().empty());
    std::string problem;
    CNA_STUDIO_EXPECT(!nowhere.remember("/somewhere/x.cnaproject", "X", 1, &problem));
    CNA_STUDIO_EXPECT(!problem.empty());

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ARowCanBeTakenOffTheListAndForgettingAnAbsentOneIsNotAFailure)
{
    const std::filesystem::path root = scratch("recent-forget");
    const StudioRecentProjectsStore store{(root / "recent.json").generic_string()};

    CNA_STUDIO_EXPECT(store.remember("/gone/forever/Old.cnaproject", "Old", 1));
    CNA_STUDIO_EXPECT_EQ(store.load().size(), std::size_t{1});

    CNA_STUDIO_EXPECT(store.forget("/gone/forever/Old.cnaproject"));
    CNA_STUDIO_EXPECT(store.load().empty());

    // Forgetting something that is not there rewrites nothing and is not an error.
    CNA_STUDIO_EXPECT(store.forget("/never/was/There.cnaproject"));

    std::filesystem::remove_all(root);
}

// ------------------------------------------------------------------------------------------------
// Validation on open (STUDIO-08012)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryProjectDiagnosticNamesWhatIsWrongAndWhatToDo)
{
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("validation");
    std::filesystem::create_directories(root);

    Project project = Project::createDefault("Broken", root.generic_string());
    project.setStartupScene("Scenes/NotThere.cnascene");

    const std::vector<StudioProjectDiagnostic> diagnostics =
        validateStudioProject(project, languages);

    // A missing startup scene, and missing Scenes and Assets directories: three things wrong with
    // a project that nonetheless *opens*, because a broken project is exactly the project somebody
    // needs the editor for.
    CNA_STUDIO_EXPECT(diagnostics.size() >= 3);

    bool sawStartupScene = false;
    for (const StudioProjectDiagnostic& diagnostic : diagnostics)
    {
        CNA_STUDIO_EXPECT(!diagnostic.subject.empty());
        CNA_STUDIO_EXPECT(diagnostic.message.size() > 10);

        // Actionable means naming the fix, not the symptom. Every diagnostic here has one.
        CNA_STUDIO_EXPECT(!diagnostic.remedy.empty());

        if (diagnostic.subject == "startupScene")
        {
            sawStartupScene = true;
            CNA_STUDIO_EXPECT(diagnostic.severity == StudioProjectDiagnosticSeverity::Error);
            CNA_STUDIO_EXPECT(diagnostic.message.find("Scenes/NotThere.cnascene")
                              != std::string::npos);
        }
    }
    CNA_STUDIO_EXPECT(sawStartupScene);

    // Errors first. A pane that buried the one thing stopping a build under four remarks would be
    // sorted by the order the checks happen to run in, which is not an order anybody reads.
    for (std::size_t index = 1; index < diagnostics.size(); ++index)
    {
        CNA_STUDIO_EXPECT(static_cast<int>(diagnostics[index - 1].severity)
                          >= static_cast<int>(diagnostics[index].severity));
    }

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(AProjectInAnUnknownLanguageOpensAndSaysWhatStudioCanStillDo)
{
    // The one case where a diagnostic has to say what Studio *can* do rather than only what it
    // cannot: scenes and assets are still editable, and refusing to open would take away the only
    // tool that could have fixed the file.
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("unknown-language");
    std::filesystem::create_directories(root / "Scenes");
    std::filesystem::create_directories(root / "Assets");
    { std::ofstream scene{root / "Scenes" / "Main.cnascene", std::ios::binary}; scene << "{}"; }

    Project project = Project::createDefault("Foreign", root.generic_string());
    project.setStartupScene("Scenes/Main.cnascene");
    project.setLanguage("a-binding-this-build-does-not-have");

    bool sawLanguage = false;
    for (const StudioProjectDiagnostic& diagnostic : validateStudioProject(project, languages))
    {
        if (diagnostic.subject != "language") { continue; }
        sawLanguage = true;
        CNA_STUDIO_EXPECT(diagnostic.severity == StudioProjectDiagnosticSeverity::Error);
        CNA_STUDIO_EXPECT(diagnostic.remedy.find("still") != std::string::npos);
    }
    CNA_STUDIO_EXPECT(sawLanguage);

    std::filesystem::remove_all(root);
}

// ------------------------------------------------------------------------------------------------
// The Hub's own form (STUDIO-08001)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheHubShowsWhereItWouldCreateBeforeAnythingIsCommittedTo)
{
    // Where it goes is visible before the user commits to it rather than discovered afterwards,
    // and the rule that composes it is the same one the command line uses -- two spellings of
    // `<location>/<name>` would be two chances to disagree about where a project went.
    StudioProjectHubState state;
    CNA_STUDIO_EXPECT(studioProjectHubTargetDirectory(state).empty());

    state.name = "My Game";
    CNA_STUDIO_EXPECT(studioProjectHubTargetDirectory(state).empty());

    state.location = "/home/someone/Projects";
    CNA_STUDIO_EXPECT_EQ(studioProjectHubTargetDirectory(state),
                         std::string{"/home/someone/Projects/My Game"});

    // A name that reduces to nothing has no directory, and the Hub says so rather than offering
    // to create `/home/someone/Projects`.
    state.name = "///";
    CNA_STUDIO_EXPECT(studioProjectHubTargetDirectory(state).empty());
}

CNA_STUDIO_TEST(TheHubsFormBecomesTheRequestTheCommandLineWouldMake)
{
    const StudioLanguageRegistry languages = studioBuiltInLanguages();

    StudioProjectHubState state;
    state.name = "Hub Made";
    state.location = "/tmp/projects";
    state.templateId = "empty-3d";

    const StudioNewProjectRequest request = studioProjectHubRequest(state, languages);
    CNA_STUDIO_EXPECT_EQ(request.name, std::string{"Hub Made"});
    CNA_STUDIO_EXPECT_EQ(request.directory, std::string{"/tmp/projects/Hub Made"});
    CNA_STUDIO_EXPECT_EQ(request.templateId, std::string{"empty-3d"});

    // An unset language resolves to the build's default rather than being left empty, so the
    // project's `language` key is written rather than absent -- a project Studio created says
    // what it is.
    CNA_STUDIO_EXPECT_EQ(request.languageId, languages.defaultLanguageId());
}

CNA_STUDIO_TEST(ValidatingAPathWhoseParentsDoNotExistLeavesNoneOfThemBehind)
{
    // The probe creates the directory to find out whether it can write there, and
    // `create_directories` makes parents as well as the leaf -- so removing only the leaf would
    // leave half a path behind, once per keystroke in the Hub's location field.
    const StudioTemplateCatalogue templates = shippedTemplates();
    const StudioLanguageRegistry languages = studioBuiltInLanguages();
    const std::filesystem::path root = scratch("deep");

    StudioNewProjectRequest request;
    request.name = "Deep";
    request.directory = (root / "a" / "b" / "c" / "Deep").generic_string();
    request.templateId = "empty-2d";

    CNA_STUDIO_EXPECT(validateStudioNewProject(request, templates, languages).empty());

    // Not the leaf, and not one of the three directories above it either.
    CNA_STUDIO_EXPECT(!std::filesystem::exists(root / "a"));

    // And the directory that was already there is still there: the unwind stops at the nearest
    // existing ancestor rather than deleting up to the root.
    CNA_STUDIO_EXPECT(std::filesystem::exists(root));

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(ARecentEntrysTimestampSurvivesBeingWrittenAndReadBack)
{
    // Through a value that does not fit in an int, because the list is ordered by it and the
    // obvious `asInt` round trip truncates in 2038 -- which is free to fix now and a format
    // migration later.
    const std::filesystem::path root = scratch("recent-time");
    const StudioRecentProjectsStore store{(root / "recent.json").generic_string()};

    const std::int64_t farFuture = 4102444800;  // 2100-01-01
    CNA_STUDIO_EXPECT(store.remember("/somewhere/Later.cnaproject", "Later", farFuture));
    CNA_STUDIO_EXPECT(store.remember("/somewhere/Sooner.cnaproject", "Sooner", 1));

    const std::vector<StudioRecentProject> entries = store.load();
    CNA_STUDIO_EXPECT_EQ(entries.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(entries[0].name, std::string{"Later"});
    CNA_STUDIO_EXPECT_EQ(entries[0].openedAt, farFuture);

    std::filesystem::remove_all(root);
}

CNA_STUDIO_TEST(TheTemplateSearchPathsCoverAnInstalledStudioAndADevelopmentBuild)
{
    // The function that decides whether a Studio finds any templates at all. Untested, its two
    // failure modes are silent in opposite directions: an installed Studio that offers none, and a
    // development build whose Hub is empty so nobody notices the installed case is broken either.
    const std::vector<std::string> paths =
        studioTemplateSearchPaths("/opt/cna-studio/bin/cna-studio");

    CNA_STUDIO_EXPECT(paths.size() >= 3);

    // Beside the executable, for a self-contained directory somebody unpacked.
    CNA_STUDIO_EXPECT(std::any_of(paths.begin(), paths.end(), [](const std::string& path) {
        return path == "/opt/cna-studio/bin/templates";
    }));

    // And under `share/`, for a Unix install. Normalised, so it is a path somebody can read in a
    // log rather than one with `/../` in the middle of it.
    CNA_STUDIO_EXPECT(std::any_of(paths.begin(), paths.end(), [](const std::string& path) {
        return path == "/opt/cna-studio/share/cna-studio/templates";
    }));

    // The source tree is baked in at configure time and is last, so an installed template with the
    // same id as a shipped one wins -- "most specific first" is what makes the order mean anything.
    //
    // Compared as paths rather than as strings: CMake bakes in `${CMAKE_CURRENT_SOURCE_DIR}/..`
    // spellings that name the same directory as this test's `CNA_STUDIO_SOURCE_ROOT` without
    // matching it character for character, and a check that failed over `/../` would be a check
    // about how the two were spelt.
    CNA_STUDIO_EXPECT(std::filesystem::path{paths.back()}.lexically_normal()
                      == (sourceRoot() / "templates").lexically_normal());

    // And this build's own paths find the four templates that ship with it, which is the property
    // `--list-templates` and STUDIO-08011 both rest on.
    StudioTemplateCatalogue catalogue;
    for (const std::string& path : studioTemplateSearchPaths(std::string_view{}))
    {
        (void)catalogue.addSearchPath(path);
    }
    CNA_STUDIO_EXPECT_EQ(catalogue.all().size(), std::size_t{4});
}
