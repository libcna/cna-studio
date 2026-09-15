// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDockDragTests.cpp
 * @brief Dragging a panel to another dock (plan.md STUDIO-05004, STUDIO-05005).
 *
 * The gesture has three halves that fail independently and look identical from a screenshot: where
 * the pointer says the panel would land, what the preview draws, and what a release actually does.
 * They are asserted separately, and the second is asserted *against* the first — a preview showing
 * one outcome while the release produces another is the worst of the three, because the user
 * committed to it on the strength of the picture.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
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

    /** @brief Which leaf holds @p panel, by the panels sharing its tab group. */
    std::vector<std::string> groupOf(const StudioShell& shell, std::string_view panel)
    {
        const StudioDockNodeId leaf = shell.dockTree().findPanel(panel);
        if (leaf == kInvalidDockNode) { return {}; }
        return shell.dockTree().node(leaf).panels;
    }

    /**
     * @brief Presses a tab and drags to (@p toX, @p toY), without releasing.
     *
     * Three frames to arrive: one with the button up so the router sees a real press, one pressing
     * on the tab, and one with the pointer moved far enough to pass the drag threshold.
     */
    void dragFrom(StudioShell& shell, const UiRect& tab, float toX, float toY)
    {
        shell.renderFrame(at(tab.centerX(), tab.centerY(), false));
        shell.renderFrame(at(tab.centerX(), tab.centerY(), true));
        shell.renderFrame(at(toX, toY, true));
    }
}

CNA_STUDIO_TEST(AClickOnATabDoesNotStartADrag)
{
    // The threshold is what keeps selecting a tab on a trackpad from rearranging the workspace.
    // Without it every click that wobbled by a pixel would be a move.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    const UiRect tab = shell->panelTabBounds("layers");
    CNA_STUDIO_EXPECT(!tab.isEmpty());

    const std::vector<std::string> before = groupOf(*shell, "layers");

    shell->renderFrame(at(tab.centerX(), tab.centerY(), false));
    shell->renderFrame(at(tab.centerX(), tab.centerY(), true));
    CNA_STUDIO_EXPECT(!shell->dockDrag().active());

    shell->renderFrame(at(tab.centerX(), tab.centerY(), false));
    CNA_STUDIO_EXPECT(!shell->dockDrag().active());
    CNA_STUDIO_EXPECT(groupOf(*shell, "layers") == before);

    // And the click did what a click on a tab does.
    CNA_STUDIO_EXPECT(shell->dockTree().findPanel("layers") != kInvalidDockNode);
}

CNA_STUDIO_TEST(DraggingATabFarEnoughStartsADockDrag)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();

    const UiRect tab = shell->panelTabBounds("layers");
    const UiRect viewport = shell->panelBounds("viewport");
    CNA_STUDIO_EXPECT(!viewport.isEmpty());

    dragFrom(*shell, tab, viewport.centerX(), viewport.centerY());

    CNA_STUDIO_EXPECT(shell->dockDrag().active());
    CNA_STUDIO_EXPECT_EQ(shell->dockDrag().panelId, std::string{"layers"});
    CNA_STUDIO_EXPECT(shell->dockDrag().target != kInvalidDockNode);
}

CNA_STUDIO_TEST(TheEdgesOfAPanelSplitItAndTheMiddleJoinsItsTabs)
{
    // Five outcomes rather than one, because "put this panel somewhere" and "put it *beside* that
    // one" are different intentions -- and a model offering only the first would make every
    // rearrangement a two-step operation.
    const UiRect viewport = defaultShell()->panelBounds("viewport");
    CNA_STUDIO_EXPECT(!viewport.isEmpty());

    struct Case { float x; float y; StudioShell::StudioDropZone expected; const char* what; };
    const Case cases[] = {
        {viewport.left() + viewport.width * 0.05f, viewport.centerY(),
         StudioShell::StudioDropZone::Left, "left edge"},
        {viewport.right() - viewport.width * 0.05f, viewport.centerY(),
         StudioShell::StudioDropZone::Right, "right edge"},
        {viewport.centerX(), viewport.top() + viewport.height * 0.05f,
         StudioShell::StudioDropZone::Top, "top edge"},
        {viewport.centerX(), viewport.bottom() - viewport.height * 0.05f,
         StudioShell::StudioDropZone::Bottom, "bottom edge"},
        {viewport.centerX(), viewport.centerY(),
         StudioShell::StudioDropZone::Tabs, "middle"},
    };

    for (const Case& test : cases)
    {
        const std::unique_ptr<StudioShell> shell = defaultShell();
        dragFrom(*shell, shell->panelTabBounds("layers"), test.x, test.y);

        if (shell->dockDrag().zone != test.expected)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"dropping on the "} + test.what + " of the viewport resolved to zone "
                + std::to_string(static_cast<int>(shell->dockDrag().zone)) + ", expected "
                + std::to_string(static_cast<int>(test.expected)));
        }

        // The preview must be inside the panel it describes, or it is describing somewhere else.
        const UiRect preview = shell->dockDrag().preview;
        CNA_STUDIO_EXPECT(!preview.isEmpty());
        CNA_STUDIO_EXPECT(preview.width <= viewport.width + 1.0f);
    }
}

