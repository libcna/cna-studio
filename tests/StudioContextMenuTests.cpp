// SPDX-License-Identifier: MS-PL
/**
 * @file StudioContextMenuTests.cpp
 * @brief Context menus: the same popup chain, anchored at a point (plan.md STUDIO-06005).
 *
 * The thing worth testing is not that a menu appears on right-click. It is that a context menu is
 * the *same* menu — same rows, same submenus, same keyboard, same dismissal — rather than a second
 * implementation that will drift from the first.
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

    UiInputState at(float x, float y)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.deltaSeconds = 1.0f / 60.0f;
        return input;
    }

    UiInputState withButton(UiMouseButton button, float x, float y)
    {
        UiInputState input = at(x, y);
        input.setMouseDown(button, true);
        return input;
    }

    UiInputState withKey(UiKey key, float x = 600.0f, float y = 400.0f)
    {
        UiInputState input = at(x, y);
        input.setKeyDown(key, true);
        return input;
    }

    struct Harness
    {
        StudioShell shell;
        std::vector<std::string> invoked;

        Harness()
        {
            shell.resetLayout();
            for (const char* id : {"test.cut", "test.copy", "test.paste"})
            {
                StudioAction action;
                action.id = id;
                action.label = id;
                action.run = [this, id] { invoked.emplace_back(id); };
                shell.actions().add(std::move(action));
            }
        }

        void frame(const UiInputState& input) { shell.renderFrame(input); }
        void settle(float x = 600.0f, float y = 300.0f) { frame(at(x, y)); }

        /** @brief A right press and release at a point, with settling frames around it. */
        void rightClick(float x, float y)
        {
            frame(at(x, y));
            frame(withButton(UiMouseButton::Right, x, y));
            frame(at(x, y));
        }

        void leftClick(float x, float y)
        {
            frame(at(x, y));
            frame(withButton(UiMouseButton::Left, x, y));
            frame(at(x, y));
        }

        /** @brief Three plain rows and a submenu, which is enough to test every rule. */
        [[nodiscard]] static std::vector<StudioMenuEntry> rows()
        {
            return {
                "test.cut",
                "test.copy",
                std::string{kStudioMenuSeparatorId},
                StudioMenuEntry::submenu("More", {"test.paste"}),
            };
        }
    };
}

CNA_STUDIO_TEST(AContextMenuOpensWithItsCornerOnThePoint)
{
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu(Harness::rows(), 400.0f, 300.0f);
    harness.settle();

    CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT(harness.shell.isPopupOpen());
    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});

    const UiRect popup = harness.shell.menuPopupBounds(0);
    CNA_STUDIO_EXPECT_EQ(popup.left(), 400.0f);
    CNA_STUDIO_EXPECT_EQ(popup.top(), 300.0f);
}

CNA_STUDIO_TEST(AContextMenuNearAnEdgeFlipsToTheOtherSideOfThePointer)
{
    // Sliding along the edge instead would leave the popup under the pointer, and the first thing
    // the user did would be to choose a row by accident.
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu(Harness::rows(), kWidth - 10.0f, kHeight - 10.0f);
    harness.settle();

    const UiRect popup = harness.shell.menuPopupBounds(0);
    CNA_STUDIO_EXPECT(popup.right() <= kWidth - 10.0f + 1.0f);
    CNA_STUDIO_EXPECT(popup.bottom() <= kHeight - 10.0f + 1.0f);
    CNA_STUDIO_EXPECT(popup.left() >= 0.0f);
    CNA_STUDIO_EXPECT(popup.top() >= 0.0f);
}

CNA_STUDIO_TEST(AContextMenuCarriesSubmenusLikeAnyOtherMenu)
{
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu(Harness::rows(), 400.0f, 300.0f);
    harness.settle();

    const int more = 3;
    CNA_STUDIO_EXPECT_EQ(std::string{harness.shell.menuRowActionId(0, more)}, std::string{"More"});

    const UiRect row = harness.shell.menuRowBounds(0, more);
    for (int i = 0; i < 3; ++i) { harness.frame(at(row.centerX(), row.centerY())); }

    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(std::string{harness.shell.menuRowActionId(1, 0)}, std::string{"test.paste"});
}

CNA_STUDIO_TEST(ChoosingARowInvokesItAndClosesTheContextMenu)
{
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu(Harness::rows(), 400.0f, 300.0f);
    harness.settle();

    const UiRect row = harness.shell.menuRowBounds(0, 1);
    harness.leftClick(row.centerX(), row.centerY());

    CNA_STUDIO_EXPECT_EQ(harness.invoked.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(harness.invoked.front(), std::string{"test.copy"});
    CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
}

CNA_STUDIO_TEST(PressingAwayDismissesAContextMenuWithEitherButton)
{
    // Either, because a right-click elsewhere is a request for a *different* context menu, and
    // one that left the first one up would stack popups.
    for (const UiMouseButton button : {UiMouseButton::Left, UiMouseButton::Right})
    {
        Harness harness;
        harness.settle();
        harness.shell.openContextMenu(Harness::rows(), 400.0f, 300.0f);
        harness.settle();
        CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());

        harness.frame(at(900.0f, 600.0f));
        harness.frame(withButton(button, 900.0f, 600.0f));
        harness.frame(at(900.0f, 600.0f));

        CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
        CNA_STUDIO_EXPECT(harness.invoked.empty());
    }
}

