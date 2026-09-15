// SPDX-License-Identifier: MS-PL
/**
 * @file StudioOutlinerPanelTests.cpp
 * @brief The World Outliner, and the tree widget underneath it (plan.md STUDIO-07006).
 *
 * The second panel ported off Dear ImGui, and the first that reads the document model rather than
 * a log. The cases split along the seam that matters: what the tree *is* -- which rows, in which
 * order, at which depth -- is decided by `studioOutlinerRows` and asserted without a frame; what
 * the user can *do* to it is driven through the real widget with synthesised input.
 *
 * Splitting them is not tidiness. A wrong hierarchy and a dead click look identical from a
 * screenshot, and telling them apart is the difference between a five-minute fix and an afternoon.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

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

    /** @brief Adds an entity to @p scene and returns its id. */
    Uuid addEntity(SceneDocument& scene, const std::string& name, const Uuid& parent = {})
    {
        StudioEntity entity{Uuid::generate(), name};
        entity.setParentId(parent);
        const Uuid id = entity.getId();
        scene.addEntity(std::move(entity));
        return id;
    }

    /** @brief A context holding a small hierarchy: two roots, one of them with two children. */
    struct Fixture
    {
        StudioContext context;
        Uuid camera;
        Uuid player;
        Uuid weapon;
        Uuid shield;

        Fixture()
        {
            camera = addEntity(context.getScene(), "Main Camera");
            player = addEntity(context.getScene(), "Player");
            weapon = addEntity(context.getScene(), "Weapon", player);
            shield = addEntity(context.getScene(), "Shield", player);
        }
    };

    /** @brief Finds a row by label, or nullptr. */
    const StudioTreeRow* rowNamed(const std::vector<StudioTreeRow>& rows, std::string_view label)
    {
        for (const StudioTreeRow& row : rows)
        {
            if (row.label == label) { return &row; }
        }
        return nullptr;
    }

    /** @brief The shell, with the outliner raised and given the panel's content. */
    std::unique_ptr<StudioShell> shellShowingTheOutliner()
    {
        auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
        shell->resetLayout();
        shell->renderFrame(at(-1.0f, -1.0f));
        CNA_STUDIO_EXPECT(shell->activatePanel("outliner"));
        return shell;
    }
}

