// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellInteractionTests.cpp
 * @brief The application frame as something a user operates: menus, toolbar, tabs, shortcuts.
 *
 * Every case drives the real `StudioShell` with synthesised input and asserts on what it did — not
 * on what it drew. "Does clicking File open the File menu" and "does the toolbar invoke the same
 * action the menu does" are questions a golden image cannot answer and a user notices immediately.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 1280.0f;
    constexpr float kHeight = 720.0f;

    /** @brief Builds an input snapshot for the shell's display size. */
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

    /** @brief An input snapshot with one key held. */
    UiInputState withKey(UiKey key, UiKeyModifiers modifiers = {}, float x = 600.0f, float y = 400.0f)
    {
        UiInputState input = at(x, y);
        input.setKeyDown(key, true);
        input.modifiers = modifiers;
        return input;
    }

    /** @brief Counts how many times an action was recorded as invoked, across a run. */
    struct Recorder
    {
        std::vector<std::string> log;

        /** @brief Gives every registered action a handler that records its id. */
        void attach(StudioActionRegistry& registry)
        {
            for (const StudioAction& action : registry.commands())
            {
                StudioAction copy = action;
                const std::string id = action.id;
                copy.run = [this, id]() { log.push_back(id); };
                registry.add(std::move(copy));
            }
        }

        [[nodiscard]] int count(std::string_view id) const
        {
            int total = 0;
            for (const std::string& entry : log) { if (entry == id) { ++total; } }
            return total;
        }
    };

    /** @brief A shell with handlers attached, so an invocation is observable. */
    struct Harness
    {
        StudioShell shell;
        Recorder recorder;

        Harness() { recorder.attach(shell.actions()); }

        /** @brief Runs one frame. */
        void frame(const UiInputState& input) { shell.renderFrame(input); }

        /** @brief Settles the shell so later frames have a previous snapshot to diff against. */
        void settle(float x = 600.0f, float y = 400.0f) { frame(at(x, y)); }

        /** @brief Press and release at a point, with a settling frame in between. */
        void click(float x, float y)
        {
            frame(at(x, y));
            frame(at(x, y, /*leftDown=*/true));
            frame(at(x, y));
        }

        /** @brief Index of the menu whose title reads @p title. */
        [[nodiscard]] int menuIndex(std::string_view title) const
        {
            const auto& menus = shell.menus();
            for (std::size_t i = 0; i < menus.size(); ++i)
            {
                if (menus[i].title == title) { return static_cast<int>(i); }
            }
            return -1;
        }

        /** @brief Index of the open menu's row invoking @p id. */
        [[nodiscard]] int rowIndex(std::string_view id) const
        {
            for (std::size_t i = 0; i < shell.menuRowCount(); ++i)
            {
                if (shell.menuRowActionId(i) == id) { return static_cast<int>(i); }
            }
            return -1;
        }

        /** @brief Index of the toolbar entry invoking @p id. */
        [[nodiscard]] int toolbarIndex(std::string_view id) const
        {
            for (std::size_t i = 0; i < shell.toolbarEntryCount(); ++i)
            {
                if (shell.toolbarEntryActionId(i) == id) { return static_cast<int>(i); }
            }
            return -1;
        }
    };
}

// ------------------------------------------------------------------------------------------------
// Menus open, switch and close (STUDIO-06003, STUDIO-06004)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ClickingAMenuTitleOpensAndClosesIt)
{
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    CNA_STUDIO_EXPECT(!file.isEmpty());

    harness.click(file.centerX(), file.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), 0);
    CNA_STUDIO_EXPECT(harness.shell.menuRowCount() > 0);

    harness.click(file.centerX(), file.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
}

CNA_STUDIO_TEST(MovingAcrossTheBarSwitchesMenusWithoutASecondClick)
{
    // The behaviour every desktop menu bar has. Its absence is noticed within seconds of use.
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    const UiRect edit = harness.shell.menuTitleBounds(1);
    harness.click(file.centerX(), file.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), 0);

    harness.frame(at(edit.centerX(), edit.centerY()));
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), 1);
}

