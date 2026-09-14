// SPDX-License-Identifier: MS-PL
/**
 * @file StudioLogPanelTests.cpp
 * @brief The first panel ported off Dear ImGui, and the machinery it needed (plan.md STUDIO-07005).
 *
 * Three things are under test here, because porting the first panel is what forced all three into
 * existence: a log model no UI owns, a scrolling region, and the shell's seam for panel content.
 * Each is checked on its own before the panel that uses them, so a failure says which one broke.
 *
 * The cases drive the real widgets through both frame passes and assert on behaviour, not pixels.
 * "Does clicking Errors hide the info lines" and "does a hundred-thousand-line log still describe
 * only a screenful" are the questions that decide whether this panel is usable; a golden image
 * answers neither.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioLogPanel.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"
#include "CNA/Studio/Ui/StudioUi.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 1280.0f;
    constexpr float kHeight = 720.0f;

    /** @brief How much geometry a draw produced, across every list. */
    std::size_t vertexCount(const UiDrawData& data)
    {
        std::size_t total = 0;
        for (const UiDrawList& list : data.lists) { total += list.vertices.size(); }
        return total;
    }

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A shell with its default workspace and the Output Log raised. */
    std::unique_ptr<StudioShell> shellShowingTheLog()
    {
        auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
        shell->resetLayout();
        shell->renderFrame(at(-1.0f, -1.0f));
        CNA_STUDIO_EXPECT(shell->activatePanel("output"));
        return shell;
    }
}

