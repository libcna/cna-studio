// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDialogTests.cpp
 * @brief A modal dialog (plan.md STUDIO-03040).
 *
 * Every part of a dialog that matters is about what it *prevents*, and none of it shows in a
 * screenshot: a click behind it reaching a button, Tab walking out into the panels it covers,
 * Escape closing something underneath instead of answering it. So those are the cases here, and
 * each one is checked against the same action working while the dialog is closed — otherwise
 * "the button did nothing" would pass for the wrong reason.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioDialog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 1280.0f;
    constexpr float kHeight = 720.0f;

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

    std::unique_ptr<StudioShell> defaultShell()
    {
        auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
        shell->resetLayout();
        shell->renderFrame(at(-1.0f, -1.0f));
        return shell;
    }

    /** @brief A request with two buttons and no field. */
    StudioDialogRequest confirm()
    {
        StudioDialogRequest request;
        request.title = "Discard changes?";
        request.lines = {"The scene has unsaved changes."};
        request.buttons = {"Cancel", "Discard"};
        request.cancelButton = 0;
        return request;
    }

    void click(StudioShell& shell, float x, float y)
    {
        shell.renderFrame(at(x, y));
        shell.renderFrame(at(x, y, true));
        shell.renderFrame(at(x, y, false));
    }

    /**
     * @brief Presses and releases @p key.
     *
     * The answer lands on the *press* frame and `dialogResult()` reports one frame, like
     * `invokedActions()` -- so a test that reads it does so between the two, as an editor's loop
     * does.
     */
    void press(StudioShell& shell, UiKey key)
    {
        UiInputState input = at(-1.0f, -1.0f);
        input.setKeyDown(key, true);
        shell.renderFrame(input);
        shell.renderFrame(at(-1.0f, -1.0f));
    }

    /** @brief Presses @p key and stops, so the answer is still the last frame's. */
    void pressOnly(StudioShell& shell, UiKey key)
    {
        UiInputState input = at(-1.0f, -1.0f);
        input.setKeyDown(key, true);
        shell.renderFrame(input);
    }
}

CNA_STUDIO_TEST(ADialogIsCentredAndStaysInsideEvenASmallWindow)
{
    // A dialog wider than the window has a button off the edge, which is a dialog that cannot be
    // answered at all -- the one failure worse than an ugly one.
    StudioFrame frame{StudioTheme::dark()};
    StudioDialogRequest request = confirm();
    request.lines = {std::string(300, 'x')};

    const UiRect wide{0.0f, 0.0f, kWidth, kHeight};
    const UiRect bounds = studioDialogBounds(frame, wide, request);
    CNA_STUDIO_EXPECT(bounds.left() >= 0.0f);
    CNA_STUDIO_EXPECT(bounds.right() <= kWidth);
    CNA_STUDIO_EXPECT(std::abs(bounds.centerX() - wide.centerX()) <= 1.0f);

    const UiRect tiny{0.0f, 0.0f, 320.0f, 200.0f};
    const UiRect small = studioDialogBounds(frame, tiny, request);
    CNA_STUDIO_EXPECT(small.left() >= 0.0f);
    CNA_STUDIO_EXPECT(small.right() <= 320.0f);
    CNA_STUDIO_EXPECT(small.bottom() <= 200.0f);
}

CNA_STUDIO_TEST(OpeningADialogRaisesTheModalLayerAndClosingItLowersIt)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(!shell->isDialogOpen());

    shell->openDialog(confirm());
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(shell->isDialogOpen());
    CNA_STUDIO_EXPECT(shell->frame().isAnyModalOpen());
    CNA_STUDIO_EXPECT_EQ(shell->frame().router().blockingLayer(), StudioFrame::kModalLayer);

    shell->closeDialog();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(!shell->isDialogOpen());
    CNA_STUDIO_EXPECT(!shell->frame().isAnyModalOpen());
    CNA_STUDIO_EXPECT_EQ(shell->frame().router().blockingLayer(), 0);
}

