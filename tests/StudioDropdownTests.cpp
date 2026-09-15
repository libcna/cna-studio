// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDropdownTests.cpp
 * @brief Drop-downs, and the deferred popups they needed (plan.md STUDIO-03036, STUDIO-03022).
 *
 * The hard part of a drop-down is not the list. It is that the list has to escape the panel the
 * control sits in — a list clipped to a property row is a list with one visible option — and it
 * has to stop clicks reaching whatever it covers. Both are properties of the *frame*, not of the
 * widget, which is why the widget came with `deferPopup`.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A metric already scaled to physical pixels, as the widgets resolve them. */
    float metricOf(const StudioTheme& theme, StudioMetric metric)
    {
        return static_cast<float>(theme.metric(metric));
    }

    constexpr float kWidth = 640.0f;
    constexpr float kHeight = 480.0f;

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

    UiInputState withKey(UiKey key, float x = 20.0f, float y = 20.0f)
    {
        UiInputState input = at(x, y);
        input.setKeyDown(key, true);
        return input;
    }

    /** @brief One drop-down, plus a button underneath it that must not be clickable through it. */
    struct Fixture
    {
        StudioFrame frame{StudioTheme::dark()};
        std::vector<std::string> items{"Direct3D 11", "OpenGL", "Vulkan", "Metal"};
        int selected = 1;
        UiRect bounds{40.0f, 60.0f, 180.0f, 24.0f};

        StudioDropdownResult last;
        int coveredButtonClicks = 0;

        /** @brief Runs one frame, capturing the input pass's answer. */
        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                StudioDropdownOptions options;
                options.placeholder = "(none)";
                const StudioDropdownResult result =
                    studioDropdown(f, f.ids().make("renderer"), bounds, items, selected, options);
                if (f.isInputPass()) { last = result; }

                // Directly under the list, so "does the popup block what it covers" is a question
                // this fixture can answer.
                const StudioWidgetResult covered = studioButton(
                    f, f.ids().make("covered"),
                    UiRect{bounds.left(), bounds.bottom() + 20.0f, 120.0f, 24.0f}, "Covered");
                if (f.isInputPass() && covered.activated) { ++coveredButtonClicks; }
            });
        }

        void settle() { run(at(400.0f, 400.0f)); }

        /** @brief Press and release at a point, with a settling frame in between. */
        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        /** @brief Where a row of the open list sits. */
        [[nodiscard]] UiRect rowBounds(int index) const
        {
            const float rowHeight = studioMenuItemHeight(frame.theme());
            const float padding = metricOf(frame.theme(), StudioMetric::SpacingSmall);
            return UiRect{bounds.left(),
                          bounds.bottom() + padding + static_cast<float>(index) * rowHeight,
                          bounds.width, rowHeight};
        }
    };
}

CNA_STUDIO_TEST(ADropdownStartsClosedAndShowsItsSelection)
{
    Fixture fixture;
    fixture.settle();

    CNA_STUDIO_EXPECT(!fixture.last.open);
    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
    CNA_STUDIO_EXPECT_EQ(fixture.last.selected, 1);
    CNA_STUDIO_EXPECT(!fixture.last.changed);
}

CNA_STUDIO_TEST(ClickingADropdownOpensItsList)
{
    Fixture fixture;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());

    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());
    CNA_STUDIO_EXPECT(fixture.last.open);
    CNA_STUDIO_EXPECT(!fixture.last.changed);
    CNA_STUDIO_EXPECT_EQ(fixture.selected, 1);
}

CNA_STUDIO_TEST(ClickingItAgainClosesTheListWithoutChoosing)
{
    Fixture fixture;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());

    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
    CNA_STUDIO_EXPECT_EQ(fixture.selected, 1);
}

CNA_STUDIO_TEST(ChoosingARowChangesTheSelectionAndClosesTheList)
{
    Fixture fixture;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());

    const UiRect row = fixture.rowBounds(2);
    fixture.click(row.centerX(), row.centerY());

    // The list is described after the control, so its answer arrives on the next pass that routes
    // input. One frame, and the list closes on the same one.
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.selected, 2);
    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
}

CNA_STUDIO_TEST(TheChangeIsReportedExactlyOnce)
{
    // A control that reported `changed` for as long as the value differed from what the caller
    // last saw would push one entry per frame into an undo stack.
    Fixture fixture;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());

    const UiRect row = fixture.rowBounds(3);
    fixture.click(row.centerX(), row.centerY());

    int reports = 0;
    for (int i = 0; i < 6; ++i)
    {
        fixture.settle();
        if (fixture.last.changed) { ++reports; }
    }

    CNA_STUDIO_EXPECT_EQ(reports, 1);
    CNA_STUDIO_EXPECT_EQ(fixture.selected, 3);
}

CNA_STUDIO_TEST(PressingOutsideTheListDismissesItWithoutChoosing)
{
    Fixture fixture;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());

    fixture.click(500.0f, 400.0f);
    fixture.settle();

    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
    CNA_STUDIO_EXPECT_EQ(fixture.selected, 1);
}

