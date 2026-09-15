// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTooltipTests.cpp
 * @brief Tooltips: the delay, the one-at-a-time rule, and the placement (plan.md STUDIO-03021).
 *
 * These became necessary rather than nice the moment the toolbar went icon-only: an icon toolbar
 * with no tooltips is less discoverable than the row of words it replaced, so the tooltip is part
 * of that feature rather than a later polish pass.
 *
 * The delay is the part worth testing hardest. Without one, dragging the pointer across a toolbar
 * flashes six tooltips on the way to the seventh — and the flicker is what the eye follows, so the
 * one the user actually wanted is the one they do not read.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <memory>
#include <string>

using namespace CNA::Studio;

namespace
{
    UiInputState at(float x, float y, float deltaSeconds = 1.0f / 60.0f)
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 720.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = x >= 0.0f && y >= 0.0f;
        input.deltaSeconds = deltaSeconds;
        return input;
    }

    /** @brief A frame with one tooltip-carrying button, driven for a given number of frames. */
    struct Fixture
    {
        StudioFrame frame{StudioTheme::dark()};
        UiRect bounds{100.0f, 100.0f, 120.0f, 30.0f};

        /** @brief Runs one frame with the pointer at (@p x, @p y). */
        void hover(float x, float y, float deltaSeconds = 1.0f / 60.0f)
        {
            runStudioFrame(frame, at(x, y, deltaSeconds), [&](StudioFrame& f) {
                StudioButtonOptions options;
                options.tooltip = "Save  (Ctrl+S)";
                (void)studioButton(f, f.ids().make("save"), bounds, "Save", options);

                StudioButtonOptions other;
                other.tooltip = "Undo  (Ctrl+Z)";
                (void)studioButton(f, f.ids().make("undo"),
                                   UiRect{bounds.right() + 10.0f, bounds.top(), 120.0f, 30.0f},
                                   "Undo", other);
            });
        }

        /** @brief Runs enough frames at 1/60s for the delay to elapse. */
        void rest(float x, float y)
        {
            const int frames = static_cast<int>(frame.tooltipDelay() * 60.0f) + 3;
            for (int i = 0; i < frames; ++i) { hover(x, y); }
        }
    };
}

