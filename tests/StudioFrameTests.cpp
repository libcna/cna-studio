// SPDX-License-Identifier: MS-PL
/**
 * @file StudioFrameTests.cpp
 * @brief The frame lifecycle, cursor resolution and the widget interaction helpers.
 *
 * All headless. Whether a button fires once per click rather than twice, whether a toggle survives
 * being described in two passes, and whether a disabled control still blocks the pointer are
 * questions about sequencing — which is precisely what a screenshot cannot answer and what breaks
 * first when a UI grows.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTextMeasure.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief Builds an input snapshot with the pointer somewhere and optionally a button down. */
    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = 800.0f;
        input.displayHeight = 600.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    const UiRect kButton{10.0f, 10.0f, 120.0f, 28.0f};
    const WidgetId kA{0x51A};
    const WidgetId kB{0x51B};

    /** @brief Runs one whole frame over a description, returning how many times it activated. */
    int activations(StudioFrame& frame, const UiInputState& input,
                    const std::function<StudioWidgetResult(StudioFrame&)>& describe)
    {
        int count = 0;
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            if (describe(f).activated) { ++count; }
        });
        return count;
    }
}

// ------------------------------------------------------------------------------------------------
// Phase sequencing (STUDIO-03015)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AFrameWalksItsPhasesInOrder)
{
    StudioFrame frame;
    CNA_STUDIO_EXPECT(frame.phase() == StudioFramePhase::Idle);

    frame.beginFrame(at(0.0f, 0.0f));
    CNA_STUDIO_EXPECT(frame.phase() == StudioFramePhase::Build);
    frame.beginLayout();
    CNA_STUDIO_EXPECT(frame.phase() == StudioFramePhase::Layout);
    frame.beginInput();
    CNA_STUDIO_EXPECT(frame.isInputPass());
    frame.beginDraw();
    CNA_STUDIO_EXPECT(frame.isDrawPass());
    frame.endFrame();

    CNA_STUDIO_EXPECT(frame.phase() == StudioFramePhase::Idle);
    CNA_STUDIO_EXPECT_EQ(frame.phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(frame.frameIndex(), std::uint64_t{1});
}

CNA_STUDIO_TEST(SkippingAPhaseIsRecordedRatherThanIgnored)
{
    StudioFrame frame;
    frame.beginFrame(at(0.0f, 0.0f));
    frame.beginDraw(); // Layout and Input skipped.

    CNA_STUDIO_EXPECT(frame.phaseViolations() > 0);
    CNA_STUDIO_EXPECT(frame.phaseViolationLog().front().find("Draw") != std::string::npos);
}

CNA_STUDIO_TEST(LayoutCanBeTestedWithoutEverDrawing)
{
    // The acceptance condition of STUDIO-03015, spelled as a test: a layout run enters two phases,
    // asserts on geometry, and never touches a draw list.
    StudioFrame frame;
    frame.beginFrame(at(0.0f, 0.0f));
    frame.beginLayout();

    UiRect area{0.0f, 0.0f, 800.0f, 600.0f};
    const UiRect header = area.splitTop(static_cast<float>(
        frame.theme().metric(StudioMetric::MenuBarHeight)));

    CNA_STUDIO_EXPECT(header.height > 0.0f);
    CNA_STUDIO_EXPECT_EQ(frame.phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(InteractingOutsideADescriptionPassIsAViolation)
{
    StudioFrame frame;
    frame.beginFrame(at(50.0f, 20.0f));
    frame.beginLayout();
    const StudioInteraction result = frame.interact(kA, kButton);

    CNA_STUDIO_EXPECT(!result.hovered);
    CNA_STUDIO_EXPECT(frame.phaseViolations() > 0);
}

CNA_STUDIO_TEST(TouchingTheDrawListOutsideTheDrawPassIsAViolation)
{
    StudioFrame frame;
    frame.beginFrame(at(0.0f, 0.0f));
    frame.beginLayout();
    frame.beginInput();
    (void)frame.drawList();

    CNA_STUDIO_EXPECT(frame.phaseViolations() > 0);
}

CNA_STUDIO_TEST(TheDrawPassReplaysWhatTheInputPassDecided)
{
    StudioFrame frame;
    std::vector<bool> hovered;

    runStudioFrame(frame, at(50.0f, 20.0f), [&](StudioFrame& f) {
        hovered.push_back(f.interact(kA, kButton).hovered);
    });

    CNA_STUDIO_EXPECT_EQ(hovered.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(hovered[0]);
    CNA_STUDIO_EXPECT(hovered[1]);
    CNA_STUDIO_EXPECT_EQ(frame.phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(TheTwoPassesIssueTheSameIdsWithoutCollisions)
{
    StudioFrame frame;
    std::vector<std::uint64_t> seen;

    runStudioFrame(frame, at(0.0f, 0.0f), [&](StudioFrame& f) {
        f.ids().push("panel");
        seen.push_back(f.ids().make("row").value());
        f.ids().pop();
    });

    CNA_STUDIO_EXPECT_EQ(seen.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(seen[0], seen[1]);
    CNA_STUDIO_EXPECT_EQ(frame.ids().collisionCount(), std::size_t{0});
}

CNA_STUDIO_TEST(TheRetainedStateStoreAdvancesOncePerFrameNotOncePerPass)
{
    StudioFrame frame;
    const std::uint64_t before = frame.state().frame();
    runStudioFrame(frame, at(0.0f, 0.0f), [](StudioFrame&) {});
    CNA_STUDIO_EXPECT_EQ(frame.state().frame(), before + 1);
}

CNA_STUDIO_TEST(AFrameProducesDrawDataOnlyFromItsDrawPass)
{
    StudioFrame frame;
    runStudioFrame(frame, at(0.0f, 0.0f), [](StudioFrame& f) {
        if (f.isDrawPass())
        {
            f.drawList().fillRect(UiRect{0.0f, 0.0f, 10.0f, 10.0f}, StudioColor{255, 0, 0, 255});
        }
    });

    CNA_STUDIO_EXPECT_EQ(frame.drawData().lists.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(frame.drawData().lists.front().vertices.size() == 4);
}

CNA_STUDIO_TEST(AThemeCannotBeSwappedInTheMiddleOfAFrame)
{
    // Two passes with different metrics would hit-test one rectangle and draw another, for
    // exactly one frame, which is the hardest kind of input bug to reproduce.
    StudioFrame frame;
    frame.beginFrame(at(0.0f, 0.0f));
    frame.setTheme(StudioTheme::light());

    CNA_STUDIO_EXPECT(frame.phaseViolations() > 0);
    CNA_STUDIO_EXPECT(frame.theme().name() != "light");
}

// ------------------------------------------------------------------------------------------------
// Cursor requests (STUDIO-03020)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(OnlyTheWidgetUnderThePointerGetsItsCursor)
{
    StudioFrame frame;
    runStudioFrame(frame, at(50.0f, 20.0f), [](StudioFrame& f) {
        f.interact(kA, kButton);
        f.interact(kB, UiRect{400.0f, 400.0f, 50.0f, 50.0f});
        f.requestCursor(kB, StudioCursor::Crosshair);
        f.requestCursor(kA, StudioCursor::ResizeHorizontal);
    });

    CNA_STUDIO_EXPECT(frame.cursor() == StudioCursor::ResizeHorizontal);
}

CNA_STUDIO_TEST(TheWidgetHoldingTheMouseKeepsTheCursorWhenThePointerLeavesIt)
{
    StudioFrame frame;
    const auto describe = [](StudioFrame& f) {
        f.interact(kA, kButton);
        f.interact(kB, UiRect{400.0f, 400.0f, 100.0f, 100.0f});
        f.requestCursor(kA, StudioCursor::ResizeHorizontal);
        f.requestCursor(kB, StudioCursor::Crosshair);
    };

    runStudioFrame(frame, at(50.0f, 20.0f), describe);
    runStudioFrame(frame, at(50.0f, 20.0f, /*leftDown=*/true), describe);
    runStudioFrame(frame, at(450.0f, 450.0f, /*leftDown=*/true), describe);

    CNA_STUDIO_EXPECT(frame.cursor() == StudioCursor::ResizeHorizontal);
}

CNA_STUDIO_TEST(TheCursorReturnsToAnArrowWhenNobodyAsks)
{
    StudioFrame frame;
    runStudioFrame(frame, at(50.0f, 20.0f), [](StudioFrame& f) {
        f.interact(kA, kButton);
        f.requestCursor(kA, StudioCursor::Text);
    });
    CNA_STUDIO_EXPECT(frame.cursor() == StudioCursor::Text);

    runStudioFrame(frame, at(700.0f, 500.0f), [](StudioFrame& f) { f.interact(kA, kButton); });
    CNA_STUDIO_EXPECT(frame.cursor() == StudioCursor::Arrow);
}

CNA_STUDIO_TEST(EveryCursorShapeHasAName)
{
    for (int i = 0; i < static_cast<int>(StudioCursor::Count); ++i)
    {
        CNA_STUDIO_EXPECT(!studioCursorName(static_cast<StudioCursor>(i)).empty());
    }
}

// ------------------------------------------------------------------------------------------------
// Text measurement and truncation (STUDIO-03026, STUDIO-04006)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(Utf8IsCountedInCodePointsNotBytes)
{
    CNA_STUDIO_EXPECT_EQ(countUtf8CodePoints("abc"), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(countUtf8CodePoints("\xC3\xA9\xC3\xA8"), std::size_t{2});   // éè
    CNA_STUDIO_EXPECT_EQ(countUtf8CodePoints("\xE4\xB8\x96\xE7\x95\x8C"), std::size_t{2}); // 世界
    CNA_STUDIO_EXPECT_EQ(countUtf8CodePoints("\xF0\x9F\x8E\xAE"), std::size_t{1});   // 🎮
}

CNA_STUDIO_TEST(ApproximateMetricsPutTheBaselineAboveTheBoxCentre)
{
    StudioFontStyle style;
    style.sizePx = 20.0f;
    const StudioTextMetrics metrics = approximateStudioTextMetrics(style, "Hg");

    CNA_STUDIO_EXPECT(metrics.ascent > metrics.descent);
    CNA_STUDIO_EXPECT(metrics.lineHeight > metrics.height());

    const float baseline = metrics.centeredBaseline(0.0f, 40.0f);
    CNA_STUDIO_EXPECT(baseline > 20.0f);
    CNA_STUDIO_EXPECT(baseline < 40.0f);
}

CNA_STUDIO_TEST(ALabelThatFitsIsNotTruncated)
{
    StudioFrame frame;
    const StudioFontStyle style = frame.theme().font(StudioFontRole::Body);
    CNA_STUDIO_EXPECT_EQ(studioTruncateText(frame, style, "Save", 400.0f), std::string{"Save"});
}

CNA_STUDIO_TEST(ALabelThatDoesNotFitGainsAnEllipsisOnACodePointBoundary)
{
    StudioFrame frame;
    const StudioFontStyle style = frame.theme().font(StudioFontRole::Body);
    const std::string fitted = studioTruncateText(frame, style, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 24.0f);

    CNA_STUDIO_EXPECT(fitted.size() >= 3);
    CNA_STUDIO_EXPECT(fitted.find("\xE2\x80\xA6") != std::string::npos);

    // Every byte before the ellipsis must still form whole code points: the truncation is only
    // correct if what remains decodes.
    const std::string body = fitted.substr(0, fitted.size() - 3);
    CNA_STUDIO_EXPECT_EQ(body.size() % 2, std::size_t{0});
    CNA_STUDIO_EXPECT(frame.measureText(style, fitted).width <= 24.0f);
}

CNA_STUDIO_TEST(ALabelWithNoRoomAtAllStillShowsAnEllipsis)
{
    StudioFrame frame;
    const StudioFontStyle style = frame.theme().font(StudioFontRole::Body);
    CNA_STUDIO_EXPECT_EQ(studioTruncateText(frame, style, "Content Browser", 5.0f),
                         std::string{"\xE2\x80\xA6"});
    CNA_STUDIO_EXPECT(studioTruncateText(frame, style, "Content Browser", 0.0f).empty());
}

// ------------------------------------------------------------------------------------------------
// Buttons (STUDIO-03003)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AButtonActivatesOncePerClickDespiteBeingDescribedTwice)
{
    StudioFrame frame;
    const auto button = [](StudioFrame& f) {
        return studioButton(f, kA, kButton, "Save");
    };

    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f), button), 0);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f, /*leftDown=*/true), button), 0);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f), button), 1);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f), button), 0);
}