CNA_STUDIO_TEST(EscapeClosesAContextMenuOneLevelAtATime)
{
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu(Harness::rows(), 400.0f, 300.0f);
    harness.settle();
    harness.shell.openSubmenu(0, 3);
    harness.settle();
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{2});

    harness.frame(withKey(UiKey::Escape));
    harness.settle();
    CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});

    harness.frame(withKey(UiKey::Escape));
    harness.settle();
    CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
}

CNA_STUDIO_TEST(ArrowsAndEnterWorkInAContextMenuToo)
{
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu(Harness::rows(), 400.0f, 300.0f);
    harness.settle();

    harness.frame(withKey(UiKey::DownArrow));
    harness.settle();
    CNA_STUDIO_EXPECT_EQ(harness.shell.highlightedMenuEntry(), 0);

    harness.frame(withKey(UiKey::Enter));
    harness.settle();

    CNA_STUDIO_EXPECT_EQ(harness.invoked.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(harness.invoked.front(), std::string{"test.cut"});
    CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
}

CNA_STUDIO_TEST(ArrowingSidewaysInAContextMenuDoesNotWalkTheMenuBar)
{
    // There is no bar to walk: a context menu has no visible title, and opening File from one
    // would move the popup somewhere the user was not looking.
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu({"test.cut", "test.copy"}, 400.0f, 300.0f);
    harness.settle();

    harness.frame(withKey(UiKey::DownArrow));
    harness.settle();
    harness.frame(withKey(UiKey::RightArrow));
    harness.settle();

    CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());

    harness.frame(withKey(UiKey::LeftArrow));
    harness.settle();
    CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());
}

CNA_STUDIO_TEST(OnlyOnePopupChainIsOpenAtATime)
{
    Harness harness;
    harness.settle();
    harness.shell.setOpenMenu(0);
    harness.settle();
    CNA_STUDIO_EXPECT(harness.shell.isMenuOpen());

    harness.shell.openContextMenu(Harness::rows(), 400.0f, 300.0f);
    harness.settle();
    CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT(!harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{1});

    harness.shell.setOpenMenu(0);
    harness.settle();
    CNA_STUDIO_EXPECT(harness.shell.isMenuOpen());
    CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
}

CNA_STUDIO_TEST(AContextMenuBlocksThePanelsUnderneathIt)
{
    // Otherwise a click meant for a row would also reach whatever it was covering, which is the
    // one failure a screenshot of a context menu can never show.
    Harness harness;
    harness.settle();

    const UiRect viewport = harness.shell.panelBounds("viewport");
    CNA_STUDIO_EXPECT(!viewport.isEmpty());
    CNA_STUDIO_EXPECT_EQ(harness.shell.frame().router().blockingLayer(), 0);

    harness.shell.openContextMenu(Harness::rows(), viewport.left() + 20.0f,
                                  viewport.top() + 20.0f);
    harness.settle();

    // Everything below the menu layer stops routing, which is the same rule a menu-bar menu uses.
    CNA_STUDIO_EXPECT_EQ(harness.shell.frame().router().blockingLayer(), StudioShell::kMenuLayer);

    // And a tab under the popup is not hoverable while it is up.
    const UiRect tab = harness.shell.panelTabBounds("viewport");
    harness.frame(at(tab.centerX(), tab.centerY()));
    CNA_STUDIO_EXPECT(!harness.shell.frame().router().hoveredId().isValid());
}

CNA_STUDIO_TEST(AnEmptyContextMenuIsNotOpenedAtAll)
{
    // A popup with nothing in it is a rectangle the user has to click away, and it is exactly what
    // a caller produces when the thing right-clicked offers no commands.
    Harness harness;
    harness.settle();
    harness.shell.openContextMenu({}, 400.0f, 300.0f);
    harness.settle();

    CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT_EQ(harness.shell.menuLevelCount(), std::size_t{0});
}

// ---------------------------------------------------------------------------------------------
// The shell's own first user: a panel tab
// ---------------------------------------------------------------------------------------------

namespace
{
    /** @brief The row of menu @p level whose action id is @p id, or -1. */
    int rowWithId(const StudioShell& shell, std::size_t level, std::string_view id)
    {
        for (std::size_t i = 0; i < shell.menuRowCount(level); ++i)
        {
            if (shell.menuRowActionId(level, i) == id) { return static_cast<int>(i); }
        }
        return -1;
    }
}