// -------------------------------------------------------------------------------------------
// The log model
// -------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ARepeatedMessageIsOneEntryWithACount)
{
    // Four hundred identical lines is one thing that happened four hundred times. Collapsing keeps
    // the interesting lines on screen, which is the entire purpose of a console.
    StudioLog log;
    for (int i = 0; i < 400; ++i) { log.append(LogSeverity::Warning, "texture not found"); }

    CNA_STUDIO_EXPECT_EQ(log.entries().size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(log.entries().front().repeats, std::size_t{400});

    // And nothing is hidden: the count reaches the clipboard too.
    CNA_STUDIO_EXPECT(log.toText().find("(x400)") != std::string::npos);

    // Only *consecutive* repeats collapse. Two failures with something in between are two events,
    // and merging them would misreport the order things happened in.
    log.append(LogSeverity::Info, "loaded scene");
    log.append(LogSeverity::Warning, "texture not found");
    CNA_STUDIO_EXPECT_EQ(log.entries().size(), std::size_t{3});
}

CNA_STUDIO_TEST(TheLogIsBoundedAndSaysHowMuchItDropped)
{
    // An editor left open for a day with a noisy import can produce millions of lines. A log that
    // grew without limit would turn that into an out-of-memory crash that loses the user's scene.
    StudioLog log{16};
    for (int i = 0; i < 100; ++i) { log.append(LogSeverity::Info, "line " + std::to_string(i)); }

    CNA_STUDIO_EXPECT_EQ(log.entries().size(), std::size_t{16});
    CNA_STUDIO_EXPECT_EQ(log.droppedCount(), std::size_t{84});

    // The *most recent* lines are the ones kept: a console that discarded the newest messages to
    // preserve the oldest would be exactly backwards.
    CNA_STUDIO_EXPECT_EQ(log.entries().back().message, std::string{"line 99"});
    CNA_STUDIO_EXPECT_EQ(log.entries().front().message, std::string{"line 84"});

    // Clearing resets the dropped count as well: the history the user is now looking at is
    // complete, and saying otherwise would be false.
    log.clear();
    CNA_STUDIO_EXPECT_EQ(log.droppedCount(), std::size_t{0});
}

CNA_STUDIO_TEST(FilteringCountsAndTextAgreeWithEachOther)
{
    StudioLog log;
    log.append(LogSeverity::Trace, "a");
    log.append(LogSeverity::Info, "b");
    log.append(LogSeverity::Warning, "c");
    log.append(LogSeverity::Error, "d");

    CNA_STUDIO_EXPECT_EQ(log.countAtLeast(LogSeverity::Trace), std::size_t{4});
    CNA_STUDIO_EXPECT_EQ(log.countAtLeast(LogSeverity::Warning), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(log.countAtLeast(LogSeverity::Error), std::size_t{1});

    // Text pasted into a bug report must say what the panel said. A Copy that ignored the filter
    // would attach a hundred trace lines to a report about one error.
    const std::string errorsOnly = log.toText(LogSeverity::Error);
    CNA_STUDIO_EXPECT(errorsOnly.find('d') != std::string::npos);
    CNA_STUDIO_EXPECT(errorsOnly.find('a') == std::string::npos);
    CNA_STUDIO_EXPECT(errorsOnly.find('c') == std::string::npos);
}

// -------------------------------------------------------------------------------------------
// The panel
// -------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheOutputLogDrawsItsMessagesThroughTheShell)
{
    StudioLog log;
    log.append(LogSeverity::Info, "the scene loaded");
    log.append(LogSeverity::Error, "a shader would not compile");

    const std::unique_ptr<StudioShell> shell = shellShowingTheLog();

    bool drew = false;
    CNA_STUDIO_EXPECT(shell->setPanelContent("output",
        [&](StudioFrame& frame, const UiRect& bounds) {
            drew = true;
            CNA_STUDIO_EXPECT(bounds.width > 0.0f);
            CNA_STUDIO_EXPECT(bounds.height > 0.0f);
            (void)studioLogPanel(frame, bounds, log);
        }));

    const std::size_t before = vertexCount(shell->drawData());
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(drew);
    // Geometry, not a flag: a content function that ran and drew nothing would set `drew` just as
    // happily as one that drew the log.
    CNA_STUDIO_EXPECT(vertexCount(shell->drawData()) > before);
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(APanelWithNoContentCostsNothingAndDrawsAnEmptySurface)
{
    // Every panel starts here, and most of them stay here until Phase 7 reaches them. An unported
    // panel must not be a broken one.
    const std::unique_ptr<StudioShell> shell = shellShowingTheLog();
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(vertexCount(shell->drawData()) > 0);
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});

    // Content can be given and taken away again without the shell minding.
    CNA_STUDIO_EXPECT(shell->setPanelContent("output", {}));
    CNA_STUDIO_EXPECT(!shell->setPanelContent("no.such.panel",
        [](StudioFrame&, const UiRect&) {}));
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(ClearingTheLogIsReportedRatherThanDoneBehindTheOwnersBack)
{
    // The panel is handed a const log. Clearing is the owner's decision because the owner may be
    // writing to it from somewhere the panel knows nothing about.
    StudioLog log;
    log.append(LogSeverity::Info, "something happened");

    const std::unique_ptr<StudioShell> shell = shellShowingTheLog();

    UiRect clearBounds;
    CNA_STUDIO_EXPECT(shell->setPanelContent("output",
        [&](StudioFrame& frame, const UiRect& bounds) {
            const StudioLogPanelResult result = studioLogPanel(frame, bounds, log);
            if (result.cleared) { log.clear(); }
            if (frame.isDrawPass()) { clearBounds = bounds; }
        }));

    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(!clearBounds.isEmpty());
    CNA_STUDIO_EXPECT_EQ(log.entries().size(), std::size_t{1});

    // Clear sits second in the toolbar, after Copy. Found by walking the top row rather than by
    // hard-coding a pixel, so a spacing change does not silently make this test press nothing.
    bool clearedIt = false;
    for (float x = clearBounds.left(); x < clearBounds.left() + 260.0f && !clearedIt; x += 4.0f)
    {
        const float y = clearBounds.top() + 12.0f;
        shell->renderFrame(at(x, y, true));
        shell->renderFrame(at(x, y, false));
        clearedIt = log.entries().empty();
    }
    CNA_STUDIO_EXPECT(clearedIt);
}

CNA_STUDIO_TEST(TheSeverityOfALineIsVisibleWithoutReadingIt)
{
    // Colour is the only thing that separates an error from a trace at a glance, and a console
    // where they look the same is a console nobody scans.
    const StudioTheme dark = StudioTheme::dark();

    const StudioColor error = studioLogSeverityColor(dark, LogSeverity::Error);
    const StudioColor warning = studioLogSeverityColor(dark, LogSeverity::Warning);
    const StudioColor info = studioLogSeverityColor(dark, LogSeverity::Info);
    const StudioColor trace = studioLogSeverityColor(dark, LogSeverity::Trace);

    const auto differs = [](const StudioColor& a, const StudioColor& b) {
        return a.r != b.r || a.g != b.g || a.b != b.b;
    };

    CNA_STUDIO_EXPECT(differs(error, info));
    CNA_STUDIO_EXPECT(differs(warning, info));
    CNA_STUDIO_EXPECT(differs(error, warning));
    CNA_STUDIO_EXPECT(differs(trace, info));

    // And the same holds in the light theme, which is where a colour chosen by eye on a dark
    // background usually stops working.
    const StudioTheme light = StudioTheme::light();
    CNA_STUDIO_EXPECT(differs(studioLogSeverityColor(light, LogSeverity::Error),
                              studioLogSeverityColor(light, LogSeverity::Info)));
    CNA_STUDIO_EXPECT(differs(studioLogSeverityColor(light, LogSeverity::Warning),
                              studioLogSeverityColor(light, LogSeverity::Info)));
}

CNA_STUDIO_TEST(AHugeLogCostsTheSameAsASmallOne)
{
    // The property that decides whether this panel is usable at all. A console that laid out every
    // line it holds would stall the editor the moment an import went wrong, which is precisely when
    // somebody is reading it.
    StudioLog big{200000};
    for (int i = 0; i < 100000; ++i)
    {
        big.append(LogSeverity::Info, "line " + std::to_string(i));
    }

    StudioLog small;
    small.append(LogSeverity::Info, "line 0");

    const auto verticesFor = [](const StudioLog& log) {
        auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
        shell->resetLayout();
        shell->renderFrame(at(-1.0f, -1.0f));
        (void)shell->activatePanel("output");
        (void)shell->setPanelContent("output",
            [&log](StudioFrame& frame, const UiRect& bounds) {
                (void)studioLogPanel(frame, bounds, log);
            });
        shell->renderFrame(at(-1.0f, -1.0f));
        return vertexCount(shell->drawData());
    };

    const std::size_t hundredThousand = verticesFor(big);
    const std::size_t one = verticesFor(small);

    CNA_STUDIO_EXPECT(hundredThousand > one);

    // Bounded by the screen, not by the log. Ten times the geometry of a one-line log is already
    // generous for a panel a few dozen rows tall; a hundred thousand times would be the bug.
    CNA_STUDIO_EXPECT(hundredThousand < one * 40);
}

// -------------------------------------------------------------------------------------------
// Scrolling
// -------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AScrollViewOnlyGrowsABarWhenTheContentDoesNotFit)
{
    StudioFrame frame{StudioTheme::dark()};

    const UiRect bounds{0.0f, 0.0f, 400.0f, 200.0f};
    const auto run = [&](float contentHeight) {
        StudioScrollResult result;
        runStudioFrame(frame, at(-1.0f, -1.0f), [&](StudioFrame& f) {
            StudioScrollOptions options;
            options.contentHeight = contentHeight;
            result = studioBeginScroll(f, f.ids().make("view"), bounds, options);
            studioEndScroll(f);
        });
        return result;
    };

    const StudioScrollResult fits = run(100.0f);
    CNA_STUDIO_EXPECT(!fits.hasVerticalBar);
    // The whole width goes to the content: a scrollbar gutter reserved for a bar that is not there
    // is a column of wasted space on every panel that happens to fit.
    CNA_STUDIO_EXPECT_EQ(fits.viewport.width, bounds.width);
    CNA_STUDIO_EXPECT_EQ(fits.offsetY, 0.0f);

    const StudioScrollResult overflows = run(1000.0f);
    CNA_STUDIO_EXPECT(overflows.hasVerticalBar);
    CNA_STUDIO_EXPECT(overflows.viewport.width < bounds.width);
}

CNA_STUDIO_TEST(TheWheelScrollsAndTheViewStopsAtBothEnds)
{
    StudioFrame frame{StudioTheme::dark()};
    const UiRect bounds{0.0f, 0.0f, 400.0f, 200.0f};

    const auto turn = [&](float wheel) {
        UiInputState input = at(bounds.centerX(), bounds.centerY());
        input.wheelY = wheel;

        StudioScrollResult result;
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            StudioScrollOptions options;
            options.contentHeight = 1000.0f;
            result = studioBeginScroll(f, f.ids().make("view"), bounds, options);
            studioEndScroll(f);
        });
        return result;
    };

    CNA_STUDIO_EXPECT_EQ(turn(0.0f).offsetY, 0.0f);

    const float afterOneNotch = turn(-1.0f).offsetY;
    CNA_STUDIO_EXPECT(afterOneNotch > 0.0f);

    // Rolling to the end stops there rather than scrolling into empty space below the content.
    for (int i = 0; i < 50; ++i) { (void)turn(-1.0f); }
    const StudioScrollResult atEnd = turn(0.0f);
    CNA_STUDIO_EXPECT(atEnd.atEnd);
    CNA_STUDIO_EXPECT_EQ(atEnd.offsetY, 1000.0f - bounds.height);

    // And back, stopping at zero rather than at a negative offset.
    for (int i = 0; i < 100; ++i) { (void)turn(1.0f); }
    CNA_STUDIO_EXPECT_EQ(turn(0.0f).offsetY, 0.0f);
}

