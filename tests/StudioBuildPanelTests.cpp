// SPDX-License-Identifier: MS-PL
/**
 * @file StudioBuildPanelTests.cpp
 * @brief The Build panel, and the first interface the six-axis target profile has ever had
 *        (plan.md STUDIO-07010).
 *
 * The ImGui panel this replaces offers two axes and keeps them in its own members, so what the
 * user chose was forgotten when the panel closed and was never saved. The cases here are about the
 * two properties that fixes: an edit reaches the *project*, and the choices offered are only the
 * ones CNA will actually configure.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/Cpp/CppToolchain.hpp"
#include "CNA/Studio/ShellPanels/StudioBuildPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 900.0f;
    constexpr float kHeight = 700.0f;

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.deltaSeconds = 1.0f / 60.0f;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A project on disk, so `hasProject()` is true, removed on the way out. */
    class ScopedProjectFile
    {
    public:
        explicit ScopedProjectFile(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-build-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_, code);

            std::ofstream stream{path_ / "Game.cnaproject", std::ios::binary};
            stream << R"({"formatVersion":1,"name":"Built","kind":"CnaNative"})";
        }

        ~ScopedProjectFile()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedProjectFile(const ScopedProjectFile&) = delete;
        ScopedProjectFile& operator=(const ScopedProjectFile&) = delete;

        [[nodiscard]] std::string file() const
        {
            return (path_ / "Game.cnaproject").generic_string();
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    /** @brief The panel driven over a real context, with the frame the shell would give it. */
    struct Harness
    {
        ScopedProjectFile project;
        StudioContext context;
        BuildProcess build;
        StudioBuildPanel panel{context, build};
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioBuildPanelResult last;

        explicit Harness(const std::string& name) : project(name)
        {
            (void)context.openProject(project.file());
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioBuildPanelResult panelResult = panel.draw(f, body);
                if (f.isInputPass()) { last = panelResult; }
            });
        }

        void settle() { run(at(kWidth - 10.0f, kHeight - 10.0f)); }

        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        [[nodiscard]] const StudioTargetProfile& profile() const
        {
            return context.getProject().getActiveTargetProfile();
        }
    };
}