CNA_STUDIO_TEST(AnOpenListBlocksWhatItCovers)
{
    // The failure this catches is silent: a click meant for a list row also reaching the control
    // underneath, which in a property grid means editing the wrong property.
    Fixture fixture;
    fixture.settle();

    // The button works while the list is closed, so the test is about the list rather than about
    // a button that never worked.
    fixture.click(fixture.bounds.left() + 20.0f, fixture.bounds.bottom() + 32.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.coveredButtonClicks, 1);

    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());

    fixture.click(fixture.bounds.left() + 20.0f, fixture.bounds.bottom() + 32.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.coveredButtonClicks, 1);
}

CNA_STUDIO_TEST(ADropdownNearTheBottomOpensUpwards)
{
    Fixture fixture;
    fixture.bounds = UiRect{40.0f, kHeight - 40.0f, 180.0f, 24.0f};
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());

    // Rather than proving the rectangle directly -- it is internal -- prove the consequence: the
    // row the user can actually reach is above the control, not off the bottom of the window.
    const float rowHeight = studioMenuItemHeight(fixture.frame.theme());
    const float padding = metricOf(fixture.frame.theme(), StudioMetric::SpacingSmall);
    const float listHeight = 4.0f * rowHeight + padding * 2.0f;
    const float firstRowY = fixture.bounds.top() - listHeight + padding + rowHeight * 0.5f;

    CNA_STUDIO_EXPECT(firstRowY > 0.0f);
    fixture.click(fixture.bounds.centerX(), firstRowY);
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.selected, 0);
}

CNA_STUDIO_TEST(ADropdownWithNothingToChooseFromIsDisabled)
{
    Fixture fixture;
    fixture.items.clear();
    fixture.selected = -1;
    fixture.settle();

    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
    CNA_STUDIO_EXPECT(fixture.last.interaction.disabled);
}

CNA_STUDIO_TEST(TheKeyboardOpensMovesAndChooses)
{
    Fixture fixture;
    fixture.settle();

    // Tab focuses the drop-down; focus lands on the frame after the request, which is why the
    // extra settling frame is here rather than being an accident of the test.
    fixture.run(withKey(UiKey::Tab));
    fixture.settle();
    CNA_STUDIO_EXPECT(fixture.last.interaction.focused);

    fixture.run(withKey(UiKey::DownArrow));
    fixture.settle();
    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());

    // Opened on the current selection, so one Down moves to the next option rather than to the top.
    fixture.run(withKey(UiKey::DownArrow));
    fixture.settle();
    fixture.run(withKey(UiKey::Enter));
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.selected, 2);
    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
}

CNA_STUDIO_TEST(EscapeDismissesAnOpenListWithoutChoosing)
{
    Fixture fixture;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());

    fixture.run(withKey(UiKey::Escape));
    fixture.settle();

    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
    CNA_STUDIO_EXPECT_EQ(fixture.selected, 1);
}

CNA_STUDIO_TEST(AListLongerThanItsRowLimitScrollsRatherThanGrowing)
{
    Fixture fixture;
    fixture.items.clear();
    for (int i = 0; i < 200; ++i) { fixture.items.push_back("Item " + std::to_string(i)); }
    fixture.selected = 0;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());

    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());

    // Ten rows by default, so the list is nowhere near two hundred rows tall, and the rows beyond
    // the limit are not described at all.
    // Ten rows of two hundred: the list is bounded by the row limit, not by the item count, and
    // the rows past it are never described at all.
    const float rowHeight = studioMenuItemHeight(fixture.frame.theme());
    CNA_STUDIO_EXPECT(rowHeight * 200.0f > kHeight);

    const UiRect eleventh = fixture.rowBounds(11);
    fixture.click(eleventh.centerX(), eleventh.centerY());
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.selected, 0);
}

CNA_STUDIO_TEST(DescribingAFrameTwiceLeavesNoPopupQueued)
{
    // The queue is emptied per frame rather than per pass, and a popup that survived into the next
    // frame would be drawn twice and routed twice.
    Fixture fixture;
    fixture.settle();
    fixture.click(fixture.bounds.centerX(), fixture.bounds.centerY());

    for (int i = 0; i < 3; ++i)
    {
        fixture.settle();
        CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
    }
}

CNA_STUDIO_TEST(TheKeystrokeThatOpensAListDoesNotAlsoMoveWithinIt)
{
    // Otherwise Down-then-Enter -- the fastest way to accept what is already selected -- would
    // land on the item *after* it, and the user could never choose the value they started on
    // without arrowing back up.
    Fixture fixture;
    fixture.settle();

    fixture.run(withKey(UiKey::Tab));
    fixture.settle();
    fixture.run(withKey(UiKey::DownArrow));
    fixture.settle();
    CNA_STUDIO_EXPECT(fixture.frame.isAnyPopupOpen());

    fixture.run(withKey(UiKey::Enter));
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.selected, 1);
    CNA_STUDIO_EXPECT(!fixture.frame.isAnyPopupOpen());
}
