// SPDX-License-Identifier: MS-PL
/**
 * @file StudioNotificationTests.cpp
 * @brief Background results the user was not looking at (plan.md STUDIO-06014).
 *
 * The interesting cases are all about *time* and *absence*, which is why the model is separate from
 * the widget: a toast that expires too early, a toast that never expires, a toast that vanished
 * while the pointer was travelling to its button, and a burst that covers the editor with the thing
 * it is reporting about. None of those can be driven from a frame alone, and all of them are the
 * difference between an editor that tells you things and one that interrupts you.
 *
 * The stack itself is driven through the real shell, because the two things that can silently go
 * wrong there are geometry (a button off the bottom of the window) and the input layer (a Dismiss
 * the user can see and cannot press, because a floating window is underneath it).
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioNotifications.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 1280.0f;
    constexpr float kHeight = 760.0f;

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

    UiInputState away()
    {
        UiInputState input = at(-1.0f, -1.0f);
        input.mouseInWindow = false;
        return input;
    }

    StudioNotification named(std::string id, StudioNotificationSeverity severity,
                             std::string title)
    {
        StudioNotification notification;
        notification.id = std::move(id);
        notification.severity = severity;
        notification.title = std::move(title);
        return notification;
    }

    struct ShellFixture
    {
        StudioContext context;
        StudioLog log;
        std::unique_ptr<StudioShell> shell = std::make_unique<StudioShell>(StudioTheme::dark());

        ShellFixture()
        {
            shell->resetLayout();
            (void)bindStudioShellActions(*shell, context, log);
            shell->notifications().setLog(&log);
            shell->renderFrame(away());
        }

        void click(float x, float y)
        {
            shell->renderFrame(at(x, y));
            shell->renderFrame(at(x, y, /*leftDown=*/true));
            shell->renderFrame(at(x, y));
        }
    };

    bool logContains(const StudioLog& log, std::string_view needle)
    {
        for (const StudioLogEntry& entry : log.entries())
        {
            if (entry.message.find(needle) != std::string::npos) { return true; }
        }
        return false;
    }
}

// --- What is showing, and for how long -------------------------------------------------------

CNA_STUDIO_TEST(AnOrdinaryResultCountsDownAndGoes)
{
    StudioNotificationCenter centre;
    centre.post(StudioNotificationSeverity::Success, "Exported");

    CNA_STUDIO_EXPECT(centre.posted().size() == 1);
    CNA_STUDIO_EXPECT_EQ(centre.tick(StudioNotificationCenter::kShortSeconds * 0.5), std::size_t{0});
    CNA_STUDIO_EXPECT(centre.posted().size() == 1);
    CNA_STUDIO_EXPECT_EQ(centre.tick(StudioNotificationCenter::kShortSeconds), std::size_t{1});
    CNA_STUDIO_EXPECT(centre.empty());
}

CNA_STUDIO_TEST(AFailureStaysUntilItIsDismissed)
{
    // The whole reason to raise a failure is that nobody was watching. One that waited four seconds
    // and left is a failure the user meets again by other means, later, with less context.
    StudioNotificationCenter centre;
    centre.post(named("studio.build", StudioNotificationSeverity::Error, "Build failed"));

    centre.tick(60.0 * 60.0);
    CNA_STUDIO_EXPECT(centre.posted().size() == 1);
    CNA_STUDIO_EXPECT(centre.posted().front().isSticky());

    CNA_STUDIO_EXPECT(centre.dismiss("studio.build"));
    CNA_STUDIO_EXPECT(centre.empty());
    CNA_STUDIO_EXPECT(!centre.dismiss("studio.build"));
}

CNA_STUDIO_TEST(TheCountdownDoesNotRunWhileTheUserIsReading)
{
    // Zero is how the shell says "the pointer is over the stack". A toast that disappeared while it
    // was being read, or while the pointer was travelling to its button, is worse than one that
    // never appeared -- the button is the reason a failure is announced rather than logged.
    StudioNotificationCenter centre;
    centre.post(StudioNotificationSeverity::Info, "Saved");

    for (int i = 0; i < 600; ++i) { centre.tick(0.0); }
    CNA_STUDIO_EXPECT(centre.posted().size() == 1);

    centre.tick(StudioNotificationCenter::kShortSeconds + 1.0);
    CNA_STUDIO_EXPECT(centre.empty());
}

CNA_STUDIO_TEST(AWarningStaysLongerThanAnInfoAndStillGoes)
{
    CNA_STUDIO_EXPECT(studioNotificationLifetime(StudioNotificationSeverity::Warning)
                      > studioNotificationLifetime(StudioNotificationSeverity::Info));
    CNA_STUDIO_EXPECT(studioNotificationLifetime(StudioNotificationSeverity::Warning) > 0.0f);
    CNA_STUDIO_EXPECT(studioNotificationLifetime(StudioNotificationSeverity::Error) < 0.0f);
}