CNA_STUDIO_TEST(AButtonNeverActivatesInTheDrawPass)
{
    StudioFrame frame;
    runStudioFrame(frame, at(50.0f, 20.0f), [](StudioFrame& f) { studioButton(f, kA, kButton, "S"); });
    runStudioFrame(frame, at(50.0f, 20.0f, true), [](StudioFrame& f) { studioButton(f, kA, kButton, "S"); });

    bool activatedInDraw = false;
    runStudioFrame(frame, at(50.0f, 20.0f), [&](StudioFrame& f) {
        const StudioWidgetResult result = studioButton(f, kA, kButton, "S");
        if (f.isDrawPass() && result.activated) { activatedInDraw = true; }
    });

    CNA_STUDIO_EXPECT(!activatedInDraw);
}

CNA_STUDIO_TEST(PressingAButtonAndSlidingOffCancelsIt)
{
    StudioFrame frame;
    const auto button = [](StudioFrame& f) { return studioButton(f, kA, kButton, "Save"); };

    activations(frame, at(50.0f, 20.0f), button);
    activations(frame, at(50.0f, 20.0f, /*leftDown=*/true), button);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(500.0f, 400.0f, /*leftDown=*/true), button), 0);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(500.0f, 400.0f), button), 0);
}

CNA_STUDIO_TEST(ADisabledButtonNeitherActivatesNorHovers)
{
    StudioFrame frame;
    StudioButtonOptions options;
    options.enabled = false;

    StudioWidgetResult seen;
    const auto button = [&](StudioFrame& f) {
        seen = studioButton(f, kA, kButton, "Save", options);
        return seen;
    };

    activations(frame, at(50.0f, 20.0f), button);
    CNA_STUDIO_EXPECT(seen.interaction.disabled);
    CNA_STUDIO_EXPECT(!seen.interaction.hovered);

    activations(frame, at(50.0f, 20.0f, true), button);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f), button), 0);
}

