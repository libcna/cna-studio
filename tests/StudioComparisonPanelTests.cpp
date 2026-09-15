// SPDX-License-Identifier: MS-PL
/**
 * @file StudioComparisonPanelTests.cpp
 * @brief The same scene on every installed renderer (plan.md STUDIO-07014).
 *
 * The legacy panel could only be exercised by launching several games, which is why it never was.
 * This one reads a snapshot, so the cases worth having are the ones a real run makes expensive to
 * reach: a capture that never arrived, two frames of different sizes, a renderer that would not
 * launch, and the difference between "still working" and "finished and nothing came back".
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioComparisonPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cmath>

#include <optional>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 900.0f;
    constexpr float kHeight = 320.0f;

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

    /** @brief The reference entry: captured, compared against nothing. */
    ComparisonEntry reference(std::string backend)
    {
        ComparisonEntry entry;
        entry.backend = std::move(backend);
        entry.capturePath = "/project/build/comparison/" + entry.backend + ".png";
        entry.fileStem = entry.backend;
        entry.captured = true;
        entry.isReference = true;
        return entry;
    }

    /** @brief A captured entry whose frame differs from the reference by @p differing pixels. */
    ComparisonEntry compared(std::string backend, std::size_t differing, int maxDelta = 0)
    {
        ComparisonEntry entry;
        entry.backend = std::move(backend);
        entry.capturePath = "/project/build/comparison/" + entry.backend + ".png";
        entry.fileStem = entry.backend;
        entry.captured = true;
        entry.difference.comparable = true;
        entry.difference.totalPixels = 10000;
        entry.difference.differingPixels = differing;
        entry.difference.maxChannelDelta = differing == 0 ? 0 : maxDelta;
        if (differing != 0)
        {
            entry.difference.boundingBox = StudioRectangle{12, 34, 56, 78};
            entry.differencePath = "/project/build/comparison/" + entry.backend + "-diff.png";
        }
        return entry;
    }

    struct Fixture
    {
        StudioComparisonView view;
        std::vector<ComparisonEntry> entries;
        StudioTreeState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioComparisonResult last;

        Fixture()
        {
            view.hasProject = true;
            view.outputDirectory = "/project/build/comparison";
            view.entries = &entries;
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioComparisonResult drawn =
                    studioComparisonPanel(f, body, view, state);
                if (f.isInputPass()) { last = drawn; }
            });
        }

        void settle() { run(at(kWidth - 5.0f, kHeight - 5.0f)); }

        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        [[nodiscard]] std::vector<StudioTreeRow> rows() const
        {
            return studioComparisonRows(entries, view.state, state);
        }

        /**
         * @brief The row whose label begins with @p prefix, by value.
         *
         * By value because the rows are rebuilt on every call, so a pointer into them points into
         * a temporary that has already gone.
         */
        [[nodiscard]] std::optional<StudioTreeRow> rowStarting(std::string_view prefix) const
        {
            for (const StudioTreeRow& row : rows())
            {
                if (row.label.rfind(prefix, 0) == 0) { return row; }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<StudioTreeRow> rowLabelled(std::string_view label) const
        {
            for (const StudioTreeRow& row : rows())
            {
                if (row.label == label) { return row; }
            }
            return std::nullopt;
        }
    };

    bool contains(const std::string& text, std::string_view needle)
    {
        return text.find(needle) != std::string::npos;
    }
}

CNA_STUDIO_TEST(WithNoProjectThereIsNothingToCompareAndThePanelSaysSo)
{
    // Not a Compare button that does nothing. A control that is drawn and refuses is
    // indistinguishable from one that is broken.
    Fixture fixture;
    fixture.view.hasProject = false;
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.rowsTotal, std::size_t{0});
    CNA_STUDIO_EXPECT(!fixture.last.compareRequested);
}

