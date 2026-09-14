// SPDX-License-Identifier: MS-PL
/**
 * @file StudioHighDpiTests.cpp
 * @brief The shell at 100, 125, 150, 175 and 200 per cent, checked rather than looked at.
 *
 * `plan.md` STUDIO-03028.
 *
 * DPI scaling is where a UI that was only ever developed at 100% falls apart, and it does so in
 * ways a screenshot at one scale cannot show: a hairline that rounds to zero and disappears, a
 * panel edge at a fractional pixel that puts a faint seam down the window, a minimum size that was
 * a constant and so is half as useful at 200%, and text scaled from one master size instead of
 * rasterised at the size it is drawn at. Each of those has a test here.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioShellLayout.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief The DPI scales Studio supports as a matter of policy. */
    constexpr float kScales[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};

    /** @brief Whether a value is a whole number of pixels. */
    bool isWholePixel(float value)
    {
        return std::abs(value - std::round(value)) < 0.001f;
    }

    /** @brief Names a scale for a failure message. */
    std::string percent(float scale)
    {
        return std::to_string(static_cast<int>(std::lround(scale * 100.0f))) + "%";
    }

    /** @brief Runs one shell frame at a scale and returns it, kept alive for its draw data. */
    std::shared_ptr<StudioShell> shellAt(float scale, float width = 1600.0f, float height = 900.0f)
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);

        auto shell = std::make_shared<StudioShell>(theme);
        UiInputState input;
        input.displayWidth = width;
        input.displayHeight = height;
        input.mouseInWindow = false;
        shell->renderFrame(input);
        return shell;
    }
}

// ------------------------------------------------------------------------------------------------
// Metrics
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryMetricScalesAndNoHairlineEverDisappears)
{
    // A one-pixel separator multiplied by 0.75 rounds to zero and the rule vanishes. Studio does
    // not support scales below 100%, but the clamp is what makes that a policy rather than a
    // rendering accident -- and the same clamp is why a border stays visible at every scale.
    for (const float scale : {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 3.0f})
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);

        for (const StudioMetric metric : {StudioMetric::BorderWidth, StudioMetric::SeparatorThickness,
                                          StudioMetric::FocusRingWidth})
        {
            if (theme.metric(metric) < 1)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    std::string{studioMetricName(metric)} + " rounds to "
                    + std::to_string(theme.metric(metric)) + " at " + percent(scale));
            }
            CNA_STUDIO_EXPECT(theme.metric(metric) >= 1);
        }
    }
}

CNA_STUDIO_TEST(ChromeAndControlsGrowWithTheScale)
{
    StudioTheme at100 = StudioTheme::dark();
    for (const float scale : kScales)
    {
        StudioTheme scaled = StudioTheme::dark();
        scaled.setScale(scale);

        for (const StudioMetric metric : {StudioMetric::MenuBarHeight, StudioMetric::ToolbarHeight,
                                          StudioMetric::StatusBarHeight, StudioMetric::TabHeight,
                                          StudioMetric::ControlHeight, StudioMetric::RowHeight,
                                          StudioMetric::IconSize})
        {
            const float expected = static_cast<float>(at100.metric(metric)) * scale;
            CNA_STUDIO_EXPECT(std::abs(static_cast<float>(scaled.metric(metric)) - expected) <= 1.0f);
        }
        // And the logical value never changes: scaling is applied on read, in one place, so no
        // caller can forget to apply it and none can apply it twice.
        CNA_STUDIO_EXPECT_EQ(scaled.logicalMetric(StudioMetric::ControlHeight),
                             at100.logicalMetric(StudioMetric::ControlHeight));
    }
}

CNA_STUDIO_TEST(AMinimumHitTargetStaysAMinimumHitTarget)
{
    // A constant expressed in logical pixels is half as large in physical ones at 200%, which is
    // how a control that was comfortable becomes one people miss.
    StudioTheme at100 = StudioTheme::dark();
    StudioTheme at200 = StudioTheme::dark();
    at200.setScale(2.0f);

    CNA_STUDIO_EXPECT_EQ(at200.metric(StudioMetric::MinimumHitTarget),
                         at100.metric(StudioMetric::MinimumHitTarget) * 2);
}

