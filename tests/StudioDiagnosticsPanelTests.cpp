// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDiagnosticsPanelTests.cpp
 * @brief What this Studio is running on (plan.md STUDIO-07011).
 *
 * The ImGui panel reaches through the application object into a live graphics device, which is why
 * it could never be tested and could not exist in the CNA-free build at all. This one reads a
 * snapshot the host fills in — so the interesting cases are the ones a device would have made
 * untestable: no device at all, a renderer that fails the host contract, and no player builds.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 900.0f;
    constexpr float kHeight = 400.0f;

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

    /** @brief A host evaluation with one met and one unmet requirement. */
    StudioHostEvaluation someEvaluation(bool canHost)
    {
        StudioHostEvaluation evaluation;
        evaluation.canHostStudio = canHost;
        evaluation.rendererName = "opengles3";
        evaluation.platformName = "sdl3";
        evaluation.modernApiAvailable = true;

        StudioRequirementOutcome met;
        met.subject = "TextureFormatRgba8";
        met.severity = StudioRequirementSeverity::Required;
        met.status = StudioRequirementStatus::Satisfied;
        evaluation.outcomes.push_back(std::move(met));

        StudioRequirementOutcome unmet;
        unmet.subject = "ScissorTest";
        unmet.severity = StudioRequirementSeverity::Required;
        unmet.status = StudioRequirementStatus::Unsupported;
        unmet.detail = "the renderer says no";
        evaluation.outcomes.push_back(std::move(unmet));

        return evaluation;
    }

    struct Fixture
    {
        StudioDiagnosticsInfo info;
        StudioTreeState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioDiagnosticsResult last;

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioDiagnosticsResult drawn =
                    studioDiagnosticsPanel(f, body, info, state);
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
            return studioDiagnosticsRows(info, state);
        }

        /**
         * @brief The row reading @p label, by value.
         *
         * By value deliberately: the rows are built fresh on every call, so a pointer into them is
         * a pointer into a temporary that has already gone — which is exactly the dangling read
         * the sanitizer caught the first version of this helper making.
         */
        [[nodiscard]] std::optional<StudioTreeRow> rowLabelled(std::string_view label) const
        {
            for (const StudioTreeRow& row : rows())
            {
                if (row.label == label) { return row; }
            }
            return std::nullopt;
        }
    };

    /** @brief Whether @p text contains @p needle. */
    bool contains(const std::string& text, std::string_view needle)
    {
        return text.find(needle) != std::string::npos;
    }
}

CNA_STUDIO_TEST(WithNoGraphicsDeviceThePanelSaysUnknownRatherThanNothing)
{
    // A blank cell reads as a panel that failed to draw. "unknown" reads as the answer, which on a
    // build with no device it is.
    Fixture fixture;
    fixture.settle();

    const std::vector<StudioTreeRow> rows = fixture.rows();
    CNA_STUDIO_EXPECT(!rows.empty());

    const std::optional<StudioTreeRow> renderer = fixture.rowLabelled("Renderer");
    CNA_STUDIO_EXPECT(renderer.has_value());
    CNA_STUDIO_EXPECT_EQ(renderer->detail, std::string{"unknown"});

    // And the host contract says it was not asked, rather than showing an empty group that reads
    // as "this renderer meets nothing".
    bool saidNotEvaluated = false;
    for (const StudioTreeRow& row : rows)
    {
        if (row.detail == "not evaluated") { saidNotEvaluated = true; }
    }
    CNA_STUDIO_EXPECT(saidNotEvaluated);
}

CNA_STUDIO_TEST(TheHostContractsOutcomesAreShownWithTheirSeverity)
{
    Fixture fixture;
    fixture.info.host = someEvaluation(/*canHost=*/false);
    fixture.info.renderer = "opengles3";
    fixture.settle();

    const std::optional<StudioTreeRow> met = fixture.rowLabelled("TextureFormatRgba8");
    const std::optional<StudioTreeRow> unmet = fixture.rowLabelled("ScissorTest");

    CNA_STUDIO_EXPECT(met.has_value());
    CNA_STUDIO_EXPECT(unmet.has_value());
    CNA_STUDIO_EXPECT(met->detailRole == StudioColorRole::Success);
    CNA_STUDIO_EXPECT(unmet->detailRole == StudioColorRole::Error);
}

CNA_STUDIO_TEST(NoPlayerBuildsIsSaidRatherThanLeftBlank)
{
    // "Run this on Vulkan" means "launch cna-player-vulkan", and a user whose Play menu is empty
    // needs to see why.
    Fixture fixture;
    fixture.settle();

    CNA_STUDIO_EXPECT(fixture.rowLabelled("None beside this executable").has_value());

    fixture.info.players.push_back(PlayerBuild{"software", "/opt/cna/cna-player-software"});
    CNA_STUDIO_EXPECT(!fixture.rowLabelled("None beside this executable").has_value());
    CNA_STUDIO_EXPECT(fixture.rowLabelled("software").has_value());
}

CNA_STUDIO_TEST(EveryRendererStudioKnowsIsListedWithItsHostTier)
{
    Fixture fixture;
    fixture.settle();

    std::size_t listed = 0;
    bool sawStudioHost = false;
    for (const StudioTreeRow& row : fixture.rows())
    {
        if (row.id.rfind("renderer:", 0) != 0) { continue; }
        ++listed;
        if (row.detail == "studio") { sawStudioHost = true; }
    }

    CNA_STUDIO_EXPECT_EQ(listed, getKnownRenderers().size());
    CNA_STUDIO_EXPECT(sawStudioHost);
}