CNA_STUDIO_TEST(APressOnCompareIsReportedRatherThanActedOnByThePanel)
{
    // The panel starts nothing: it reports what was pressed and the owner of the run decides. That
    // is what lets the same run outlive the tab it was started from.
    Fixture fixture;
    fixture.settle();
    fixture.click(30.0f, 16.0f);

    CNA_STUDIO_EXPECT(fixture.last.compareRequested);
    CNA_STUDIO_EXPECT(!fixture.last.cancelRequested);
}

CNA_STUDIO_TEST(WhileARunIsInFlightTheOnlyButtonIsCancel)
{
    // Two comparisons at once is several games racing for one set of capture files, so starting a
    // second is unreachable rather than handled.
    Fixture fixture;
    fixture.view.state = ComparisonState::Capturing;
    fixture.settle();
    fixture.click(30.0f, 16.0f);

    CNA_STUDIO_EXPECT(fixture.last.cancelRequested);
    CNA_STUDIO_EXPECT(!fixture.last.compareRequested);
}

CNA_STUDIO_TEST(AProblemIsSaidBeforeTheButtonRatherThanAfterPressingIt)
{
    // The common case is a user with one player build installed. "Nothing happened" would be the
    // worst possible answer to pressing Compare.
    Fixture fixture;
    fixture.view.problem = "only one player build was found";
    fixture.settle();
    fixture.click(30.0f, 16.0f);

    CNA_STUDIO_EXPECT(!fixture.last.compareRequested);
    CNA_STUDIO_EXPECT_EQ(fixture.last.rowsTotal, std::size_t{0});

    // But it must not hide a run that is already going: a comparison started before the last
    // player build was deleted still has results worth reading.
    fixture.view.state = ComparisonState::Capturing;
    fixture.entries.push_back(reference("software"));
    fixture.settle();
    CNA_STUDIO_EXPECT(fixture.last.rowsTotal > 0);
}

CNA_STUDIO_TEST(EveryRendererGetsARowAndTheReferenceIsNamedOnIt)
{
    // Which renderer everything else was measured against is not a detail: the same pair of
    // captures reads as "opengl is wrong" or "software is wrong" depending on it.
    Fixture fixture;
    fixture.entries.push_back(reference("software"));
    fixture.entries.push_back(compared("opengl", 0));

    const std::optional<StudioTreeRow> software = fixture.rowStarting("software");
    CNA_STUDIO_EXPECT(software.has_value());
    CNA_STUDIO_EXPECT(contains(software->label, "reference"));
    CNA_STUDIO_EXPECT(software->hasChildren);

    const std::optional<StudioTreeRow> opengl = fixture.rowStarting("opengl");
    CNA_STUDIO_EXPECT(opengl.has_value());
    CNA_STUDIO_EXPECT(!contains(opengl->label, "reference"));
}

CNA_STUDIO_TEST(AgreementAndDisagreementDoNotReadTheSame)
{
    // Told apart by colour as well as by words, because a list of grey sentences is a list the eye
    // has to read line by line -- and the whole point of the panel is the one row that is wrong.
    Fixture fixture;
    fixture.entries.push_back(reference("software"));
    fixture.entries.push_back(compared("opengl", 0));
    fixture.entries.push_back(compared("vulkan", 137, 42));

    const std::optional<StudioTreeRow> same = fixture.rowStarting("opengl");
    CNA_STUDIO_EXPECT(same.has_value());
    CNA_STUDIO_EXPECT_EQ(same->detail, std::string{"identical"});
    CNA_STUDIO_EXPECT(same->detailRole == StudioColorRole::Success);

    const std::optional<StudioTreeRow> different = fixture.rowStarting("vulkan");
    CNA_STUDIO_EXPECT(different.has_value());
    CNA_STUDIO_EXPECT(contains(different->detail, "137 of 10000"));
    CNA_STUDIO_EXPECT(contains(different->detail, "1.37%"));
    CNA_STUDIO_EXPECT(different->detailRole == StudioColorRole::Warning);
}