CNA_STUDIO_TEST(RepostingAnIdReplacesItWhereItStands)
{
    // Ten saves that each fail to write is one toast saying so. Ten toasts saying so is a stack
    // that covers the editor with one fact -- and one that jumped to the bottom each time would
    // move out from under the pointer reaching for its button.
    StudioNotificationCenter centre;
    centre.post(named("a", StudioNotificationSeverity::Error, "First"));
    centre.post(named("b", StudioNotificationSeverity::Info, "Second"));
    centre.post(named("a", StudioNotificationSeverity::Error, "First, again"));

    CNA_STUDIO_EXPECT(centre.posted().size() == 2);
    CNA_STUDIO_EXPECT(centre.posted()[0].title == "First, again");
    CNA_STUDIO_EXPECT(centre.posted()[1].title == "Second");
}

CNA_STUDIO_TEST(AnUnnamedNotificationIsAlwaysItsOwn)
{
    // Deduplication needs a name. Two unrelated messages that happened to arrive together must not
    // collapse into one just because neither was given an id.
    StudioNotificationCenter centre;
    centre.post(StudioNotificationSeverity::Info, "One");
    centre.post(StudioNotificationSeverity::Info, "Two");
    CNA_STUDIO_EXPECT(centre.posted().size() == 2);
}

CNA_STUDIO_TEST(TheStackIsCappedAndSaysHowManyItIsNotShowing)
{
    StudioNotificationCenter centre;
    for (std::size_t i = 0; i < StudioNotificationCenter::kMaxVisible + 3; ++i)
    {
        centre.post(named("n" + std::to_string(i), StudioNotificationSeverity::Error,
                          "Failure " + std::to_string(i)));
    }

    CNA_STUDIO_EXPECT(centre.showing().size() == StudioNotificationCenter::kMaxVisible);
    CNA_STUDIO_EXPECT_EQ(centre.hiddenCount(), std::size_t{3});

    // The newest are the ones shown, errors included: they are all in the log and in the history,
    // and a stack that could grow without bound would cover the editor it is reporting about.
    CNA_STUDIO_EXPECT(centre.showing().back().title == "Failure 6");
}

CNA_STUDIO_TEST(DismissingByPositionRemovesTheOneThatWasDrawn)
{
    // The shell indexes into what it drew, which is the tail of what is posted. Getting this wrong
    // dismisses a different toast from the one whose button was pressed -- and looks, from the
    // outside, exactly like a button that does nothing.
    StudioNotificationCenter centre;
    for (std::size_t i = 0; i < StudioNotificationCenter::kMaxVisible + 2; ++i)
    {
        centre.post(named("n" + std::to_string(i), StudioNotificationSeverity::Error,
                          "Failure " + std::to_string(i)));
    }

    const std::vector<StudioNotification> drawn = centre.showing();
    CNA_STUDIO_EXPECT(centre.dismissAt(0));
    CNA_STUDIO_EXPECT(centre.showing().size() == StudioNotificationCenter::kMaxVisible);

    for (const StudioNotification& remaining : centre.posted())
    {
        CNA_STUDIO_EXPECT(remaining.id != drawn.front().id);
    }
    CNA_STUDIO_EXPECT(!centre.dismissAt(StudioNotificationCenter::kMaxVisible));
}

CNA_STUDIO_TEST(EveryNotificationIsWrittenToTheLog)
{
    // A toast is ephemeral by design, which makes it the wrong place to keep anything. Without this
    // "it told me and I missed it" has nowhere to be answered.
    StudioLog log;
    StudioNotificationCenter centre;
    centre.setLog(&log);

    centre.post(named("studio.build", StudioNotificationSeverity::Error, "Build failed"));
    centre.dismissAll();

    CNA_STUDIO_EXPECT(centre.empty());
    CNA_STUDIO_EXPECT(logContains(log, "Build failed"));
    CNA_STUDIO_EXPECT(log.entries().back().severity == LogSeverity::Error);

    // And the history outlives the toast, which is what the "and N more" line points at.
    CNA_STUDIO_EXPECT(centre.history().size() == 1);
}

CNA_STUDIO_TEST(TheDetailIsLoggedWithItsTitleRatherThanAsASecondLine)
{
    // One entry, because the log collapses repeats: two would collapse the title and leave a column
    // of details with nothing saying what they are about.
    StudioLog log;
    StudioNotificationCenter centre;
    centre.setLog(&log);
    centre.post(StudioNotificationSeverity::Error, "Build failed", "3 errors in Level01.cpp");

    CNA_STUDIO_EXPECT(log.entries().size() == 1);
    CNA_STUDIO_EXPECT(logContains(log, "Build failed"));
    CNA_STUDIO_EXPECT(logContains(log, "3 errors in Level01.cpp"));
}

