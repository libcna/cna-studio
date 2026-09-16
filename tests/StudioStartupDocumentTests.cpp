// SPDX-License-Identifier: MS-PL
/**
 * @file StudioStartupDocumentTests.cpp
 * @brief What a Studio opens when it starts, and the guard that keeps a flag from going missing.
 *
 * `plan.md` STUDIO-07053.
 *
 * The defect these close is not that `--scene` was broken. It is that `--scene` was *parsed*,
 * *documented in the usage text*, and read by the Dear ImGui prototype alone — so on the native
 * shell, which is the default UI, it did nothing whatsoever. There was no failure to observe: the
 * project's own startup scene opened, which is what happens when the flag is absent, so a user
 * passing it saw a plausible Studio containing the wrong scene.
 *
 * A flag that stops existing on the way to the second UI is invisible to every test that exercises
 * one UI at a time. `NoParsedFlagIsReadByThePrototypeAlone` below is the one that would have caught
 * it, and it is written over the parser rather than over a list somebody maintains.
 */

#include "TestHarness.hpp"
#include "SourceScan.hpp"

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/StudioStartupDocument.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace CNA::Studio;

namespace
{
    std::filesystem::path makeScratchDirectory(const std::string& name)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path()
            / ("cna-studio-tests-" + name + "-" + Uuid::generate().toString());
        std::filesystem::create_directories(directory);
        return directory;
    }

    void writeFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream << contents;
    }

    /** @brief A scene document with one entity, under whatever name the caller wants to see. */
    std::string sceneJson(const std::string& name)
    {
        return R"({"formatVersion":1,"sceneId":")" + Uuid::generate().toString()
             + R"(","name":")" + name + R"(","entities":[]})";
    }

    /** @brief A minimal native project whose startup scene is `Scenes/Startup.cnascene`. */
    std::filesystem::path writeProject(const std::filesystem::path& directory)
    {
        writeFile(directory / "Scenes" / "Startup.cnascene", sceneJson("Startup"));
        const std::filesystem::path project = directory / "Fixture.cnaproject";
        writeFile(project,
                  R"({"formatVersion":1,"name":"Fixture","kind":"CnaNative",)"
                  R"("startupScene":"Scenes/Startup.cnascene"})");
        return project;
    }
}

// ------------------------------------------------------------------------------------------------
// What opens
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(WithNoProjectTheStartupDocumentIsANewSceneRatherThanNothing)
{
    // A scene with no camera renders nothing, and nothing is what a broken editor also renders. So
    // a Studio started with no arguments opens a document rather than an empty context -- and all
    // four entry points have to agree about that, which is why it is decided in one place.
    StudioContext context;
    const StudioStartupDocument opened = openStudioStartupDocument(context, "", "");

    CNA_STUDIO_EXPECT(opened.succeeded());
    CNA_STUDIO_EXPECT(!opened.projectOpened);
    CNA_STUDIO_EXPECT(!opened.sceneOpened);
    CNA_STUDIO_EXPECT_EQ(context.getScene().getName(), std::string{"Untitled"});
}