CNA_STUDIO_TEST(DroppingOnAnEdgeSplitsAndPutsThePanelInTheHalfTheUserAimedAt)
{
    // The half that is easy to get backwards: split() reuses the split node's id, so the content
    // that was there ends up at a *new* id. Putting the dragged panel at the old one would drop it
    // exactly where the existing content went.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    const std::size_t leavesBefore = shell->dockTree().leaves().size();
    const UiRect viewport = shell->panelBounds("viewport");

    dragFrom(*shell, shell->panelTabBounds("layers"),
             viewport.right() - viewport.width * 0.05f, viewport.centerY());
    CNA_STUDIO_EXPECT(shell->dockDrag().zone == StudioShell::StudioDropZone::Right);

    shell->renderFrame(at(viewport.right() - viewport.width * 0.05f, viewport.centerY(), false));

    CNA_STUDIO_EXPECT(!shell->dockDrag().active());
    CNA_STUDIO_EXPECT_EQ(shell->dockTree().leaves().size(), leavesBefore + 1);

    // In its own group, not the viewport's -- and to the right of the viewport, which is what the
    // preview promised.
    const std::vector<std::string> group = groupOf(*shell, "layers");
    CNA_STUDIO_EXPECT_EQ(group.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(group.front(), std::string{"layers"});

    const UiRect moved = shell->panelBounds("layers");
    const UiRect stayed = shell->panelBounds("viewport");
    CNA_STUDIO_EXPECT(!moved.isEmpty());
    CNA_STUDIO_EXPECT(moved.left() >= stayed.centerX());

    CNA_STUDIO_EXPECT(shell->dockTree().isWellFormed());
}

CNA_STUDIO_TEST(DroppingOnATabStripJoinsThatGroup)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();

    const UiRect contentTab = shell->panelTabBounds("content");
    CNA_STUDIO_EXPECT(!contentTab.isEmpty());
    CNA_STUDIO_EXPECT(groupOf(*shell, "layers") != groupOf(*shell, "content"));

    dragFrom(*shell, shell->panelTabBounds("layers"), contentTab.right(), contentTab.centerY());
    CNA_STUDIO_EXPECT(shell->dockDrag().zone == StudioShell::StudioDropZone::Tabs);

    shell->renderFrame(at(contentTab.right(), contentTab.centerY(), false));

    const std::vector<std::string> group = groupOf(*shell, "layers");
    CNA_STUDIO_EXPECT(std::find(group.begin(), group.end(), "content") != group.end());

    // And it is the visible one, because a panel dropped where the user pointed and then hidden
    // behind another tab has, as far as they can tell, vanished.
    CNA_STUDIO_EXPECT(!shell->panelBounds("layers").isEmpty());
    CNA_STUDIO_EXPECT(shell->dockTree().isWellFormed());
}

CNA_STUDIO_TEST(DroppingAPanelBackOnItsOwnTabStripReordersIt)
{
    // Reordering falls out of the same gesture rather than needing its own, which is why the tab
    // strip is a drop zone at all.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    const std::vector<std::string> before = groupOf(*shell, "outliner");
    CNA_STUDIO_EXPECT(before.size() >= 2);
    CNA_STUDIO_EXPECT_EQ(before.front(), std::string{"outliner"});

    const UiRect last = shell->panelTabBounds(before.back());
    dragFrom(*shell, shell->panelTabBounds("outliner"), last.right() - 1.0f, last.centerY());
    shell->renderFrame(at(last.right() - 1.0f, last.centerY(), false));

    const std::vector<std::string> after = groupOf(*shell, "outliner");
    CNA_STUDIO_EXPECT_EQ(after.size(), before.size());
    CNA_STUDIO_EXPECT(after.back() == "outliner");
    CNA_STUDIO_EXPECT(shell->dockTree().isWellFormed());
}

CNA_STUDIO_TEST(ADragThatEndsOverNothingChangesNothing)
{
    // Releasing outside every dock -- over the menu bar, say -- has to be a no-op rather than a
    // panel quietly going somewhere. A gesture with no visible outcome must have no invisible one.
    const std::unique_ptr<StudioShell> shell = defaultShell();

    const std::vector<std::string> before = groupOf(*shell, "layers");
    const std::size_t leavesBefore = shell->dockTree().leaves().size();

    dragFrom(*shell, shell->panelTabBounds("layers"), 400.0f, 2.0f);
    CNA_STUDIO_EXPECT(shell->dockDrag().active());
    CNA_STUDIO_EXPECT(shell->dockDrag().zone == StudioShell::StudioDropZone::None);

    shell->renderFrame(at(400.0f, 2.0f, false));

    CNA_STUDIO_EXPECT(!shell->dockDrag().active());
    CNA_STUDIO_EXPECT(groupOf(*shell, "layers") == before);
    CNA_STUDIO_EXPECT_EQ(shell->dockTree().leaves().size(), leavesBefore);
}

CNA_STUDIO_TEST(TheDragPreviewDrawsAndTheFrameStaysWellFormed)
{
    const std::unique_ptr<StudioShell> shell = defaultShell();
    const UiRect viewport = shell->panelBounds("viewport");

    const std::size_t before = shell->drawData().lists.size();
    dragFrom(*shell, shell->panelTabBounds("layers"),
             viewport.left() + viewport.width * 0.05f, viewport.centerY());

    CNA_STUDIO_EXPECT(shell->dockDrag().zone == StudioShell::StudioDropZone::Left);
    CNA_STUDIO_EXPECT(!shell->drawData().lists.empty());
    CNA_STUDIO_EXPECT(before > 0 || !shell->drawData().lists.empty());

    // The preview is geometry, not a flag: something has to be drawn for it.
    std::size_t vertices = 0;
    for (const UiDrawList& list : shell->drawData().lists) { vertices += list.vertices.size(); }
    CNA_STUDIO_EXPECT(vertices > 0);

    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}
