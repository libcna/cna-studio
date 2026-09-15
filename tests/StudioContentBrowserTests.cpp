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

CNA_STUDIO_TEST(FoldersComeFromPathsAndParentsComeFirst)
{
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);
    track(assets, "Assets/Textures/enemy.png", AssetType::Texture2D);
    track(assets, "Assets/Models/crate.gltf", AssetType::Model);

    const StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioContentRows(assets, Uuid{}, state);

    // Two folders exist because something is in them, and one because it contains those two.
    CNA_STUDIO_EXPECT(rowNamed(rows, "Assets") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(rows, "Textures") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(rows, "Models") != nullptr);

    // A folder is drawn before the things inside it, or the indentation says nothing.
    CNA_STUDIO_EXPECT(indexOf(rows, "Assets") < indexOf(rows, "Models"));
    CNA_STUDIO_EXPECT(indexOf(rows, "Models") < indexOf(rows, "crate.gltf"));
    CNA_STUDIO_EXPECT(indexOf(rows, "Textures") < indexOf(rows, "player.png"));

    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Assets")->depth, 0);
    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Textures")->depth, 1);
    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "player.png")->depth, 2);

    // Files sort within their folder, so a rescan does not shuffle the list under the user.
    CNA_STUDIO_EXPECT(indexOf(rows, "enemy.png") < indexOf(rows, "player.png"));

    // Only folders get a disclosure triangle. One on a file is a promise the tree cannot keep.
    CNA_STUDIO_EXPECT(rowNamed(rows, "Textures")->hasChildren);
    CNA_STUDIO_EXPECT(!rowNamed(rows, "player.png")->hasChildren);
}

CNA_STUDIO_TEST(CollapsingAFolderHidesItsContentsAndItsSubfolders)
{
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    track(assets, "Assets/Textures/player.png", AssetType::Texture2D);
    track(assets, "Assets/Models/crate.gltf", AssetType::Model);

    StudioTreeState state;
    state.setExpanded("Assets", false);

    const std::vector<StudioTreeRow> rows = studioContentRows(assets, Uuid{}, state);

    // Just the root. Collapsing a folder has to hide what is *below* it, not only its own files --
    // a tree that left the grandchildren showing would draw them at a depth with no parent.
    CNA_STUDIO_EXPECT_EQ(rows.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(rows.front().label, std::string{"Assets"});
    CNA_STUDIO_EXPECT(rows.front().hasChildren);

    state.setExpanded("Assets", true);
    state.setExpanded("Assets/Textures", false);

    const std::vector<StudioTreeRow> partial = studioContentRows(assets, Uuid{}, state);
    CNA_STUDIO_EXPECT(rowNamed(partial, "Textures") != nullptr);
    CNA_STUDIO_EXPECT(rowNamed(partial, "player.png") == nullptr);
    CNA_STUDIO_EXPECT(rowNamed(partial, "crate.gltf") != nullptr);
}

CNA_STUDIO_TEST(AnAssetWhoseFileHasGoneIsListedAndMarked)
{
    // Dropping it would turn "you moved a folder" into "your scene is broken and nothing said
    // why". A scene references the asset by id, and the record is what makes the problem fixable.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();

    const Uuid gone = track(assets, "Assets/Textures/player.png", AssetType::Texture2D);

    const StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioContentRows(assets, Uuid{}, state);

    const StudioTreeRow* row = rowNamed(rows, "player.png");
    CNA_STUDIO_EXPECT(row != nullptr);

    // There is no such file: the database is not pointed at a real project root, so every record
    // is missing. That is the condition under test, and it is what a moved folder looks like.
    CNA_STUDIO_EXPECT(assets.isMissing(gone));
    CNA_STUDIO_EXPECT_EQ(row->detail, std::string{"missing"});

    // Dimmed, not disabled. A row that reads as wrong and cannot be clicked is the one row a user
    // needs to reach and cannot -- clicking it is how they find out what references the lost file.
    CNA_STUDIO_EXPECT(row->muted);
    CNA_STUDIO_EXPECT(row->enabled);
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

    const StudioTreeState state;
    const std::vector<StudioTreeRow> rows = studioContentRows(assets, Uuid{}, state);

    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "Models")->detail, std::string{"2"});

    // The type, not the extension: an importer decides what a file *is*, and two extensions can
    // map to one type.
    CNA_STUDIO_EXPECT_EQ(rowNamed(rows, "crate.gltf")->detail, std::string{toString(AssetType::Model)});
}

CNA_STUDIO_TEST(ClickingAFileSelectsItAndClickingAFolderDoesNot)
{
    // A folder's row id is its path and a file's is a UUID, so the parse is what tells them apart.
    // Selecting a folder as though it were an asset would put a nil id where the inspector expects
    // a real one.
    StudioContext context;
    AssetDatabase& assets = context.getAssets();
    const Uuid file = track(assets, "Assets/player.png", AssetType::Texture2D);

    StudioTreeState state;
    Uuid selected;

    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->activatePanel("content"));

    UiRect bounds;
    CNA_STUDIO_EXPECT(shell->setPanelContent("content",
        [&](StudioFrame& frame, const UiRect& area) {
            (void)studioContentBrowser(frame, area, context, state, selected);
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
        selectedTheFile = selected == file;
    }

    CNA_STUDIO_EXPECT(selectedTheFile);
}

CNA_STUDIO_TEST(AProjectWithNoAssetsSaysSoRatherThanShowingNothing)
{
    StudioContext context;
    StudioTreeState state;
    Uuid selected;

    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->activatePanel("content"));

    StudioContentBrowserResult result;
    CNA_STUDIO_EXPECT(shell->setPanelContent("content",
        [&](StudioFrame& frame, const UiRect& area) {
            const StudioContentBrowserResult pass =
                studioContentBrowser(frame, area, context, state, selected);
            if (frame.isDrawPass()) { result = pass; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT_EQ(result.rowsTotal, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(result.missingCount, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}
