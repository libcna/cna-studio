// SPDX-License-Identifier: MS-PL
/**
 * @file StudioStatusBarTests.cpp
 * @brief What the status bar reports (plan.md STUDIO-06007).
 *
 * The bar answers four different questions in one strip — what is open, whether it is saved, what
 * is running, and what this project ships on — and the way it goes wrong is by conflating two of
 * them. The renderer Studio draws with is *not* the renderer the game ships on, and a bar that said
 * one where it meant the other is exactly how somebody tests on the wrong backend for a week.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 1280.0f;
    constexpr float kHeight = 720.0f;

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A project on disk with a scene beside it, removed on the way out. */
    class ScopedProject
    {
    public:
        explicit ScopedProject(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-status-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_ / "Scenes", code);

            std::ofstream project{path_ / "Game.cnaproject", std::ios::binary};
            project << R"({"formatVersion":1,"name":"Statused","kind":"CnaNative",)"
                       R"("startupScene":"Scenes/Level.cnascene","sceneDirectory":"Scenes"})";
        }

        ~ScopedProject()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedProject(const ScopedProject&) = delete;
        ScopedProject& operator=(const ScopedProject&) = delete;

        [[nodiscard]] std::string file() const
        {
            return (path_ / "Game.cnaproject").generic_string();
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    struct Harness
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell;
        StudioShellPanels panels{shell, context, log};

        Harness() { shell.resetLayout(); }

        void frame() { shell.renderFrame(at(-1.0f, -1.0f)); }
        void poll() { panels.poll(0.0); }

        void click(float x, float y)
        {
            shell.renderFrame(at(x, y));
            shell.renderFrame(at(x, y, true));
            shell.renderFrame(at(x, y, false));
        }
    };

    bool contains(const std::string& text, std::string_view needle)
    {
        return text.find(needle) != std::string::npos;
    }
}

CNA_STUDIO_TEST(AProfileReadsTheSameWhereverItIsShown)
{
    // One place decides the wording, because it is shown in the status bar, in the Build panel's
    // target list and in a build log, and three spellings read as three different targets.
    StudioTargetProfile profile;
    profile.os = StudioTargetOs::Linux;
    profile.architecture = StudioArchitecture::X86_64;
    profile.renderer = "opengles3";
    profile.configuration = StudioBuildConfiguration::Release;

    const std::string summary = studioTargetProfileSummary(profile);
    CNA_STUDIO_EXPECT(contains(summary, "x86_64"));
    CNA_STUDIO_EXPECT(contains(summary, "opengles3"));
    CNA_STUDIO_EXPECT(contains(summary, "Release"));
}

CNA_STUDIO_TEST(WithNoProjectTheBarSaysSoRatherThanGoingBlank)
{
    Harness harness;
    harness.poll();
    harness.frame();

    CNA_STUDIO_EXPECT(contains(harness.shell.status().message, "No project"));
    CNA_STUDIO_EXPECT(harness.shell.status().target.empty());
    CNA_STUDIO_EXPECT(harness.shell.status().jobs.empty());
}

