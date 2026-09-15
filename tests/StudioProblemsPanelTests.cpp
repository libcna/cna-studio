// SPDX-License-Identifier: MS-PL
/**
 * @file StudioProblemsPanelTests.cpp
 * @brief The Problems panel: two reports, one list (plan.md STUDIO-07012).
 *
 * The good state of a report is emptiness, which is exactly what makes it easy to ship broken: a
 * panel that found nothing and a panel that never ran look identical. So the cases here assert on
 * what the report *says* as much as on what it draws, and the empty case is a case rather than an
 * absence of one.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 900.0f;
    constexpr float kHeight = 420.0f;

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

    /** @brief Adds a component of @p typeId with its declared defaults filled in. */
    StudioComponent& addComponent(StudioEntity& entity, const ComponentRegistry& registry,
                                  const std::string& typeId)
    {
        StudioComponent component{typeId};
        if (const ComponentDescriptor* descriptor = registry.find(typeId))
        {
            component.applyDefaults(*descriptor);
        }
        return entity.addComponent(std::move(component));
    }

    /** @brief A context whose scene can be given problems on demand. */
    struct Fixture
    {
        StudioContext context;
        StudioProblemsState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioProblemsResult last;

        /** @brief Adds an entity whose sprite names an asset nothing imported. */
        Uuid addBrokenReference(const std::string& name, const Uuid& assetId)
        {
            StudioEntity entity{Uuid::generate(), name};
            addComponent(entity, context.getComponentRegistry(),
                         BuiltinComponentIds::kTransform);
            StudioComponent& sprite = addComponent(entity, context.getComponentRegistry(),
                                                   BuiltinComponentIds::kSpriteRenderer);
            sprite.setProperty("texture",
                               PropertyValue{PropertyValue::AssetReference{assetId}});
            return context.getScene().addEntity(std::move(entity));
        }

        /** @brief Adds a camera, which is an error once there are two of them. */
        Uuid addCamera(const std::string& name)
        {
            StudioEntity entity{Uuid::generate(), name};
            addComponent(entity, context.getComponentRegistry(),
                         BuiltinComponentIds::kTransform);
            addComponent(entity, context.getComponentRegistry(), BuiltinComponentIds::kCamera);
            return context.getScene().addEntity(std::move(entity));
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioProblemsResult drawn =
                    studioProblemsPanel(f, body, context, state);
                if (f.isInputPass()) { last = drawn; }
            });
        }

        void settle() { run(at(kWidth - 10.0f, kHeight - 10.0f)); }

        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        /** @brief The report's rows, without drawing anything. */
        [[nodiscard]] std::vector<StudioTreeRow> rows()
        {
            StudioProblemsResult ignored;
            return studioProblemRows(context, state, ignored);
        }

        /** @brief Index of the row whose id is @p id, or -1. */
        [[nodiscard]] int rowIndex(std::string_view id)
        {
            const std::vector<StudioTreeRow> all = rows();
            for (std::size_t i = 0; i < all.size(); ++i)
            {
                if (all[i].id == id) { return static_cast<int>(i); }
            }
            return -1;
        }
    };
}

CNA_STUDIO_TEST(ACleanSceneSaysSoRatherThanShowingNothing)
{
    // A panel that found nothing and a panel that never ran look identical, and the second is what
    // a blank rectangle reads as.
    Fixture fixture;
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.brokenReferences, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(fixture.last.errors, std::size_t{0});

    const std::vector<StudioTreeRow> rows = fixture.rows();
    CNA_STUDIO_EXPECT_EQ(rows.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(rows[0].detail, std::string{"none"});
    CNA_STUDIO_EXPECT_EQ(rows[1].detail, std::string{"none"});
    CNA_STUDIO_EXPECT(!rows[0].hasChildren);
}

CNA_STUDIO_TEST(ABrokenReferenceIsReportedWithWhatRefersToIt)
{
    Fixture fixture;
    const Uuid missing = Uuid::generate();
    fixture.addBrokenReference("Player", missing);
    fixture.addBrokenReference("Crate", missing);
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.brokenReferences, std::size_t{2});

    const std::string assetRow = std::string{kStudioBrokenAssetRowPrefix} + missing.toString();
    CNA_STUDIO_EXPECT(fixture.rowIndex(assetRow) >= 0);

    // Two entities, one missing asset: one group row with both users under it, rather than two
    // rows saying the same thing about the same asset.
    const std::vector<StudioTreeRow> rows = fixture.rows();
    std::size_t users = 0;
    for (const StudioTreeRow& row : rows) { if (row.depth == 2) { ++users; } }
    CNA_STUDIO_EXPECT_EQ(users, std::size_t{2});
}

