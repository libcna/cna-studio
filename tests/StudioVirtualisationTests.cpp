// SPDX-License-Identifier: MS-PL
/**
 * @file StudioVirtualisationTests.cpp
 * @brief The windows a scrolling surface builds its model from (`plan.md` STUDIO-30010).
 *
 * The property these exist to protect is the one that does not show up in a screenshot: a surface
 * holding a hundred thousand items must cost what one holding forty costs. Culling *inside* the
 * loop looks the same on screen and is not the same thing — it stops the drawing and still walks
 * every item, and it cannot stop the model being built in the first place, which is the expensive
 * half.
 *
 * So the windows are arithmetic over a viewport and an item count, answerable before any item
 * exists, and they are tested as arithmetic rather than through a frame.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cstddef>

using namespace CNA::Studio;

namespace
{
    /** @brief A scroll result standing in for one `studioBeginScroll` would return. */
    StudioScrollResult viewOf(float width, float height, float offsetY)
    {
        StudioScrollResult view;
        view.viewport = UiRect{0.0f, 0.0f, width, height};
        view.offsetY = offsetY;
        return view;
    }
}

CNA_STUDIO_TEST(AListsWindowIsTheRowsOnScreenPlusOneEachSide)
{
    // The slack is not decoration: without it the edge row pops in and out as the offset crosses a
    // row boundary, which reads as the list flickering.
    const StudioScrollResult view = viewOf(200.0f, 100.0f, 0.0f);

    std::size_t first = 0;
    std::size_t last = 0;
    view.visibleRows(10.0f, 1000, first, last);

    CNA_STUDIO_EXPECT_EQ(first, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(last, std::size_t{12});

    // Scrolled: the window moves rather than growing.
    const StudioScrollResult scrolled = viewOf(200.0f, 100.0f, 500.0f);
    scrolled.visibleRows(10.0f, 1000, first, last);
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{50});
    CNA_STUDIO_EXPECT_EQ(last, std::size_t{62});

    // A hundred thousand rows cost the same as a hundred, which is the whole point.
    scrolled.visibleRows(10.0f, 100000, first, last);
    CNA_STUDIO_EXPECT_EQ(last - first, std::size_t{12});

    // Past the end, and empty, are answers rather than crashes.
    viewOf(200.0f, 100.0f, 100000.0f).visibleRows(10.0f, 10, first, last);
    CNA_STUDIO_EXPECT_EQ(first, last);
    view.visibleRows(10.0f, 0, first, last);
    CNA_STUDIO_EXPECT_EQ(first, last);
    view.visibleRows(0.0f, 10, first, last);
    CNA_STUDIO_EXPECT_EQ(first, last);
}