CNA_STUDIO_TEST(AMenuWithNoEntriesIsDisabledRatherThanOpeningAnEmptyBox)
{
    Harness harness;
    harness.settle();

    const int project = harness.menuIndex("Project");
    CNA_STUDIO_EXPECT(project >= 0);

    const UiRect bounds = harness.shell.menuTitleBounds(static_cast<std::size_t>(project));
    harness.click(bounds.centerX(), bounds.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
}

CNA_STUDIO_TEST(ClickingOutsideAnOpenMenuClosesItWithoutInvokingAnything)
{
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), 0);

    harness.click(kWidth * 0.75f, kHeight * 0.6f);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
    CNA_STUDIO_EXPECT(harness.recorder.log.empty());
}

CNA_STUDIO_TEST(EscapeClosesAnOpenMenu)
{
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());
    harness.frame(withKey(UiKey::Escape));

    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
    CNA_STUDIO_EXPECT(harness.recorder.log.empty());
}

CNA_STUDIO_TEST(TheMenuOpenedFromTheRightmostTitleStaysOnScreen)
{
    Harness harness;
    harness.settle();

    const auto last = harness.shell.menus().size() - 1;
    const UiRect title = harness.shell.menuTitleBounds(last);
    harness.click(title.centerX(), title.centerY());

    const UiRect popup = harness.shell.menuPopupBounds();
    CNA_STUDIO_EXPECT(!popup.isEmpty());
    CNA_STUDIO_EXPECT(popup.right() <= kWidth + 0.01f);
    CNA_STUDIO_EXPECT(popup.left() >= -0.01f);
    CNA_STUDIO_EXPECT(popup.bottom() <= kHeight + 0.01f);
}

// ------------------------------------------------------------------------------------------------
// Menu items invoke actions (STUDIO-06001, STUDIO-06004)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ClickingAMenuItemInvokesItsActionAndClosesTheMenu)
{
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());

    const int row = harness.rowIndex("studio.file.save");
    CNA_STUDIO_EXPECT(row >= 0);
    const UiRect bounds = harness.shell.menuRowBounds(static_cast<std::size_t>(row));

    harness.click(bounds.centerX(), bounds.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 1);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
}

CNA_STUDIO_TEST(PressTheTitleDragDownAndReleaseInvokesTheItem)
{
    // The gesture a user makes without thinking about it. The press lands on the title, so the
    // item was never the capture holder and a click-based item would simply never fire.
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.frame(at(file.centerX(), file.centerY()));
    harness.frame(at(file.centerX(), file.centerY(), /*leftDown=*/true));
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), 0);

    const int row = harness.rowIndex("studio.file.openProject");
    CNA_STUDIO_EXPECT(row >= 0);
    const UiRect bounds = harness.shell.menuRowBounds(static_cast<std::size_t>(row));

    harness.frame(at(bounds.centerX(), bounds.centerY(), /*leftDown=*/true));
    harness.frame(at(bounds.centerX(), bounds.centerY()));

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.openProject"), 1);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
}

CNA_STUDIO_TEST(ADisabledMenuItemIsNotInvocable)
{
    Harness harness;
    StudioAction save = *harness.shell.actions().find("studio.file.save");
    save.isEnabled = []() { return false; };
    harness.shell.actions().add(std::move(save));
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());

    const int row = harness.rowIndex("studio.file.save");
    const UiRect bounds = harness.shell.menuRowBounds(static_cast<std::size_t>(row));
    harness.click(bounds.centerX(), bounds.centerY());

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 0);
}

CNA_STUDIO_TEST(ArrowKeysMoveTheHighlightSkippingSeparatorsAndEnterInvokes)
{
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(), -1);

    // Down three times from nothing: new, open, then past the separator to save.
    harness.frame(withKey(UiKey::DownArrow));
    harness.frame(at(600.0f, 400.0f));
    harness.frame(withKey(UiKey::DownArrow));
    harness.frame(at(600.0f, 400.0f));
    harness.frame(withKey(UiKey::DownArrow));

    const int highlighted = harness.shell.highlightedMenuEntry();
    CNA_STUDIO_EXPECT(highlighted >= 0);
    CNA_STUDIO_EXPECT(harness.shell.menuRowActionId(static_cast<std::size_t>(highlighted))
                      != kStudioMenuSeparatorId);
    CNA_STUDIO_EXPECT_EQ(std::string{harness.shell.menuRowActionId(
                             static_cast<std::size_t>(highlighted))},
                         std::string{"studio.file.save"});

    harness.frame(at(600.0f, 400.0f));
    harness.frame(withKey(UiKey::Enter));
    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 1);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
}