CNA_STUDIO_TEST(FollowingNewOutputStopsTheMomentTheUserScrollsAway)
{
    // The single most common complaint about log windows: one that yanks the view back to the
    // bottom while somebody is reading further up. "Auto-scroll" has never meant "take the
    // scrollbar away from me".
    StudioFrame frame{StudioTheme::dark()};
    const UiRect bounds{0.0f, 0.0f, 400.0f, 200.0f};

    float contentHeight = 400.0f;
    const auto grow = [&](float wheel) {
        UiInputState input = at(bounds.centerX(), bounds.centerY());
        input.wheelY = wheel;

        StudioScrollResult result;
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            StudioScrollOptions options;
            options.contentHeight = contentHeight;
            options.stickToEnd = true;
            result = studioBeginScroll(f, f.ids().make("view"), bounds, options);
            studioEndScroll(f);
        });
        contentHeight += 100.0f;
        return result;
    };

    // Starting at the end, growth keeps it there.
    (void)grow(0.0f);
    for (int i = 0; i < 5; ++i)
    {
        CNA_STUDIO_EXPECT(grow(0.0f).atEnd);
    }

    // Scroll up, and growth must leave the view where the reader put it.
    const float parked = grow(3.0f).offsetY;
    CNA_STUDIO_EXPECT(!grow(0.0f).atEnd);
    CNA_STUDIO_EXPECT_EQ(grow(0.0f).offsetY, parked);
}