CNA_STUDIO_TEST(SceneIssuesAreReportedWithTheirSeverityInColour)
{
    // "error" and "warning" have to be told apart at a glance: a list that says which in grey
    // words is a list the eye has to read line by line.
    Fixture fixture;
    fixture.addCamera("Main Camera");
    fixture.addCamera("Cutscene Camera");
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.errors, std::size_t{2});

    bool sawError = false;
    for (const StudioTreeRow& row : fixture.rows())
    {
        if (row.id.rfind("issue:", 0) != 0) { continue; }
        if (row.detail == std::string{"error"})
        {
            sawError = true;
            CNA_STUDIO_EXPECT(row.detailRole == StudioColorRole::Error);
        }
    }
    CNA_STUDIO_EXPECT(sawError);
}

CNA_STUDIO_TEST(ClickingAnIssueRowAsksToSelectTheEntityAtFault)
{
    // The shortest path from "something is wrong" to the thing that is wrong, which is what the
    // report exists for.
    Fixture fixture;
    const Uuid first = fixture.addCamera("Main Camera");
    const Uuid second = fixture.addCamera("Cutscene Camera");
    fixture.settle();

    int issueRow = -1;
    const std::vector<StudioTreeRow> rows = fixture.rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].id.rfind("issue:", 0) == 0 && issueRow < 0) { issueRow = static_cast<int>(i); }
    }
    CNA_STUDIO_EXPECT(issueRow >= 0);

    const float rowHeight = static_cast<float>(fixture.frame.theme().metric(StudioMetric::RowHeight));
    const float toolbar = static_cast<float>(fixture.frame.theme().metric(StudioMetric::ControlHeight))
                        + static_cast<float>(fixture.frame.theme().metric(StudioMetric::SpacingSmall)) * 2.0f;
    fixture.click(200.0f, toolbar + (static_cast<float>(issueRow) + 0.5f) * rowHeight);

    // One of the two cameras, because the rule reports one issue per offending entity so that
    // either row leads somewhere real.
    CNA_STUDIO_EXPECT(fixture.last.selectEntity == first || fixture.last.selectEntity == second);
}

CNA_STUDIO_TEST(ClearReferenceIsRefusedUntilABrokenAssetIsSelected)
{
    // Drawn disabled rather than drawn enabled and then doing nothing, which is the difference
    // between a tool that explains itself and one that appears to ignore a click.
    Fixture fixture;
    fixture.addBrokenReference("Player", Uuid::generate());
    fixture.settle();

    CNA_STUDIO_EXPECT(fixture.state.selectedRow.empty());

    const float toolbar = static_cast<float>(fixture.frame.theme().metric(StudioMetric::ControlHeight))
                        + static_cast<float>(fixture.frame.theme().metric(StudioMetric::SpacingSmall)) * 2.0f;
    fixture.click(40.0f, toolbar * 0.5f);
    CNA_STUDIO_EXPECT(!fixture.last.clearAsset.isValid());
}

CNA_STUDIO_TEST(ClearingASelectedBrokenAssetAsksForItById)
{
    Fixture fixture;
    const Uuid missing = Uuid::generate();
    fixture.addBrokenReference("Player", missing);

    // Selected through the panel's own state, because "does the toolbar act on the selection" is
    // the thing under test, not "does clicking a row select it" -- which has its own case.
    fixture.state.selectedRow = std::string{kStudioBrokenAssetRowPrefix} + missing.toString();
    fixture.settle();

    const float toolbar = static_cast<float>(fixture.frame.theme().metric(StudioMetric::ControlHeight))
                        + static_cast<float>(fixture.frame.theme().metric(StudioMetric::SpacingSmall)) * 2.0f;
    fixture.click(40.0f, toolbar * 0.5f);

    CNA_STUDIO_EXPECT(fixture.last.clearAsset == missing);
}