CNA_STUDIO_TEST(NoRowIsHighlightedUntilTheKeyboardAsksForOne)
{
    // Pre-selecting the first item would make Enter -- pressed to dismiss something else -- run a
    // command the user never looked at.
    Harness harness;
    harness.settle();
    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());

    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(), -1);
    harness.frame(withKey(UiKey::Enter));
    CNA_STUDIO_EXPECT(harness.recorder.log.empty());
}

// ------------------------------------------------------------------------------------------------
// An open menu blocks what is under it (STUDIO-03022)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AnOpenMenuBlocksTheToolbarBeneathIt)
{
    Harness harness;
    harness.settle();

    const int save = harness.toolbarIndex("studio.file.save");
    CNA_STUDIO_EXPECT(save >= 0);
    const UiRect button = harness.shell.toolbarEntryBounds(static_cast<std::size_t>(save));

    // With no menu open the toolbar responds.
    harness.click(button.centerX(), button.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 1);

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), 0);

    // Now it must not: the press dismisses the menu and reaches nothing.
    harness.click(button.centerX(), button.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 1);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
}

// ------------------------------------------------------------------------------------------------
// The toolbar invokes the same actions (STUDIO-06006)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AToolbarButtonInvokesTheSameActionItsMenuItemDoes)
{
    Harness harness;
    harness.settle();

    const int undo = harness.toolbarIndex("studio.edit.undo");
    CNA_STUDIO_EXPECT(undo >= 0);
    const UiRect button = harness.shell.toolbarEntryBounds(static_cast<std::size_t>(undo));
    harness.click(button.centerX(), button.centerY());
    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.edit.undo"), 1);

    const UiRect edit = harness.shell.menuTitleBounds(1);
    harness.click(edit.centerX(), edit.centerY());
    const int row = harness.rowIndex("studio.edit.undo");
    CNA_STUDIO_EXPECT(row >= 0);
    const UiRect bounds = harness.shell.menuRowBounds(static_cast<std::size_t>(row));
    harness.click(bounds.centerX(), bounds.centerY());

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.edit.undo"), 2);
}

CNA_STUDIO_TEST(ADisabledActionIsRefusedFromTheToolbarToo)
{
    Harness harness;
    StudioAction redo = *harness.shell.actions().find("studio.edit.redo");
    redo.isEnabled = []() { return false; };
    harness.shell.actions().add(std::move(redo));
    harness.settle();

    const int index = harness.toolbarIndex("studio.edit.redo");
    const UiRect button = harness.shell.toolbarEntryBounds(static_cast<std::size_t>(index));
    harness.click(button.centerX(), button.centerY());

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.edit.redo"), 0);
}

CNA_STUDIO_TEST(AnActionWithNoHandlerIsRecordedAsRefusedRatherThanDroppedSilently)
{
    StudioShell shell;   // no handlers attached
    shell.renderFrame(at(600.0f, 400.0f));

    const UiRect file = shell.menuTitleBounds(0);
    shell.renderFrame(at(file.centerX(), file.centerY()));
    shell.renderFrame(at(file.centerX(), file.centerY(), true));
    shell.renderFrame(at(file.centerX(), file.centerY()));

    int row = -1;
    for (std::size_t i = 0; i < shell.menuRowCount(); ++i)
    {
        if (shell.menuRowActionId(i) == "studio.file.save") { row = static_cast<int>(i); }
    }
    const UiRect bounds = shell.menuRowBounds(static_cast<std::size_t>(row));
    shell.renderFrame(at(bounds.centerX(), bounds.centerY()));
    shell.renderFrame(at(bounds.centerX(), bounds.centerY(), true));
    shell.renderFrame(at(bounds.centerX(), bounds.centerY()));

    CNA_STUDIO_EXPECT(shell.invokedActions().empty());
    CNA_STUDIO_EXPECT_EQ(shell.refusedActions().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(shell.refusedActions().front().find("not implemented") != std::string::npos);
}

// ------------------------------------------------------------------------------------------------
// Shortcuts and their scope (STUDIO-06008)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AShortcutInvokesItsActionThroughTheRegistry)
{
    Harness harness;
    harness.settle();

    UiKeyModifiers control;
    control.control = true;
    harness.frame(withKey(UiKey::S, control));

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 1);
}