CNA_STUDIO_TEST(ATooltipWaitsForThePointerToRestRatherThanAppearingAtOnce)
{
    Fixture fixture;

    fixture.hover(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(!fixture.frame.tooltip().visible());

    fixture.rest(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.tooltip().visible());
    CNA_STUDIO_EXPECT_EQ(fixture.frame.tooltip().text, std::string{"Save  (Ctrl+S)"});
}

CNA_STUDIO_TEST(MovingToAnotherControlStartsTheWaitAgain)
{
    // The whole point of the delay. A tooltip that kept its clock across a move would appear
    // instantly on the second control, which is the flicker it exists to prevent.
    Fixture fixture;
    fixture.rest(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.tooltip().visible());

    const float otherX = fixture.bounds.right() + 70.0f;
    fixture.hover(otherX, fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(!fixture.frame.tooltip().visible());

    fixture.rest(otherX, fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.tooltip().visible());
    CNA_STUDIO_EXPECT_EQ(fixture.frame.tooltip().text, std::string{"Undo  (Ctrl+Z)"});
}

CNA_STUDIO_TEST(LeavingAndComingBackWaitsAgainRatherThanResumingTheClock)
{
    // A clock that carried across a gap would make the tooltip snap back the instant the pointer
    // returned, which is indistinguishable from having no delay for anyone sweeping a toolbar.
    Fixture fixture;
    fixture.rest(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.tooltip().visible());

    fixture.hover(-1.0f, -1.0f);
    CNA_STUDIO_EXPECT(!fixture.frame.tooltip().visible());

    fixture.hover(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(!fixture.frame.tooltip().visible());
}

CNA_STUDIO_TEST(OnlyTheControlUnderThePointerShowsOne)
{
    // Every widget offers a tooltip; at most one is showing. Offering is not the same as winning,
    // and a frame that let the last offer win would show whichever control happened to be
    // described last.
    Fixture fixture;
    fixture.rest(fixture.bounds.centerX(), fixture.bounds.centerY());

    CNA_STUDIO_EXPECT(fixture.frame.tooltip().visible());
    CNA_STUDIO_EXPECT_EQ(fixture.frame.tooltip().text, std::string{"Save  (Ctrl+S)"});

    // And the anchor is the widget's own rectangle, so a caller can place the tooltip beside the
    // control rather than under the pointer.
    CNA_STUDIO_EXPECT_EQ(fixture.frame.tooltip().anchor.left(), fixture.bounds.left());
    CNA_STUDIO_EXPECT_EQ(fixture.frame.tooltip().anchor.bottom(), fixture.bounds.bottom());
}

CNA_STUDIO_TEST(NoTooltipAppearsWhileSomethingIsBeingDragged)
{
    // One that appeared halfway through a splitter drag would cover the thing being dragged, at
    // the one moment the user is watching it most closely.
    Fixture fixture;

    const int frames = static_cast<int>(fixture.frame.tooltipDelay() * 60.0f) + 3;
    for (int i = 0; i < frames; ++i)
    {
        UiInputState input = at(fixture.bounds.centerX(), fixture.bounds.centerY());
        input.setMouseDown(UiMouseButton::Left, i > 0);
        runStudioFrame(fixture.frame, input, [&](StudioFrame& f) {
            StudioButtonOptions options;
            options.tooltip = "Save  (Ctrl+S)";
            (void)studioButton(f, f.ids().make("save"), fixture.bounds, "Save", options);
        });
    }

    CNA_STUDIO_EXPECT(!fixture.frame.tooltip().visible());
}

CNA_STUDIO_TEST(TheShellsToolbarOffersATooltipCarryingTheShortcut)
{
    // An icon-only toolbar is discoverable only through this, and a tooltip that merely repeated
    // the word the button would have shown would make the icons no more discoverable than before.
    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));

    const UiRect save = shell->toolbarEntryBounds(0);
    CNA_STUDIO_EXPECT(!save.isEmpty());

    const int frames = static_cast<int>(shell->frame().tooltipDelay() * 60.0f) + 3;
    for (int i = 0; i < frames; ++i) { shell->renderFrame(at(save.centerX(), save.centerY())); }

    const StudioFrame::StudioTooltipRequest& tooltip = shell->frame().tooltip();
    CNA_STUDIO_EXPECT(tooltip.visible());
    CNA_STUDIO_EXPECT(tooltip.text.find("Save") != std::string::npos);
    CNA_STUDIO_EXPECT(tooltip.text.find("Ctrl") != std::string::npos);

    // Two lines: the command and its shortcut, then what it does.
    CNA_STUDIO_EXPECT(tooltip.text.find('\n') != std::string::npos);

    // And it is drawn, above everything, rather than merely resolved.
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(ATooltipStaysInsideTheWindow)
{
    // Clamped horizontally and flipped above the control when there is no room below. A tooltip cut
    // in half by the window edge says nothing, and the controls most likely to hit the edge are the
    // ones at the end of a toolbar and along the bottom of the shell.
    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();

    UiInputState corner = at(-1.0f, -1.0f);
    corner.displayWidth = 400.0f;
    corner.displayHeight = 260.0f;
    shell->renderFrame(corner);

    const UiRect last = shell->toolbarEntryBounds(0);
    CNA_STUDIO_EXPECT(!last.isEmpty());

    const int frames = static_cast<int>(shell->frame().tooltipDelay() * 60.0f) + 3;
    for (int i = 0; i < frames; ++i)
    {
        UiInputState input = at(last.centerX(), last.centerY());
        input.displayWidth = 400.0f;
        input.displayHeight = 260.0f;
        shell->renderFrame(input);
    }

    CNA_STUDIO_EXPECT(shell->frame().tooltip().visible());

    // Every vertex of every list is inside the window: the tooltip is the last thing drawn and the
    // only thing placed relative to a control rather than to the layout, so if anything escapes
    // the window it is this.
    float minX = 1e9f;
    float maxX = -1e9f;
    float minY = 1e9f;
    float maxY = -1e9f;
    for (const UiDrawList& list : shell->drawData().lists)
    {
        for (const UiVertex& vertex : list.vertices)
        {
            minX = std::min(minX, vertex.x);
            maxX = std::max(maxX, vertex.x);
            minY = std::min(minY, vertex.y);
            maxY = std::max(maxY, vertex.y);
        }
    }
    CNA_STUDIO_EXPECT(minX >= -0.5f);
    CNA_STUDIO_EXPECT(minY >= -0.5f);
    CNA_STUDIO_EXPECT(maxX <= 400.5f);
    CNA_STUDIO_EXPECT(maxY <= 260.5f);
}

CNA_STUDIO_TEST(TheShellsFirstToolbarButtonIsUnderThePointTheScreenshotTestsUse)
{
    // The screenshot tests place the pointer at (20, 40) and claim to photograph a pressed and a
    // tooltipped toolbar button. Nothing in a capture says whether the pointer actually landed on
    // one: (40, 40) was the gap between two buttons for as long as those tests existed, so the
    // "pressed toolbar" picture was a picture of the toolbar at rest and passed every run. This
    // pins the number, so moving the toolbar fails here rather than quietly emptying those
    // captures of the thing they exist to show.
    StudioShell shell;

    UiInputState input;
    input.displayWidth = 1280.0f;
    input.displayHeight = 720.0f;
    input.mouseX = 20.0f;
    input.mouseY = 40.0f;
    input.mouseInWindow = true;
    shell.renderFrame(input);
    shell.renderFrame(input);

    CNA_STUDIO_EXPECT(shell.toolbarEntryCount() > 0);
    CNA_STUDIO_EXPECT(shell.toolbarEntryBounds(0).contains(20.0f, 40.0f));
    CNA_STUDIO_EXPECT(shell.frame().router().hoveredId().isValid());
}
