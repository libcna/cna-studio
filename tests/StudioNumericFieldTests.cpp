// SPDX-License-Identifier: MS-PL
/**
 * @file StudioNumericFieldTests.cpp
 * @brief A number that is typed into or dragged sideways to scrub (plan.md STUDIO-07055).
 *
 * The prototype's vector fields scrub: press, move sideways, and the value follows. The native
 * field committed on Enter and nothing else, so setting a position meant selecting the text and
 * typing four characters — for a value the user usually wants to *feel* their way to rather than
 * know in advance.
 *
 * The three cases that decide whether a scrub is usable are the threshold, the anchor and the
 * merge. Without the threshold a click nudges the value and nobody dares click. Without the anchor
 * the value drifts and dragging out and back does not return to where it started, which is the
 * first thing anybody tries. Without the merge a forty-pixel drag is forty undo entries.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = 640.0f;
        input.displayHeight = 480.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief One field, driven frame by frame, with its value and what it last reported. */
    struct Fixture
    {
        StudioFrame frame;
        UiRect bounds{100.0f, 100.0f, 120.0f, 24.0f};
        float value = 0.0f;
        StudioNumericFieldOptions options;
        StudioNumericFieldResult last;
        int changes = 0;

        Fixture()
        {
            frame.setTheme(StudioTheme::dark());
            options.step = 0.5f;
        }

        /** @brief Runs one whole frame: input pass then draw pass, as a shell does. */
        void run(float x, float y, bool down)
        {
            frame.beginFrame(at(x, y, down));
            frame.beginInput();
            const StudioNumericFieldResult input =
                studioNumericField(frame, frame.ids().make("field"), bounds, value, options);
            last = input;
            if (input.changed) { ++changes; }
            frame.beginDraw();
            (void)studioNumericField(frame, frame.ids().make("field"), bounds, value, options);
            frame.endFrame();
        }

        /** @brief The centre of the field, which is where a press lands. */
        [[nodiscard]] float centreX() const { return bounds.centerX(); }
        [[nodiscard]] float centreY() const { return bounds.centerY(); }
    };
}

CNA_STUDIO_TEST(AHorizontalDragMovesTheValueByTheStepPerPixel)
{
    // Proportional to the distance travelled, not to the value. A step proportional to the current
    // value would make a field at zero unmovable, which is exactly where a user most often starts.
    Fixture fixture;
    fixture.value = 10.0f;

    // Button up first: the router treats the first frame's state as its own previous state, so a
    // test whose opening frame already has the button down registers no press at all.
    fixture.run(fixture.centreX(), fixture.centreY(), false);
    fixture.run(fixture.centreX(), fixture.centreY(), true);

    // Past the threshold, in one move. Twenty pixels at half a unit each.
    fixture.run(fixture.centreX() + 20.0f, fixture.centreY(), true);

    CNA_STUDIO_EXPECT(fixture.last.dragging);
    CNA_STUDIO_EXPECT_EQ(fixture.value, 20.0f);

    // And back the other way, which must return to where it started rather than accumulating.
    // A scrub that summed frame deltas would round every increment and land somewhere near 10.
    fixture.run(fixture.centreX(), fixture.centreY(), true);
    CNA_STUDIO_EXPECT_EQ(fixture.value, 10.0f);

    fixture.run(fixture.centreX() - 20.0f, fixture.centreY(), true);
    CNA_STUDIO_EXPECT_EQ(fixture.value, 0.0f);
}

CNA_STUDIO_TEST(AClickWithoutMovementLeavesTheValueAloneAndFocusesTheField)
{
    // The case that decides whether the feature is usable at all. A field that changes when you
    // click it is a field nobody dares click -- and a trackpad click wobbles by a pixel or two, so
    // "did not move" has to mean "did not move much" rather than "moved by zero".
    Fixture fixture;
    fixture.value = 7.5f;

    fixture.run(fixture.centreX(), fixture.centreY(), false);
    fixture.run(fixture.centreX(), fixture.centreY(), true);
    // Two pixels: a wobble, not a gesture.
    fixture.run(fixture.centreX() + 2.0f, fixture.centreY(), true);
    fixture.run(fixture.centreX() + 2.0f, fixture.centreY(), false);

    CNA_STUDIO_EXPECT(!fixture.last.dragging);
    CNA_STUDIO_EXPECT_EQ(fixture.value, 7.5f);
    CNA_STUDIO_EXPECT_EQ(fixture.changes, 0);

    // And the press still reached the text field, so the caret is placed and typing works.
    CNA_STUDIO_EXPECT(fixture.last.text.interaction.focused);
}

