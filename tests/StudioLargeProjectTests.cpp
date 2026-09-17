// SPDX-License-Identifier: MS-PL
/**
 * @file StudioLargeProjectTests.cpp
 * @brief A hundred thousand assets, browsed (`plan.md` STUDIO-09016, STUDIO-30022).
 *
 * The property under test is the one that does not show up in a screenshot and that every editor
 * loses gradually: **what a project costs to browse must depend on the folder, not on the project.**
 * Every pass that was O(project) here was invisible at two hundred assets and fatal at a hundred
 * thousand, and each of them looked perfectly reasonable when it was written.
 *
 * These are counted rather than timed. A wall-clock assertion on a shared CI machine is a test that
 * fails for reasons that have nothing to do with the code; counting *how much work is done* says
 * the same thing and says it the same way on every machine.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

using namespace CNA::Studio;

namespace
{
    /** @brief How many assets the stress cases use. */
    constexpr std::size_t kAssets = 100000;

    /** @brief How many top-level folders they are spread over. */
    constexpr std::size_t kFolders = 50;

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

    /**
     * @brief Fills @p assets with @p kAssets records over @p kFolders folders, two levels deep.
     *
     * No files on disk and no project root: this is about the *index*, and the filesystem has its
     * own tests. A hundred thousand real files would make the suite take minutes to say something
     * about arithmetic.
     */
    void fill(AssetDatabase& assets)
    {
        for (std::size_t i = 0; i < kAssets; ++i)
        {
            AssetRecord record;
            record.id = Uuid::generate();

            // Two levels, because a flat directory exercises neither the subtree skipping nor the
            // ancestor walk the counts do.
            record.sourcePath = "Assets/Folder" + std::to_string(i % kFolders) + "/Sub"
                              + std::to_string((i / kFolders) % 4) + "/asset"
                              + std::to_string(i) + ".png";
            record.type = AssetType::Texture2D;
            assets.add(std::move(record));
        }
    }
}

CNA_STUDIO_TEST(BrowsingAFolderOfAHundredThousandAssetsCostsWhatTheFolderHolds)
{
    AssetDatabase assets;
    fill(assets);
    CNA_STUDIO_EXPECT_EQ(assets.getCount(), kAssets);

    // The project root shows the folders under it and nothing else, however many assets are down
    // there. Listing it used to walk every record twice -- once for the contents and once per
    // subfolder for its count.
    const std::vector<StudioContentCard> root = studioContentCards(assets, "Assets", Uuid{});
    CNA_STUDIO_EXPECT_EQ(root.size(), kFolders);
    for (const StudioContentCard& card : root) { CNA_STUDIO_EXPECT(card.isFolder()); }

    // And each of them says how much is under it, which is the question a card answers.
    CNA_STUDIO_EXPECT_EQ(root.front().detail, std::to_string(kAssets / kFolders) + " items");

    // One leaf folder: its own files, and nothing about the other ninety-nine thousand.
    const std::vector<StudioContentCard> leaf =
        studioContentCards(assets, "Assets/Folder0/Sub0", Uuid{});
    CNA_STUDIO_EXPECT_EQ(leaf.size(), kAssets / kFolders / 4);
    for (const StudioContentCard& card : leaf) { CNA_STUDIO_EXPECT(!card.isFolder()); }

    // Sorted within the folder, because a rescan must not shuffle what the user is looking at.
    for (std::size_t i = 1; i < leaf.size(); ++i)
    {
        CNA_STUDIO_EXPECT(leaf[i - 1].label <= leaf[i].label);
    }
}