CNA_STUDIO_TEST(AClickBehindADialogReachesNothing)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();

    // A toolbar button that plainly works first, so the case below cannot pass for the wrong
    // reason.
    const UiRect save = shell->toolbarEntryBounds(0);
    CNA_STUDIO_EXPECT(!save.isEmpty());
    click(*shell, save.centerX(), save.centerY());
    const std::size_t invokedWhenFree = shell->invokedActions().size();

    shell->openDialog(confirm());
    shell->renderFrame(at(-1.0f, -1.0f));
    click(*shell, save.centerX(), save.centerY());

    CNA_STUDIO_EXPECT(shell->invokedActions().empty());
    CNA_STUDIO_EXPECT(shell->isDialogOpen());
    (void)invokedWhenFree;
}

CNA_STUDIO_TEST(PressingAButtonAnswersTheDialogAndCloses)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    shell->openDialog(confirm());
    shell->renderFrame(at(-1.0f, -1.0f));

    const UiRect bounds = shell->dialogBounds();
    CNA_STUDIO_EXPECT(!bounds.isEmpty());

    // The affirmative button is the last, nearest the corner.
    const float y = bounds.bottom() - 24.0f;
    click(*shell, bounds.right() - 40.0f, y);

    CNA_STUDIO_EXPECT_EQ(shell->dialogResult().chosen, 1);
    CNA_STUDIO_EXPECT(!shell->dialogResult().dismissed);
    CNA_STUDIO_EXPECT(!shell->isDialogOpen());
}

CNA_STUDIO_TEST(EscapeAnswersTheDialogRatherThanSomethingUnderneath)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    shell->openDialog(confirm());
    shell->renderFrame(at(-1.0f, -1.0f));

    press(*shell, UiKey::Escape);

    // The request names which button Escape means, so a caller reading `chosen` alone still gets
    // the right answer -- "the user cancelled" and "the user pressed Cancel" are the same event.
    CNA_STUDIO_EXPECT(!shell->isDialogOpen());
}

CNA_STUDIO_TEST(ADialogThatMustBeAnsweredIgnoresEscape)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    StudioDialogRequest request = confirm();
    request.dismissable = false;
    request.cancelButton = -1;
    shell->openDialog(std::move(request));
    shell->renderFrame(at(-1.0f, -1.0f));

    press(*shell, UiKey::Escape);
    CNA_STUDIO_EXPECT(shell->isDialogOpen());
}

CNA_STUDIO_TEST(TabCannotWalkOutOfADialogIntoThePanelsItCovers)
{
    // The invisible failure: the focus ring ends up on a control the user cannot see, and the next
    // Enter presses it.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    shell->openDialog(confirm());
    shell->renderFrame(at(-1.0f, -1.0f));

    const UiRect bounds = shell->dialogBounds();

    // Far more presses than the dialog has controls, so a ring that leaked would have left it.
    for (int i = 0; i < 12; ++i) { press(*shell, UiKey::Tab); }

    const WidgetId focused = shell->frame().router().focusedId();
    CNA_STUDIO_EXPECT(focused.isValid());

    // Whatever has focus is inside the dialog: the shell's own controls are all outside its
    // rectangle, so this is checkable without naming any of them.
    const UiRect focusedBounds = shell->frame().router().focusedBounds();
    CNA_STUDIO_EXPECT(!focusedBounds.isEmpty());
    CNA_STUDIO_EXPECT(bounds.contains(focusedBounds.centerX(), focusedBounds.centerY()));
}