// ------------------------------------------------------------------------------------------------
// Layout
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheShellChromeIsWellFormedAtEveryScale)
{
    for (const float scale : kScales)
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);

        const StudioShellLayout layout = computeStudioShellLayout(1600.0f, 900.0f, theme);
        if (!layout.isWellFormed())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "shell chrome is malformed at " + percent(scale));
        }
        CNA_STUDIO_EXPECT(layout.isWellFormed());
        CNA_STUDIO_EXPECT(!layout.dockArea.isEmpty());
    }
}

CNA_STUDIO_TEST(EveryDockEdgeLandsOnAWholePixelAtEveryScale)
{
    // The seam test. A panel edge at x = 123.4 leaves a partially covered column between two
    // panels: faint at 100%, and at 150% it lands differently on every splitter in the window.
    for (const float scale : kScales)
    {
        const std::shared_ptr<StudioShell> shell = shellAt(scale);
        for (const StudioDockNodeId leaf : shell->dockTree().leaves())
        {
            const UiRect& bounds = shell->dockTree().node(leaf).bounds;
            if (!isWholePixel(bounds.left()) || !isWholePixel(bounds.top())
                || !isWholePixel(bounds.width) || !isWholePixel(bounds.height))
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "dock edge is not on a whole pixel at " + percent(scale));
            }
            CNA_STUDIO_EXPECT(isWholePixel(bounds.left()));
            CNA_STUDIO_EXPECT(isWholePixel(bounds.width));
        }
    }
}

CNA_STUDIO_TEST(PanelsStayExactlyAdjacentAcrossEverySplitter)
{
    // Snapping each panel independently would round two neighbours apart, leaving a one-pixel gap
    // of app background between them -- or together, overlapping by one. The split is snapped, not
    // the panels, precisely so this holds.
    for (const float scale : kScales)
    {
        const std::shared_ptr<StudioShell> shell = shellAt(scale);
        const StudioDockTree& dock = shell->dockTree();

        for (const StudioDockNodeId id : dock.splits())
        {
            const StudioDockNode& node = dock.node(id);
            const UiRect& first = dock.node(node.first).bounds;
            const UiRect& second = dock.node(node.second).bounds;

            if (node.orientation == StudioDockOrientation::Horizontal)
            {
                CNA_STUDIO_EXPECT_EQ(node.splitter.left(), first.right());
                CNA_STUDIO_EXPECT_EQ(node.splitter.right(), second.left());
            }
            else
            {
                CNA_STUDIO_EXPECT_EQ(node.splitter.top(), first.bottom());
                CNA_STUDIO_EXPECT_EQ(node.splitter.bottom(), second.top());
            }
        }
    }
}

CNA_STUDIO_TEST(ADockAreaThatFitsAtOneHundredPerCentStillFitsAtTwoHundred)
{
    // Everything doubles at 200%, so a window that comfortably held four panels holds fewer. What
    // must not happen is a panel resolving to nothing, or the layout inverting.
    for (const float scale : kScales)
    {
        const std::shared_ptr<StudioShell> shell = shellAt(scale, 1280.0f, 720.0f);
        CNA_STUDIO_EXPECT(shell->dockTree().isWellFormed());

        for (const StudioDockNodeId leaf : shell->dockTree().leaves())
        {
            const UiRect& bounds = shell->dockTree().node(leaf).bounds;
            if (bounds.width <= 0.0f || bounds.height <= 0.0f)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "a panel collapsed to nothing at " + percent(scale));
            }
            CNA_STUDIO_EXPECT(bounds.width > 0.0f);
            CNA_STUDIO_EXPECT(bounds.height > 0.0f);
        }
    }
}