CNA_STUDIO_TEST(TheOutlinerShowsTheSceneHierarchyParentsBeforeChildren)
{
    Fixture fixture;
    StudioTreeState state;

    const std::vector<StudioTreeRow> rows =
        studioOutlinerRows(fixture.context.getScene(), {}, state);

    CNA_STUDIO_EXPECT_EQ(rows.size(), std::size_t{4});

    // Order is what makes indentation readable: a child drawn above its parent is not a tree.
    //
    // Within a level it is the scene document's own order -- sort order first, then name -- not
    // the order entities happened to be added in. The outliner does not get to invent an order:
    // showing siblings differently from how the scene holds them is how a user reorders something
    // in one place and cannot find it in another.
    CNA_STUDIO_EXPECT_EQ(rows[0].label, std::string{"Main Camera"});
    CNA_STUDIO_EXPECT_EQ(rows[1].label, std::string{"Player"});
    CNA_STUDIO_EXPECT_EQ(rows[2].label, std::string{"Shield"});
    CNA_STUDIO_EXPECT_EQ(rows[3].label, std::string{"Weapon"});

    CNA_STUDIO_EXPECT_EQ(rows[0].depth, 0);
    CNA_STUDIO_EXPECT_EQ(rows[1].depth, 0);
    CNA_STUDIO_EXPECT_EQ(rows[2].depth, 1);
    CNA_STUDIO_EXPECT_EQ(rows[3].depth, 1);

    // Only a row with children gets a disclosure triangle. One on a leaf is a promise the tree
    // cannot keep, and a user clicks it once and learns to distrust the whole column.
    CNA_STUDIO_EXPECT(rows[1].hasChildren);
    CNA_STUDIO_EXPECT(!rows[0].hasChildren);
    CNA_STUDIO_EXPECT(!rows[2].hasChildren);

    // And the same order the scene itself reports, rather than one this panel arrived at
    // independently and happens to agree with today.
    const std::vector<Uuid> children = fixture.context.getScene().getChildren(fixture.player);
    CNA_STUDIO_EXPECT_EQ(children.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(rows[2].id, children[0].toString());
    CNA_STUDIO_EXPECT_EQ(rows[3].id, children[1].toString());
}

CNA_STUDIO_TEST(CollapsingARowHidesItsChildrenAndNothingElse)
{
    Fixture fixture;
    StudioTreeState state;

    state.setExpanded(fixture.player.toString(), false);
    const std::vector<StudioTreeRow> collapsed =
        studioOutlinerRows(fixture.context.getScene(), {}, state);

    CNA_STUDIO_EXPECT_EQ(collapsed.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(rowNamed(collapsed, "Main Camera") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(collapsed, "Player") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(collapsed, "Weapon") == nullptr);

    // The collapsed parent keeps its triangle -- it is how the user gets its children back.
    const StudioTreeRow* player = rowNamed(collapsed, "Player");
    CNA_STUDIO_EXPECT(player != nullptr && player->hasChildren);

    state.setExpanded(fixture.player.toString(), true);
    CNA_STUDIO_EXPECT_EQ(studioOutlinerRows(fixture.context.getScene(), {}, state).size(),
                         std::size_t{4});
}

CNA_STUDIO_TEST(ATreeStartsOpenRatherThanMakingTheUserFindItsContents)
{
    // A tree that starts entirely collapsed shows one line and makes the user work to discover
    // that their scene has anything in it. Defaulting to open costs a scroll; defaulting to closed
    // costs a click per level before anything can be seen.
    Fixture fixture;
    const StudioTreeState fresh;

    CNA_STUDIO_EXPECT(fresh.isExpanded(fixture.player.toString()));
    CNA_STUDIO_EXPECT_EQ(fresh.collapsedCount(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(studioOutlinerRows(fixture.context.getScene(), {}, fresh).size(),
                         std::size_t{4});
}

CNA_STUDIO_TEST(TheOutlinerMarksWhatIsSelected)
{
    Fixture fixture;
    StudioTreeState state;

    const std::vector<StudioTreeRow> none =
        studioOutlinerRows(fixture.context.getScene(), {}, state);
    for (const StudioTreeRow& row : none) { CNA_STUDIO_EXPECT(!row.selected); }

    const std::vector<StudioTreeRow> some =
        studioOutlinerRows(fixture.context.getScene(), {fixture.weapon, fixture.camera}, state);

    CNA_STUDIO_EXPECT(rowNamed(some, "Weapon")->selected);
    CNA_STUDIO_EXPECT(rowNamed(some, "Main Camera")->selected);
    CNA_STUDIO_EXPECT(!rowNamed(some, "Player")->selected);
}

CNA_STUDIO_TEST(ClickingARowSelectsItThroughTheContext)
{
    // Through the context, not into a field of the panel's own. The viewport, the inspector and
    // the gizmos all read the context's selection, and a panel with a private one disagrees with
    // the rest of the editor the moment anything else changes it.
    Fixture fixture;
    StudioTreeState state;

    const std::unique_ptr<StudioShell> shell = shellShowingTheOutliner();

    UiRect panelBounds;
    CNA_STUDIO_EXPECT(shell->setPanelContent("outliner",
        [&](StudioFrame& frame, const UiRect& bounds) {
            (void)studioOutlinerPanel(frame, bounds, fixture.context, state);
            if (frame.isDrawPass()) { panelBounds = bounds; }
        }));

    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(!panelBounds.isEmpty());
    CNA_STUDIO_EXPECT(fixture.context.getSelection().empty());

    // The first row, whatever height the theme gives it. Found by walking down from the top of the
    // panel rather than by assuming a row height, so a metric change does not turn this into a
    // test that clicks empty space and passes for the wrong reason.
    bool selectedSomething = false;
    for (float y = panelBounds.top() + 2.0f;
         y < panelBounds.top() + 80.0f && !selectedSomething; y += 3.0f)
    {
        const float x = panelBounds.centerX();
        shell->renderFrame(at(x, y, true));
        shell->renderFrame(at(x, y, false));
        selectedSomething = !fixture.context.getSelection().empty();
    }

    CNA_STUDIO_EXPECT(selectedSomething);
    CNA_STUDIO_EXPECT_EQ(fixture.context.getSelection().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(fixture.context.getSelection().front() == fixture.camera);
}

CNA_STUDIO_TEST(AnEmptyOutlinerSaysWhichEmptyItIs)
{
    // "No project is open" and "this scene is empty" call for different next actions. A panel that
    // gave the same words for both would send half its readers looking in the wrong place.
    StudioContext empty;
    StudioTreeState state;

    const std::unique_ptr<StudioShell> shell = shellShowingTheOutliner();

    std::size_t rowsTotal = 1;
    CNA_STUDIO_EXPECT(shell->setPanelContent("outliner",
        [&](StudioFrame& frame, const UiRect& bounds) {
            const StudioOutlinerResult result = studioOutlinerPanel(frame, bounds, empty, state);
            if (frame.isDrawPass()) { rowsTotal = result.rowsTotal; }
        }));

    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT_EQ(rowsTotal, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(ADeepSceneIsDrawnRatherThanDescended)
{
    // Two failures guarded at once. A chain thousands deep must not recurse until the stack runs
    // out -- a crash on opening somebody's scene is the worst outcome an outliner has -- and a
    // scene with thousands of entities must cost a screenful, not a scene.
    StudioContext context;
    StudioTreeState state;

    Uuid parent;
    for (int i = 0; i < 2000; ++i)
    {
        parent = addEntity(context.getScene(), "entity " + std::to_string(i), parent);
    }

    const std::vector<StudioTreeRow> rows =
        studioOutlinerRows(context.getScene(), {}, state);

    // Depth-limited, so the flattener returns rather than running out of stack.
    CNA_STUDIO_EXPECT(!rows.empty());
    CNA_STUDIO_EXPECT(rows.size() <= std::size_t{2000});

    const std::unique_ptr<StudioShell> shell = shellShowingTheOutliner();
    std::size_t drawn = 0;
    CNA_STUDIO_EXPECT(shell->setPanelContent("outliner",
        [&](StudioFrame& frame, const UiRect& bounds) {
            const StudioOutlinerResult result = studioOutlinerPanel(frame, bounds, context, state);
            if (frame.isDrawPass()) { drawn = result.rowsDrawn; }
        }));

    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(drawn > 0);
    CNA_STUDIO_EXPECT(drawn < std::size_t{100});
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

// ------------------------------------------------------------------------------------------------
// Showing and hiding from the row (STUDIO-35060)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryOutlinerRowCarriesAVisibilityToggle)
{
    // An outliner where hiding an entity means selecting it, finding the Details panel and
    // unticking a box is one where nobody hides anything -- and hiding things is how a large scene
    // is worked on at all. The affordance has to be on the row.
    SceneDocument scene;
    const Uuid visible = scene.addEntity(StudioEntity{Uuid::generate(), "Visible"});
    StudioEntity hiddenEntity{Uuid::generate(), "Hidden"};
    hiddenEntity.setEnabled(false);
    const Uuid hidden = scene.addEntity(std::move(hiddenEntity));

    StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioOutlinerRows(scene, {}, state);

    const auto rowFor = [&rows](const Uuid& id) -> const StudioTreeRow* {
        for (const StudioTreeRow& row : rows)
        {
            if (row.id == id.toString()) { return &row; }
        }
        return nullptr;
    };

    CNA_STUDIO_EXPECT(rowFor(visible) != nullptr);
    CNA_STUDIO_EXPECT(rowFor(visible)->toggleIcon == StudioIcon::Visible);
    CNA_STUDIO_EXPECT(rowFor(visible)->toggleOffIcon == StudioIcon::Hidden);
    CNA_STUDIO_EXPECT(rowFor(visible)->toggleOn);

    // The pair has to be one drawing with one difference or the control reads as two unrelated
    // states rather than as on and off, and the tooltip says what the click will *do* rather than
    // what the state *is* -- "Hidden" on a button is a label a user has to invert to use.
    CNA_STUDIO_EXPECT(!rowFor(hidden)->toggleOn);
    CNA_STUDIO_EXPECT_EQ(rowFor(visible)->toggleTooltip, std::string{"Hide this entity"});
    CNA_STUDIO_EXPECT_EQ(rowFor(hidden)->toggleTooltip, std::string{"Show this entity"});
}