// --- The stack on the shell --------------------------------------------------------------------

CNA_STUDIO_TEST(TheStackIsLaidOutInsideTheWindow)
{
    ShellFixture fixture;
    for (int i = 0; i < 3; ++i)
    {
        fixture.shell->notifications().post(
            named("n" + std::to_string(i), StudioNotificationSeverity::Error,
                  "Something went wrong " + std::to_string(i)));
    }
    fixture.shell->renderFrame(away());

    const UiRect bounds = fixture.shell->notificationBounds();
    CNA_STUDIO_EXPECT(!bounds.isEmpty());
    CNA_STUDIO_EXPECT(bounds.left() >= 0.0f);
    CNA_STUDIO_EXPECT(bounds.right() <= kWidth);
    CNA_STUDIO_EXPECT(bounds.top() >= 0.0f);
    CNA_STUDIO_EXPECT(bounds.bottom() <= kHeight);

    CNA_STUDIO_EXPECT(validate(fixture.shell->drawData()).valid);
    CNA_STUDIO_EXPECT_EQ(fixture.shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(NothingIsLaidOutWhenNothingIsShowing)
{
    ShellFixture fixture;
    CNA_STUDIO_EXPECT(fixture.shell->notificationBounds().isEmpty());
}

CNA_STUDIO_TEST(AToastTakesInputAboveAFloatingWindow)
{
    // The failure this prevents is specific: a float parked in the corner, a toast drawn over it,
    // and a Dismiss the user can see and cannot press because the window underneath is holding the
    // input layer. The router's layers are a modal stack, so this is decided by a number.
    ShellFixture fixture;
    CNA_STUDIO_EXPECT(fixture.shell->floatPanel("details"));
    fixture.shell->notifications().post(
        named("studio.build", StudioNotificationSeverity::Error, "Build failed"));
    fixture.shell->renderFrame(away());

    const UiRect bounds = fixture.shell->notificationBounds();
    CNA_STUDIO_EXPECT(!bounds.isEmpty());

    fixture.shell->renderFrame(at(bounds.centerX(), bounds.centerY()));
    CNA_STUDIO_EXPECT_EQ(fixture.shell->frame().router().blockingLayer(), StudioShell::kToastLayer);
    CNA_STUDIO_EXPECT(StudioShell::kToastLayer > StudioShell::kFloatingLayer);
    CNA_STUDIO_EXPECT(StudioShell::kToastLayer < StudioFrame::kPopupLayer);
}

CNA_STUDIO_TEST(AnOpenMenuTakesInputBackFromTheStack)
{
    // A menu was opened on purpose, a moment ago; a toast arrived by itself. The menu wins.
    ShellFixture fixture;
    fixture.shell->notifications().post(
        named("studio.build", StudioNotificationSeverity::Error, "Build failed"));
    fixture.shell->renderFrame(away());

    const UiRect bounds = fixture.shell->notificationBounds();
    fixture.shell->setOpenMenu(0);
    fixture.shell->renderFrame(at(bounds.centerX(), bounds.centerY()));

    CNA_STUDIO_EXPECT_EQ(fixture.shell->frame().router().blockingLayer(), StudioShell::kMenuLayer);
}

CNA_STUDIO_TEST(TheCountdownStopsWhileThePointerIsOverTheStack)
{
    ShellFixture fixture;
    fixture.shell->notifications().post(StudioNotificationSeverity::Info, "Saved");
    fixture.shell->renderFrame(away());

    const UiRect bounds = fixture.shell->notificationBounds();
    CNA_STUDIO_EXPECT(!bounds.isEmpty());

    // Far longer than its lifetime, with the pointer resting on it throughout.
    for (int i = 0; i < 600; ++i)
    {
        fixture.shell->renderFrame(at(bounds.centerX(), bounds.centerY()));
    }
    CNA_STUDIO_EXPECT(!fixture.shell->notifications().empty());

    // And it goes once the pointer leaves.
    for (int i = 0; i < 600; ++i) { fixture.shell->renderFrame(away()); }
    CNA_STUDIO_EXPECT(fixture.shell->notifications().empty());
}

CNA_STUDIO_TEST(PressingDismissRemovesThatToast)
{
    ShellFixture fixture;
    fixture.shell->notifications().post(
        named("studio.build", StudioNotificationSeverity::Error, "Build failed"));
    fixture.shell->renderFrame(away());

    const UiRect bounds = fixture.shell->notificationBounds();
    CNA_STUDIO_EXPECT(!bounds.isEmpty());

    // The dismiss is at the end of the title's line, in the top-right of the toast.
    const float dismissX = bounds.right() - 12.0f;
    const float dismissY = bounds.y + 14.0f;
    fixture.click(dismissX, dismissY);

    CNA_STUDIO_EXPECT(fixture.shell->notifications().empty());
    CNA_STUDIO_EXPECT(fixture.shell->notificationBounds().isEmpty());
}

CNA_STUDIO_TEST(PressingTheActionRunsTheCommandAndTakesTheToastAway)
{
    ShellFixture fixture;

    StudioNotification notification =
        named("studio.build", StudioNotificationSeverity::Error, "Build failed");
    notification.detail = "3 errors";
    notification.actionId = StudioShell::showPanelActionId("build");
    fixture.shell->notifications().post(std::move(notification));

    // Closed first, so "show" has something to do. The default arrangement already has a Build tab,
    // and asserting against a panel that was open either way would assert nothing.
    CNA_STUDIO_EXPECT(fixture.shell->closePanel("build"));
    fixture.shell->renderFrame(away());

    const UiRect bounds = fixture.shell->notificationBounds();
    CNA_STUDIO_EXPECT(!bounds.isEmpty());

    // The action button sits at the bottom right of the toast.
    fixture.click(bounds.right() - 40.0f, bounds.bottom() - 16.0f);

    CNA_STUDIO_EXPECT(fixture.shell->notifications().empty());
    CNA_STUDIO_EXPECT(fixture.shell->isPanelOpen("build"));
    CNA_STUDIO_EXPECT(fixture.shell->isPanelActive("build"));
}

CNA_STUDIO_TEST(AnActionNamingNoCommandIsNotDrawnAsAButtonThatDoesNothing)
{
    // A notification outlives whatever posted it, and a command can be removed. A button offering
    // an answer that cannot be given is worse than no button.
    ShellFixture fixture;

    StudioNotification notification =
        named("studio.build", StudioNotificationSeverity::Error, "Build failed");
    notification.actionId = "studio.nothing.at.all";
    notification.actionLabel = "Do it";
    fixture.shell->notifications().post(std::move(notification));

    fixture.shell->renderFrame(away());
    CNA_STUDIO_EXPECT_EQ(fixture.shell->frame().phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT(validate(fixture.shell->drawData()).valid);

    // Two rows tall rather than three: the title and nothing else.
    const UiRect bounds = fixture.shell->notificationBounds();
    CNA_STUDIO_EXPECT(!bounds.isEmpty());
    CNA_STUDIO_EXPECT(bounds.height < 64.0f);
}

CNA_STUDIO_TEST(AStackTallerThanTheWindowStopsAtTheTopRatherThanDrawingOffIt)
{
    ShellFixture fixture;
    for (std::size_t i = 0; i < StudioNotificationCenter::kMaxVisible; ++i)
    {
        StudioNotification notification =
            named("n" + std::to_string(i), StudioNotificationSeverity::Error, "Failure");
        notification.detail = "with a detail line";
        notification.actionId = StudioShell::showPanelActionId("build");
        fixture.shell->notifications().post(std::move(notification));
    }

    UiInputState tiny = away();
    tiny.displayWidth = 420.0f;
    tiny.displayHeight = 260.0f;
    fixture.shell->renderFrame(tiny);

    const UiRect bounds = fixture.shell->notificationBounds();
    CNA_STUDIO_EXPECT(bounds.top() >= 0.0f);
    CNA_STUDIO_EXPECT(bounds.bottom() <= 260.0f);
    CNA_STUDIO_EXPECT(bounds.right() <= 420.0f);
    CNA_STUDIO_EXPECT(bounds.left() >= 0.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.shell->frame().phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT(validate(fixture.shell->drawData()).valid);
}

CNA_STUDIO_TEST(ShowingAPanelNeverHidesOne)
{
    // The whole reason this command exists rather than the Window menu's toggle. "Show Build" on a
    // toast, pressed while Build is open, must not be the thing that closes it.
    ShellFixture fixture;
    const std::string show = StudioShell::showPanelActionId("build");

    CNA_STUDIO_EXPECT(fixture.shell->actions().find(show) != nullptr);
    CNA_STUDIO_EXPECT(fixture.shell->openPanel("build"));
    CNA_STUDIO_EXPECT(fixture.shell->isPanelOpen("build"));

    for (int i = 0; i < 3; ++i)
    {
        fixture.shell->invoke(show);
        CNA_STUDIO_EXPECT(fixture.shell->isPanelOpen("build"));
        CNA_STUDIO_EXPECT(fixture.shell->isPanelActive("build"));
    }

    // And it is not on the Window menu, which already has the toggle: a menu with both would have
    // two rows meaning nearly the same thing and one checkmark between them.
    for (const StudioMenuDefinition& menu : fixture.shell->menus())
    {
        for (const StudioMenuEntry& entry : menu.entries)
        {
            CNA_STUDIO_EXPECT(entry.id != show);
        }
    }
}