CNA_STUDIO_TEST(MenuGeometryIsOnWholePixelsAndInsideTheWindowAtEveryScale)
{
    for (const float scale : kScales)
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);
        StudioShell shell{theme};

        UiInputState input;
        input.displayWidth = 1600.0f;
        input.displayHeight = 900.0f;
        input.mouseInWindow = false;

        for (std::size_t i = 0; i < shell.menus().size(); ++i)
        {
            shell.setOpenMenu(static_cast<int>(i));
            shell.renderFrame(input);

            const UiRect title = shell.menuTitleBounds(i);
            CNA_STUDIO_EXPECT(isWholePixel(title.left()));
            CNA_STUDIO_EXPECT(isWholePixel(title.width));

            const UiRect popup = shell.menuPopupBounds();
            if (popup.isEmpty()) { continue; }
            CNA_STUDIO_EXPECT(isWholePixel(popup.left()));
            CNA_STUDIO_EXPECT(isWholePixel(popup.width));
            CNA_STUDIO_EXPECT(popup.right() <= input.displayWidth + 0.01f);
            CNA_STUDIO_EXPECT(popup.bottom() <= input.displayHeight + 0.01f);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Text
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TextIsRasterisedAtEveryScaleRatherThanScaledFromOne)
{
    StudioFontAtlas atlas;
    const StudioFontFace* previous = nullptr;

    for (const float scale : kScales)
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);

        const StudioFontFace& face = atlas.face(theme.font(StudioFontRole::Body));
        CNA_STUDIO_EXPECT(&face != previous);
        previous = &face;

        const float expected = static_cast<float>(theme.font(StudioFontRole::Body).sizePx);
        CNA_STUDIO_EXPECT(std::abs(face.sizePx() - std::round(expected)) < 0.01f);

        // Real ink at every scale: a face whose glyphs came out empty would measure correctly and
        // draw nothing.
        const StudioGlyph* glyph = face.glyph(U'W');
        CNA_STUDIO_EXPECT(glyph != nullptr && glyph->hasInk());
    }
    CNA_STUDIO_EXPECT_EQ(atlas.droppedGlyphs(), std::size_t{0});
}

CNA_STUDIO_TEST(LabelsGrowWithTheScaleRatherThanStayingPut)
{
    StudioFontAtlas atlas;

    StudioTheme at100 = StudioTheme::dark();
    StudioTheme at200 = StudioTheme::dark();
    at200.setScale(2.0f);

    const float small = atlas.measure(at100.font(StudioFontRole::Body), "Content Browser").width;
    const float large = atlas.measure(at200.font(StudioFontRole::Body), "Content Browser").width;

    CNA_STUDIO_EXPECT(large > small * 1.8f);
    CNA_STUDIO_EXPECT(large < small * 2.2f);
}

// ------------------------------------------------------------------------------------------------
// The whole frame
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheShellDrawsCleanlyAtEveryScale)
{
    for (const float scale : kScales)
    {
        const std::shared_ptr<StudioShell> shell = shellAt(scale);

        CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
        CNA_STUDIO_EXPECT_EQ(shell->frame().ids().collisionCount(), std::size_t{0});
        CNA_STUDIO_EXPECT(shell->drawData().getTotalVertexCount() > 0);

        std::size_t commands = 0;
        for (const UiDrawList& list : shell->drawData().lists) { commands += list.commands.size(); }
        if (commands >= 32)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the shell costs " + std::to_string(commands) + " draw calls at " + percent(scale));
        }
        CNA_STUDIO_EXPECT(commands < 32);

        for (const UiDrawList& list : shell->drawData().lists)
        {
            for (const UiVertex& vertex : list.vertices)
            {
                CNA_STUDIO_EXPECT(vertex.x >= -1.0f && vertex.x <= 1601.0f);
                CNA_STUDIO_EXPECT(vertex.y >= -1.0f && vertex.y <= 901.0f);
            }
        }
    }
}

CNA_STUDIO_TEST(InputLandsOnTheSameControlAtEveryScale)
{
    // The failure this catches is the worst kind: a UI that looks right at 150% and responds to
    // clicks a few pixels from where the control is drawn, because something scaled the drawing
    // and not the hit test.
    for (const float scale : kScales)
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);
        StudioShell shell{theme};

        UiInputState input;
        input.displayWidth = 1600.0f;
        input.displayHeight = 900.0f;
        input.mouseInWindow = true;

        const UiRect title = [&]() {
            input.mouseX = -1.0f;
            input.mouseY = -1.0f;
            shell.renderFrame(input);
            return shell.menuTitleBounds(0);
        }();
        CNA_STUDIO_EXPECT(!title.isEmpty());

        input.mouseX = title.centerX();
        input.mouseY = title.centerY();
        shell.renderFrame(input);
        input.setMouseDown(UiMouseButton::Left, true);
        shell.renderFrame(input);

        if (shell.openMenu() != 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "clicking the centre of the File title did not open it at " + percent(scale));
        }
        CNA_STUDIO_EXPECT_EQ(shell.openMenu(), 0);
    }
}
