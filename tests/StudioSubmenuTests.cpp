// SPDX-License-Identifier: MS-PL
/**
 * @file StudioSubmenuTests.cpp
 * @brief Nested submenus: opening on hover, the corner-cutting delay, and keyboard traversal
 *        (plan.md STUDIO-06017).
 *
 * A submenu is the part of a menu bar that is easiest to get *nearly* right and most obvious when
 * it is wrong. The two failures that matter are both invisible in a screenshot: a submenu that
 * slams shut while the pointer is travelling diagonally towards it, and a keyboard that cannot
 * reach it at all.
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

    UiInputState at(float x, float y, bool leftDown = false, float deltaSeconds = 1.0f / 60.0f)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.deltaSeconds = deltaSeconds;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    UiInputState withKey(UiKey key, float x = 600.0f, float y = 400.0f)
    {
        UiInputState input = at(x, y);
        input.setKeyDown(key, true);
        return input;
    }

    /**
     * @brief A shell whose first menu is a hand-built tree, so nesting can be tested to any depth
     *        without the default menus having to grow a shape no product would ship.
     */
    struct Harness
    {
        StudioShell shell;
        std::vector<std::string> invoked;

        Harness()
        {
            // Four commands, so a submenu row and an action row can be told apart by what they do.
            for (const char* id : {"test.alpha", "test.beta", "test.gamma", "test.delta"})
            {
                StudioAction action;
                action.id = id;
                action.label = id;
                action.run = [this, id] { invoked.emplace_back(id); };
                shell.actions().add(std::move(action));
            }

            std::vector<StudioMenuDefinition> menus;
            StudioMenuDefinition menu;
            menu.title = "Test";
            menu.entries = {
                "test.alpha",
                StudioMenuEntry::submenu("More", {
                    "test.beta",
                    StudioMenuEntry::submenu("Deeper", {"test.gamma"}),
                }),
                "test.delta",
            };
            menus.push_back(std::move(menu));
            shell.setMenus(std::move(menus));
        }

        void frame(const UiInputState& input) { shell.renderFrame(input); }

        /** @brief Settles so later frames have a previous snapshot to diff against. */
        void settle(float x = 600.0f, float y = 400.0f) { frame(at(x, y)); }

        /** @brief Holds the pointer on a point for @p frames frames. */
        void rest(float x, float y, int frames)
        {
            for (int i = 0; i < frames; ++i) { frame(at(x, y)); }
        }

        /** @brief Frames enough for a pending submenu switch to win. */
        [[nodiscard]] int switchFrames() const
        {
            return static_cast<int>(shell.submenuSwitchDelay() * 60.0f) + 3;
        }

        void click(float x, float y)
        {
            frame(at(x, y));
            frame(at(x, y, /*leftDown=*/true));
            frame(at(x, y));
        }

        /** @brief The centre of a row of one open popup. */
        [[nodiscard]] UiRect row(std::size_t level, std::size_t index) const
        {
            return shell.menuRowBounds(level, index);
        }

        /** @brief Index of the row of @p level reading @p idOrLabel. */
        [[nodiscard]] int rowIndex(std::size_t level, std::string_view idOrLabel) const
        {
            for (std::size_t i = 0; i < shell.menuRowCount(level); ++i)
            {
                if (shell.menuRowActionId(level, i) == idOrLabel) { return static_cast<int>(i); }
            }
            return -1;
        }
    };
}

// ---------------------------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AMenuWithNoSubmenuOpenIsExactlyOnePopup)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuRowCount(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), -1);
}

CNA_STUDIO_TEST(ASubmenuRowReportsItsOwnLabelRatherThanAnActionId)
{
    // A submenu names a grouping, not a command. Reading an action id off it would mean the menu
    // was carrying a row that looks invocable and is not.
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    CNA_STUDIO_EXPECT_EQ(harness.rowIndex(0, "More"), 1);
    CNA_STUDIO_EXPECT_EQ(harness.rowIndex(0, "test.delta"), 2);
}

CNA_STUDIO_TEST(OpeningASubmenuAddsAPopupWithItsOwnRows)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    CNA_STUDIO_EXPECT(harness.shell.openSubmenu(0, 1));
    harness.settle();

    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuRowCount(1), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(harness.rowIndex(1, "test.beta"), 0);
    CNA_STUDIO_EXPECT_EQ(harness.rowIndex(1, "Deeper"), 1);
}

