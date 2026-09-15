// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDockTests.cpp
 * @brief The dock node tree: structure, minimum sizes, splitter movement and persistence.
 *
 * The workspace arrangement is a user's working environment, expected to survive a restart, an
 * upgrade and a hand edit. That makes it data with invariants rather than a side effect of drawing,
 * and every one of those invariants is checked here with no window anywhere in sight.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioDockTree.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kSplitter = 4.0f;
    constexpr float kTabStrip = 24.0f;
    const UiRect kArea{0.0f, 0.0f, 1200.0f, 800.0f};

    /** @brief A three-leaf tree: a left dock, a centre, and a bottom strip under both. */
    StudioDockTree buildWorkspace(StudioDockNodeId& left, StudioDockNodeId& bottom,
                                  StudioDockNodeId& centre)
    {
        StudioDockTree tree;
        centre = tree.root();
        bottom = tree.split(centre, StudioDockSide::Bottom, 0.3f);
        centre = tree.sibling(bottom);
        left = tree.split(centre, StudioDockSide::Left, 0.2f);
        centre = tree.sibling(left);

        tree.addPanel(left, "outliner");
        tree.addPanel(bottom, "content");
        tree.addPanel(bottom, "output");
        tree.addPanel(centre, "viewport");
        tree.layout(kArea, kSplitter, kTabStrip);
        return tree;
    }
}