CNA_STUDIO_TEST(ADisabledButtonStillStopsThePointerReachingWhatItCovers)
{
    StudioFrame frame;
    StudioButtonOptions disabled;
    disabled.enabled = false;

    bool behindHovered = false;
    runStudioFrame(frame, at(50.0f, 20.0f), [&](StudioFrame& f) {
        f.interact(kB, kButton);                                // described first: underneath
        const StudioWidgetResult front = studioButton(f, kA, kButton, "Save", disabled);
        (void)front;
        behindHovered = f.router().hoveredId() == kB;
    });

    CNA_STUDIO_EXPECT(!behindHovered);
}

CNA_STUDIO_TEST(ADisabledButtonIsNotATabStop)
{
    StudioFrame frame;
    StudioButtonOptions disabled;
    disabled.enabled = false;

    UiInputState tab = at(700.0f, 500.0f);
    tab.setKeyDown(UiKey::Tab, true);

    runStudioFrame(frame, at(700.0f, 500.0f), [&](StudioFrame& f) {
        studioButton(f, kA, kButton, "A", disabled);
        studioButton(f, kB, UiRect{10.0f, 60.0f, 120.0f, 28.0f}, "B");
    });
    runStudioFrame(frame, tab, [&](StudioFrame& f) {
        studioButton(f, kA, kButton, "A", disabled);
        studioButton(f, kB, UiRect{10.0f, 60.0f, 120.0f, 28.0f}, "B");
    });

    CNA_STUDIO_EXPECT(frame.router().focusedId() == kB);
}