CNA_STUDIO_TEST(AHeldShortcutFiresOnceRatherThanEveryFrame)
{
    Harness harness;
    harness.settle();

    UiKeyModifiers control;
    control.control = true;
    harness.frame(withKey(UiKey::S, control));
    harness.frame(withKey(UiKey::S, control));
    harness.frame(withKey(UiKey::S, control));

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 1);
}

CNA_STUDIO_TEST(AnUnmodifiedShortcutInsideATextFieldTypesRatherThanCommands)
{
    Harness harness;
    harness.settle();

    // `F` frames the selection in a viewport and types an `f` in a name field. Neither the
    // viewport nor the field should have to know about the other.
    harness.shell.setTextInputActive(true);
    harness.frame(withKey(UiKey::F));
    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.view.focusSelected"), 0);

    harness.shell.setTextInputActive(false);
    harness.frame(at(600.0f, 400.0f));
    harness.frame(withKey(UiKey::F));
    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.view.focusSelected"), 1);
}

CNA_STUDIO_TEST(AnOpenMenuOwnsTheKeyboard)
{
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());

    UiKeyModifiers control;
    control.control = true;
    harness.frame(withKey(UiKey::S, control));

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 0);
}

CNA_STUDIO_TEST(ADisabledActionIsRefusedFromItsShortcut)
{
    Harness harness;
    StudioAction save = *harness.shell.actions().find("studio.file.save");
    save.isEnabled = []() { return false; };
    harness.shell.actions().add(std::move(save));
    harness.settle();

    UiKeyModifiers control;
    control.control = true;
    harness.frame(withKey(UiKey::S, control));

    CNA_STUDIO_EXPECT_EQ(harness.recorder.count("studio.file.save"), 0);
}

// ------------------------------------------------------------------------------------------------
// Docks and tabs (STUDIO-05004)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ClickingADockTabMakesItsPanelActive)
{
    Harness harness;
    harness.settle();

    CNA_STUDIO_EXPECT(!harness.shell.panelBounds("content").isEmpty());
    CNA_STUDIO_EXPECT(harness.shell.panelBounds("output").isEmpty());

    const UiRect tab = harness.shell.panelTabBounds("output");
    CNA_STUDIO_EXPECT(!tab.isEmpty());

    harness.click(tab.centerX(), tab.centerY());
    CNA_STUDIO_EXPECT(!harness.shell.panelBounds("output").isEmpty());
    CNA_STUDIO_EXPECT(harness.shell.panelBounds("content").isEmpty());
}

CNA_STUDIO_TEST(DraggingASplitterResizesTheDocksItSeparates)
{
    Harness harness;
    harness.settle();

    const UiRect before = harness.shell.panelBounds("outliner");
    CNA_STUDIO_EXPECT(!before.isEmpty());

    // The splitter between the outliner and everything right of it.
    StudioDockNodeId split = kInvalidDockNode;
    for (const StudioDockNodeId id : harness.shell.dockTree().splits())
    {
        const StudioDockNode& node = harness.shell.dockTree().node(id);
        if (!node.splitter.isEmpty()
            && node.orientation == StudioDockOrientation::Horizontal
            && node.splitter.left() > before.left()
            && node.splitter.left() < before.right() + 20.0f)
        {
            split = id;
            break;
        }
    }
    CNA_STUDIO_EXPECT(split != kInvalidDockNode);

    const UiRect grip = harness.shell.dockTree().node(split).splitter;
    const float x = grip.centerX();
    const float y = grip.centerY();

    harness.frame(at(x, y));
    harness.frame(at(x, y, /*leftDown=*/true));
    CNA_STUDIO_EXPECT(harness.shell.cursor() == StudioCursor::ResizeHorizontal);

    harness.frame(at(x + 60.0f, y, /*leftDown=*/true));
    harness.frame(at(x + 60.0f, y));

    const UiRect after = harness.shell.panelBounds("outliner");
    CNA_STUDIO_EXPECT(after.width > before.width + 40.0f);
}