CNA_STUDIO_TEST(OnlyTheRowsOnScreenAreWorthDescribing)
{
    StudioScrollResult result;
    result.viewport = UiRect{0.0f, 0.0f, 400.0f, 200.0f};
    result.offsetY = 0.0f;

    std::size_t first = 0;
    std::size_t last = 0;

    result.visibleRows(20.0f, 100000, first, last);
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{0});
    // Ten rows of viewport plus a row of slack at each end, so an edge row half out of view is
    // still described and the list does not pop as the offset crosses a boundary.
    CNA_STUDIO_EXPECT_EQ(last, std::size_t{12});

    result.offsetY = 1000.0f;
    result.visibleRows(20.0f, 100000, first, last);
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{50});
    CNA_STUDIO_EXPECT_EQ(last, std::size_t{62});

    // Past the end, and an empty list, must both come back empty rather than wrapping around.
    result.offsetY = 100000.0f * 20.0f;
    result.visibleRows(20.0f, 100000, first, last);
    CNA_STUDIO_EXPECT_EQ(first, last);

    result.offsetY = 0.0f;
    result.visibleRows(20.0f, 0, first, last);
    CNA_STUDIO_EXPECT_EQ(first, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(last, std::size_t{0});
}

// -------------------------------------------------------------------------------------------
// The migration seam
// -------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(BothConsolesReadOneLog)
{
    // The property that makes this a *port* rather than a second console showing something else.
    // Two logs would make the migration impossible to check: every difference between the panels
    // would be a difference in what was logged rather than in how it was drawn, and nobody could
    // tell a faithful port from a plausible-looking one.
    NullStudioUi legacy;
    legacy.log(LogSeverity::Info, "the scene loaded");
    legacy.log(LogSeverity::Error, "a shader would not compile");

    // What the legacy UI collected is exactly what the Studio panel is handed.
    const StudioLog& shared = legacy.getLogModel();
    CNA_STUDIO_EXPECT_EQ(shared.entries().size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(shared.countAtLeast(LogSeverity::Error), std::size_t{1});

    const std::unique_ptr<StudioShell> shell = shellShowingTheLog();
    std::size_t rowsSeen = 0;
    CNA_STUDIO_EXPECT(shell->setPanelContent("output",
        [&](StudioFrame& frame, const UiRect& bounds) {
            (void)studioLogPanel(frame, bounds, shared);
            if (frame.isDrawPass()) { rowsSeen = shared.entries().size(); }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT_EQ(rowsSeen, std::size_t{2});

    // And clearing through either reaches both, because there is only one.
    legacy.clearLog();
    CNA_STUDIO_EXPECT(shared.entries().empty());

    // The legacy console's own accessors keep working on it, unchanged: the ImGui panels are a
    // compatibility fallback until they are deleted, not something to break on the way past.
    CNA_STUDIO_EXPECT(legacy.getLog().empty());
    legacy.log(LogSeverity::Warning, "still here");
    CNA_STUDIO_EXPECT_EQ(legacy.getLog().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(legacy.getLogText().find("still here") != std::string::npos);
}