CNA_STUDIO_TEST(SpaceActivatesTheFocusedButton)
{
    StudioFrame frame;
    const auto button = [](StudioFrame& f) { return studioButton(f, kA, kButton, "Save"); };

    // Click it once to give it focus, then act on it with the keyboard from elsewhere.
    activations(frame, at(50.0f, 20.0f), button);
    activations(frame, at(50.0f, 20.0f, true), button);
    activations(frame, at(50.0f, 20.0f), button);

    UiInputState space = at(600.0f, 400.0f);
    space.setKeyDown(UiKey::Space, true);
    CNA_STUDIO_EXPECT_EQ(activations(frame, space, button), 1);

    // Held, not pressed again: a key that repeats must not fire the action every frame.
    CNA_STUDIO_EXPECT_EQ(activations(frame, space, button), 0);
}

CNA_STUDIO_TEST(SpaceDoesNotActivateAButtonWhileAFieldIsTakingText)
{
    StudioFrame frame;
    const auto button = [](StudioFrame& f) {
        f.router().setWantsTextInput(true);
        return studioButton(f, kA, kButton, "Save");
    };

    activations(frame, at(50.0f, 20.0f), button);
    activations(frame, at(50.0f, 20.0f, true), button);
    activations(frame, at(50.0f, 20.0f), button);

    UiInputState space = at(600.0f, 400.0f);
    space.setKeyDown(UiKey::Space, true);
    CNA_STUDIO_EXPECT_EQ(activations(frame, space, button), 0);
}