// ------------------------------------------------------------------------------------------------
// Structure (STUDIO-05001, STUDIO-05002)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ANewTreeIsOneEmptyLeaf)
{
    const StudioDockTree tree;
    CNA_STUDIO_EXPECT(tree.isLive(tree.root()));
    CNA_STUDIO_EXPECT(tree.node(tree.root()).isLeaf());
    CNA_STUDIO_EXPECT_EQ(tree.leaves().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(tree.splits().empty());
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(SplittingALeafKeepsItsPanelsAndAddsAnEmptyNeighbour)
{
    StudioDockTree tree;
    tree.addPanel(tree.root(), "viewport");

    const StudioDockNodeId created = tree.split(tree.root(), StudioDockSide::Left, 0.25f);
    CNA_STUDIO_EXPECT(created != kInvalidDockNode);
    CNA_STUDIO_EXPECT(!tree.node(tree.root()).isLeaf());
    CNA_STUDIO_EXPECT(tree.node(created).panels.empty());

    const StudioDockNodeId kept = tree.sibling(created);
    CNA_STUDIO_EXPECT_EQ(tree.node(kept).panels.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(tree.node(kept).panels.front(), std::string{"viewport"});
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(SplittingASplitIsRefusedRatherThanGuessedAt)
{
    StudioDockTree tree;
    tree.split(tree.root(), StudioDockSide::Left, 0.25f);
    CNA_STUDIO_EXPECT_EQ(tree.split(tree.root(), StudioDockSide::Top, 0.5f), kInvalidDockNode);
}

CNA_STUDIO_TEST(APanelLivesInExactlyOnePlace)
{
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    StudioDockTree tree = buildWorkspace(left, bottom, centre);

    // Adding a docked panel somewhere else moves it rather than producing two tabs that both
    // claim to be the outliner.
    tree.addPanel(bottom, "outliner");
    CNA_STUDIO_EXPECT_EQ(tree.findPanel("outliner"), bottom);
    CNA_STUDIO_EXPECT(tree.isWellFormed());

    std::size_t occurrences = 0;
    for (const std::string& panel : tree.panels()) { if (panel == "outliner") { ++occurrences; } }
    CNA_STUDIO_EXPECT_EQ(occurrences, std::size_t{1});
}

CNA_STUDIO_TEST(RemovingTheLastPanelCollapsesItsLeafAndItsSplit)
{
    // A leaf nobody can see still takes its share of the split, so without collapsing, closing the
    // last panel in a dock leaves a permanent empty stripe that no gesture can remove.
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    StudioDockTree tree = buildWorkspace(left, bottom, centre);

    CNA_STUDIO_EXPECT_EQ(tree.leaves().size(), std::size_t{3});
    CNA_STUDIO_EXPECT(tree.removePanel("outliner"));
    CNA_STUDIO_EXPECT_EQ(tree.leaves().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(tree.isWellFormed());

    tree.layout(kArea, kSplitter, kTabStrip);
    const StudioDockNodeId viewport = tree.findPanel("viewport");
    CNA_STUDIO_EXPECT_EQ(tree.node(viewport).bounds.left(), kArea.left());
}

CNA_STUDIO_TEST(RemovingTheOnlyPanelLeavesAnEmptyRootRatherThanNoTree)
{
    StudioDockTree tree;
    tree.addPanel(tree.root(), "viewport");
    CNA_STUDIO_EXPECT(tree.removePanel("viewport"));

    CNA_STUDIO_EXPECT(tree.isLive(tree.root()));
    CNA_STUDIO_EXPECT(tree.node(tree.root()).isLeaf());
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(RemovingATabLeftOfTheActiveOneKeepsTheSamePanelShowing)
{
    StudioDockTree tree;
    tree.addPanel(tree.root(), "content");
    tree.addPanel(tree.root(), "output");
    tree.addPanel(tree.root(), "build");
    tree.activatePanel("build");

    CNA_STUDIO_EXPECT(tree.removePanel("content"));
    const StudioDockNode& leaf = tree.node(tree.root());
    CNA_STUDIO_EXPECT_EQ(leaf.panels[leaf.activePanel], std::string{"build"});
}

CNA_STUDIO_TEST(MovingAPanelPutsItWhereItWasAskedFor)
{
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    StudioDockTree tree = buildWorkspace(left, bottom, centre);

    CNA_STUDIO_EXPECT(tree.movePanel("outliner", bottom, 0));
    CNA_STUDIO_EXPECT_EQ(tree.node(bottom).panels.front(), std::string{"outliner"});
    CNA_STUDIO_EXPECT_EQ(tree.node(bottom).activePanel, std::size_t{0});
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

// ------------------------------------------------------------------------------------------------
// Geometry and minimums (STUDIO-05002, STUDIO-05003)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ResolvedLeavesTileTheAreaWithoutOverlapping)
{
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    const StudioDockTree tree = buildWorkspace(left, bottom, centre);

    const std::vector<StudioDockNodeId> leaves = tree.leaves();
    for (const StudioDockNodeId leaf : leaves)
    {
        const UiRect& bounds = tree.node(leaf).bounds;
        CNA_STUDIO_EXPECT(bounds.left() >= kArea.left() - 0.01f);
        CNA_STUDIO_EXPECT(bounds.right() <= kArea.right() + 0.01f);
        CNA_STUDIO_EXPECT(bounds.top() >= kArea.top() - 0.01f);
        CNA_STUDIO_EXPECT(bounds.bottom() <= kArea.bottom() + 0.01f);
    }

    for (std::size_t i = 0; i < leaves.size(); ++i)
    {
        for (std::size_t j = i + 1; j < leaves.size(); ++j)
        {
            CNA_STUDIO_EXPECT(tree.node(leaves[i]).bounds
                                  .intersect(tree.node(leaves[j]).bounds)
                                  .isEmpty());
        }
    }
}

CNA_STUDIO_TEST(ASplitterSitsExactlyBetweenTheChildrenItSeparates)
{
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    const StudioDockTree tree = buildWorkspace(left, bottom, centre);

    for (const StudioDockNodeId id : tree.splits())
    {
        const StudioDockNode& node = tree.node(id);
        const UiRect& a = tree.node(node.first).bounds;
        const UiRect& b = tree.node(node.second).bounds;

        if (node.orientation == StudioDockOrientation::Horizontal)
        {
            CNA_STUDIO_EXPECT_EQ(node.splitter.left(), a.right());
            CNA_STUDIO_EXPECT_EQ(node.splitter.right(), b.left());
            CNA_STUDIO_EXPECT_EQ(node.splitter.width, kSplitter);
        }
        else
        {
            CNA_STUDIO_EXPECT_EQ(node.splitter.top(), a.bottom());
            CNA_STUDIO_EXPECT_EQ(node.splitter.bottom(), b.top());
            CNA_STUDIO_EXPECT_EQ(node.splitter.height, kSplitter);
        }
    }
}

CNA_STUDIO_TEST(ProportionsSurviveAResize)
{
    // Fractions rather than pixel widths: dragging a window to a larger monitor must rescale the
    // arrangement, not leave the inspector 300px wide on a 4K display.
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    StudioDockTree tree = buildWorkspace(left, bottom, centre);

    const float small = tree.node(left).bounds.width / kArea.width;
    tree.layout(UiRect{0.0f, 0.0f, kArea.width * 2.0f, kArea.height * 2.0f}, kSplitter, kTabStrip);
    const float large = tree.node(left).bounds.width / (kArea.width * 2.0f);

    CNA_STUDIO_EXPECT(std::abs(small - large) < 0.01f);
}

CNA_STUDIO_TEST(ALeafsMinimumIncludesItsTabStrip)
{
    // Without the strip in the minimum, dragging a horizontal splitter to its limit leaves a dock
    // that is all tabs and no panel.
    StudioDockTree tree;
    tree.addPanel(tree.root(), "viewport");
    tree.layout(kArea, kSplitter, kTabStrip, 100.0f);

    CNA_STUDIO_EXPECT_EQ(tree.minimumWidth(), 100.0f);
    CNA_STUDIO_EXPECT_EQ(tree.minimumHeight(), 100.0f + kTabStrip);
}

CNA_STUDIO_TEST(ASplitsMinimumIsTheSumOfItsChildrensAlongItsAxis)
{
    StudioDockTree tree;
    const StudioDockNodeId created = tree.split(tree.root(), StudioDockSide::Left, 0.5f);
    (void) created;
    tree.layout(kArea, kSplitter, kTabStrip, 100.0f);

    CNA_STUDIO_EXPECT_EQ(tree.minimumWidth(), 200.0f);
    CNA_STUDIO_EXPECT_EQ(tree.minimumHeight(), 100.0f + kTabStrip);
}

CNA_STUDIO_TEST(AnAreaTooSmallForTheMinimumsScalesBothChildrenRatherThanStarvingOne)
{
    // Clamping the first child to its minimum would leave the second nothing at all, which reads
    // as a panel that has vanished rather than as a cramped workspace.
    StudioDockTree tree;
    tree.split(tree.root(), StudioDockSide::Left, 0.5f);
    tree.layout(UiRect{0.0f, 0.0f, 120.0f, 400.0f}, kSplitter, kTabStrip, 100.0f);

    for (const StudioDockNodeId leaf : tree.leaves())
    {
        CNA_STUDIO_EXPECT(tree.node(leaf).bounds.width > 0.0f);
    }
}

CNA_STUDIO_TEST(DraggingASplitterMovesItAndStopsAtBothMinimums)
{
    StudioDockTree tree;
    const StudioDockNodeId created = tree.split(tree.root(), StudioDockSide::Left, 0.25f);
    (void) created;
    tree.layout(kArea, kSplitter, kTabStrip, 100.0f);

    const StudioDockNodeId split = tree.root();
    const float before = tree.node(tree.node(split).first).bounds.width;

    CNA_STUDIO_EXPECT(tree.moveSplitter(split, 100.0f, 100.0f));
    tree.layout(kArea, kSplitter, kTabStrip, 100.0f);
    CNA_STUDIO_EXPECT(std::abs(tree.node(tree.node(split).first).bounds.width - (before + 100.0f))
                      < 0.01f);

    // Push it far past the end: it stops rather than inverting or eating its neighbour.
    for (int i = 0; i < 50; ++i)
    {
        tree.moveSplitter(split, 500.0f, 100.0f);
        tree.layout(kArea, kSplitter, kTabStrip, 100.0f);
    }
    CNA_STUDIO_EXPECT(tree.node(tree.node(split).second).bounds.width >= 100.0f - 0.01f);
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(ALeafsTabStripSitsAboveItsBodyAndTheyTileIt)
{
    StudioDockTree tree;
    tree.addPanel(tree.root(), "viewport");
    tree.layout(kArea, kSplitter, kTabStrip);

    const StudioDockLeafGeometry geometry = tree.leafGeometry(tree.root(), kTabStrip);
    CNA_STUDIO_EXPECT_EQ(geometry.tabStrip.height, kTabStrip);
    CNA_STUDIO_EXPECT_EQ(geometry.tabStrip.bottom(), geometry.body.top());
    CNA_STUDIO_EXPECT_EQ(geometry.body.bottom(), kArea.bottom());
}

// ------------------------------------------------------------------------------------------------
// Persistence (STUDIO-05008, STUDIO-05011, STUDIO-05012)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ALayoutRoundTripsThroughJson)
{
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    StudioDockTree tree = buildWorkspace(left, bottom, centre);
    tree.activatePanel("output");

    const JsonValue document = tree.toJson();
    std::string problem;
    const StudioDockTree restored = StudioDockTree::fromJson(document, &problem);

    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT(restored.isWellFormed());
    CNA_STUDIO_EXPECT_EQ(restored.panels().size(), tree.panels().size());
    CNA_STUDIO_EXPECT_EQ(restored.leaves().size(), tree.leaves().size());

    const StudioDockNodeId restoredBottom = restored.findPanel("output");
    const StudioDockNode& node = restored.node(restoredBottom);
    CNA_STUDIO_EXPECT_EQ(node.panels[node.activePanel], std::string{"output"});
}

CNA_STUDIO_TEST(ASerializedLayoutCarriesNoResolvedPixels)
{
    // Pixels belong to the window the layout was last shown in, not to the layout. A document that
    // stored them would restore a 4K arrangement onto a laptop.
    StudioDockTree tree;
    tree.addPanel(tree.root(), "viewport");
    tree.layout(kArea, kSplitter, kTabStrip);

    const std::string text = Json::write(tree.toJson());
    CNA_STUDIO_EXPECT(text.find("bounds") == std::string::npos);
    CNA_STUDIO_EXPECT(text.find("1200") == std::string::npos);
}

CNA_STUDIO_TEST(ACorruptLayoutYieldsTheDefaultRatherThanAFailure)
{
    for (const char* text : {"{}", "[]", "null",
                             R"({"version":1,"root":{"kind":"nonsense"}})",
                             R"({"version":1,"root":{"kind":"split","first":{"kind":"leaf"}}})"})
    {
        const JsonParseResult parsed = Json::parse(text);
        std::string problem;
        const StudioDockTree tree =
            StudioDockTree::fromJson(parsed.succeeded ? parsed.value : JsonValue{}, &problem);

        CNA_STUDIO_EXPECT(!problem.empty());
        CNA_STUDIO_EXPECT(tree.isWellFormed());
        CNA_STUDIO_EXPECT_EQ(tree.leaves().size(), std::size_t{1});
    }
}

CNA_STUDIO_TEST(ALayoutFromANewerStudioIsRefusedWithAReason)
{
    // Refused rather than guessed at: a newer Studio may have written node kinds this one cannot
    // represent, and silently dropping them would lose a workspace the user built.
    const JsonParseResult parsed = Json::parse(R"({"version":99,"root":{"kind":"leaf"}})");
    std::string problem;
    const StudioDockTree tree = StudioDockTree::fromJson(parsed.value, &problem);

    CNA_STUDIO_EXPECT(problem.find("newer") != std::string::npos);
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

CNA_STUDIO_TEST(ADeeplyNestedLayoutIsRefusedRatherThanOverflowingTheStack)
{
    std::string text = R"({"version":1,"root":)";
    std::string tail;
    for (int i = 0; i < 200; ++i)
    {
        text += R"({"kind":"split","orientation":"vertical","fraction":0.5,"second":{"kind":"leaf"},"first":)";
        tail += "}";
    }
    text += R"({"kind":"leaf"})" + tail + "}";

    const JsonParseResult parsed = Json::parse(text);
    CNA_STUDIO_EXPECT(parsed.succeeded);

    std::string problem;
    const StudioDockTree tree = StudioDockTree::fromJson(parsed.value, &problem);
    CNA_STUDIO_EXPECT(!problem.empty());
    CNA_STUDIO_EXPECT(tree.isWellFormed());
}

// ------------------------------------------------------------------------------------------------
// The shell's workspace (STUDIO-05007, STUDIO-05009, STUDIO-05011, STUDIO-05012)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheShellsWorkspaceRoundTripsAndRestores)
{
    StudioShell shell;
    UiInputState input;
    input.displayWidth = 1600.0f;
    input.displayHeight = 900.0f;
    shell.renderFrame(input);

    shell.closePanel("layers");
    shell.renderFrame(input);
    const JsonValue saved = shell.saveLayout();

    shell.resetLayout();
    shell.renderFrame(input);
    CNA_STUDIO_EXPECT(shell.isPanelOpen("layers"));

    std::string problem;
    CNA_STUDIO_EXPECT(shell.loadLayout(saved, &problem));
    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT(!shell.isPanelOpen("layers"));
    CNA_STUDIO_EXPECT(shell.isPanelOpen("viewport"));
}

CNA_STUDIO_TEST(APanelThisBuildNoLongerHasIsDroppedAndNamed)
{
    StudioShell shell;
    const JsonParseResult parsed = Json::parse(
        R"({"version":1,"root":{"kind":"split","orientation":"horizontal","fraction":0.3,)"
        R"("first":{"kind":"leaf","panels":["retired-panel"],"active":0},)"
        R"("second":{"kind":"leaf","panels":["viewport"],"active":0}}})");

    std::string problem;
    CNA_STUDIO_EXPECT(!shell.loadLayout(parsed.value, &problem));
    CNA_STUDIO_EXPECT(problem.find("retired-panel") != std::string::npos);
    CNA_STUDIO_EXPECT(shell.isPanelOpen("viewport"));
    CNA_STUDIO_EXPECT(shell.dockTree().isWellFormed());
}

CNA_STUDIO_TEST(APanelTheUserCannotCloseIsRestoredHoweverTheDocumentArrived)
{
    StudioShell shell;
    const JsonParseResult parsed =
        Json::parse(R"({"version":1,"root":{"kind":"leaf","panels":["outliner"],"active":0}})");

    std::string problem;
    CNA_STUDIO_EXPECT(!shell.loadLayout(parsed.value, &problem));
    CNA_STUDIO_EXPECT(shell.isPanelOpen("viewport"));
    CNA_STUDIO_EXPECT(problem.find("viewport") != std::string::npos);
}

CNA_STUDIO_TEST(TheViewportCannotBeClosed)
{
    StudioShell shell;
    CNA_STUDIO_EXPECT(!shell.closePanel("viewport"));
    CNA_STUDIO_EXPECT(shell.isPanelOpen("viewport"));
}

CNA_STUDIO_TEST(AClosedPanelReopensSomewhereVisible)
{
    StudioShell shell;
    UiInputState input;
    input.displayWidth = 1600.0f;
    input.displayHeight = 900.0f;
    shell.renderFrame(input);

    CNA_STUDIO_EXPECT(shell.closePanel("problems"));
    shell.renderFrame(input);
    CNA_STUDIO_EXPECT(!shell.isPanelOpen("problems"));

    CNA_STUDIO_EXPECT(shell.openPanel("problems"));
    shell.renderFrame(input);
    CNA_STUDIO_EXPECT(shell.isPanelOpen("problems"));
    CNA_STUDIO_EXPECT(!shell.panelTabBounds("problems").isEmpty());
}

CNA_STUDIO_TEST(ResetLayoutIsReachableFromTheWindowMenu)
{
    StudioShell shell;
    UiInputState input;
    input.displayWidth = 1600.0f;
    input.displayHeight = 900.0f;
    shell.renderFrame(input);

    shell.closePanel("layers");
    shell.renderFrame(input);
    CNA_STUDIO_EXPECT(!shell.isPanelOpen("layers"));

    CNA_STUDIO_EXPECT(shell.actions().invoke("studio.window.resetLayout")
                      == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT(shell.isPanelOpen("layers"));
}

CNA_STUDIO_TEST(AWorkspaceThatIsNotSoundIsReplacedRatherThanDrawn)
{
    // A layout read from a file somebody edited by hand is not a layout this code wrote, so the
    // shell checks before describing anything rather than drawing a broken workspace.
    StudioShell shell;
    shell.dockTree().node(shell.dockTree().root()).first = 9999;

    UiInputState input;
    input.displayWidth = 1600.0f;
    input.displayHeight = 900.0f;
    shell.renderFrame(input);

    CNA_STUDIO_EXPECT(shell.dockTree().isWellFormed());
    CNA_STUDIO_EXPECT(shell.isPanelOpen("viewport"));
    CNA_STUDIO_EXPECT(!shell.refusedActions().empty());
}

// ------------------------------------------------------------------------------------------------
// Resizing the window without losing the arrangement (STUDIO-04011)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AFloatSurvivesAWindowThatShrinksAndGrowsBackAgain)
{
    // The clamp that keeps a float reachable used to overwrite the position it was clamping, so a
    // window briefly made small carried its floats into the corner and left them there. Resizing
    // is not an edit: the user did not move that palette, and finding it moved after restoring the
    // window is the kind of thing people stop trusting a layout over.
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    StudioDockTree tree = buildWorkspace(left, bottom, centre);

    const std::size_t index = tree.floatPanel("outliner", 760.0f, 520.0f, 360.0f, 240.0f);
    CNA_STUDIO_EXPECT(index != kInvalidFloatingDock);
    if (index == kInvalidFloatingDock) { return; }

    tree.layout(kArea, kSplitter, kTabStrip);
    const UiRect placed = tree.floating()[index].bounds;
    CNA_STUDIO_EXPECT(placed.width > 0.0f);

    // Small enough that the float cannot fit where it was put.
    tree.layout(UiRect{0.0f, 0.0f, 500.0f, 400.0f}, kSplitter, kTabStrip);
    const UiRect squeezed = tree.floating()[index].bounds;

    // Still wholly inside the smaller window, which is the point of the clamp.
    CNA_STUDIO_EXPECT(squeezed.left() >= -0.5f);
    CNA_STUDIO_EXPECT(squeezed.top() >= -0.5f);
    CNA_STUDIO_EXPECT(squeezed.right() <= 500.5f);
    CNA_STUDIO_EXPECT(squeezed.bottom() <= 400.5f);

    // And back. This is the assertion the clamp used to fail.
    tree.layout(kArea, kSplitter, kTabStrip);
    const UiRect restored = tree.floating()[index].bounds;
    CNA_STUDIO_EXPECT_EQ(restored.x, placed.x);
    CNA_STUDIO_EXPECT_EQ(restored.y, placed.y);
    CNA_STUDIO_EXPECT_EQ(restored.width, placed.width);
    CNA_STUDIO_EXPECT_EQ(restored.height, placed.height);
}

CNA_STUDIO_TEST(MovingAFloatInASmallWindowIsAnEditAndSticks)
{
    // The other half: a clamp that never wrote back would make the *user's own* drag in a small
    // window snap away the moment the window grew. Resizing is not an edit; dragging is.
    StudioDockNodeId left = kInvalidDockNode;
    StudioDockNodeId bottom = kInvalidDockNode;
    StudioDockNodeId centre = kInvalidDockNode;
    StudioDockTree tree = buildWorkspace(left, bottom, centre);

    const std::size_t index = tree.floatPanel("outliner", 760.0f, 520.0f, 360.0f, 240.0f);
    if (index == kInvalidFloatingDock) { return; }

    tree.layout(UiRect{0.0f, 0.0f, 500.0f, 400.0f}, kSplitter, kTabStrip);
    tree.floatingAt(index).x = 20.0f;
    tree.floatingAt(index).y = 30.0f;
    tree.layout(UiRect{0.0f, 0.0f, 500.0f, 400.0f}, kSplitter, kTabStrip);

    tree.layout(kArea, kSplitter, kTabStrip);
    CNA_STUDIO_EXPECT_EQ(tree.floating()[index].bounds.x, 20.0f);
    CNA_STUDIO_EXPECT_EQ(tree.floating()[index].bounds.y, 30.0f);
}