CNA_STUDIO_TEST(RightClickingAPanelTabOffersCloseFloatAndTheWholePanelList)
{
    // Found by id rather than by row number: the menu gains entries over time, and a test that
    // pinned indices would fail for the one reason that is never a defect.
    Harness harness;
    harness.settle();

    const UiRect tab = harness.shell.panelTabBounds("outliner");
    CNA_STUDIO_EXPECT(!tab.isEmpty());

    harness.rightClick(tab.centerX(), tab.centerY());

    CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT_EQ(std::string{harness.shell.menuRowActionId(0, 0)},
                         StudioShell::closePanelActionId("outliner"));
    CNA_STUDIO_EXPECT(rowWithId(harness.shell, 0, StudioShell::floatPanelActionId("outliner")) >= 0);
    CNA_STUDIO_EXPECT(rowWithId(harness.shell, 0, kStudioDockAllActionId) >= 0);
    CNA_STUDIO_EXPECT(rowWithId(harness.shell, 0, kStudioPanelMenuLabel) >= 0);
}

CNA_STUDIO_TEST(RightClickingATabSelectsItFirst)
{
    // A context menu acting on a tab the user could not see was chosen would be acting behind
    // their back.
    Harness harness;
    harness.settle();

    const UiRect tab = harness.shell.panelTabBounds("layers");
    CNA_STUDIO_EXPECT(!tab.isEmpty());
    // A panel that is docked but is not its group's active tab has nowhere to draw, so an empty
    // body rectangle is exactly "not the one showing".
    CNA_STUDIO_EXPECT(harness.shell.panelBounds("layers").isEmpty());

    harness.rightClick(tab.centerX(), tab.centerY());
    CNA_STUDIO_EXPECT(!harness.shell.panelBounds("layers").isEmpty());
}

CNA_STUDIO_TEST(CloseInATabsContextMenuClosesThatPanel)
{
    Harness harness;
    harness.settle();
    CNA_STUDIO_EXPECT(harness.shell.isPanelOpen("outliner"));

    const UiRect tab = harness.shell.panelTabBounds("outliner");
    harness.rightClick(tab.centerX(), tab.centerY());

    const UiRect row = harness.shell.menuRowBounds(0, 0);
    harness.leftClick(row.centerX(), row.centerY());

    CNA_STUDIO_EXPECT(!harness.shell.isPanelOpen("outliner"));
    CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
}

CNA_STUDIO_TEST(ClosingAPanelFromATabLeavesAWayToBringItBack)
{
    Harness harness;
    harness.settle();

    const UiRect tab = harness.shell.panelTabBounds("outliner");
    harness.rightClick(tab.centerX(), tab.centerY());
    const UiRect close = harness.shell.menuRowBounds(0, 0);
    harness.leftClick(close.centerX(), close.centerY());
    CNA_STUDIO_EXPECT(!harness.shell.isPanelOpen("outliner"));

    // The Panels submenu of any other tab still lists it, unchecked.
    const UiRect other = harness.shell.panelTabBounds("viewport");
    harness.rightClick(other.centerX(), other.centerY());
    const int submenu = rowWithId(harness.shell, 0, kStudioPanelMenuLabel);
    CNA_STUDIO_EXPECT(submenu >= 0);
    harness.shell.openSubmenu(0, static_cast<std::size_t>(submenu));
    harness.settle();

    const std::string wanted = StudioShell::panelActionId("outliner");
    const int row = rowWithId(harness.shell, 1, wanted);
    CNA_STUDIO_EXPECT(row >= 0);
    CNA_STUDIO_EXPECT(!harness.shell.actions().isChecked(wanted));

    harness.shell.invoke(wanted);
    harness.settle();
    CNA_STUDIO_EXPECT(harness.shell.isPanelOpen("outliner"));
}

CNA_STUDIO_TEST(APanelTheUserMustNotCloseOffersCloseDisabled)
{
    Harness harness;
    harness.settle();

    const UiRect tab = harness.shell.panelTabBounds("viewport");
    harness.rightClick(tab.centerX(), tab.centerY());

    CNA_STUDIO_EXPECT(harness.shell.isContextMenuOpen());
    CNA_STUDIO_EXPECT(!harness.shell.actions().isEnabled(
        StudioShell::closePanelActionId("viewport")));

    const UiRect row = harness.shell.menuRowBounds(0, 0);
    harness.leftClick(row.centerX(), row.centerY());
    CNA_STUDIO_EXPECT(harness.shell.isPanelOpen("viewport"));
}

CNA_STUDIO_TEST(RightClickingBesideTheLastTabOffersNothing)
{
    // Routed from a tab's own rectangle rather than from the strip, so the empty space beside the
    // tabs does not act on whichever panel happened to be nearest.
    Harness harness;
    harness.settle();

    // The bottom group's strip runs the full window width, so a point far to the right of its
    // last tab is unambiguously inside a tab strip and outside every tab in it.
    const UiRect tab = harness.shell.panelTabBounds("output");
    CNA_STUDIO_EXPECT(!tab.isEmpty());
    CNA_STUDIO_EXPECT(tab.right() + 400.0f < kWidth);

    harness.rightClick(tab.right() + 400.0f, tab.centerY());

    CNA_STUDIO_EXPECT(!harness.shell.isContextMenuOpen());
}