CNA_STUDIO_TEST(TheReportIsAlsoTextBecauseItExistsToBePasted)
{
    // A panel somebody has to transcribe by hand is a panel nobody puts in a bug report.
    Fixture fixture;
    fixture.info.renderer = "vulkan";
    fixture.info.platform = "sdl3";
    fixture.info.modernApi = true;
    fixture.info.frames = 120;
    fixture.info.drawCalls = 11;
    fixture.info.host = someEvaluation(/*canHost=*/false);
    fixture.info.players.push_back(PlayerBuild{"software", "/opt/cna/cna-player-software"});

    const std::string text = studioDiagnosticsText(fixture.info);

    CNA_STUDIO_EXPECT(contains(text, "Renderer: vulkan"));
    CNA_STUDIO_EXPECT(contains(text, "Platform: sdl3"));
    CNA_STUDIO_EXPECT(contains(text, "Modern graphics API: yes"));
    CNA_STUDIO_EXPECT(contains(text, "Frames: 120"));
    CNA_STUDIO_EXPECT(contains(text, "NOT met"));
    // The renderer's own words survive into the report, which is the part that answers "why".
    CNA_STUDIO_EXPECT(contains(text, "the renderer says no"));
    CNA_STUDIO_EXPECT(contains(text, "cna-player-software"));
}

CNA_STUDIO_TEST(CopyReportHandsBackTheSameTextTheReportBuilds)
{
    Fixture fixture;
    fixture.info.renderer = "software";
    fixture.settle();

    const float toolbar =
        static_cast<float>(fixture.frame.theme().metric(StudioMetric::ControlHeight))
        + static_cast<float>(fixture.frame.theme().metric(StudioMetric::SpacingSmall)) * 2.0f;
    fixture.click(40.0f, toolbar * 0.5f);

    CNA_STUDIO_EXPECT(fixture.last.copyRequested);
    CNA_STUDIO_EXPECT_EQ(fixture.last.copyText, studioDiagnosticsText(fixture.info));
}

CNA_STUDIO_TEST(TheReportSurvivesRepeatedFramesWithoutPhaseViolations)
{
    Fixture fixture;
    fixture.info.host = someEvaluation(/*canHost=*/true);
    for (int i = 0; i < 4; ++i)
    {
        fixture.settle();
        CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
    }
    CNA_STUDIO_EXPECT(fixture.last.rowsDrawn > 0);
}

CNA_STUDIO_TEST(TheAtlasIsReportedAndADroppedGlyphIsSaidAsMissingText)
{
    // The atlas has counted the glyphs it could not fit since it was written, and until
    // STUDIO-04018 nothing displayed the count -- so "the text stops partway down this panel"
    // was a mystery to exactly the person looking at the panel that answers mysteries.
    Fixture fixture;
    fixture.info.atlasSize = 2048;
    fixture.info.atlasOccupancy = 0.372f;
    fixture.info.atlasGrowths = 1;
    fixture.settle();

    const std::string text = studioDiagnosticsText(fixture.info);
    CNA_STUDIO_EXPECT(contains(text, "2048x2048"));
    CNA_STUDIO_EXPECT(contains(text, "grown 1x"));
    // One decimal, because a Latin UI at 1x uses a fraction of a percent and "0% full" reads as
    // an atlas that is not working rather than one that is bigger than the text in front of it.
    CNA_STUDIO_EXPECT(contains(text, "37.2% full"));
    CNA_STUDIO_EXPECT(!contains(text, "DROPPED"));

    fixture.info.atlasDroppedGlyphs = 12;
    const std::string lost = studioDiagnosticsText(fixture.info);
    CNA_STUDIO_EXPECT(contains(lost, "12 glyphs DROPPED (text is missing)"));

    // And in the tree, which is what a user actually reads -- the report is for pasting into a
    // bug. Said in the error colour, because it is a fault rather than a statistic.
    const std::vector<StudioTreeRow> rows = studioDiagnosticsRows(fixture.info, fixture.state);
    bool said = false;
    for (const StudioTreeRow& row : rows)
    {
        if (row.label.find("Glyphs dropped") == std::string::npos) { continue; }
        said = true;
        CNA_STUDIO_EXPECT_EQ(row.detail, std::string{"12"});
        CNA_STUDIO_EXPECT(row.detailRole == StudioColorRole::Error);
    }
    CNA_STUDIO_EXPECT(said);
}

CNA_STUDIO_TEST(AnAtlasNoBuildHasIsNotReportedAsAZeroSizedOne)
{
    // Zero is the "nobody filled this in" value, and a row saying "0x0, 0% full" is worse than no
    // row: it says the atlas is broken rather than that nothing asked it.
    Fixture fixture;
    fixture.info.atlasSize = 0;
    fixture.settle();

    CNA_STUDIO_EXPECT(!contains(studioDiagnosticsText(fixture.info), "Glyph atlas"));
    for (const StudioTreeRow& row : studioDiagnosticsRows(fixture.info, fixture.state))
    {
        CNA_STUDIO_EXPECT(row.label.find("Glyph atlas") == std::string::npos);
    }
}