CNA_STUDIO_TEST(TheFolderCountsAreMaintainedRatherThanDerived)
{
    AssetDatabase assets;
    fill(assets);

    // Direct and cumulative, both O(1), and both answering the question their own surface asks.
    CNA_STUDIO_EXPECT_EQ(assets.getDirectAssetCount("Assets"), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(assets.getTotalAssetCount("Assets"), kAssets);
    CNA_STUDIO_EXPECT_EQ(assets.getDirectAssetCount("Assets/Folder0/Sub0"),
                         kAssets / kFolders / 4);
    CNA_STUDIO_EXPECT_EQ(assets.getTotalAssetCount("Assets/Folder0"), kAssets / kFolders);

    // They stay right through the operations that move a path, which is the only way an
    // incrementally maintained count goes wrong.
    const Uuid moved = assets.findByPath("Assets/Folder0/Sub0/asset0.png")->id;
    const std::size_t before = assets.getDirectAssetCount("Assets/Folder0/Sub0");

    AssetRecord replacement = *assets.find(moved);
    replacement.sourcePath = "Assets/Folder1/Sub0/asset0.png";
    CNA_STUDIO_EXPECT(assets.add(std::move(replacement)));

    CNA_STUDIO_EXPECT_EQ(assets.getDirectAssetCount("Assets/Folder0/Sub0"), before - 1);
    CNA_STUDIO_EXPECT_EQ(assets.getDirectAssetCount("Assets/Folder1/Sub0"), before + 1);
    CNA_STUDIO_EXPECT_EQ(assets.getTotalAssetCount("Assets"), kAssets);

    CNA_STUDIO_EXPECT(assets.removeRecord(moved));
    CNA_STUDIO_EXPECT_EQ(assets.getTotalAssetCount("Assets"), kAssets - 1);
    CNA_STUDIO_EXPECT_EQ(assets.getDirectAssetCount("Assets/Folder1/Sub0"), before);

    // A folder that has lost everything stops being a folder, rather than lingering as a zero.
    AssetDatabase small;
    AssetRecord only;
    only.id = Uuid::generate();
    only.sourcePath = "Assets/Lonely/one.png";
    const Uuid id = only.id;
    CNA_STUDIO_EXPECT(small.add(std::move(only)));
    CNA_STUDIO_EXPECT_EQ(small.getFolderPaths().size(), std::size_t{2});

    CNA_STUDIO_EXPECT(small.removeRecord(id));
    CNA_STUDIO_EXPECT(small.getFolderPaths().empty());
    CNA_STUDIO_EXPECT_EQ(small.getTotalAssetCount("Assets"), std::size_t{0});
}

CNA_STUDIO_TEST(TheFolderPaneOverAHundredThousandAssetsIsAboutFoldersNotAssets)
{
    AssetDatabase assets;
    fill(assets);

    StudioTreeState state;

    // Project, fifty folders, and their four subfolders each -- the shape of the tree, with nothing
    // proportional to the hundred thousand files under it.
    const std::vector<StudioTreeRow> rows = studioContentFolderRows(assets, {}, state);
    CNA_STUDIO_EXPECT_EQ(rows.size(), 1 + 1 + kFolders + kFolders * 4);

    // Collapsed, it is one row plus the project's own child, whatever is underneath.
    StudioTreeState collapsed;
    collapsed.setExpanded(std::string{kStudioContentRootRowId}, false);
    CNA_STUDIO_EXPECT_EQ(studioContentFolderRows(assets, {}, collapsed).size(), std::size_t{1});
}

CNA_STUDIO_TEST(DrawingAHundredThousandAssetsDescribesAScreenfulRatherThanAProject)
{
    // The whole of STUDIO-09016 through the real panel: a folder holding a hundred thousand files,
    // drawn, with the work done bounded by what fits rather than by what exists.
    //
    // Both presentations, because they are two surfaces over one folder and the property has to
    // hold on whichever the user left the panel in. It is also the case that caught the list out:
    // it culled its *drawing* from the first day and still built a hundred thousand rows to do it.
    for (const StudioContentView view : {StudioContentView::Grid, StudioContentView::List})
    {
        StudioContext context;
        AssetDatabase& assets = context.getAssets();

        for (std::size_t i = 0; i < kAssets; ++i)
        {
            AssetRecord record;
            record.id = Uuid::generate();

            // All in one folder, which is the case virtualisation exists for: the folder's listing
            // genuinely has a hundred thousand entries and forty of them are on screen.
            record.sourcePath = "Assets/Flat/asset" + std::to_string(i) + ".png";
            record.type = AssetType::Texture2D;
            assets.add(std::move(record));
        }

        StudioContentBrowserState state;
        state.view = view;
        state.folder = "Assets/Flat";
        state.folderPaneWidth = 0.0f;

        auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
        shell->resetLayout();
        shell->renderFrame(at(-1.0f, -1.0f));
        CNA_STUDIO_EXPECT(shell->activatePanel("content"));

        StudioContentBrowserResult drawn;
        CNA_STUDIO_EXPECT(shell->setPanelContent("content",
            [&](StudioFrame& frame, const UiRect& area) {
                const StudioContentBrowserResult pass =
                    studioContentBrowser(frame, area, context, state);
                if (frame.isDrawPass()) { drawn = pass; }
            }));
        shell->renderFrame(at(-1.0f, -1.0f));

        const std::string which =
            view == StudioContentView::Grid ? "the grid" : "the list";

        CNA_STUDIO_EXPECT_EQ(drawn.rowsTotal, kAssets);

        // A screenful, with the window's one row of slack at each end. Not "fewer than a hundred
        // thousand" -- a bound that loose would pass with a per-item cull, which walks them all.
        if (drawn.rowsDrawn == 0 || drawn.rowsDrawn > 400)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         which + " described " + std::to_string(drawn.rowsDrawn)
                                             + " items for a screenful of a "
                                             + std::to_string(kAssets) + "-asset folder.");
        }

        // And -- the assertion the drawn count cannot make -- a screenful is also all that was
        // *built*. This is the number the old list got wrong: it culled its drawing perfectly and
        // constructed a hundred thousand rows, each with three strings, twice a frame to do it.
        if (drawn.rowsBuilt == 0 || drawn.rowsBuilt > 400)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         which + " built " + std::to_string(drawn.rowsBuilt)
                                             + " cards for a screenful of a "
                                             + std::to_string(kAssets) + "-asset folder.");
        }

        CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
        CNA_STUDIO_EXPECT_EQ(shell->frame().ids().collisionCount(), std::size_t{0});

        // Drawing asked the filesystem nothing, at a hundred thousand assets as at four hundred
        // (STUDIO-30015). This is the pass that would have made 200 000 stat calls.
        const std::uint64_t probes = assets.getPresenceProbeCount();
        shell->renderFrame(at(-1.0f, -1.0f));
        CNA_STUDIO_EXPECT_EQ(assets.getPresenceProbeCount(), probes);
    }
}

