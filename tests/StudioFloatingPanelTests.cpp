// SPDX-License-Identifier: MS-PL
/**
 * @file StudioFloatingPanelTests.cpp
 * @brief Undocking a panel into a window of its own (plan.md STUDIO-05006).
 *
 * A float is the one part of the workspace that is *not* laid out by the tree, so the things that
 * can go wrong are different from everything else in the dock model: a window off the edge of a
 * smaller screen, two windows claiming the same panel, a window left holding nothing, and the one
 * that is invisible from a screenshot — a button under a float lighting up through it.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"

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

    /** @brief A tree with two leaves, one holding two panels. */
    StudioDockTree twoLeaves()
    {
        StudioDockTree tree;
        const StudioDockNodeId right = tree.split(tree.root(), StudioDockSide::Right, 0.4f);
        const StudioDockNodeId left = tree.sibling(right);
        tree.addPanel(left, "outliner");
        tree.addPanel(right, "details");
        tree.addPanel(right, "history");
        tree.layout(UiRect{0.0f, 0.0f, kWidth, kHeight}, 4.0f, 24.0f);
        return tree;
    }

    /** @brief A point in a float's title bar: right of its tabs, left of its close button. */
    UiRect titleBarOf(const StudioShell& shell, std::size_t index)
    {
        const UiRect bounds = shell.dockTree().floating()[index].bounds;
        const float strip = 24.0f;
        return UiRect{bounds.right() - strip * 2.5f, bounds.top(), strip, strip};
    }
}

// --- The model ---------------------------------------------------------------------------------

