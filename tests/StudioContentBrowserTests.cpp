// SPDX-License-Identifier: MS-PL
/**
 * @file StudioContentBrowserTests.cpp
 * @brief The Content Browser: folders derived from paths, and missing sources shown as missing.
 *
 * `plan.md` STUDIO-07008.
 *
 * The two things worth checking are the two this panel could get wrong without looking wrong. The
 * folder tree is *derived* from project-relative paths rather than read from disk, so an ordering
 * or nesting mistake produces a tree that renders perfectly and describes a project nobody has. And
 * an asset whose source file has gone is still a tracked asset — dropping it from the list would
 * turn "you moved a folder" into "your scene is broken and nothing said why".
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace CNA::Studio;

namespace
{
    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 720.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A temporary project root with real files in it, removed on the way out. */
    class ScopedProject
    {
    public:
        explicit ScopedProject(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-content-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_, code);
        }

        ~ScopedProject()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedProject(const ScopedProject&) = delete;
        ScopedProject& operator=(const ScopedProject&) = delete;

        [[nodiscard]] std::string root() const { return path_.generic_string(); }

        /** @brief Creates an empty file at @p relative, parent directories included. */
        void write(const std::string& relative) const
        {
            const std::filesystem::path file = path_ / relative;
            std::error_code code;
            std::filesystem::create_directories(file.parent_path(), code);
            std::ofstream stream{file, std::ios::binary};
            stream << "x";
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    /** @brief Adds a tracked asset at @p path and returns its id. */
    Uuid track(AssetDatabase& assets, const std::string& path, AssetType type)
    {
        AssetRecord record;
        record.id = Uuid::generate();
        record.sourcePath = path;
        record.type = type;
        const Uuid id = record.id;
        CNA_STUDIO_EXPECT(assets.add(std::move(record)));
        return id;
    }

    const StudioTreeRow* rowNamed(const std::vector<StudioTreeRow>& rows, std::string_view label)
    {
        for (const StudioTreeRow& row : rows)
        {
            if (row.label == label) { return &row; }
        }
        return nullptr;
    }

    /** @brief The index of the row labelled @p label, or -1. */
    int indexOf(const std::vector<StudioTreeRow>& rows, std::string_view label)
    {
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].label == label) { return static_cast<int>(i); }
        }
        return -1;
    }
}