CNA_STUDIO_TEST(AGridsColumnCountIsOneFunctionRatherThanTwoAgreeingByLuck)
{
    // The extent and the layout are computed in different places, and a grid whose two ideas of the
    // column count disagreed would scroll past its own last row.
    CNA_STUDIO_EXPECT_EQ(studioGridColumns(420.0f, 100.0f, 10.0f), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(studioGridColumns(440.0f, 100.0f, 10.0f), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(studioGridColumns(450.0f, 100.0f, 10.0f), std::size_t{4});

    // Floored at one: a panel narrower than a card still has to show it, clipped, rather than
    // dividing by zero and drawing nothing.
    CNA_STUDIO_EXPECT_EQ(studioGridColumns(30.0f, 100.0f, 10.0f), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(studioGridColumns(0.0f, 100.0f, 10.0f), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(studioGridColumns(420.0f, 0.0f, 10.0f), std::size_t{1});
}

CNA_STUDIO_TEST(AGridsExtentMatchesTheRowsItsWindowWillProduce)
{
    // Asserted together rather than separately, because the failure they guard against is the two
    // disagreeing: an extent that says ten rows and a layout that lays out eleven scrolls the last
    // one somewhere nobody can reach.
    constexpr float kCell = 100.0f;
    constexpr float kSpacing = 10.0f;
    constexpr float kWidth = 450.0f;   // four columns

    // Nine items over four columns is three rows.
    const float height = studioGridContentHeight(kWidth, kCell, kCell, kSpacing, 9);
    CNA_STUDIO_EXPECT_EQ(height, 3.0f * (kCell + kSpacing) + kSpacing);

    // Nothing to show has no extent at all, rather than one margin's worth of empty scroll.
    CNA_STUDIO_EXPECT_EQ(studioGridContentHeight(kWidth, kCell, kCell, kSpacing, 0), 0.0f);

    std::size_t columns = 0;
    std::size_t first = 0;
    std::size_t last = 0;

    // A viewport tall enough for two rows, at the top.
    viewOf(kWidth, 2.0f * (kCell + kSpacing), 0.0f)
        .visibleCells(kCell, kCell, kSpacing, 9, columns, first, last);

    CNA_STUDIO_EXPECT_EQ(columns, std::size_t{4});
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{0});

    // Two rows on screen plus one of slack each side, clamped to what there is.
    CNA_STUDIO_EXPECT_EQ(last, std::size_t{9});
}

CNA_STUDIO_TEST(AGridsWindowMovesWithTheScrollAndStaysTheSameSize)
{
    constexpr float kCell = 100.0f;
    constexpr float kSpacing = 10.0f;
    constexpr float kWidth = 450.0f;   // four columns
    constexpr std::size_t kItems = 100000;

    std::size_t columns = 0;
    std::size_t first = 0;
    std::size_t last = 0;

    // Two rows' worth of viewport, scrolled ten rows down.
    viewOf(kWidth, 2.0f * (kCell + kSpacing), 10.0f * (kCell + kSpacing) + kSpacing)
        .visibleCells(kCell, kCell, kSpacing, kItems, columns, first, last);

    CNA_STUDIO_EXPECT_EQ(columns, std::size_t{4});
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{40});

    // Four rows of items -- two on screen and one of slack each side -- whatever the item count.
    CNA_STUDIO_EXPECT_EQ(last - first, std::size_t{16});

    std::size_t smallColumns = 0;
    std::size_t smallFirst = 0;
    std::size_t smallLast = 0;
    viewOf(kWidth, 2.0f * (kCell + kSpacing), 10.0f * (kCell + kSpacing) + kSpacing)
        .visibleCells(kCell, kCell, kSpacing, 200, smallColumns, smallFirst, smallLast);
    CNA_STUDIO_EXPECT_EQ(smallLast - smallFirst, last - first);
}

CNA_STUDIO_TEST(ATreesWindowIsAnsweredBeforeItsRowsExist)
{
    // The tree's counterpart of the grid's window, and the reason it is a separate call rather
    // than something `studioTreeView` does on its own: a caller that wants to build only the
    // window has to be told which rows those are *before* it builds any, and the only way to know
    // is to have opened the scroll region first.
    const StudioTheme theme = StudioTheme::dark();
    const float rowHeight = studioTreeRowHeight(theme);
    CNA_STUDIO_EXPECT(rowHeight > 0.0f);

    // Nothing to show is an empty window, not a first row of zero and a count of one.
    const StudioTreeWindow none = studioTreeWindow(viewOf(200.0f, 100.0f, 0.0f), 0, theme);
    CNA_STUDIO_EXPECT(!none.isWindowed());
    CNA_STUDIO_EXPECT_EQ(none.rowCount, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(none.firstRow, std::size_t{0});

    // Ten rows' worth of viewport, at the top: ten on screen plus one of slack each side. The
    // half-row of viewport keeps the arithmetic off a boundary, so the test asserts the rule
    // rather than a rounding mode.
    const StudioTreeWindow top =
        studioTreeWindow(viewOf(200.0f, 9.5f * rowHeight, 0.0f), 100000, theme);
    CNA_STUDIO_EXPECT(top.isWindowed());
    CNA_STUDIO_EXPECT_EQ(top.totalRows, std::size_t{100000});
    CNA_STUDIO_EXPECT_EQ(top.firstRow, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(top.rowCount, std::size_t{12});

    // Scrolled: the window moves rather than growing, and a hundred thousand rows cost what a
    // hundred cost -- which is the whole of STUDIO-09016 in one assertion.
    const StudioTreeWindow scrolled =
        studioTreeWindow(viewOf(200.0f, 9.5f * rowHeight, 500.5f * rowHeight), 100000, theme);
    CNA_STUDIO_EXPECT_EQ(scrolled.firstRow, std::size_t{500});
    CNA_STUDIO_EXPECT_EQ(scrolled.rowCount, top.rowCount);

    const StudioTreeWindow small =
        studioTreeWindow(viewOf(200.0f, 9.5f * rowHeight, 500.5f * rowHeight), 600, theme);
    CNA_STUDIO_EXPECT_EQ(small.rowCount, top.rowCount);

    // Past the end is an empty window at the end rather than one that wraps round.
    const StudioTreeWindow past =
        studioTreeWindow(viewOf(200.0f, 9.5f * rowHeight, 5000.0f * rowHeight), 10, theme);
    CNA_STUDIO_EXPECT_EQ(past.rowCount, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(past.firstRow, std::size_t{10});
}

CNA_STUDIO_TEST(AGridsWindowAnswersTheAwkwardCasesRatherThanCrashing)
{
    std::size_t columns = 0;
    std::size_t first = 0;
    std::size_t last = 0;

    // Nothing to show.
    viewOf(450.0f, 200.0f, 0.0f).visibleCells(100.0f, 100.0f, 10.0f, 0, columns, first, last);
    CNA_STUDIO_EXPECT_EQ(first, last);
    CNA_STUDIO_EXPECT(columns >= 1);

    // A cell with no height.
    viewOf(450.0f, 200.0f, 0.0f).visibleCells(100.0f, 0.0f, 10.0f, 50, columns, first, last);
    CNA_STUDIO_EXPECT_EQ(first, last);

    // Scrolled past the end: an empty window at the end rather than one that wraps round.
    viewOf(450.0f, 200.0f, 1000000.0f)
        .visibleCells(100.0f, 100.0f, 10.0f, 8, columns, first, last);
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{8});
    CNA_STUDIO_EXPECT_EQ(last, std::size_t{8});

    // A negative offset -- which an over-scroll can produce for a frame -- clamps to the top rather
    // than indexing backwards.
    viewOf(450.0f, 200.0f, -50.0f).visibleCells(100.0f, 100.0f, 10.0f, 50, columns, first, last);
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{0});
    CNA_STUDIO_EXPECT(last > 0);
}