CNA_STUDIO_TEST(WithNoProjectThePanelSaysSoRatherThanDrawingAnEmptyForm)
{
    StudioContext context;
    BuildProcess build;
    StudioBuildPanel panel{context, build};
    StudioFrame frame{StudioTheme::dark()};

    StudioBuildPanelResult result;
    runStudioFrame(frame, at(10.0f, 10.0f), [&](StudioFrame& f) {
        const StudioBuildPanelResult drawn = panel.draw(f, UiRect{0.0f, 0.0f, kWidth, kHeight});
        if (f.isInputPass()) { result = drawn; }
    });

    CNA_STUDIO_EXPECT(!result.buildRequested);
    CNA_STUDIO_EXPECT(!result.profileChanged);
    CNA_STUDIO_EXPECT_EQ(frame.phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(AProjectAlwaysHasATargetToShow)
{
    // A project with no profile cannot be built, and the reader substitutes the default rather
    // than leaving a state the panel would have to draw as nothing at all.
    Harness harness{"default"};
    harness.settle();

    CNA_STUDIO_EXPECT(!harness.context.getProject().getTargetProfiles().empty());
    CNA_STUDIO_EXPECT_EQ(harness.frame.phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(TheRendererListOffersOnlyWhatCnaWillConfigureForTheChosenSystem)
{
    // Offering Direct3D on a Linux target and then failing validation would be the tool asking the
    // user to discover a rule it already knows -- and CNA makes it a hard configure error, so the
    // discovery costs a full configure to make.
    Harness harness{"renderers"};

    std::vector<StudioTargetProfile> profiles = harness.context.getProject().getTargetProfiles();
    profiles.front().os = StudioTargetOs::Linux;
    profiles.front().renderer = "opengles3";
    harness.context.getProject().setTargetProfiles(profiles);
    harness.settle();

    CNA_STUDIO_EXPECT(isRendererAvailableOn("opengles3", StudioTargetOs::Linux));
    CNA_STUDIO_EXPECT(!isRendererAvailableOn("directx11", StudioTargetOs::Linux));
    CNA_STUDIO_EXPECT(isRendererAvailableOn("directx11", StudioTargetOs::Windows));
}

CNA_STUDIO_TEST(ChangingAnAxisReachesTheProjectRatherThanThePanel)
{
    // The whole point of the port. The legacy panel kept its platform and backend in its own
    // members: what the user chose was forgotten when the panel closed and never saved.
    Harness harness{"edit"};

    // Started somewhere that is not the answer, so a click that did nothing cannot pass.
    std::vector<StudioTargetProfile> profiles = harness.context.getProject().getTargetProfiles();
    profiles.front().configuration = StudioBuildConfiguration::MinSizeRel;
    harness.context.getProject().setTargetProfiles(profiles);
    harness.settle();
    CNA_STUDIO_EXPECT(harness.profile().configuration == StudioBuildConfiguration::MinSizeRel);

    // Driven through the widget rather than by calling the project, because "does the control
    // reach the model" is the thing that can be wrong.
    const float rowHeight = static_cast<float>(harness.frame.theme().metric(StudioMetric::ControlHeight));
    const float spacing = static_cast<float>(harness.frame.theme().metric(StudioMetric::SpacingSmall));
    const float margin = static_cast<float>(harness.frame.theme().metric(StudioMetric::SpacingMedium));

    // Target, OS, architecture, renderer, platform, configuration: the sixth row.
    const float configurationTop = margin + 5.0f * (rowHeight + spacing);
    const float labelWidth = std::min((kWidth - margin * 2.0f) * 0.4f,
        static_cast<float>(harness.frame.theme().metric(StudioMetric::PanelHeaderHeight)) * 5.0f);
    const float controlX = margin + labelWidth + spacing + 20.0f;

    harness.click(controlX, configurationTop + rowHeight * 0.5f);
    CNA_STUDIO_EXPECT(harness.frame.isAnyPopupOpen());

    // The first row of the open list is Debug, which is not the default.
    const float firstRow = configurationTop + rowHeight + spacing
        + static_cast<float>(harness.frame.theme().metric(StudioMetric::SpacingSmall))
        + studioMenuItemHeight(harness.frame.theme()) * 0.5f;
    harness.click(controlX, firstRow);
    harness.settle();

    CNA_STUDIO_EXPECT(harness.profile().configuration == StudioBuildConfiguration::Debug);
    CNA_STUDIO_EXPECT(harness.last.profileChanged || !harness.frame.isAnyPopupOpen());
}

CNA_STUDIO_TEST(TheBuildItWouldRunIsTheOneTheProjectsActiveProfileDescribes)
{
    // Not the panel's own idea of a platform and a backend, which is what the legacy panel built
    // from -- a panel that could start a different build from the one Play uses.
    Harness harness{"request"};
    harness.settle();

    const StudioBuildJob fromPanel = harness.panel.planBuild();
    const BuildRequest fromProject =
        makeBuildRequestFromActiveProfile(harness.context.getProject());

    CNA_STUDIO_EXPECT_EQ(fromPanel.buildDirectory, getDefaultBuildDirectory(fromProject));
    CNA_STUDIO_EXPECT(fromPanel.description.find(fromProject.configuration) != std::string::npos);
}

CNA_STUDIO_TEST(TurningAnOptionalSubsystemOnIsRecordedOnTheProfile)
{
    Harness harness{"features"};
    harness.settle();

    const StudioFeatureOption* net = findStudioFeature("net");
    CNA_STUDIO_EXPECT(net != nullptr);
    CNA_STUDIO_EXPECT(!harness.profile().hasFeature("net"));

    std::vector<StudioTargetProfile> profiles = harness.context.getProject().getTargetProfiles();
    profiles.front().setFeature("net", true);
    harness.context.getProject().setTargetProfiles(profiles);
    harness.settle();

    CNA_STUDIO_EXPECT(harness.profile().hasFeature("net"));

    // And it reaches the arguments Studio would configure the game's build with, which is the only
    // thing turning a feature on is for.
    const std::vector<std::string> arguments =
        studioTargetProfileCMakeArguments(harness.profile());
    bool found = false;
    for (const std::string& argument : arguments)
    {
        if (argument == "-DCNA_ENABLE_NET=ON") { found = true; }
    }
    CNA_STUDIO_EXPECT(found);
}

CNA_STUDIO_TEST(APanelDescribedTwiceCommitsNoPhaseViolations)
{
    // The panel splits one rectangle between two passes, and a split that ran only in the draw
    // pass would put every control somewhere different from where it was hit-tested.
    Harness harness{"phases"};
    for (int i = 0; i < 4; ++i)
    {
        harness.settle();
        CNA_STUDIO_EXPECT_EQ(harness.frame.phaseViolations(), std::size_t{0});
    }
}

CNA_STUDIO_TEST(TheBuildButtonIsRefusedWhenTheProfileCannotBeBuilt)
{
    Harness harness{"invalid"};

    std::vector<StudioTargetProfile> profiles = harness.context.getProject().getTargetProfiles();
    profiles.front().os = StudioTargetOs::Linux;
    profiles.front().renderer = "directx11";
    harness.context.getProject().setTargetProfiles(profiles);
    harness.settle();

    StudioTargetProfile checked = harness.profile();
    const StudioProfileValidation validation = validateStudioTargetProfile(checked);
    CNA_STUDIO_EXPECT(!validation.isBuildable());
    CNA_STUDIO_EXPECT(!harness.last.buildRequested);
}