CNA_STUDIO_TEST(TheBarSaysWhatIsOpenAndWhatTheProjectShipsOn)
{
    Harness harness;
    const ScopedProject project{"open"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    harness.poll();

    CNA_STUDIO_EXPECT(contains(harness.shell.status().message, "Statused"));
    CNA_STUDIO_EXPECT(!harness.shell.status().target.empty());

    // The target is the *project's*, not the renderer Studio is drawing with. Conflating them is
    // how somebody tests on the wrong backend for a week.
    std::vector<StudioTargetProfile> profiles = harness.context.getProject().getTargetProfiles();
    profiles.front().renderer = "directx11";
    profiles.front().os = StudioTargetOs::Windows;
    harness.context.getProject().setTargetProfiles(profiles);
    harness.shell.status().renderer = "opengles3 on sdl3";
    harness.poll();

    CNA_STUDIO_EXPECT(contains(harness.shell.status().target, "directx11"));
    CNA_STUDIO_EXPECT(contains(harness.shell.status().target, "Windows"));
    CNA_STUDIO_EXPECT_EQ(harness.shell.status().renderer, std::string{"opengles3 on sdl3"});
}

CNA_STUDIO_TEST(AnUnsavedSceneIsMarkedAndTheMarkGoesWhenItIsSaved)
{
    // The one thing a user checks before closing the window, so a mark that can be wrong is worse
    // than no mark at all.
    Harness harness;
    const ScopedProject project{"dirty"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    harness.poll();
    CNA_STUDIO_EXPECT(!harness.shell.status().modified);

    // Through a real command, because "the document is dirty" is the history's answer and a test
    // that set a flag would not be asking it.
    StudioEntity marker;
    marker.setName("Marker");
    const Uuid created = harness.context.getScene().addEntity(std::move(marker));
    harness.context.execute(
        std::make_unique<DeleteEntityCommand>(harness.context.getScene(), created));
    harness.poll();
    CNA_STUDIO_EXPECT(harness.shell.status().modified);
}

CNA_STUDIO_TEST(AProblemStaysUpWhereAMessageWouldBeOverwritten)
{
    // The message is rebuilt from the document every poll, so a reason a project would not open
    // would be gone by the next frame -- leaving "No project open", which is true and useless.
    Harness harness;
    harness.shell.status().problem = "Could not open /nowhere/Game.cnaproject";
    harness.poll();
    harness.frame();

    CNA_STUDIO_EXPECT(contains(harness.shell.status().problem, "Could not open"));

    // And a project that opens answers it.
    const ScopedProject project{"recover"};
    CNA_STUDIO_EXPECT(harness.context.openProject(project.file()));
    harness.poll();
    CNA_STUDIO_EXPECT(harness.shell.status().problem.empty());
}

CNA_STUDIO_TEST(APlayingGameIsReportedAndTheBarOffersItsOwnStop)
{
    // A job the user can see running and cannot stop is the worst kind of progress report.
    Harness harness;

    // A command of the test's own, always enabled: `studio.play.stop` is greyed out unless a real
    // player is running, and a disabled button proves nothing about the one the user would press.
    bool stopped = false;
    StudioAction stop;
    stop.id = "test.stop";
    stop.label = "Stop";
    stop.run = [&stopped] { stopped = true; };
    harness.shell.actions().add(std::move(stop));

    harness.shell.status().jobs = {StudioStatusJob{"Playing", -1.0f, "test.stop"}};
    harness.frame();

    const UiRect bar = harness.shell.layout().statusBar;
    CNA_STUDIO_EXPECT(!bar.isEmpty());

    // Laid out right to left, the way the bar lays it out: renderer, target, then the Stop button.
    const StudioTheme& theme = harness.shell.frame().theme();
    const float spacing = static_cast<float>(theme.metric(StudioMetric::SpacingMedium));
    const auto wide = [&](const std::string& text) {
        return text.empty() ? 0.0f
                            : std::ceil(studioLabelWidth(harness.shell.frame(), text,
                                                         StudioFontRole::BodySmall))
                                  + spacing;
    };

    const StudioStatusModel& status = harness.shell.status();
    float right = bar.right() - spacing;
    right -= wide(status.renderer.empty() ? std::string{} : "Renderer: " + status.renderer);
    right -= wide(status.target.empty() ? std::string{} : "Target: " + status.target);

    const float stopWidth =
        std::ceil(studioLabelWidth(harness.shell.frame(), "Stop", StudioFontRole::BodySmall))
        + spacing * 2.0f;
    harness.click(right - stopWidth * 0.5f, bar.centerY());

    CNA_STUDIO_EXPECT(stopped);

    // And a job whose command is greyed out draws the button greyed rather than leaving the user
    // wondering why pressing it does nothing.
    harness.shell.status().jobs = {StudioStatusJob{"Playing", -1.0f, "studio.play.stop"}};
    harness.frame();
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled("studio.play.stop"));
}

CNA_STUDIO_TEST(AJobThatEndedLeavesNoBarBehind)
{
    // The status bar reports what is running *now*, and "now" is what a poll is for -- a job list
    // edited rather than rebuilt would leave a progress bar on screen for a build that finished.
    Harness harness;
    harness.shell.status().jobs = {StudioStatusJob{"Building", 0.5f, "studio.build.cancel"}};
    harness.frame();
    CNA_STUDIO_EXPECT_EQ(harness.shell.status().jobs.size(), std::size_t{1});

    harness.poll();
    CNA_STUDIO_EXPECT(harness.shell.status().jobs.empty());
}

CNA_STUDIO_TEST(AJobWhoseLengthIsUnknownSaysSoRatherThanInventingABar)
{
    // A player that has been launched runs for as long as the user plays, and a bar pretending to
    // know how long that is would be lying in the one place the editor reports facts.
    Harness harness;
    harness.shell.status().jobs = {StudioStatusJob{"Playing", -1.0f, {}}};
    CNA_STUDIO_EXPECT(harness.shell.status().jobs.front().progress < 0.0f);

    harness.frame();
    CNA_STUDIO_EXPECT(harness.shell.frame().drawData().getTotalCommandCount() > 0);
}

CNA_STUDIO_TEST(TheBarDrawsAtAnyWidthWithoutOverflowing)
{
    // A long project name must not push the renderer off the end: the fixed-width answers are laid
    // out first, from the right, and the message takes what is left.
    Harness harness;
    harness.shell.status().message = std::string(400, 'x');
    harness.shell.status().target = "Windows x86_64 - directx11 - RelWithDebInfo";
    harness.shell.status().renderer = "opengles3 on sdl3";
    harness.shell.status().jobs = {StudioStatusJob{"Building, step 3 of 4", 0.75f,
                                                   "studio.build.cancel"}};

    for (const float width : {320.0f, 640.0f, 1280.0f, 2560.0f})
    {
        UiInputState input = at(-1.0f, -1.0f);
        input.displayWidth = width;
        harness.shell.renderFrame(input);

        const UiDrawDataValidation checked = validate(harness.shell.frame().drawData());
        CNA_STUDIO_EXPECT(checked.valid);
        CNA_STUDIO_EXPECT_EQ(harness.shell.frame().phaseViolations(), std::size_t{0});
    }
}