CNA_STUDIO_TEST(TheScrubEndsWhenTheButtonComesUp)
{
    // Reported as *not* dragging on the frame the button comes up, which is what lets a caller see
    // the gesture end and close its undo merge chain. A flag that stayed true until the next press
    // would fold two separate drags into one entry.
    Fixture fixture;
    fixture.value = 0.0f;

    fixture.run(fixture.centreX(), fixture.centreY(), false);
    fixture.run(fixture.centreX(), fixture.centreY(), true);
    fixture.run(fixture.centreX() + 30.0f, fixture.centreY(), true);
    CNA_STUDIO_EXPECT(fixture.last.dragging);

    fixture.run(fixture.centreX() + 30.0f, fixture.centreY(), false);
    CNA_STUDIO_EXPECT(!fixture.last.dragging);

    // A second drag starts from wherever the first one left the value, not from where it began.
    const float afterFirst = fixture.value;
    fixture.run(fixture.centreX() + 30.0f, fixture.centreY(), true);
    fixture.run(fixture.centreX() + 50.0f, fixture.centreY(), true);
    CNA_STUDIO_EXPECT(fixture.last.dragging);
    CNA_STUDIO_EXPECT_EQ(fixture.value, afterFirst + 20.0f * 0.5f);
}

CNA_STUDIO_TEST(AnIntegralFieldScrubsInWholeNumbers)
{
    // A count, an index or a colour channel. Rounded rather than truncated, so a drag left from 3
    // passes through 2 at the same distance a drag right passes through 4 -- truncation makes the
    // negative half of every gesture feel slower than the positive half.
    Fixture fixture;
    fixture.options.integral = true;
    fixture.options.step = 0.25f;
    fixture.value = 3.0f;

    fixture.run(fixture.centreX(), fixture.centreY(), false);
    fixture.run(fixture.centreX(), fixture.centreY(), true);
    fixture.run(fixture.centreX() + 10.0f, fixture.centreY(), true);

    CNA_STUDIO_EXPECT(fixture.last.dragging);
    // 3 + 10 * 0.25 = 5.5, rounded to 6 rather than truncated to 5.
    CNA_STUDIO_EXPECT_EQ(fixture.value, 6.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.value, std::round(fixture.value));
}

CNA_STUDIO_TEST(AFieldThatIsNotDraggableBehavesAsAPlainTextField)
{
    // A property whose value has no meaningful increment -- an id, a version -- is still a number
    // and is still typed into. Offering a scrub there would be offering a gesture whose result is
    // never what anybody wanted.
    Fixture fixture;
    fixture.options.draggable = false;
    fixture.value = 42.0f;

    fixture.run(fixture.centreX(), fixture.centreY(), false);
    fixture.run(fixture.centreX(), fixture.centreY(), true);
    fixture.run(fixture.centreX() + 60.0f, fixture.centreY(), true);

    CNA_STUDIO_EXPECT(!fixture.last.dragging);
    CNA_STUDIO_EXPECT_EQ(fixture.value, 42.0f);
}

CNA_STUDIO_TEST(TheScrubDoesNotShareRetainedStateWithTheEditSession)
{
    // The defect the first attempt at this widget had, and it is worth a case of its own because it
    // was invisible in every way except the one that mattered: the scrub used `WidgetState::active`
    // to mean "a drag is in flight", which is the field `studioTextField` uses to mean "an edit
    // session is open". Clearing it after each frame's drag check cleared the session, so every
    // keystroke in every numeric field in Studio was discarded on the next frame -- and the field
    // still drew, still focused, still reported hover.
    //
    // Retained state is shared by whatever shares the id, and a wrapper is one of those things.
    Fixture fixture;
    fixture.value = 1.0f;

    // Click in, without moving: this is an edit session, not a scrub.
    fixture.run(fixture.centreX(), fixture.centreY(), false);
    fixture.run(fixture.centreX(), fixture.centreY(), true);
    fixture.run(fixture.centreX(), fixture.centreY(), false);

    // Several frames of nothing. The session has to survive them -- that is the whole purpose of
    // retained state, and it is exactly what the shared flag destroyed.
    for (int i = 0; i < 5; ++i) { fixture.run(fixture.centreX(), fixture.centreY(), false); }

    CNA_STUDIO_EXPECT(fixture.last.text.interaction.focused);
    CNA_STUDIO_EXPECT(!fixture.last.dragging);
    CNA_STUDIO_EXPECT_EQ(fixture.value, 1.0f);
}