CNA_STUDIO_TEST(ScrollingAHundredThousandAssetsShowsTheRowsTheScrollbarSaysItDoes)
{
    // The failure a window makes possible and a full list cannot: rows drawn at the wrong height,
    // or a slice indexed from its own start while positioned from the list's. It shows up as the
    // browser scrolling to the middle of a folder and displaying the first forty files.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    for (std::size_t i = 0; i < kAssets; ++i)
    {
        AssetRecord record;
        record.id = Uuid::generate();

        // Zero-padded, so path order and numeric order are the same and "the six hundredth row"
        // means one particular file rather than whichever way the strings happened to sort.
        std::string number = std::to_string(i);
        number.insert(0, 6 - std::min<std::size_t>(6, number.size()), '0');
        record.sourcePath = "Assets/Flat/asset" + number + ".png";
        record.type = AssetType::Texture2D;
        assets.add(std::move(record));
    }

    StudioContentBrowserState state;
    state.view = StudioContentView::List;
    state.folder = "Assets/Flat";
    state.folderPaneWidth = 0.0f;

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
            if (frame.isDrawPass())
            {
                drawn = pass;
                bounds = area;
            }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(bounds.height > 0.0f);

    // Scrolled a long way down by the wheel, which is the gesture that moves the window: a notch
    // is three rows, so this lands well past anything the first window held.
    const float insideX = bounds.left() + 30.0f;
    const float insideY = bounds.centerY();

    for (int turn = 0; turn < 200; ++turn)
    {
        UiInputState input = at(insideX, insideY);
        input.wheelY = -1.0f;
        shell->renderFrame(input);
    }
    shell->renderFrame(at(insideX, insideY));

    // Still a screenful, still built rather than merely drawn, and still no phase disagreement --
    // which is what a window asked *before* the wheel was consumed would have broken, because the
    // two passes of a scrolling frame would then have held different rows under the same ids.
    CNA_STUDIO_EXPECT(drawn.rowsDrawn > 0);
    CNA_STUDIO_EXPECT(drawn.rowsBuilt <= 400);
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(shell->frame().ids().collisionCount(), std::size_t{0});

    // Clicking a visible row selects the asset the scroll position says is there, rather than the
    // first file in the folder. At the point the wheel was turned, which is known to be over the
    // listing rather than over the bar above it -- and a sweep from the panel's top edge would
    // start on the breadcrumb, navigate, and test nothing.
    shell->renderFrame(at(insideX, insideY, /*leftDown=*/true));
    shell->renderFrame(at(insideX, insideY, /*leftDown=*/false));

    const Uuid selected = context.getSelectedAsset();
    CNA_STUDIO_EXPECT(selected.isValid());

    const AssetRecord* record = assets.find(selected);
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record != nullptr)
    {
        // Somewhere around the six hundredth file, not the first -- which is what a slice indexed
        // from its own start, or drawn at its own offset, would have selected.
        CNA_STUDIO_EXPECT(record->sourcePath > "Assets/Flat/asset000400.png");
    }
}