CNA_STUDIO_TEST(FilesSortWithinTheirFolderSoARescanDoesNotShuffleThem)
{
    // Was asserted on the list's whole-project tree until STUDIO-09002 made the list a second
    // presentation of one folder. The ordering matters for the same reason it always did: the
    // database's own order is insertion order, and a browser whose files moved about as the
    // project was rescanned would be unusable.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);
    track(assets, "Assets/Textures/enemy.png", AssetType::Texture2D);
    track(assets, "Assets/Textures/boss.png", AssetType::Texture2D);

    const std::vector<StudioContentCard> cards =
        studioContentCards(assets, "Assets/Textures", Uuid{});

    CNA_STUDIO_EXPECT_EQ(cards.size(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(cards[0].label, std::string{"boss.png"});
    CNA_STUDIO_EXPECT_EQ(cards[1].label, std::string{"enemy.png"});
    CNA_STUDIO_EXPECT_EQ(cards[2].label, std::string{"player.png"});
}

CNA_STUDIO_TEST(AnAssetWhoseFileHasGoneIsListedAndMarked)
{
    // Dropping it would turn "you moved a folder" into "your scene is broken and nothing said
    // why". A scene references the asset by id, and the record is what makes the problem fixable.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    const Uuid gone = track(assets, "Assets/Textures/player.png", AssetType::Texture2D);

    // There is no such file: the database is not pointed at a real project root, so every record
    // is missing. That is the condition under test, and it is what a moved folder looks like.
    CNA_STUDIO_EXPECT(assets.isMissing(gone));

    const std::vector<StudioContentCard> cards =
        studioContentCards(assets, "Assets/Textures", Uuid{});
    CNA_STUDIO_EXPECT_EQ(cards.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(cards.front().missing);
    CNA_STUDIO_EXPECT_EQ(cards.front().detail, std::string{"missing"});
}

CNA_STUDIO_TEST(AFileShowsItsTypeAndAFolderShowsHowMuchIsInIt)
{
    // Against files that really exist, because a missing asset shows "missing" in place of its
    // type -- which is the right answer and the wrong thing to be asserting here.
    const ScopedProject project{"types"};

    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    assets.setProjectRoot(project.root());
    project.write("Assets/Models/crate.gltf");
    project.write("Assets/Models/barrel.gltf");

    track(assets, "Assets/Models/crate.gltf", AssetType::Model);
    track(assets, "Assets/Models/barrel.gltf", AssetType::Model);

    // The type, not the extension: an importer decides what a file *is*, and two extensions can
    // map to one type.
    const std::vector<StudioContentCard> cards =
        studioContentCards(assets, "Assets/Models", Uuid{});
    CNA_STUDIO_EXPECT_EQ(cards.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(cards.front().detail, std::string{toString(AssetType::Model)});

    // And the count beside the folder, which the navigation pane carries now.
    const StudioTreeState state;
    const std::vector<StudioTreeRow> folders = studioContentFolderRows(assets, {}, state);
    CNA_STUDIO_EXPECT_EQ(rowNamed(folders, "Models")->detail, std::string{"2"});
}

CNA_STUDIO_TEST(ClickingAFileSelectsItAndClickingAFolderDoesNot)
{
    // A folder's row id is its path and a file's is a UUID, so the parse is what tells them apart.
    // Selecting a folder as though it were an asset would put a nil id where the inspector expects
    // a real one.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    const Uuid file = track(assets, "Assets/player.png", AssetType::Texture2D);

    // The list view, because what this case is about is the tree: a click landing on the file row
    // rather than on the folder above it. The grid has its own cases below.
    StudioContentBrowserState state;
    state.view = StudioContentView::List;
    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->activatePanel("content"));

    UiRect bounds;
    CNA_STUDIO_EXPECT(shell->setPanelContent("content",
        [&](StudioFrame& frame, const UiRect& area) {
            (void)studioContentBrowser(frame, area, context, state);
            if (frame.isDrawPass()) { bounds = area; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(!bounds.isEmpty());

    // The first row is the "Assets" folder; the file is below it. Sweeping rather than assuming a
    // row height, so a metric change cannot turn this into a test that clicks empty space.
    bool selectedTheFile = false;
    for (float y = bounds.top() + 2.0f; y < bounds.top() + 120.0f && !selectedTheFile; y += 3.0f)
    {
        const float x = bounds.centerX();
        shell->renderFrame(at(x, y, false));
        shell->renderFrame(at(x, y, true));
        shell->renderFrame(at(x, y, false));
        selectedTheFile = context.getSelectedAsset() == file;
    }

    CNA_STUDIO_EXPECT(selectedTheFile);
}

CNA_STUDIO_TEST(AProjectWithNoAssetsSaysSoRatherThanShowingNothing)
{
    StudioContext context;
    StudioContentBrowserState state;

    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->activatePanel("content"));

    StudioContentBrowserResult result;
    CNA_STUDIO_EXPECT(shell->setPanelContent("content",
        [&](StudioFrame& frame, const UiRect& area) {
            const StudioContentBrowserResult pass =
                studioContentBrowser(frame, area, context, state);
            if (frame.isDrawPass()) { result = pass; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT_EQ(result.rowsTotal, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(result.missingCount, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

// ------------------------------------------------------------------------------------------------
// The grid (STUDIO-35040)
//
// Two things fail separately here and a screenshot cannot tell them apart: deciding *what a folder
// holds*, which is arithmetic over a database, and deciding *where a card goes*, which is layout.
// studioContentCards is the first, and is tested with no frame at all.
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheGridShowsOneFoldersImmediateContentsAndNotTheWholeProject)
{
    // The difference between the grid and the tree beside it. A grid of every asset under a folder
    // is a wall, and the folder a user is *in* is the unit they think in.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);
    track(assets, "Assets/Textures/enemy.png", AssetType::Texture2D);
    track(assets, "Assets/Models/crate.gltf", AssetType::Model);
    track(assets, "Assets/Models/detail/bolt.gltf", AssetType::Model);

    // At the root: one folder, no files.
    const std::vector<StudioContentCard> root = studioContentCards(assets, {}, Uuid{});
    CNA_STUDIO_EXPECT_EQ(root.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(root.front().isFolder());
    CNA_STUDIO_EXPECT_EQ(root.front().label, std::string{"Assets"});

    // Inside Assets: two folders, no files. Not the four assets underneath them.
    const std::vector<StudioContentCard> inside =
        studioContentCards(assets, "Assets", Uuid{});
    CNA_STUDIO_EXPECT_EQ(inside.size(), std::size_t{2});
    for (const StudioContentCard& card : inside) { CNA_STUDIO_EXPECT(card.isFolder()); }

    // Inside Textures: two files and no folder.
    const std::vector<StudioContentCard> textures =
        studioContentCards(assets, "Assets/Textures", Uuid{});
    CNA_STUDIO_EXPECT_EQ(textures.size(), std::size_t{2});
    for (const StudioContentCard& card : textures)
    {
        CNA_STUDIO_EXPECT(!card.isFolder());
        // The kind's icon, unless the file has gone -- which it has here, because the database is
        // not pointed at a real project root. Written as the rule rather than as the answer,
        // because the override is deliberate and a test asserting Texture unconditionally would be
        // a test demanding the override be removed.
        CNA_STUDIO_EXPECT(card.icon
                          == (card.missing ? StudioIcon::Warning : StudioIcon::Texture));
    }

    // And Models, which has both: the folder comes first. A user navigating is looking for a
    // folder; a user browsing is looking at assets, and the first is the one interrupted by having
    // to scan past the second.
    const std::vector<StudioContentCard> models =
        studioContentCards(assets, "Assets/Models", Uuid{});
    CNA_STUDIO_EXPECT_EQ(models.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(models.front().isFolder());
    CNA_STUDIO_EXPECT(!models.back().isFolder());
}

CNA_STUDIO_TEST(AFolderCardSaysHowMuchIsUnderIt)
{
    // Everything underneath, not only the immediate children: "3 items" on a folder a user has not
    // opened is the number that tells them whether opening it is worth the click.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    track(assets, "Assets/Models/crate.gltf", AssetType::Model);
    track(assets, "Assets/Models/detail/bolt.gltf", AssetType::Model);
    track(assets, "Assets/Models/detail/nut.gltf", AssetType::Model);

    const std::vector<StudioContentCard> cards =
        studioContentCards(assets, "Assets", Uuid{});
    CNA_STUDIO_EXPECT_EQ(cards.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(cards.front().detail, std::string{"3 items"});
}

CNA_STUDIO_TEST(TheBreadcrumbNamesTheRootAndEveryLevelBelowIt)
{
    const auto crumbs = studioContentBreadcrumb("Assets/Models/detail");
    CNA_STUDIO_EXPECT_EQ(crumbs.size(), std::size_t{4});

    // "Project" rather than "Assets", although the root usually contains a folder called Assets --
    // which is exactly why: "Assets / Assets / Models" is a user wondering which of the two they
    // are in.
    CNA_STUDIO_EXPECT_EQ(crumbs[0].first, std::string{"Project"});
    CNA_STUDIO_EXPECT(crumbs[0].second.empty());
    CNA_STUDIO_EXPECT_EQ(crumbs[1].first, std::string{"Assets"});
    CNA_STUDIO_EXPECT_EQ(crumbs[1].second, std::string{"Assets"});
    CNA_STUDIO_EXPECT_EQ(crumbs[3].first, std::string{"detail"});
    CNA_STUDIO_EXPECT_EQ(crumbs[3].second, std::string{"Assets/Models/detail"});

    // The root alone is still a crumb. A breadcrumb that vanished at the top would leave nothing
    // to say where the user is when they are where they started.
    CNA_STUDIO_EXPECT_EQ(studioContentBreadcrumb({}).size(), std::size_t{1});
}

CNA_STUDIO_TEST(AMissingAssetsCardSaysSoInTheWarningColour)
{
    // The one card whose *state* matters more than its kind. A folder of two hundred textures with
    // one missing is a folder where the missing one has to be findable without reading any of them.
    // There is no such file: the database is not pointed at a real project root, so every record
    // is missing. That is the condition under test, and it is what a moved folder looks like.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    track(assets, "Assets/Textures/gone.png", AssetType::Texture2D);

    const std::vector<StudioContentCard> cards =
        studioContentCards(assets, "Assets/Textures", Uuid{});
    CNA_STUDIO_EXPECT_EQ(cards.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(cards.front().missing);
    CNA_STUDIO_EXPECT(cards.front().icon == StudioIcon::Warning);
    CNA_STUDIO_EXPECT(cards.front().iconRole == StudioColorRole::Warning);
    CNA_STUDIO_EXPECT_EQ(cards.front().detail, std::string{"missing"});
}

CNA_STUDIO_TEST(BothViewsHaveANameAndTheGridIsTheDefault)
{
    CNA_STUDIO_EXPECT(!studioContentViewName(StudioContentView::List).empty());
    CNA_STUDIO_EXPECT(!studioContentViewName(StudioContentView::Grid).empty());

    // The default is the grid, which is what every professional content browser defaults to and
    // for a reason about content rather than fashion: an asset is a thing with an appearance, and
    // a browser that shows only its name is a file manager.
    CNA_STUDIO_EXPECT(StudioContentBrowserState{}.view == StudioContentView::Grid);
}

// ------------------------------------------------------------------------------------------------
// The folder pane (STUDIO-09001)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheFolderPaneShowsFoldersAndNoFilesAtAll)
{
    // The whole difference between this tree and the list's, and the difference that makes a
    // folder tree worth having: a tree holding every asset in the project is a second copy of the
    // content pane, and the reason to have a tree is to move between folders without reading them.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);
    track(assets, "Assets/Models/crate.gltf", AssetType::Model);
    track(assets, "readme.txt", AssetType::RawData);

    const StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioContentFolderRows(assets, {}, state);

    CNA_STUDIO_EXPECT(rowNamed(rows, "Project") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(rows, "Assets") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(rows, "Textures") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(rows, "Models") != nullptr);

    // Not one file, including the one at the project root -- which is the case an implementation
    // that filtered on "has a slash in it" would get wrong.
    CNA_STUDIO_EXPECT(rowNamed(rows, "player.png") == nullptr);
    CNA_STUDIO_EXPECT(rowNamed(rows, "crate.gltf") == nullptr);
    CNA_STUDIO_EXPECT(rowNamed(rows, "readme.txt") == nullptr);

    // Parents before children, and one deeper than the list's because `Project` is above them all.
    CNA_STUDIO_EXPECT(indexOf(rows, "Project") < indexOf(rows, "Assets"));
    CNA_STUDIO_EXPECT(indexOf(rows, "Assets") < indexOf(rows, "Models"));
    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Project")->depth, 0);
    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Assets")->depth, 1);
    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Textures")->depth, 2);
}

CNA_STUDIO_TEST(TheProjectRootIsARowAndItIsWhereTheEmptyFolderPathPointsAt)
{
    // A tree whose only way back to the top is collapsing everything is a tree people navigate by
    // clicking the breadcrumb instead, which makes half the pane decoration.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);

    const StudioTreeState state;

    const std::vector<StudioTreeRow> atRoot = studioContentFolderRows(assets, {}, state);
    CNA_STUDIO_EXPECT(rowNamed(atRoot, "Project")->selected);
    CNA_STUDIO_EXPECT(!rowNamed(atRoot, "Assets")->selected);

    // The id is a constant rather than the empty string: an empty id would share its expansion
    // state with every row that had not been given one.
    CNA_STUDIO_EXPECT_EQ(rowNamed(atRoot, "Project")->id, std::string{kStudioContentRootRowId});

    const std::vector<StudioTreeRow> inTextures =
        studioContentFolderRows(assets, "Assets/Textures", state);
    CNA_STUDIO_EXPECT(!rowNamed(inTextures, "Project")->selected);
    CNA_STUDIO_EXPECT(rowNamed(inTextures, "Textures")->selected);

    // And a folder's id is its path, so navigating is one assignment rather than a lookup.
    CNA_STUDIO_EXPECT_EQ(rowNamed(inTextures, "Textures")->id, std::string{"Assets/Textures"});
}

CNA_STUDIO_TEST(AFolderPaneRowCountsWhatIsDirectlyInItRatherThanEverythingUnderIt)
{
    // Cumulative counts would make `Assets` read as holding the whole project, which is true and
    // useless: the number a user wants beside a folder is how much they will see when they click.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    track(assets, "Assets/notes.txt", AssetType::RawData);
    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);
    track(assets, "Assets/Textures/enemy.png", AssetType::Texture2D);

    const StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioContentFolderRows(assets, {}, state);

    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Assets")->detail, std::string{"1"});
    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Textures")->detail, std::string{"2"});

    // A folder holding only other folders shows no count rather than a zero: "0" beside a folder
    // full of subfolders reads as empty.
    CNA_STUDIO_EXPECT(rowNamed(rows, "Project")->detail.empty());
}

CNA_STUDIO_TEST(AFolderWithSubfoldersGetsATriangleEvenWithNoFilesOfItsOwn)
{
    // The one thing a tree must never do is present a leaf that turns out to have children.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);

    const StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioContentFolderRows(assets, {}, state);

    CNA_STUDIO_EXPECT(rowNamed(rows, "Assets")->hasChildren);
    CNA_STUDIO_EXPECT(!rowNamed(rows, "Textures")->hasChildren);

    // And a sibling whose name is a prefix of another's is not mistaken for its parent:
    // `Assets2` is not inside `Assets`, however the strings sort.
    track(assets, "Assets2/readme.txt", AssetType::RawData);
    const std::vector<StudioTreeRow> again = studioContentFolderRows(assets, {}, state);
    CNA_STUDIO_EXPECT(rowNamed(again, "Assets2") != nullptr);
    CNA_STUDIO_EXPECT(!rowNamed(again, "Assets2")->hasChildren);
}

CNA_STUDIO_TEST(CollapsingInTheFolderPaneHidesDescendantsAndCollapsingTheRootHidesEverything)
{
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);

    StudioTreeState state;

    // Open by default, which is what a folder tree should be: the state stores *collapsed* ids, so
    // a user opening the Content Browser for the first time sees their folders rather than one row.
    CNA_STUDIO_EXPECT_EQ(studioContentFolderRows(assets, {}, state).size(), std::size_t{3});

    // Collapsed root: the row is still there -- it is where "go to the top" lives -- and nothing
    // below it is.
    state.setExpanded(std::string{kStudioContentRootRowId}, false);
    const std::vector<StudioTreeRow> folded = studioContentFolderRows(assets, {}, state);
    CNA_STUDIO_EXPECT_EQ(folded.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(rowNamed(folded, "Project") != nullptr);

    state.setExpanded(std::string{kStudioContentRootRowId}, true);
    state.setExpanded("Assets", false);
    const std::vector<StudioTreeRow> partial = studioContentFolderRows(assets, {}, state);
    CNA_STUDIO_EXPECT(rowNamed(partial, "Assets") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(partial, "Textures") == nullptr);
}

CNA_STUDIO_TEST(TheFolderPaneIsBesideBothViewsAndClickingAFolderNavigatesThere)
{
    // Beside both presentations rather than inside either. Where a user is and what they are
    // looking at are two questions, and a navigation tree that appeared in only one view would
    // make switching views also mean switching how you move around.
    for (const StudioContentView view : {StudioContentView::Grid, StudioContentView::List})
    {
        ScopedProject project{"folderpane"};
        project.write("Assets/Textures/player.png");
        project.write("Assets/Models/crate.gltf");

        StudioContext context;
        context.getAssets().setProjectRoot(project.root());
        track(context.getAssets(), "Assets/Textures/player.png", AssetType::Texture2D);
        track(context.getAssets(), "Assets/Models/crate.gltf", AssetType::Model);

        StudioContentBrowserState state;
        state.view = view;

        auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
        shell->resetLayout();
        shell->renderFrame(at(-1.0f, -1.0f));
        CNA_STUDIO_EXPECT(shell->activatePanel("content"));

        StudioContentBrowserResult drawn;
        UiRect bounds;
        CNA_STUDIO_EXPECT(shell->setPanelContent("content",
            [&](StudioFrame& frame, const UiRect& area) {
                const StudioContentBrowserResult pass =
                    studioContentBrowser(frame, area, context, state);
                if (frame.isDrawPass()) { drawn = pass; bounds = area; }
            }));
        shell->renderFrame(at(-1.0f, -1.0f));

        if (drawn.folderRowsDrawn == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"the folder pane drew nothing in "}
                + std::string{studioContentViewName(view)} + " view.");
            continue;
        }

        // Project, Assets, Models, Textures.
        CNA_STUDIO_EXPECT_EQ(drawn.folderRowsTotal, std::size_t{4});
        CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});

        // Clicking a folder navigates there, in both views. Swept rather than assuming a row
        // height, so a metric change cannot turn this into a test that clicks empty space.
        bool navigated = false;
        for (float y = bounds.top() + 2.0f; y < bounds.top() + 160.0f && !navigated; y += 3.0f)
        {
            const float x = bounds.left() + 40.0f;
            shell->renderFrame(at(x, y, false));
            shell->renderFrame(at(x, y, true));
            shell->renderFrame(at(x, y, false));
            navigated = state.folder == "Assets/Models" || state.folder == "Assets/Textures";
        }

        if (!navigated)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"clicking the folder pane navigated nowhere in "}
                + std::string{studioContentViewName(view)} + " view.");
        }
    }
}