CNA_STUDIO_TEST(NestingIsNotLimitedToOneLevel)
{
    // Two is the depth at which an implementation that special-cased "the submenu" falls over.
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();
    harness.shell.openSubmenu(1, 1);
    harness.settle();

    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(harness.rowIndex(2, "test.gamma"), 0);
}

CNA_STUDIO_TEST(ASubmenuOpensBesideItsRowRatherThanOverIt)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();

    const UiRect parent = harness.shell.menuPopupBounds(0);
    const UiRect child = harness.shell.menuPopupBounds(1);
    const UiRect parentRow = harness.row(0, 1);

    CNA_STUDIO_EXPECT(!child.isEmpty());
    CNA_STUDIO_EXPECT(child.left() >= parent.right() - 1.0f);
    // Its first row lines up with the row that opened it, which is what makes the two read as one
    // gesture rather than as a popup that appeared somewhere else on screen.
    CNA_STUDIO_EXPECT(child.top() <= parentRow.top());
    CNA_STUDIO_EXPECT(child.top() > parentRow.top() - 20.0f);
}

CNA_STUDIO_TEST(ASubmenuStaysInsideTheWindow)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();

    const UiRect child = harness.shell.menuPopupBounds(1);
    CNA_STUDIO_EXPECT(child.left() >= 0.0f);
    CNA_STUDIO_EXPECT(child.right() <= kWidth);
    CNA_STUDIO_EXPECT(child.top() >= 0.0f);
    CNA_STUDIO_EXPECT(child.bottom() <= kHeight);
}

CNA_STUDIO_TEST(ClosingAMenuClosesEverySubmenuUnderIt)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.shell.openSubmenu(1, 1);
    harness.settle();
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{3});

    harness.shell.setOpenMenu(-1);
    harness.settle();
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{0});

    // And reopening starts closed rather than restoring the trail, which is what every desktop
    // menu does: a submenu still hanging open from last time is a menu that looks broken.
    harness.shell.setOpenMenu(0);
    harness.settle();
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});
}

// ---------------------------------------------------------------------------------------------
// The pointer
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(RestingOnASubmenuRowOpensIt)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    const UiRect more = harness.row(0, 1);
    harness.rest(more.centerX(), more.centerY(), 3);

    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), 1);
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{2});
}

CNA_STUDIO_TEST(CuttingTheCornerDoesNotSlamTheSubmenuShut)
{
    // The reason the switch is delayed at all. Reaching a submenu means travelling diagonally
    // across a row or two of the parent menu; a switch on the first frame of that would close the
    // submenu halfway to it, and the user would learn to travel in an L instead.
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    harness.shell.setSubmenuSwitchDelay(0.25f);

    const UiRect more = harness.row(0, 1);
    harness.rest(more.centerX(), more.centerY(), 3);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), 1);

    // Crossing the row below for half the delay, which is what a diagonal cut costs in frames.
    // Anything less than the full delay has to be forgiven, or the gesture does not work.
    const UiRect below = harness.row(0, 2);
    const int crossing = static_cast<int>(harness.shell.submenuSwitchDelay() * 60.0f * 0.5f);
    CNA_STUDIO_EXPECT(crossing > 1);
    harness.rest(below.centerX(), below.centerY(), crossing);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), 1);

    // And back onto the submenu, which is still there to be reached.
    const UiRect child = harness.shell.menuPopupBounds(1);
    harness.rest(child.centerX(), child.top() + 8.0f, 2);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), 1);
}

CNA_STUDIO_TEST(RestingOnASiblingRowDoesEventuallyCloseTheSubmenu)
{
    // The other half: forgiving a pass-through must not mean the submenu never closes, or a menu
    // would fill up with popups the user cannot get rid of.
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    const UiRect more = harness.row(0, 1);
    harness.rest(more.centerX(), more.centerY(), 3);
    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), 1);

    const UiRect below = harness.row(0, 2);
    harness.rest(below.centerX(), below.centerY(), harness.switchFrames());

    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), -1);
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(0), 2);
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});
}

CNA_STUDIO_TEST(ClickingASubmenuRowOpensItRatherThanDoingNothing)
{
    // What a user who has not learned that hovering is enough will try first.
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    const UiRect more = harness.row(0, 1);
    harness.click(more.centerX(), more.centerY());

    CNA_STUDIO_EXPECT(harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), 1);
    CNA_STUDIO_EXPECT(harness.invoked.empty());
}