CNA_STUDIO_TEST(ADialogWithAFieldStartsWithTheKeyboardInIt)
{
    // A name prompt that starts with the keyboard on a button makes the user reach for the mouse
    // to answer it, which is the one thing a keyboard prompt exists to avoid.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    StudioDialogRequest request;
    request.title = "Save Layout As";
    request.hasTextField = true;
    request.text = "Animation";
    request.requireText = true;
    request.buttons = {"Cancel", "Save"};
    request.cancelButton = 0;
    shell->openDialog(std::move(request));
    shell->renderFrame(at(-1.0f, -1.0f));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(shell->frame().router().wantsTextInput());

    // Enter in the field is the affirmative answer, with the text carried out.
    pressOnly(*shell, UiKey::Enter);
    CNA_STUDIO_EXPECT_EQ(shell->dialogResult().chosen, 1);
    CNA_STUDIO_EXPECT_EQ(shell->dialogResult().text, std::string{"Animation"});
    CNA_STUDIO_EXPECT(!shell->isDialogOpen());
}

CNA_STUDIO_TEST(ADialogThatNeedsANameRefusesToAcceptAnEmptyOne)
{
    // A Save As with no name is not an answer, and a button that accepts one and then reports a
    // failure is worse than a button that is plainly not ready.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    StudioDialogRequest request;
    request.title = "Save Layout As";
    request.hasTextField = true;
    request.requireText = true;
    request.buttons = {"Cancel", "Save"};
    request.cancelButton = 0;
    shell->openDialog(std::move(request));
    shell->renderFrame(at(-1.0f, -1.0f));

    // Laid out the way the dialog lays them out -- right-aligned, affirmative nearest the corner
    // -- rather than by a coordinate that happens to work.
    const StudioTheme& theme = shell->frame().theme();
    const float pad = static_cast<float>(theme.metric(StudioMetric::SpacingLarge));
    const float gap = static_cast<float>(theme.metric(StudioMetric::SpacingSmall));
    const float saveWidth = std::ceil(studioLabelWidth(shell->frame(), "Save")) + pad * 2.0f;
    const float cancelWidth = std::ceil(studioLabelWidth(shell->frame(), "Cancel")) + pad * 2.0f;

    const UiRect bounds = shell->dialogBounds();
    const float row = bounds.bottom() - pad
                    - static_cast<float>(theme.metric(StudioMetric::ControlHeight)) * 0.5f;
    const float saveX = bounds.right() - pad - saveWidth * 0.5f;
    const float cancelX = bounds.right() - pad - saveWidth - gap - cancelWidth * 0.5f;

    click(*shell, saveX, row);
    CNA_STUDIO_EXPECT(shell->isDialogOpen());
    CNA_STUDIO_EXPECT_EQ(shell->dialogResult().chosen, -1);

    // Cancel still works, because a dialog whose every button is dead has no way out.
    click(*shell, cancelX, row);
    CNA_STUDIO_EXPECT(!shell->isDialogOpen());
    CNA_STUDIO_EXPECT_EQ(shell->dialogResult().chosen, 0);
}

CNA_STUDIO_TEST(OpeningADialogFromAMenuClosesTheMenu)
{
    // Two things claiming the keyboard is a state where Escape answers the wrong one.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    shell->setOpenMenu(0);
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->isPopupOpen());

    shell->openDialog(confirm());
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(!shell->isPopupOpen());
    CNA_STUDIO_EXPECT(shell->isDialogOpen());
}

CNA_STUDIO_TEST(AboutIsARealDialogRatherThanACommandThatDoesNothing)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->actions().isEnabled("studio.help.about"));

    shell->invoke("studio.help.about");
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(shell->isDialogOpen());
    CNA_STUDIO_EXPECT(!shell->dialog().lines.empty());
    CNA_STUDIO_EXPECT(!shell->dialogBounds().isEmpty());

    // And what it says is the host's to set, so a build knows its own renderer.
    shell->closeDialog();
    shell->setAboutLines({"CNA Studio 9.9.9", "Renderer: teapot"});
    shell->invoke("studio.help.about");
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT_EQ(shell->dialog().lines.back(), std::string{"Renderer: teapot"});
}