CNA_STUDIO_TEST(ASplitterCannotBeDraggedPastItsNeighboursMinimum)
{
    Harness harness;
    harness.settle();

    StudioDockNodeId split = kInvalidDockNode;
    for (const StudioDockNodeId id : harness.shell.dockTree().splits())
    {
        if (!harness.shell.dockTree().node(id).splitter.isEmpty()) { split = id; break; }
    }
    CNA_STUDIO_EXPECT(split != kInvalidDockNode);

    const UiRect grip = harness.shell.dockTree().node(split).splitter;
    float x = grip.centerX();
    const float y = grip.centerY();

    harness.frame(at(x, y));
    harness.frame(at(x, y, /*leftDown=*/true));
    for (int step = 0; step < 40; ++step)
    {
        x += 80.0f;
        harness.frame(at(x, y, /*leftDown=*/true));
    }
    harness.frame(at(x, y));

    CNA_STUDIO_EXPECT(harness.shell.dockTree().isWellFormed());
    for (const StudioDockNodeId leaf : harness.shell.dockTree().leaves())
    {
        const UiRect bounds = harness.shell.dockTree().node(leaf).bounds;
        CNA_STUDIO_EXPECT(bounds.width > 0.0f);
        CNA_STUDIO_EXPECT(bounds.height > 0.0f);
    }
}

// ------------------------------------------------------------------------------------------------
// Frame hygiene
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AShellFrameCommitsNoPhaseViolationsAndNoIdCollisions)
{
    Harness harness;
    harness.settle();
    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.click(file.centerX(), file.centerY());

    CNA_STUDIO_EXPECT_EQ(harness.shell.frame().phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(harness.shell.frame().ids().collisionCount(), std::size_t{0});
}

CNA_STUDIO_TEST(TheShellInvokesNothingOnItsFirstFrameWithAButtonAlreadyDown)
{
    // A user who launched Studio by double-clicking still has the button down as the first frame
    // runs, and the pointer may well be over the toolbar.
    Harness harness;
    UiInputState first = at(60.0f, 60.0f, /*leftDown=*/true);
    harness.frame(first);
    harness.frame(at(60.0f, 60.0f));

    CNA_STUDIO_EXPECT(harness.recorder.log.empty());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openMenu(), -1);
}

CNA_STUDIO_TEST(TheShellIsDescribedTwicePerFrameAndRoutesOnce)
{
    Harness harness;
    harness.settle();

    const UiRect file = harness.shell.menuTitleBounds(0);
    harness.frame(at(file.centerX(), file.centerY()));

    // One interaction record per interactive widget: the draw pass replays rather than re-routing,
    // so describing twice must not double the table.
    const std::size_t interactions = harness.shell.frame().interactionCount();
    CNA_STUDIO_EXPECT(interactions > 0);

    harness.frame(at(file.centerX(), file.centerY()));
    CNA_STUDIO_EXPECT_EQ(harness.shell.frame().interactionCount(), interactions);
}

CNA_STUDIO_TEST(TheShellKeepsItsMenuGeometryInsideTheWindowAtEveryTestedScale)
{
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);
        StudioShell shell{theme};

        for (std::size_t i = 0; i < shell.menus().size(); ++i)
        {
            shell.setOpenMenu(static_cast<int>(i));
            shell.renderFrame(at(-1.0f, -1.0f));

            const UiRect popup = shell.menuPopupBounds();
            if (popup.isEmpty()) { continue; }
            CNA_STUDIO_EXPECT(popup.left() >= -0.01f);
            CNA_STUDIO_EXPECT(popup.right() <= kWidth + 0.01f);
            CNA_STUDIO_EXPECT(popup.bottom() <= kHeight + 0.01f);

            for (std::size_t row = 0; row < shell.menuRowCount(); ++row)
            {
                const UiRect bounds = shell.menuRowBounds(row);
                CNA_STUDIO_EXPECT(bounds.left() >= popup.left() - 0.01f);
                CNA_STUDIO_EXPECT(bounds.right() <= popup.right() + 0.01f);
            }
        }
    }
}
