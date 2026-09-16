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
 * The guard that would have caught it, `NoParsedFlagIsReadByThePrototypeAlone`, scanned the
 * prototype's own source for exactly this shape: a flag the parser set that only `StudioApplication`
 * read. STUDIO-07030 deleted that file along with the rest of the prototype, so the guard went with
 * it -- there is only one UI left for a parsed flag to go unread by, and that is a lone caller, not
 * a divergence between two.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/StudioStartupDocument.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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