CNA_STUDIO_TEST(NoWidgetIsClickedOnTheFirstFrameWithAButtonAlreadyDown)
{
    StudioFrame frame;
    const auto button = [](StudioFrame& f) { return studioButton(f, kA, kButton, "Save"); };

    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f, /*leftDown=*/true), button), 0);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f), button), 0);
}

// ------------------------------------------------------------------------------------------------
// Toggles, checkboxes and tabs
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AToggleFlipsOncePerClick)
{
    StudioFrame frame;
    bool checked = false;
    const auto toggle = [&](StudioFrame& f) {
        return studioToggle(f, kA, kButton, "Grid", checked);
    };

    activations(frame, at(50.0f, 20.0f), toggle);
    activations(frame, at(50.0f, 20.0f, true), toggle);
    activations(frame, at(50.0f, 20.0f), toggle);
    CNA_STUDIO_EXPECT(checked);

    activations(frame, at(50.0f, 20.0f, true), toggle);
    activations(frame, at(50.0f, 20.0f), toggle);
    CNA_STUDIO_EXPECT(!checked);
}

CNA_STUDIO_TEST(ACheckboxFlipsOncePerClickAndReportsTheChange)
{
    StudioFrame frame;
    bool checked = false;
    int changes = 0;
    const auto box = [&](StudioFrame& f) {
        const StudioWidgetResult result = studioCheckbox(f, kA, kButton, "Snap", checked);
        if (result.changed) { ++changes; }
        return result;
    };

    activations(frame, at(50.0f, 20.0f), box);
    activations(frame, at(50.0f, 20.0f, true), box);
    activations(frame, at(50.0f, 20.0f), box);

    CNA_STUDIO_EXPECT(checked);
    CNA_STUDIO_EXPECT_EQ(changes, 1);
}

CNA_STUDIO_TEST(ATabReportsItsOwnActivation)
{
    StudioFrame frame;
    StudioTabOptions options;
    options.active = false;
    const auto tab = [&](StudioFrame& f) {
        return studioTab(f, kA, kButton, "Content Browser", options);
    };

    activations(frame, at(50.0f, 20.0f), tab);
    activations(frame, at(50.0f, 20.0f, true), tab);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(50.0f, 20.0f), tab), 1);
}