CNA_STUDIO_TEST(FloatingAPanelTakesItOutOfTheTreeAndLeavesTheRestWhereItWas)
{
    StudioDockTree tree = twoLeaves();
    const std::size_t leavesBefore = tree.leaves().size();

    const std::size_t window = tree.floatPanel("details", 40.0f, 40.0f);
    CNA_STUDIO_EXPECT(window != kInvalidFloatingDock);

    CNA_STUDIO_EXPECT(tree.findPanel("details") == kInvalidDockNode);
    CNA_STUDIO_EXPECT_EQ(tree.findFloatingPanel("details"), std::size_t{0});
    CNA_STUDIO_EXPECT(tree.findPanel("history") != kInvalidDockNode);
    CNA_STUDIO_EXPECT_EQ(tree.leaves().size(), leavesBefore);
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(FloatingTheLastPanelOfALeafCollapsesItLikeClosingItWould)
{
    // Undocking is the one operation that empties a leaf without the panel being closed, and a
    // leaf nobody can see still takes its share of the split.
    StudioDockTree tree = twoLeaves();
    const std::size_t leavesBefore = tree.leaves().size();

    CNA_STUDIO_EXPECT(tree.floatPanel("outliner", 40.0f, 40.0f) != kInvalidFloatingDock);

    CNA_STUDIO_EXPECT_EQ(tree.leaves().size(), leavesBefore - 1);
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(AFloatLeftHoldingNothingGoesRatherThanStayingAnEmptyWindow)
{
    StudioDockTree tree = twoLeaves();
    CNA_STUDIO_EXPECT(tree.floatPanel("details", 40.0f, 40.0f) != kInvalidFloatingDock);
    CNA_STUDIO_EXPECT_EQ(tree.floating().size(), std::size_t{1});

    CNA_STUDIO_EXPECT(tree.removePanel("details"));
    CNA_STUDIO_EXPECT(tree.floating().empty());
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(APanelIsOpenInExactlyOnePlaceAcrossDocksAndFloats)
{
    // The rule that keeps two tabs from both claiming to be the Details panel. Checked across both
    // because a layout file somebody edited by hand is exactly where the violation arrives from.
    StudioDockTree tree = twoLeaves();
    CNA_STUDIO_EXPECT(tree.floatPanel("details", 40.0f, 40.0f) != kInvalidFloatingDock);

    // Adding it back to a leaf moves it rather than copying it.
    CNA_STUDIO_EXPECT(tree.addPanel(tree.findPanel("outliner"), "details"));
    CNA_STUDIO_EXPECT(tree.floating().empty());
    CNA_STUDIO_EXPECT(tree.isWellFormed());

    std::size_t seen = 0;
    for (const std::string& panel : tree.panels())
    {
        if (panel == "details") { ++seen; }
    }
    CNA_STUDIO_EXPECT_EQ(seen, std::size_t{1});
}

CNA_STUDIO_TEST(RaisingAFloatIsARotationSoOnlyOneIsEverOnTop)
{
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("details", 10.0f, 10.0f);
    tree.floatPanel("history", 20.0f, 20.0f);
    tree.floatPanel("outliner", 30.0f, 30.0f);

    CNA_STUDIO_EXPECT_EQ(tree.floating().size(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(tree.floating().back().panels.front(), std::string{"outliner"});

    CNA_STUDIO_EXPECT_EQ(tree.raiseFloating(0), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(tree.floating().back().panels.front(), std::string{"details"});

    // The others keep their order beneath it rather than being reshuffled.
    CNA_STUDIO_EXPECT_EQ(tree.floating()[0].panels.front(), std::string{"history"});
    CNA_STUDIO_EXPECT_EQ(tree.floating()[1].panels.front(), std::string{"outliner"});

    // Raising the one already on top is a no-op rather than a rotation that changes nothing
    // twice.
    CNA_STUDIO_EXPECT_EQ(tree.raiseFloating(2), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(tree.floating().back().panels.front(), std::string{"details"});
}

CNA_STUDIO_TEST(TheFrontMostFloatIsTheOneUnderThePointer)
{
    // Overlapping floats are ordinary, and the one the user can see is the one they mean.
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("details", 100.0f, 100.0f, 200.0f, 200.0f);
    tree.floatPanel("history", 150.0f, 150.0f, 200.0f, 200.0f);
    tree.layout(UiRect{0.0f, 0.0f, kWidth, kHeight}, 4.0f, 24.0f);

    // Where they overlap, the later one wins.
    CNA_STUDIO_EXPECT_EQ(tree.floatingAt(200.0f, 200.0f), std::size_t{1});
    // And where only the first one is, it is still found.
    CNA_STUDIO_EXPECT_EQ(tree.floatingAt(110.0f, 110.0f), std::size_t{0});
    CNA_STUDIO_EXPECT(tree.floatingAt(600.0f, 600.0f) == kInvalidFloatingDock);
}

CNA_STUDIO_TEST(AFloatSavedOffTheEdgeIsClampedBackIntoView)
{
    // A window placed on a large display must not be unreachable on a laptop. A title bar that
    // cannot be grabbed is a window that cannot be moved.
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("details", 3000.0f, 1800.0f, 360.0f, 280.0f);

    tree.layout(UiRect{0.0f, 0.0f, 800.0f, 600.0f}, 4.0f, 24.0f);
    const UiRect bounds = tree.floating().front().bounds;

    CNA_STUDIO_EXPECT(bounds.right() <= 800.0f);
    CNA_STUDIO_EXPECT(bounds.bottom() <= 600.0f);
    CNA_STUDIO_EXPECT(bounds.left() >= 0.0f);
    CNA_STUDIO_EXPECT(bounds.top() >= 0.0f);
}

CNA_STUDIO_TEST(AFloatKeepsItsSizeWhenTheWindowGrowsAndADockDoesNot)
{
    // The difference in kind between the two, and the reason a float stores units rather than
    // fractions: a docked inspector that is a fifth of the width stays a fifth of the width, while
    // a palette the user sized to 360 wide would become a quarter of a 4K screen.
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("details", 20.0f, 20.0f, 360.0f, 280.0f);

    tree.layout(UiRect{0.0f, 0.0f, 1280.0f, 720.0f}, 4.0f, 24.0f);
    const UiRect small = tree.floating().front().bounds;

    tree.layout(UiRect{0.0f, 0.0f, 2560.0f, 1440.0f}, 4.0f, 24.0f);
    const UiRect large = tree.floating().front().bounds;

    CNA_STUDIO_EXPECT_EQ(small.width, large.width);
    CNA_STUDIO_EXPECT_EQ(small.height, large.height);
}

CNA_STUDIO_TEST(MovingIntoAFloatThatTheRemovalDeletesStillLandsInTheRightWindow)
{
    // The index arithmetic that is easy to get wrong and silent when it is: taking the last panel
    // out of float 0 deletes it, and every index above shifts down by one.
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("outliner", 10.0f, 10.0f);   // float 0, one panel
    tree.floatPanel("details", 20.0f, 20.0f);    // float 1
    tree.floatPanel("history", 30.0f, 30.0f);    // float 2

    CNA_STUDIO_EXPECT(tree.movePanelToFloating("outliner", 2));

    CNA_STUDIO_EXPECT_EQ(tree.floating().size(), std::size_t{2});
    const std::size_t landed = tree.findFloatingPanel("outliner");
    CNA_STUDIO_EXPECT(landed != kInvalidFloatingDock);
    CNA_STUDIO_EXPECT(tree.findFloatingPanel("history") == landed);
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(MovingAPanelIntoTheFloatItIsAlreadyAloneInIsNotADisappearance)
{
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("details", 20.0f, 20.0f);

    CNA_STUDIO_EXPECT(tree.movePanelToFloating("details", 0));
    CNA_STUDIO_EXPECT_EQ(tree.floating().size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(tree.findFloatingPanel("details"), std::size_t{0});
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(DockingAFloatingPanelIsTheSameOperationAsMovingADockedOne)
{
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("details", 20.0f, 20.0f);

    const StudioDockNodeId leaf = tree.findPanel("outliner");
    CNA_STUDIO_EXPECT(tree.movePanel("details", leaf, 0));

    CNA_STUDIO_EXPECT(tree.floating().empty());
    CNA_STUDIO_EXPECT_EQ(tree.node(leaf).panels.front(), std::string{"details"});
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(TheWorkspaceRoundTripsThroughJsonWithItsFloats)
{
    StudioDockTree tree = twoLeaves();
    tree.floatPanel("details", 120.0f, 90.0f, 400.0f, 300.0f);
    tree.floatPanel("history", 200.0f, 160.0f, 240.0f, 180.0f);

    // Moved rather than pushed onto the vector: a panel written into a float while it is still
    // docked is open in two places, which fromJson refuses -- as another case below checks.
    CNA_STUDIO_EXPECT(tree.movePanelToFloating("outliner", 0));
    tree.floatingAt(0).activePanel = 1;

    std::string problem;
    const StudioDockTree restored = StudioDockTree::fromJson(tree.toJson(), &problem);
    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT_EQ(restored.floating().size(), std::size_t{2});
    if (restored.floating().size() != 2) { return; }

    CNA_STUDIO_EXPECT_EQ(restored.floating()[0].panels.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(restored.floating()[0].activePanel, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(restored.floating()[0].x, 120.0f);
    CNA_STUDIO_EXPECT_EQ(restored.floating()[0].y, 90.0f);
    CNA_STUDIO_EXPECT_EQ(restored.floating()[1].width, 240.0f);
    CNA_STUDIO_EXPECT_EQ(restored.floating()[1].height, 180.0f);
    CNA_STUDIO_EXPECT(restored.isWellFormed());
}

CNA_STUDIO_TEST(ALayoutWrittenBeforeFloatsExistedStillLoadsAndWritesNoneBack)
{
    // The compatibility that matters in both directions: an old file has no "floating" key, and a
    // workspace with no floats must not start writing one -- a diff of a layout file should show
    // what the user changed and nothing else.
    StudioDockTree tree = twoLeaves();
    const JsonValue document = tree.toJson();
    CNA_STUDIO_EXPECT(!document["floating"].isArray());

    std::string problem;
    const StudioDockTree restored = StudioDockTree::fromJson(document, &problem);
    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT(restored.floating().empty());
}

CNA_STUDIO_TEST(ALayoutFileWithAnEmptyFloatingWindowLoadsWithoutIt)
{
    StudioDockTree tree = twoLeaves();
    JsonValue document = tree.toJson();

    JsonValue windows = JsonValue::makeArray();
    JsonValue empty = JsonValue::makeObject();
    empty.set("panels", JsonValue::makeArray());
    windows.append(std::move(empty));

    JsonValue real = JsonValue::makeObject();
    JsonValue panels = JsonValue::makeArray();
    // A panel this tree has not docked, so the only thing under test is the empty window beside it.
    panels.append(JsonValue{std::string{"content"}});
    real.set("panels", std::move(panels));
    windows.append(std::move(real));
    document.set("floating", std::move(windows));

    std::string problem;
    const StudioDockTree restored = StudioDockTree::fromJson(document, &problem);

    // The empty one is dropped rather than making the whole document a rejection: losing one
    // window the user cannot see is better than losing the workspace they built.
    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT_EQ(restored.floating().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(restored.isWellFormed());
}

CNA_STUDIO_TEST(ALayoutFileClaimingAPanelInTwoPlacesIsRefusedRatherThanLoaded)
{
    StudioDockTree tree = twoLeaves();
    JsonValue document = tree.toJson();

    JsonValue windows = JsonValue::makeArray();
    JsonValue duplicate = JsonValue::makeObject();
    JsonValue panels = JsonValue::makeArray();
    panels.append(JsonValue{std::string{"outliner"}});  // already docked
    duplicate.set("panels", std::move(panels));
    windows.append(std::move(duplicate));
    document.set("floating", std::move(windows));

    std::string problem;
    const StudioDockTree restored = StudioDockTree::fromJson(document, &problem);

    CNA_STUDIO_EXPECT(!problem.empty());
    CNA_STUDIO_EXPECT(restored.isWellFormed());
}

// --- The shell ---------------------------------------------------------------------------------

CNA_STUDIO_TEST(AFloatingPanelStillCountsAsOpen)
{
    // The Window menu's check marks read this, and a panel the user has just dragged into its own
    // window showing as closed would be the menu contradicting the screen in front of them.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->floatPanel("details"));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(shell->isPanelOpen("details"));
    CNA_STUDIO_EXPECT(shell->actions().isChecked(StudioShell::panelActionId("details")));

    // And Float is refused for a panel that already is one, rather than moving a window the user
    // did not ask to move.
    CNA_STUDIO_EXPECT(!shell->actions().isEnabled(StudioShell::floatPanelActionId("details")));
}

CNA_STUDIO_TEST(DraggingAFloatsTitleBarMovesItAndRaisesIt)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->floatPanel("details"));
    CNA_STUDIO_EXPECT(shell->floatPanel("history"));
    shell->renderFrame(at(-1.0f, -1.0f));

    // The one behind, so the drag must raise it as well as move it.
    CNA_STUDIO_EXPECT_EQ(shell->dockTree().findFloatingPanel("details"), std::size_t{0});
    const UiRect before = shell->dockTree().floating()[0].bounds;

    const UiRect title = titleBarOf(*shell, 0);
    shell->renderFrame(at(title.centerX(), title.centerY()));
    shell->renderFrame(at(title.centerX(), title.centerY(), true));
    shell->renderFrame(at(title.centerX() + 90.0f, title.centerY() + 60.0f, true));

    CNA_STUDIO_EXPECT_EQ(shell->dockTree().findFloatingPanel("details"), std::size_t{1});
    const UiRect after = shell->dockTree().floating()[1].bounds;
    CNA_STUDIO_EXPECT(std::abs((after.left() - before.left()) - 90.0f) <= 1.0f);
    CNA_STUDIO_EXPECT(std::abs((after.top() - before.top()) - 60.0f) <= 1.0f);

    // And it stops when the button comes up rather than following the pointer for ever.
    shell->renderFrame(at(title.centerX() + 90.0f, title.centerY() + 60.0f, false));
    shell->renderFrame(at(title.centerX() + 400.0f, title.centerY() + 300.0f, false));
    CNA_STUDIO_EXPECT_EQ(shell->dockTree().floating()[1].bounds.left(), after.left());
}

CNA_STUDIO_TEST(TheCornerGripResizesAndNeverBelowTheMinimum)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->floatPanel("details"));
    shell->renderFrame(at(-1.0f, -1.0f));

    const UiRect before = shell->dockTree().floating()[0].bounds;
    const float x = before.right() - 4.0f;
    const float y = before.bottom() - 4.0f;

    shell->renderFrame(at(x, y));
    shell->renderFrame(at(x, y, true));
    shell->renderFrame(at(x + 60.0f, y + 40.0f, true));

    const UiRect grown = shell->dockTree().floating()[0].bounds;
    CNA_STUDIO_EXPECT(std::abs((grown.width - before.width) - 60.0f) <= 1.0f);
    CNA_STUDIO_EXPECT(std::abs((grown.height - before.height) - 40.0f) <= 1.0f);

    // Dragged far past the corner it stops at a size that is still a window.
    shell->renderFrame(at(x - 2000.0f, y - 2000.0f, true));
    const UiRect shrunk = shell->dockTree().floating()[0].bounds;
    CNA_STUDIO_EXPECT(shrunk.width >= kMinimumFloatingExtent);
    CNA_STUDIO_EXPECT(shrunk.height >= kMinimumFloatingExtent);
}

CNA_STUDIO_TEST(AFloatBlocksThePanelBeneathItFromRespondingToThePointer)
{
    // The failure no screenshot can catch: a tab under a floating window lighting up as the
    // pointer crosses the window that covers it.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    const UiRect layers = shell->panelTabBounds("layers");
    CNA_STUDIO_EXPECT(!layers.isEmpty());

    // A window placed exactly over that tab.
    CNA_STUDIO_EXPECT(shell->floatPanel("details"));
    shell->dockTree().floatingAt(0).x = layers.centerX() - shell->layout().dockArea.left() - 40.0f;
    shell->dockTree().floatingAt(0).y = layers.centerY() - shell->layout().dockArea.top() - 40.0f;
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->dockTree().floating()[0].bounds.contains(layers.centerX(),
                                                                      layers.centerY()));

    shell->renderFrame(at(layers.centerX(), layers.centerY()));
    CNA_STUDIO_EXPECT_EQ(shell->frame().router().blockingLayer(), StudioShell::kFloatingLayer);

    // The tab under the window is not what the pointer is on, however much the dock tree agrees
    // the tab is there.
    CNA_STUDIO_EXPECT(shell->frame().router().pointerTargetId()
                      != shell->frame().ids().make("layers"));

    // And away from the window the workspace answers again.
    const UiRect viewport = shell->panelBounds("viewport");
    shell->renderFrame(at(viewport.right() - 10.0f, viewport.bottom() - 10.0f));
    CNA_STUDIO_EXPECT_EQ(shell->frame().router().blockingLayer(), 0);
}

CNA_STUDIO_TEST(ClosingAFloatClosesEveryTabInItRatherThanOnlyTheOneShowing)
{
    // Leaving a window's other tabs open somewhere invisible would be a workspace the user cannot
    // account for.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->floatPanel("details"));
    CNA_STUDIO_EXPECT(shell->dockTree().movePanelToFloating("history", 0));
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT_EQ(shell->dockTree().floating()[0].panels.size(), std::size_t{2});

    const UiRect bounds = shell->dockTree().floating()[0].bounds;
    const float x = bounds.right() - 12.0f;
    const float y = bounds.top() + 12.0f;
    shell->renderFrame(at(x, y));
    shell->renderFrame(at(x, y, true));
    shell->renderFrame(at(x, y, false));

    CNA_STUDIO_EXPECT(shell->dockTree().floating().empty());
    CNA_STUDIO_EXPECT(!shell->isPanelOpen("details"));
    CNA_STUDIO_EXPECT(!shell->isPanelOpen("history"));
}

CNA_STUDIO_TEST(DockAllIsTheWayBackAndIsGreyedOutWhenThereIsNothingToRecover)
{
    // A window dragged almost off the screen needs one command to recover, or the answer becomes
    // "reset the layout", which costs the user everything else they arranged.
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(!shell->actions().isEnabled(std::string{kStudioDockAllActionId}));

    CNA_STUDIO_EXPECT(shell->floatPanel("details"));
    CNA_STUDIO_EXPECT(shell->floatPanel("history"));
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->actions().isEnabled(std::string{kStudioDockAllActionId}));

    shell->invoke(std::string{kStudioDockAllActionId});
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(shell->dockTree().floating().empty());
    CNA_STUDIO_EXPECT(shell->isPanelOpen("details"));
    CNA_STUDIO_EXPECT(shell->isPanelOpen("history"));
    CNA_STUDIO_EXPECT(!shell->actions().isEnabled(std::string{kStudioDockAllActionId}));
    CNA_STUDIO_EXPECT(shell->dockTree().isWellFormed());
}

CNA_STUDIO_TEST(AFloatSurvivesSavingAndRestoringTheWorkspace)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    CNA_STUDIO_EXPECT(shell->floatPanel("details"));
    shell->renderFrame(at(-1.0f, -1.0f));
    const JsonValue saved = shell->saveLayout();

    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->dockTree().floating().empty());

    std::string problem;
    CNA_STUDIO_EXPECT(shell->loadLayout(saved, &problem));
    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT_EQ(shell->dockTree().floating().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(shell->isPanelOpen("details"));
}