CNA_STUDIO_TEST(ChoosingARowInsideASubmenuInvokesItAndClosesEverything)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    const UiRect more = harness.row(0, 1);
    harness.rest(more.centerX(), more.centerY(), 3);

    const UiRect beta = harness.row(1, 0);
    harness.click(beta.centerX(), beta.centerY());

    CNA_STUDIO_EXPECT_EQ(harness.invoked.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(harness.invoked.front(), std::string{"test.beta"});
    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());
}

CNA_STUDIO_TEST(APressOutsideEveryPopupClosesTheWholeChain)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();

    harness.click(kWidth - 40.0f, kHeight - 40.0f);

    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT(harness.invoked.empty());
}

CNA_STUDIO_TEST(TheRowThatOpenedASubmenuStaysHighlightedWhileThePointerIsInsideIt)
{
    // Otherwise the trail back up the chain goes dark and the user cannot see which rows they
    // came through -- which is the whole navigational value of a nested menu.
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    const UiRect more = harness.row(0, 1);
    harness.rest(more.centerX(), more.centerY(), 3);

    const UiRect beta = harness.row(1, 0);
    harness.rest(beta.centerX(), beta.centerY(), 2);

    CNA_STUDIO_EXPECT_EQ(harness.shell.openSubmenuRow(0), 1);
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(1), 0);
}

// ---------------------------------------------------------------------------------------------
// The keyboard
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(RightArrowEntersTheHighlightedSubmenuAndLandsOnItsFirstRow)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    harness.frame(withKey(UiKey::DownArrow));   // test.alpha
    harness.frame(at(600.0f, 400.0f));
    harness.frame(withKey(UiKey::DownArrow));   // More
    harness.frame(at(600.0f, 400.0f));
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(0), 1);

    harness.frame(withKey(UiKey::RightArrow));
    harness.frame(at(600.0f, 400.0f));

    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(), 0);
    CNA_STUDIO_EXPECT_EQ(harness.rowIndex(1, "test.beta"), 0);
}

CNA_STUDIO_TEST(LeftArrowBacksOutOfASubmenuRatherThanMovingAlongTheBar)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();

    harness.frame(withKey(UiKey::LeftArrow));
    harness.frame(at(600.0f, 400.0f));

    CNA_STUDIO_EXPECT(harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});
    // And the highlight lands back on the row that opened it, so Left then Right retraces the
    // step rather than dropping the user at the top of the parent menu.
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(0), 1);
}

CNA_STUDIO_TEST(ArrowingDownInsideASubmenuMovesInsideItRatherThanBehindIt)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();

    harness.frame(withKey(UiKey::DownArrow));
    harness.frame(at(600.0f, 400.0f));
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(1), 0);

    harness.frame(withKey(UiKey::DownArrow));
    harness.frame(at(600.0f, 400.0f));
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(1), 1);
    // The parent's own highlight is untouched: it is the trail, not the cursor.
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{2});
}

CNA_STUDIO_TEST(EscapeBacksOutOneLevelBeforeClosingTheMenu)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();

    harness.frame(withKey(UiKey::Escape));
    harness.frame(at(600.0f, 400.0f));
    CNA_STUDIO_EXPECT(harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});

    harness.frame(withKey(UiKey::Escape));
    harness.frame(at(600.0f, 400.0f));
    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());
}

CNA_STUDIO_TEST(EnterOnASubmenuRowOpensItRatherThanClosingTheMenu)
{
    // Enter on a row that is not a command must not behave like Enter on one that is -- closing
    // the menu and running nothing is how a user concludes a menu is broken.
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();

    harness.frame(withKey(UiKey::DownArrow));
    harness.frame(at(600.0f, 400.0f));
    harness.frame(withKey(UiKey::DownArrow));
    harness.frame(at(600.0f, 400.0f));

    harness.frame(withKey(UiKey::Enter));
    harness.frame(at(600.0f, 400.0f));

    CNA_STUDIO_EXPECT(harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{2});
    CNA_STUDIO_EXPECT(harness.invoked.empty());
}