CNA_STUDIO_TEST(WhereTheyDisagreeIsInTheRowsAndNotJustHowMuch)
{
    // A band along one edge is a viewport or scissor problem; a scattering over one sprite is a
    // filtering one. The rectangle usually is the diagnosis, so it is not left to the image file.
    Fixture fixture;
    fixture.entries.push_back(reference("software"));
    fixture.entries.push_back(compared("vulkan", 137, 42));

    const std::optional<StudioTreeRow> box = fixture.rowLabelled("Differs within");
    CNA_STUDIO_EXPECT(box.has_value());
    CNA_STUDIO_EXPECT(contains(box->detail, "56x78"));
    CNA_STUDIO_EXPECT(contains(box->detail, "(12, 34)"));

    CNA_STUDIO_EXPECT(fixture.rowLabelled("Largest channel difference").has_value());
    CNA_STUDIO_EXPECT_EQ(fixture.rowLabelled("Largest channel difference")->detail,
                         std::string{"42"});
    CNA_STUDIO_EXPECT(fixture.rowLabelled("Difference image").has_value());

    // And an identical renderer offers no rectangle, because there is no region to point at.
    fixture.entries.push_back(compared("opengl", 0));
    std::size_t boxes = 0;
    for (const StudioTreeRow& row : fixture.rows())
    {
        if (row.label == "Differs within") { ++boxes; }
    }
    CNA_STUDIO_EXPECT_EQ(boxes, std::size_t{1});
}

CNA_STUDIO_TEST(ACollapsedRendererHidesItsDetailAndKeepsItsVerdict)
{
    // The verdict is the line a user scans; the detail is what they open when one of them is wrong.
    Fixture fixture;
    fixture.entries.push_back(reference("software"));
    fixture.entries.push_back(compared("vulkan", 137, 42));

    const std::size_t open = fixture.rows().size();
    fixture.state.setExpanded("backend:vulkan", false);
    const std::vector<StudioTreeRow> closed = fixture.rows();

    CNA_STUDIO_EXPECT(closed.size() < open);
    CNA_STUDIO_EXPECT(fixture.rowStarting("vulkan").has_value());
    CNA_STUDIO_EXPECT(contains(fixture.rowStarting("vulkan")->detail, "137 of 10000"));
    CNA_STUDIO_EXPECT(!fixture.rowLabelled("Differs within").has_value());
}

CNA_STUDIO_TEST(ACaptureThatHasNotArrivedYetIsNotACaptureThatNeverWill)
{
    // The same empty entry means "be patient" during a run and "this renderer produced nothing"
    // after it, and a user cannot act on the second while it is worded as the first.
    Fixture fixture;
    fixture.entries.push_back(reference("software"));

    ComparisonEntry silent;
    silent.backend = "vulkan";
    fixture.entries.push_back(silent);

    fixture.view.state = ComparisonState::Capturing;
    CNA_STUDIO_EXPECT(contains(fixture.rowStarting("vulkan")->detail, "waiting"));
    CNA_STUDIO_EXPECT(fixture.rowStarting("vulkan")->detailRole == StudioColorRole::TextSecondary);

    fixture.view.state = ComparisonState::Finished;
    CNA_STUDIO_EXPECT(contains(fixture.rowStarting("vulkan")->detail, "no frame"));
    CNA_STUDIO_EXPECT(fixture.rowStarting("vulkan")->detailRole == StudioColorRole::Error);
}

CNA_STUDIO_TEST(ACaptureOfTheWrongSizeIsNotReportedAsADisagreement)
{
    // A size mismatch means the capture went wrong, not that the renderers draw differently, and
    // the two call for entirely different actions.
    Fixture fixture;
    fixture.entries.push_back(reference("software"));

    ComparisonEntry mismatched;
    mismatched.backend = "vulkan";
    mismatched.captured = true;
    mismatched.difference.comparable = false;
    mismatched.difference.incomparableReason = "800x600 against 1280x720";
    fixture.entries.push_back(mismatched);

    const std::optional<StudioTreeRow> row = fixture.rowStarting("vulkan");
    CNA_STUDIO_EXPECT(row.has_value());
    CNA_STUDIO_EXPECT(contains(row->detail, "cannot compare"));
    CNA_STUDIO_EXPECT(contains(row->detail, "800x600"));
    CNA_STUDIO_EXPECT(!contains(row->detail, "pixels differ"));
}