CNA_STUDIO_TEST(WithAProjectAndNoOverrideTheProjectsOwnStartupSceneOpens)
{
    const std::filesystem::path directory = makeScratchDirectory("startup-project");
    const std::filesystem::path project = writeProject(directory);

    StudioContext context;
    const StudioStartupDocument opened =
        openStudioStartupDocument(context, project.generic_string(), "");

    CNA_STUDIO_EXPECT(opened.succeeded());
    CNA_STUDIO_EXPECT(opened.projectOpened);
    // False: the project opened its own scene, which is not the same event as an override.
    // Reporting it as an override would make the flag look honoured when it was never passed.
    CNA_STUDIO_EXPECT(!opened.sceneOpened);
    CNA_STUDIO_EXPECT_EQ(context.getScene().getName(), std::string{"Startup"});

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(TheSceneOverrideReplacesTheProjectsStartupScene)
{
    // The assertion the whole task exists for. `--scene` is applied *after* the project, because
    // the project opens its own startup scene on the way in and the override's entire purpose is
    // to replace it -- an override applied first would be overwritten by the thing it overrides.
    const std::filesystem::path directory = makeScratchDirectory("startup-override");
    const std::filesystem::path project = writeProject(directory);
    const std::filesystem::path other = directory / "Scenes" / "BossArena.cnascene";
    writeFile(other, sceneJson("BossArena"));

    StudioContext context;
    const StudioStartupDocument opened =
        openStudioStartupDocument(context, project.generic_string(), other.generic_string());

    CNA_STUDIO_EXPECT(opened.succeeded());
    CNA_STUDIO_EXPECT(opened.projectOpened);
    CNA_STUDIO_EXPECT(opened.sceneOpened);
    CNA_STUDIO_EXPECT_EQ(context.getScene().getName(), std::string{"BossArena"});

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(ASceneThatWillNotOpenIsReportedAndLeavesTheProjectOpen)
{
    // Reported rather than swallowed, which is the other half of the acceptance condition -- and
    // non-fatal, because the project is open and usable behind the message. An editor that refused
    // to start over one bad path would leave the user with no way to open a good one.
    const std::filesystem::path directory = makeScratchDirectory("startup-bad-scene");
    const std::filesystem::path project = writeProject(directory);

    StudioContext context;
    const StudioStartupDocument opened = openStudioStartupDocument(
        context, project.generic_string(), (directory / "NoSuchScene.cnascene").generic_string());

    CNA_STUDIO_EXPECT(!opened.succeeded());
    CNA_STUDIO_EXPECT(opened.projectOpened);
    CNA_STUDIO_EXPECT(!opened.sceneOpened);
    CNA_STUDIO_EXPECT(opened.error.find("NoSuchScene") != std::string::npos);
    // The project's own scene is still the open one, so the shell has something to show and its
    // status bar has something true to say.
    CNA_STUDIO_EXPECT_EQ(context.getScene().getName(), std::string{"Startup"});

    std::filesystem::remove_all(directory);
}

CNA_STUDIO_TEST(AProjectThatWillNotOpenStopsBeforeTheSceneOverride)
{
    // A scene path is resolved against a project and is opened into a document that belongs to it.
    // Opening one into whatever happened to be there would be worse than not opening it: the user
    // would get their scene, in a Studio that does not know which project it came from.
    const std::filesystem::path directory = makeScratchDirectory("startup-bad-project");
    const std::filesystem::path other = directory / "BossArena.cnascene";
    writeFile(other, sceneJson("BossArena"));

    StudioContext context;
    const StudioStartupDocument opened = openStudioStartupDocument(
        context, (directory / "NoSuchProject.cnaproject").generic_string(), other.generic_string());

    CNA_STUDIO_EXPECT(!opened.succeeded());
    CNA_STUDIO_EXPECT(!opened.projectOpened);
    CNA_STUDIO_EXPECT(!opened.sceneOpened);
    CNA_STUDIO_EXPECT(opened.error.find("NoSuchProject") != std::string::npos);
    CNA_STUDIO_EXPECT(context.getScene().getName() != std::string{"BossArena"});

    std::filesystem::remove_all(directory);
}

// ------------------------------------------------------------------------------------------------
// The guard that would have caught it
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(NoParsedFlagIsReadByThePrototypeAlone)
{
    // `--scene` was parsed, documented and consumed by `StudioApplication` alone. Every test that
    // exercised it exercised the prototype, so every one of them passed; the native shell -- the
    // default UI -- ignored the flag in silence, and there was nothing to see because ignoring it
    // produces exactly what not passing it produces.
    //
    // Written over the *parser* rather than over a list of flags somebody maintains. A guard with
    // its own inventory is a guard that goes stale the first time a flag is added, and this project
    // has already paid for one of those: the migration inventory was complete over panels, menus,
    // toolbars and shortcuts, and missed `Add Component` because a button inside a panel is none of
    // those things.
    //
    // A flag the native shell genuinely has no use for is allowed, but only by name and only with
    // the task that will close it named beside it. That is the point of the allow-list: it turns
    // "nobody wired this up" into a decision somebody had to write down.
    const std::set<std::string, std::less<>> prototypeOnlyByDesign = {
        // `--compare-backends` tolerance. The comparison itself is a shell panel on both UIs; the
        // *flag* drives the prototype's batch mode, which the native shell has no equivalent of.
        // STUDIO-02056 extracts the comparison service, and the flag follows it.
        "comparisonTolerance",
        // `--plugin-dir`. STUDIO-07052: the native shell does not load the project's plugins at
        // all yet, so there is nothing for the directory to reach.
        "pluginDirectory",
        // `--recovery-dir`. The native shell runs crash recovery through `StudioRecoverySession`
        // with the default directory; the override has not been threaded through. STUDIO-07049.
        "recoveryDirectory",
        // `--view=3d` and `--orbit=YAW,PITCH`. STUDIO-07049: the 3D smoke flags run the prototype
        // only. `orbitDegrees` is the one this guard found on its first run, which is the argument
        // for writing it over the parser -- the flag list I would have typed by hand had four
        // entries in it and `--orbit` was not one of them.
        "threeDimensionalView",
        "orbitDegrees",
    };

    const std::vector<CnaStudioTest::Scan::SourceFile> sources =
        CnaStudioTest::Scan::collectSources({"src", "include"});
    CNA_STUDIO_EXPECT(!sources.empty());

    const std::string parserPath = "src/app/StudioOptions.cpp";
    const std::string prototypePath = "src/app/StudioApplication.cpp";
    const std::string optionsHeader = "include/CNA/Studio/StudioOptions.hpp";

    std::string parser;
    for (const CnaStudioTest::Scan::SourceFile& file : sources)
    {
        if (file.relativePath == parserPath)
        {
            parser = CnaStudioTest::Scan::stripCommentsAndStrings(file.text);
        }
    }
    CNA_STUDIO_EXPECT(!parser.empty());

    // Every `options.<field> =` the parser performs. Reading the assignments rather than the
    // struct's members on purpose: a member nothing parses is not a flag, and holding an internal
    // field to this rule would be noise.
    std::set<std::string> parsed;
    for (std::size_t at = parser.find("options."); at != std::string::npos;
         at = parser.find("options.", at + 1))
    {
        std::size_t end = at + 8;
        while (end < parser.size()
               && (std::isalnum(static_cast<unsigned char>(parser[end])) != 0 || parser[end] == '_'))
        {
            ++end;
        }
        std::size_t equals = end;
        while (equals < parser.size() && parser[equals] == ' ') { ++equals; }
        if (equals >= parser.size() || parser[equals] != '=') { continue; }
        if (equals + 1 < parser.size() && parser[equals + 1] == '=') { continue; }

        parsed.insert(parser.substr(at + 8, end - at - 8));
    }

    // The parser sets dozens of fields; a handful would mean the extraction above broke rather
    // than that the parser shrank.
    CNA_STUDIO_EXPECT(parsed.size() >= 30);

    std::size_t checked = 0;
    for (const std::string& field : parsed)
    {
        // Errors and help are the parser reporting on itself, not flags anything downstream reads.
        if (field == "hasError" || field == "errorMessage") { continue; }

        bool prototypeReads = false;
        bool anythingElseReads = false;

        for (const CnaStudioTest::Scan::SourceFile& file : sources)
        {
            if (file.relativePath == parserPath || file.relativePath == optionsHeader) { continue; }

            const std::string code = CnaStudioTest::Scan::stripCommentsAndStrings(file.text);
            if (code.find("." + field) == std::string::npos) { continue; }

            if (file.relativePath == prototypePath) { prototypeReads = true; }
            else { anythingElseReads = true; }
        }

        if (!prototypeReads) { continue; }
        ++checked;

        if (!anythingElseReads && prototypeOnlyByDesign.find(field) == prototypeOnlyByDesign.end())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "`--" + field + "` is parsed and read by " + prototypePath
                + " alone, so the default UI ignores it in silence. Wire it into the native shell, "
                  "or name it in this test's allow-list with the task that will.");
        }
    }

    // A run in which nothing was examined would pass every assertion above by examining nothing --
    // which is the state this guard would fall into the day someone renames the prototype's file.
    CNA_STUDIO_EXPECT(checked >= 5);
}