// ------------------------------------------------------------------------------------------------
// Menu items
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AMenuItemCommitsOnReleaseSoPressDragReleaseWorks)
{
    // The gesture every desktop menu supports: press the title, drag down the list, release on the
    // item you wanted. The press never happened on the item, so a click-based item would never fire.
    StudioFrame frame;
    const UiRect item{20.0f, 60.0f, 200.0f, 24.0f};
    const auto menuItem = [&](StudioFrame& f) {
        return studioMenuItem(f, kA, item, "Save");
    };

    activations(frame, at(30.0f, 10.0f), menuItem);
    activations(frame, at(30.0f, 10.0f, /*leftDown=*/true), menuItem);
    activations(frame, at(60.0f, 70.0f, /*leftDown=*/true), menuItem);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(60.0f, 70.0f), menuItem), 1);
}

CNA_STUDIO_TEST(ADisabledMenuItemDoesNotActivate)
{
    StudioFrame frame;
    const UiRect item{20.0f, 60.0f, 200.0f, 24.0f};
    StudioMenuItemOptions options;
    options.enabled = false;

    const auto menuItem = [&](StudioFrame& f) {
        return studioMenuItem(f, kA, item, "Save", options);
    };

    activations(frame, at(60.0f, 70.0f), menuItem);
    activations(frame, at(60.0f, 70.0f, true), menuItem);
    CNA_STUDIO_EXPECT_EQ(activations(frame, at(60.0f, 70.0f), menuItem), 0);
}

CNA_STUDIO_TEST(AMenuItemIsNotATabStop)
{
    // Menus are navigated with the arrow keys, not by tabbing into them from the panel behind.
    StudioFrame frame;
    const UiRect item{20.0f, 60.0f, 200.0f, 24.0f};

    runStudioFrame(frame, at(700.0f, 500.0f), [&](StudioFrame& f) {
        studioMenuItem(f, kA, item, "Save");
        studioButton(f, kB, kButton, "Elsewhere");
    });

    UiInputState tab = at(700.0f, 500.0f);
    tab.setKeyDown(UiKey::Tab, true);
    runStudioFrame(frame, tab, [&](StudioFrame& f) {
        studioMenuItem(f, kA, item, "Save");
        studioButton(f, kB, kButton, "Elsewhere");
    });

    CNA_STUDIO_EXPECT(frame.router().focusedId() == kB);
}

CNA_STUDIO_TEST(MenuRowsAreAtLeastAsTallAsTheMinimumHitTarget)
{
    const StudioTheme theme = StudioTheme::dark();
    CNA_STUDIO_EXPECT(studioMenuItemHeight(theme)
                      >= static_cast<float>(theme.metric(StudioMetric::MinimumHitTarget)));
    CNA_STUDIO_EXPECT(studioMenuSeparatorHeight(theme) > 0.0f);
}

// ------------------------------------------------------------------------------------------------
// Clipping, and that widgets honour it (STUDIO-03011)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AWidgetClippedAwayIsNeitherHoveredNorDrawn)
{
    StudioFrame frame;
    std::size_t vertices = 0;

    runStudioFrame(frame, at(50.0f, 20.0f), [&](StudioFrame& f) {
        f.pushClip(UiRect{0.0f, 200.0f, 800.0f, 100.0f});
        studioButton(f, kA, kButton, "Hidden");
        f.popClip();
        if (f.isDrawPass()) { vertices = f.drawList().vertexCount(); }
    });

    CNA_STUDIO_EXPECT(frame.router().hoveredId() != kA);
    CNA_STUDIO_EXPECT_EQ(vertices, std::size_t{0});
}

CNA_STUDIO_TEST(ClippingPushedThroughTheFrameReachesBothTheRouterAndTheDrawList)
{
    StudioFrame frame;
    UiRect routerClip;
    UiRect drawClip;

    runStudioFrame(frame, at(0.0f, 0.0f), [&](StudioFrame& f) {
        f.pushClip(UiRect{10.0f, 10.0f, 100.0f, 100.0f});
        routerClip = f.router().currentClip();
        if (f.isDrawPass()) { drawClip = f.drawList().currentClip(); }
        f.popClip();
    });

    CNA_STUDIO_EXPECT(routerClip == UiRect(10.0f, 10.0f, 100.0f, 100.0f));
    CNA_STUDIO_EXPECT(drawClip == UiRect(10.0f, 10.0f, 100.0f, 100.0f));
}