CNA_STUDIO_TEST(ARendererThatWouldNotLaunchSaysWhyRatherThanSittingBlank)
{
    // The commonest failure of all: a player built for one renderer and not another. It is the
    // panel's answer, not an absence of one.
    Fixture fixture;
    fixture.entries.push_back(reference("software"));

    ComparisonEntry absent;
    absent.backend = "vulkan";
    absent.errorMessage = "could not launch /opt/cna-player-vulkan: No such file or directory";
    fixture.entries.push_back(absent);

    const std::optional<StudioTreeRow> row = fixture.rowStarting("vulkan");
    CNA_STUDIO_EXPECT(row.has_value());
    CNA_STUDIO_EXPECT(contains(row->detail, "No such file"));
    CNA_STUDIO_EXPECT(row->detailRole == StudioColorRole::Error);
    CNA_STUDIO_EXPECT(fixture.rowLabelled("Problem").has_value());
}

CNA_STUDIO_TEST(TheVerdictIsOnlySaidOnceTheRunHasActuallyFinished)
{
    // "Every renderer drew the same picture" is true of a run that has compared nothing, and
    // saying it early would be the panel's one job done wrong.
    CNA_STUDIO_EXPECT(studioComparisonSummary(ComparisonState::Idle, true).empty());
    CNA_STUDIO_EXPECT(studioComparisonSummary(ComparisonState::Launching, true).empty());
    CNA_STUDIO_EXPECT(studioComparisonSummary(ComparisonState::Capturing, true).empty());
    CNA_STUDIO_EXPECT(studioComparisonSummary(ComparisonState::Failed, true).empty());

    CNA_STUDIO_EXPECT(contains(studioComparisonSummary(ComparisonState::Finished, true),
                               "same picture"));
    CNA_STUDIO_EXPECT(contains(studioComparisonSummary(ComparisonState::Finished, false),
                               "do not agree"));
}

CNA_STUDIO_TEST(ToleranceIsClampedRatherThanTrusted)
{
    // A tolerance of 255 calls every pair of images identical, which is a comparison that can
    // never report anything. A control that can be set to "always agree" is worse than none.
    Fixture fixture;
    fixture.settle();

    // Located the way the panel lays it out, rather than by a number that happens to work: the
    // button and the label are as wide as their text, so a pinned coordinate would drift with the
    // font.
    const float spacing = static_cast<float>(fixture.frame.theme().metric(StudioMetric::SpacingSmall));
    const float control = static_cast<float>(fixture.frame.theme().metric(StudioMetric::ControlHeight));
    const float button = std::ceil(studioLabelWidth(fixture.frame, "Compare")) + spacing * 4.0f;
    const float label = std::ceil(studioLabelWidth(fixture.frame, "Tolerance")) + spacing;
    const float fieldX = spacing + button + spacing * 2.0f + label + control;
    fixture.run(at(fieldX, 16.0f));
    fixture.run(at(fieldX, 16.0f, /*leftDown=*/true));
    fixture.run(at(fieldX, 16.0f));

    UiInputState typing = at(fieldX, 16.0f);
    typing.characters = {u'9', u'9', u'9'};
    fixture.run(typing);

    UiInputState enter = at(fieldX, 16.0f);
    enter.setKeyDown(UiKey::Enter, true);
    fixture.run(enter);

    CNA_STUDIO_EXPECT(fixture.last.toleranceChanged);
    CNA_STUDIO_EXPECT_EQ(fixture.last.tolerance, kStudioMaxComparisonTolerance);
}

// --- Which renderer Play launches on (docs/MIGRATION-INVENTORY.md, toolbar table) ---------------