CNA_STUDIO_TEST(EnterInsideASubmenuInvokesTheRowAndClosesEverything)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    harness.shell.openSubmenu(0, 1);
    harness.settle();

    harness.frame(withKey(UiKey::DownArrow));
    harness.frame(at(600.0f, 400.0f));
    harness.frame(withKey(UiKey::Enter));
    harness.frame(at(600.0f, 400.0f));

    CNA_STUDIO_EXPECT_EQ(harness.invoked.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(harness.invoked.front(), std::string{"test.beta"});
    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());
}

CNA_STUDIO_TEST(RightArrowOnAPlainRowMovesToTheNextMenuAsItAlwaysDid)
{
    // The generalisation must not cost the behaviour it generalises: Right on a row with no
    // submenu is still how a user walks along the menu bar.
    StudioShell shell;
    shell.renderFrame(at(600.0f, 400.0f));
    shell.setOpenMenu(0);
    shell.renderFrame(at(600.0f, 400.0f));

    shell.renderFrame(withKey(UiKey::RightArrow));
    shell.renderFrame(at(600.0f, 400.0f));

    CNA_STUDIO_EXPECT_EQ(shell.menuLevelCount(), std::size_t{1});
    CNA_STUDIO_EXPECT(shell.isMenuOpen());
}

// ---------------------------------------------------------------------------------------------
// The Window menu's panel list, which is the shell's own use of all this
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheWindowMenuListsEveryRegisteredPanelInASubmenu)
{
    StudioShell shell;
    shell.resetLayout();

    int windowMenu = -1;
    for (std::size_t i = 0; i < shell.menus().size(); ++i)
    {
        if (shell.menus()[i].title == "Window") { windowMenu = static_cast<int>(i); }
    }
    CNA_STUDIO_EXPECT(windowMenu >= 0);

    shell.renderFrame(at(600.0f, 400.0f));
    shell.setOpenMenu(windowMenu);
    shell.renderFrame(at(600.0f, 400.0f));

    int panelsRow = -1;
    for (std::size_t i = 0; i < shell.menuRowCount(); ++i)
    {
        if (shell.menuRowActionId(i) == kStudioPanelMenuLabel) { panelsRow = static_cast<int>(i); }
    }
    CNA_STUDIO_EXPECT(panelsRow >= 0);

    shell.openSubmenu(0, panelsRow);
    shell.renderFrame(at(600.0f, 400.0f));

    CNA_STUDIO_EXPECT_EQ(shell.menuRowCount(1), shell.registeredPanels().size());
    CNA_STUDIO_EXPECT(shell.menuRowCount(1) > 0);
    CNA_STUDIO_EXPECT_EQ(std::string{shell.menuRowActionId(1, 0)},
                         StudioShell::panelActionId(shell.registeredPanels()[0].id));
}

CNA_STUDIO_TEST(APanelsMenuRowIsCheckedWhileThePanelIsOpenAndTogglesIt)
{
    StudioShell shell;
    shell.resetLayout();
    shell.renderFrame(at(600.0f, 400.0f));

    const std::string id = StudioShell::panelActionId("outliner");
    CNA_STUDIO_EXPECT(shell.actions().find(id) != nullptr);
    CNA_STUDIO_EXPECT(shell.isPanelOpen("outliner"));
    CNA_STUDIO_EXPECT(shell.actions().isChecked(id));

    shell.invoke(id);
    shell.renderFrame(at(600.0f, 400.0f));
    CNA_STUDIO_EXPECT(!shell.isPanelOpen("outliner"));
    CNA_STUDIO_EXPECT(!shell.actions().isChecked(id));

    shell.invoke(id);
    shell.renderFrame(at(600.0f, 400.0f));
    CNA_STUDIO_EXPECT(shell.isPanelOpen("outliner"));
}

CNA_STUDIO_TEST(APanelTheUserMustNotCloseIsOfferedOnlyAsAWayToBringItBack)
{
    StudioShell shell;
    shell.resetLayout();
    shell.renderFrame(at(600.0f, 400.0f));

    const StudioPanelDescriptor* viewport = shell.panel("viewport");
    CNA_STUDIO_EXPECT(viewport != nullptr);
    CNA_STUDIO_EXPECT(!viewport->closable);

    // Drawn disabled rather than drawn enabled and then refusing, which is the difference between
    // a menu that explains itself and one that appears to ignore a click.
    CNA_STUDIO_EXPECT(!shell.actions().isEnabled(StudioShell::panelActionId("viewport")));
}