CNA_STUDIO_TEST(ClickingARowSelectsItSoTheToolbarCanActOnIt)
{
    Fixture fixture;
    const Uuid missing = Uuid::generate();
    fixture.addBrokenReference("Player", missing);
    fixture.settle();

    const std::string assetRow = std::string{kStudioBrokenAssetRowPrefix} + missing.toString();
    const int index = fixture.rowIndex(assetRow);
    CNA_STUDIO_EXPECT(index >= 0);

    const float rowHeight = static_cast<float>(fixture.frame.theme().metric(StudioMetric::RowHeight));
    const float toolbar = static_cast<float>(fixture.frame.theme().metric(StudioMetric::ControlHeight))
                        + static_cast<float>(fixture.frame.theme().metric(StudioMetric::SpacingSmall)) * 2.0f;
    fixture.click(200.0f, toolbar + (static_cast<float>(index) + 0.5f) * rowHeight);

    CNA_STUDIO_EXPECT_EQ(fixture.state.selectedRow, assetRow);
}

CNA_STUDIO_TEST(TheReportSurvivesRepeatedFramesWithoutPhaseViolations)
{
    Fixture fixture;
    fixture.addBrokenReference("Player", Uuid::generate());
    fixture.addCamera("A");
    fixture.addCamera("B");

    for (int i = 0; i < 4; ++i)
    {
        fixture.settle();
        CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
    }
    CNA_STUDIO_EXPECT(fixture.last.rowsDrawn > 0);
}

CNA_STUDIO_TEST(DroppingAnAssetOnABrokenRowAsksToRelinkItRatherThanClearIt)
{
    // The repair path the ImGui panel had, and the reason drag and drop was worth building: it is
    // the shortest route from "this is broken" to "this is fixed".
    Fixture fixture;
    const Uuid missing = Uuid::generate();
    fixture.addBrokenReference("Player", missing);
    fixture.settle();

    const std::string assetRow = std::string{kStudioBrokenAssetRowPrefix} + missing.toString();
    CNA_STUDIO_EXPECT(fixture.rowIndex(assetRow) >= 0);

    // The row declares itself a target for assets, which is what the Content Browser drags.
    const std::vector<StudioTreeRow> rows = fixture.rows();
    const StudioTreeRow& row = rows[static_cast<std::size_t>(fixture.rowIndex(assetRow))];
    CNA_STUDIO_EXPECT_EQ(row.dropType, std::string{kStudioAssetDragType});

    // Driven through the frame's own drag, because "does a drop reach the panel" is the thing
    // under test rather than "does the Content Browser start a drag", which has its own case.
    const Uuid replacement = Uuid::generate();
    StudioFrame::StudioDragPayload payload;
    payload.type = std::string{kStudioAssetDragType};
    payload.value = replacement.toString();
    payload.label = "replacement.png";

    const float rowHeight = static_cast<float>(fixture.frame.theme().metric(StudioMetric::RowHeight));
    const float toolbar = static_cast<float>(fixture.frame.theme().metric(StudioMetric::ControlHeight))
                        + static_cast<float>(fixture.frame.theme().metric(StudioMetric::SpacingSmall)) * 2.0f;
    const float y = toolbar
                  + (static_cast<float>(fixture.rowIndex(assetRow)) + 0.5f) * rowHeight;

    fixture.run(at(200.0f, y, /*leftDown=*/true));
    CNA_STUDIO_EXPECT(fixture.frame.beginDrag(fixture.frame.ids().make("source"), payload));

    fixture.run(at(200.0f, y, /*leftDown=*/true));
    fixture.run(at(200.0f, y));

    CNA_STUDIO_EXPECT(fixture.last.clearAsset == missing);
    CNA_STUDIO_EXPECT(fixture.last.relinkTo == replacement);
}