CNA_STUDIO_TEST(TheProjectDecidesWhichRendererPlayUsesUntilSomebodySaysOtherwise)
{
    // The common case, and the one the native shell answers *better* than the prototype's dropdown:
    // a renderer chosen by the project is a decision that survives the session and is the same for
    // everyone on the team. What was missing is overriding it for one run.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioShellPanels panels{shell, context, log};

    panels.setPlayerBuilds({PlayerBuild{"software", "/builds/cna-player-software"},
                            PlayerBuild{"opengles3", "/builds/cna-player-opengles3"}});

    CNA_STUDIO_EXPECT(panels.playerBuildOverride().empty());

    CNA_STUDIO_EXPECT(panels.selectPlayerBuild("opengles3"));
    CNA_STUDIO_EXPECT_EQ(panels.playerBuildOverride(), std::string{"opengles3"});

    // Empty means "back to the project", which is a different answer from "no choice was made".
    CNA_STUDIO_EXPECT(panels.selectPlayerBuild({}));
    CNA_STUDIO_EXPECT(panels.playerBuildOverride().empty());
}

CNA_STUDIO_TEST(ChoosingARendererThatIsNotInstalledIsRefused)
{
    // Rather than accepted and then quietly ignored at launch, which would leave the panel showing
    // one renderer and Play using another.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioShellPanels panels{shell, context, log};
    panels.setPlayerBuilds({PlayerBuild{"software", "/builds/cna-player-software"}});

    CNA_STUDIO_EXPECT(!panels.selectPlayerBuild("vulkan"));
    CNA_STUDIO_EXPECT(panels.playerBuildOverride().empty());
}

CNA_STUDIO_TEST(AnOverrideIsDroppedWhenItsBuildStopsBeingInstalled)
{
    // Discovery runs again when the executable directory changes. An override naming a build that
    // has gone would make Play fall back silently to something else, so the panel would show one
    // renderer and the game would run on another.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioShellPanels panels{shell, context, log};

    panels.setPlayerBuilds({PlayerBuild{"software", "/builds/cna-player-software"},
                            PlayerBuild{"opengles3", "/builds/cna-player-opengles3"}});
    CNA_STUDIO_EXPECT(panels.selectPlayerBuild("opengles3"));

    panels.setPlayerBuilds({PlayerBuild{"software", "/builds/cna-player-software"}});
    CNA_STUDIO_EXPECT(panels.playerBuildOverride().empty());
}

CNA_STUDIO_TEST(TheChooserIsNotDrawnWhenThereIsNothingToChooseBetween)
{
    // One installed build is the usual case and offering a choice of one is offering a control
    // that cannot do anything.
    std::vector<PlayerBuild> one{PlayerBuild{"software", "/builds/cna-player-software"}};

    // Counted as *controls* rather than as pixels: the question is whether there is something to
    // press, and a count of glyphs would also move if a label elsewhere changed.
    Fixture fixture;
    fixture.view.builds = &one;
    fixture.view.playBackend = "software";
    fixture.settle();

    const std::size_t withOne = fixture.frame.interactionCount();

    std::vector<PlayerBuild> two{PlayerBuild{"software", "/builds/cna-player-software"},
                                 PlayerBuild{"opengles3", "/builds/cna-player-opengles3"}};
    fixture.view.builds = &two;
    fixture.settle();

    // Project, and one per build.
    CNA_STUDIO_EXPECT_EQ(fixture.frame.interactionCount(), withOne + 3);
}

CNA_STUDIO_TEST(PressingARendererReportsTheChoiceRatherThanMakingItItself)
{
    // The panel is a snapshot reader: it says what was pressed and the host decides. A panel that
    // reached into the run to change it would be a panel no test could drive.
    std::vector<PlayerBuild> builds{PlayerBuild{"software", "/builds/cna-player-software"},
                                    PlayerBuild{"opengles3", "/builds/cna-player-opengles3"}};

    Fixture fixture;
    fixture.view.builds = &builds;
    fixture.view.playBackend = "software";
    fixture.settle();

    // The strip sits under the toolbar; its buttons are laid out from the left after the label.
    const float rowY = 20.0f + 30.0f;
    for (float x = 40.0f; x < kWidth && !fixture.last.playBackendChosen.has_value(); x += 8.0f)
    {
        fixture.click(x, rowY);
    }

    CNA_STUDIO_EXPECT(fixture.last.playBackendChosen.has_value());
}
