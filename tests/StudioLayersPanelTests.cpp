// SPDX-License-Identifier: MS-PL
/**
 * @file StudioLayersPanelTests.cpp
 * @brief The project's render layers, and what is on each (plan.md STUDIO-07024).
 *
 * The question a layer list exists to answer is the one the outliner cannot: the outliner is
 * ordered by the hierarchy, and a layer cuts across it. So the cases here are about membership and
 * order rather than about drawing.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/ShellPanels/StudioLayersPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 320.0f;
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

    /** @brief A project with three layers and entities spread across them. */
    struct Fixture
    {
        StudioContext context;
        StudioTreeState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioLayersResult last;

        Fixture()
        {
            Project& project = context.getProject();
            project.setLayers({"Background", "Default", "Foreground"});
            applyProjectLayers(context.getComponentRegistry(), project.getLayers());
        }

        Uuid add(const std::string& name, const std::string& layer)
        {
            StudioEntity entity{Uuid::generate(), name};
            entity.addComponent(StudioComponent{BuiltinComponentIds::kTransform});

            if (!layer.empty())
            {
                StudioComponent component{BuiltinComponentIds::kLayer};
                component.setProperty("layer", PropertyValue{PropertyValue::EnumValue{layer}});
                entity.addComponent(std::move(component));
            }
            return context.getScene().addEntity(std::move(entity));
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioLayersResult drawn = studioLayersPanel(f, body, context, state);
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

        [[nodiscard]] float rowCentre(std::size_t index) const
        {
            const auto rowHeight = static_cast<float>(frame.theme().metric(StudioMetric::RowHeight));
            return (static_cast<float>(index) + 0.5f) * rowHeight;
        }
    };
}

CNA_STUDIO_TEST(LayersAreListedInDrawOrderRatherThanSorted)
{
    // Index 0 draws first, so the order *is* the meaning. Sorting them by name would read tidier
    // and say nothing.
    Fixture fixture;
    fixture.settle();

    const std::vector<StudioTreeRow> rows = studioLayerRows(fixture.context, fixture.state);
    CNA_STUDIO_EXPECT_EQ(rows.size(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(rows[0].label, std::string{"Background"});
    CNA_STUDIO_EXPECT_EQ(rows[1].label, std::string{"Default"});
    CNA_STUDIO_EXPECT_EQ(rows[2].label, std::string{"Foreground"});
    CNA_STUDIO_EXPECT_EQ(fixture.last.layerCount, std::size_t{3});
}

CNA_STUDIO_TEST(AnEntityWithNoLayerComponentIsOnTheFirstLayer)
{
    // Which is what the runtime does with one. Reporting it as belonging to nothing would hide
    // every entity in a project that has never touched layers.
    Fixture fixture;
    const Uuid plain = fixture.add("Plain", {});
    fixture.add("Front", "Foreground");

    const std::vector<Uuid> background = studioEntitiesOnLayer(fixture.context, "Background");
    CNA_STUDIO_EXPECT_EQ(background.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(background.front() == plain);

    CNA_STUDIO_EXPECT_EQ(studioEntitiesOnLayer(fixture.context, "Foreground").size(),
                         std::size_t{1});
    CNA_STUDIO_EXPECT(studioEntitiesOnLayer(fixture.context, "Default").empty());
}

CNA_STUDIO_TEST(AnEmptyLayerIsShownDimmedRatherThanHidden)
{
    // Hiding it would make a user wonder where the layer they just added went.
    Fixture fixture;
    fixture.add("Front", "Foreground");
    fixture.settle();

    const std::vector<StudioTreeRow> rows = studioLayerRows(fixture.context, fixture.state);
    CNA_STUDIO_EXPECT_EQ(rows[1].label, std::string{"Default"});
    CNA_STUDIO_EXPECT_EQ(rows[1].detail, std::string{"empty"});
    CNA_STUDIO_EXPECT(rows[1].muted);
    CNA_STUDIO_EXPECT(!rows[1].hasChildren);
}

CNA_STUDIO_TEST(ALayersEntitiesAreListedUnderIt)
{
    Fixture fixture;
    fixture.add("A", "Foreground");
    fixture.add("B", "Foreground");
    fixture.settle();

    const std::vector<StudioTreeRow> rows = studioLayerRows(fixture.context, fixture.state);

    std::size_t children = 0;
    for (const StudioTreeRow& row : rows) { if (row.depth == 1) { ++children; } }
    CNA_STUDIO_EXPECT_EQ(children, std::size_t{2});
}

CNA_STUDIO_TEST(ClickingALayerSelectsEverythingOnIt)
{
    // How a user turns "the background is wrong" into something they can edit.
    Fixture fixture;
    fixture.add("A", "Foreground");
    fixture.add("B", "Foreground");
    fixture.add("C", "Background");
    fixture.settle();

    const std::vector<StudioTreeRow> rows = studioLayerRows(fixture.context, fixture.state);
    int foreground = -1;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].id == "layer:Foreground") { foreground = static_cast<int>(i); }
    }
    CNA_STUDIO_EXPECT(foreground >= 0);

    fixture.click(kWidth * 0.5f, fixture.rowCentre(static_cast<std::size_t>(foreground)));

    CNA_STUDIO_EXPECT_EQ(fixture.last.clickedLayer, std::string{"Foreground"});
    CNA_STUDIO_EXPECT_EQ(fixture.last.selectEntities.size(), std::size_t{2});
}

CNA_STUDIO_TEST(ClickingAnEntityUnderALayerSelectsJustThatOne)
{
    Fixture fixture;
    const Uuid first = fixture.add("A", "Foreground");
    fixture.add("B", "Foreground");
    fixture.settle();

    const std::vector<StudioTreeRow> rows = studioLayerRows(fixture.context, fixture.state);
    int child = -1;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].depth == 1 && child < 0) { child = static_cast<int>(i); }
    }
    CNA_STUDIO_EXPECT(child >= 0);

    fixture.click(kWidth * 0.5f, fixture.rowCentre(static_cast<std::size_t>(child)));

    CNA_STUDIO_EXPECT_EQ(fixture.last.selectEntities.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(fixture.last.selectEntities.front() == first);
    CNA_STUDIO_EXPECT(fixture.last.clickedLayer.empty());
}

CNA_STUDIO_TEST(WithNoProjectThePanelSaysSoRatherThanDrawingNothing)
{
    StudioContext context;
    StudioTreeState state;
    StudioFrame frame{StudioTheme::dark()};

    StudioLayersResult result;
    runStudioFrame(frame, at(10.0f, 10.0f), [&](StudioFrame& f) {
        const StudioLayersResult drawn =
            studioLayersPanel(f, UiRect{0.0f, 0.0f, kWidth, kHeight}, context, state);
        if (f.isInputPass()) { result = drawn; }
    });

    // A project always carries at least the default layer, so "no layers" is the state of having
    // no project at all -- and the panel says which.
    CNA_STUDIO_EXPECT_EQ(frame.phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT(result.selectEntities.empty());
}
