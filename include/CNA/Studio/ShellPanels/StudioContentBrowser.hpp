// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioContentBrowser.hpp
 * @brief The Content Browser — the project's assets, as folders and files.
 *
 * `plan.md` STUDIO-07008, STUDIO-09001, STUDIO-09002, STUDIO-09005, STUDIO-09006.
 *
 * The fourth panel ported, and the last of the four a person looks at first. It reads the
 * `AssetDatabase` rather than the filesystem: the database is what knows an asset's stable id, its
 * type and whether its source file has gone missing, and a browser that walked the directory
 * instead would show files Studio does not track and hide the one fact that matters about a
 * tracked file whose source has vanished.
 *
 * ### Folders come from paths, not from disk
 *
 * The database stores project-relative paths. The folder tree is derived from them, so a folder
 * exists exactly when something in it does — which is also why an empty folder on disk does not
 * appear: there is nothing in it to track, and showing it would promise a place to put things that
 * the database does not yet know about.
 *
 * ### Missing sources are the point
 *
 * An asset whose file has gone is still a tracked asset: a scene references it by id, and deleting
 * the record would turn a fixable problem into a broken scene. It is listed, marked, and counted —
 * because the question "what did I break when I moved that folder" is the one a content browser is
 * most often opened to answer.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/UiCore/StudioIcons.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace CNA::Studio
{
    class AssetDatabase;
    class StudioContext;

    /** @brief What the Content Browser did this frame. */
    /**
     * @brief The payload type an asset is carried as.
     *
     * One constant rather than a string literal at each end: a source and a target that disagree
     * about the spelling produce a drag that silently does nothing, which is the hardest kind of
     * failure to see.
     */
    inline constexpr std::string_view kStudioAssetDragType = "asset";

    /**
     * @brief The folder tree's id for the project root, which has no path of its own.
     *
     * A named constant because the root's id must not collide with a real folder's, and because
     * the expansion state is keyed on it: a tree whose root row id was the empty string would
     * share its expansion with every row that had not been given one.
     */
    inline constexpr std::string_view kStudioContentRootRowId = "<project>";

    /** @brief How the Content Browser presents what a folder holds. */
    enum class StudioContentView : std::uint8_t
    {
        /**
         * @brief One row per asset, with its kind on the right.
         *
         * Dense, sortable by eye, and the right answer for a folder of two hundred scripts. It is
         * the wrong answer for a folder of textures, where the thing a user is looking for is a
         * *picture* and a list of file names makes them open each one to find it.
         */
        List,
        /**
         * @brief A grid of cards, one per asset.
         *
         * `STUDIO-35040`. What every professional content browser defaults to, and for a reason
         * that is about content rather than about fashion: an asset is a thing with an appearance,
         * and a browser that shows only its name is a file manager.
         */
        Grid
    };

    /** @brief Returns a stable English name for a view, for preferences and tests. */
    [[nodiscard]] std::string_view studioContentViewName(StudioContentView view);

    /**
     * @brief The folder tree beside the content, as rows.
     *
     * `plan.md` STUDIO-09001. Folders only — no files. That is the whole difference between this
     * and the list view's rows, and it is the difference that makes a folder tree useful: a tree
     * holding every asset in a project is a second copy of the content pane, and the reason to
     * have a tree at all is to move between folders without reading their contents.
     *
     * A `Project` root sits above the folders, so "go back to the top" is a row rather than a
     * gesture nobody finds. It carries the empty path, which is what @ref
     * StudioContentBrowserState::folder holds for the project root.
     *
     * @param assets The database. Folders are derived from asset paths, so a folder exists exactly
     *        when something tracked is in it or under it.
     * @param folder The folder currently being shown, which is drawn selected.
     * @param state Expansion state; a collapsed folder hides its descendants.
     * @return The rows, outermost first, each folder's id being its project-relative path.
     */
    [[nodiscard]] std::vector<StudioTreeRow> studioContentFolderRows(const AssetDatabase& assets,
                                                                     const std::string& folder,
                                                                     const StudioTreeState& state);

    /** @brief What a content listing is ordered by. */
    enum class StudioContentSort : std::uint8_t
    {
        /** @brief Alphabetical, which is the order a user can predict. */
        Name,
        /** @brief Grouped by kind, then by name within a kind. */
        Type
    };

    /**
     * @brief The asset kinds this project actually holds, in a stable order.
     *
     * The type filter's options. From the database rather than from the enum: a filter offering
     * ten kinds a project has none of is a filter nobody reads.
     *
     * @param assets The database.
     * @return Each kind present, ordered by its stable name.
     */
    [[nodiscard]] std::vector<AssetType> studioContentTypesPresent(const AssetDatabase& assets);

    /** @brief Returns a stable English name for a sort order, for preferences and tests. */
    [[nodiscard]] std::string_view studioContentSortName(StudioContentSort sort);

    /**
     * @brief What to show: a search, a type filter and an order.
     *
     * One struct rather than three parameters because the three interact — a search filtered to
     * textures and sorted by name is one question, and a listing function taking them separately
     * invites a caller that applies two of the three.
     */
    struct StudioContentQuery
    {
        /**
         * @brief What the user typed. Empty browses the folder instead.
         *
         * A non-empty search leaves the current folder behind and looks at the **whole project**
         * (`STUDIO-09005`). Searching within one folder is the behaviour that makes people type a
         * name, see nothing, and conclude the asset is gone — when it is one folder over.
         */
        std::string search;

        /** @brief Show only this kind. Unset shows every kind. */
        std::optional<AssetType> type;

        /** @brief What the listing is ordered by. */
        StudioContentSort sort = StudioContentSort::Name;

        /** @brief Reverse the order. */
        bool descending = false;

        /** @brief Whether anything is being filtered or searched, for an empty-state message. */
        [[nodiscard]] bool isNarrowed() const { return !search.empty() || type.has_value(); }
    };

    /**
     * @brief Whether @p record matches @p search, over its name, its type and its path.
     *
     * Exposed because ranking and matching are the part of a search that can be wrong without
     * looking wrong, and a test that had to build a frame to check them would not be written.
     *
     * @param record The asset.
     * @param search What the user typed. Case-insensitive; empty matches everything.
     * @return True when the asset should appear in the results.
     */
    [[nodiscard]] bool studioContentMatches(const AssetRecord& record, std::string_view search);

    /**
     * @brief What one of the Content Browser's file operations did.
     *
     * `plan.md` STUDIO-09009. A rename that is refused and a rename that did nothing because the
     * name was unchanged are different outcomes, and a caller given only a message cannot tell
     * them apart -- which is how a browser ends up logging "renamed" for a rename that failed.
     */
    struct StudioContentOperation
    {
        /** @brief Whether the database changed, i.e. whether there is something to undo. */
        bool applied = false;

        /** @brief What happened, or why nothing did. Empty when nothing was attempted. */
        std::string message;
    };

    /**
     * @brief Renames an asset or a folder, through the undo stack.
     *
     * A rename *is* a move whose destination is the same folder (`AssetCommands.hpp`), which is
     * why there is no separate command for it: the id survives either way, so no scene is touched
     * and no reference breaks. That is `STUDIO-09009`'s whole acceptance condition, and it holds
     * because there is one code path rather than two that could disagree.
     *
     * @param context The editor, for its database and its undo stack.
     * @param asset The asset to rename, or a nil id when renaming a folder.
     * @param folder The folder to rename, or empty when renaming an asset.
     * @param newName The new file or folder name, without any path.
     * @return What happened.
     */
    StudioContentOperation studioContentRename(StudioContext& context, const Uuid& asset,
                                               const std::string& folder,
                                               const std::string& newName);

    /**
     * @brief Moves @p asset into @p folder, through the undo stack.
     *
     * @param context The editor.
     * @param asset The asset to move.
     * @param folder Where it goes. Empty is the project root.
     * @return What happened.
     */
    StudioContentOperation studioContentMoveInto(StudioContext& context, const Uuid& asset,
                                                 const std::string& folder);

    /**
     * @brief Duplicates @p asset beside itself, through the undo stack, and selects the copy.
     * @param context The editor.
     * @param asset The asset to copy.
     * @return What happened.
     */
    StudioContentOperation studioContentDuplicate(StudioContext& context, const Uuid& asset);

    /**
     * @brief Deletes @p asset's file and its sidecar, through the undo stack.
     * @param context The editor.
     * @param asset The asset to delete.
     * @return What happened.
     */
    StudioContentOperation studioContentDelete(StudioContext& context, const Uuid& asset);

    /**
     * @brief Re-reads @p asset from disk, keeping every import setting.
     *
     * `plan.md` STUDIO-09010, over `STUDIO-10001`. Not through the undo stack, and deliberately: a
     * reimport re-reads what is already on disk rather than changing the document, and undoing one
     * would put back facts describing a version of the file that no longer exists.
     *
     * @param context The editor.
     * @param asset The asset to reimport.
     * @return What happened. `applied` is true when the file was read.
     */
    StudioContentOperation studioContentReimport(StudioContext& context, const Uuid& asset);

    /**
     * @brief The rows the right-click menu offers for whatever is under the pointer.
     *
     * `plan.md` STUDIO-09009. Separate from the drawing so that *what a menu offers* — which
     * differs between an asset, a folder and empty space, and differs again for an asset whose
     * file has gone — can be checked without building a frame.
     *
     * A folder offers only Rename. It has no Duplicate because copying a folder is copying every
     * file under it, which is a job rather than an edit (`STUDIO-30001`), and no Delete because an
     * undoable delete captures the bytes it removed: one file is a reasonable thing to hold in the
     * undo stack, and a folder of four hundred textures is not.
     *
     * @param assets The database, consulted for whether the asset's file is still there.
     * @param asset The asset under the pointer, or a nil id.
     * @param folder The folder under the pointer, or empty.
     * @return The rows, in order. Empty when there is nothing to offer.
     */
    [[nodiscard]] std::vector<StudioContextMenuItem> studioContentMenuItems(
        const AssetDatabase& assets, const Uuid& asset, const std::string& folder);

    /** @brief What the Content Browser remembers between frames. */
    struct StudioContentBrowserState
    {
        /** @brief Which folders are open, and the scroll position. Shared by both views. */
        StudioTreeState tree;

        /**
         * @brief Expansion and scroll of the folder tree pane, kept apart from @ref tree.
         *
         * Two trees showing different things must not share one expansion set: collapsing
         * `Assets/Textures` in the navigation pane would otherwise fold away the files the content
         * pane is showing, which reads as the browser losing its place.
         */
        StudioTreeState folderTree;

        /**
         * @brief Width of the folder pane, in unscaled pixels. Zero hides it.
         *
         * Draggable, and remembered: a pane that reset to its default every time the panel was
         * re-laid out would be one nobody bothers to resize. Zero is a real value — a browser
         * docked into a narrow strip is more useful as content alone than as two things too thin
         * to read.
         */
        float folderPaneWidth = 168.0f;

        /** @brief List or grid. */
        StudioContentView view = StudioContentView::Grid;

        /**
         * @brief The folder the grid is showing, as a path. Empty is the project root.
         *
         * The grid shows *one* folder, unlike the list, which shows the whole tree at once. That
         * is the difference between the two presentations rather than an incidental one: a grid of
         * every asset in a project is a wall, and the folder a user is in is the unit they think
         * in.
         */
        std::string folder;

        /** @brief How wide a card is, in unscaled pixels. */
        float cardSize = 96.0f;

        /** @brief The search, the type filter and the order. See @ref StudioContentQuery. */
        StudioContentQuery query;

        /** @brief Whether the filter and sort controls are showing. */
        bool filtersOpen = false;

        /**
         * @brief The asset the open right-click menu is about, or nil.
         *
         * Remembered rather than re-derived from the selection, because a right-click on a row
         * that is *not* selected opens a menu about that row: a menu that acted on the selection
         * instead would delete the wrong file for every user who did not notice.
         */
        Uuid menuAsset;

        /** @brief The folder the open right-click menu is about, or empty. */
        std::string menuFolder;
    };

    struct StudioContentBrowserResult
    {
        /** @brief How many rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many rows the database and the current expansion produced. */
        std::size_t rowsTotal = 0;

        /** @brief How many listed assets have no source file on disk. */
        std::size_t missingCount = 0;

        /** @brief The asset the user clicked, if any. Input pass only. */
        Uuid selectedAsset;

        /** @brief How many cards the grid drew. Zero in list view. */
        std::size_t cardsDrawn = 0;

        /** @brief Which view drew this frame. */
        StudioContentView view = StudioContentView::List;

        /** @brief How many rows the folder pane drew. Zero when it is hidden. */
        std::size_t folderRowsDrawn = 0;

        /** @brief How many folders the database's paths imply, the `Project` root included. */
        std::size_t folderRowsTotal = 0;

        /** @brief The folder the grid is showing, for the breadcrumb and for tests. */
        std::string folder;

        /** @brief A right-click landed on a row or a card this frame. Input pass only. */
        bool menuRequested = false;

        /** @brief The asset the right-click landed on, or nil when it was a folder. */
        Uuid menuAsset;

        /** @brief The folder the right-click landed on, or empty when it was an asset. */
        std::string menuFolder;

        /**
         * @brief What the user just renamed, duplicated, deleted or moved.
         *
         * `plan.md` STUDIO-09009. Every one of them goes through the undo stack, so the visible
         * effect arrives via the database; this says *that* it happened, which is what a test
         * driving a frame has no other way to see.
         */
        StudioContentOperation lastOperation;
    };

    /**
     * @brief The icon that says what kind of asset this is.
     *
     * Public so that the Content Browser and the Details panel's asset inspector (`STUDIO-07045`)
     * use one mapping rather than two: a file that reads as audio in one panel and as a generic
     * file in the other is a drift nobody notices until they are looked at side by side, which is
     * how three of this migration's gaps were found.
     *
     * @param type The asset's kind.
     * @return Its icon; `StudioIcon::File` for a kind with no picture of its own.
     */
    [[nodiscard]] StudioIcon studioAssetIcon(AssetType type);


    /**
     * @brief One card in the grid: an asset or a folder, with what it is and what it is called.
     *
     * Built by @ref studioContentCards and handed to the drawing, so the two can fail separately.
     * Deciding *what a folder holds* is arithmetic over a database and is testable with no frame;
     * deciding *where a card goes* is layout. A screenshot cannot tell the two apart and a bug in
     * either looks like the other.
     */
    struct StudioContentCard
    {
        /** @brief The asset's id, or empty for a folder. */
        Uuid assetId;
        /** @brief The folder's path, or empty for an asset. */
        std::string folder;
        /** @brief What the card says. */
        std::string label;
        /** @brief The kind, under the label: `"Texture2D"`, `"3 items"`. */
        std::string detail;
        /** @brief The picture. */
        StudioIcon icon = StudioIcon::File;
        /** @brief Its colour. Warning for an asset whose file has gone. */
        StudioColorRole iconRole = StudioColorRole::TextSecondary;
        /** @brief Whether this is the selected asset. */
        bool selected = false;
        /** @brief Whether the asset's source file is missing. */
        bool missing = false;

        /**
         * @brief Whether the file has changed since it was last imported (`plan.md` STUDIO-09010).
         *
         * Free to ask: `AssetRecord` carries both stamps, one kept by the watcher and one by the
         * last import, so marking every row is arithmetic rather than a `stat` per row.
         */
        bool needsReimport = false;

        /**
         * @brief The folder this asset is in, shown only in search results.
         *
         * Empty while browsing, because the breadcrumb already says where everything on screen is.
         * A search result's whole problem is the opposite: `player.png` is three folders deep and
         * there are two of them, and a result list that showed only names would make the user open
         * each to find out which is which.
         */
        std::string location;

        /** @brief Whether clicking this card enters a folder rather than selecting an asset. */
        [[nodiscard]] bool isFolder() const { return !folder.empty(); }
    };

    /**
     * @brief What one folder holds, as cards: its subfolders first, then its assets.
     *
     * Folders first because a user navigating is looking for a folder and a user browsing is
     * looking at assets, and the first of those is the one that is *interrupted* by having to scan
     * past two hundred textures.
     *
     * @param assets The database.
     * @param folder The folder to show. Empty is the project root.
     * @param selected The currently selected asset, marked on its card.
     * @return The cards, in display order.
     */
    [[nodiscard]] std::vector<StudioContentCard> studioContentCards(const AssetDatabase& assets,
                                                                     const std::string& folder,
                                                                     const Uuid& selected,
                                                                     const StudioContentQuery& query = {});

    /**
     * @brief The path segments of @p folder, outermost first, for a breadcrumb.
     *
     * The project root is always the first entry and is named rather than shown as an empty
     * string: "Project / Assets" is a place and "/ Assets" is a path fragment. It is called
     * "Project" rather than "Assets" although the root usually *contains* a folder of that name --
     * which is exactly why, because "Assets / Assets / Textures" is a user wondering which of the
     * two they are in.
     *
     * @param folder The folder path, or empty for the root.
     * @return One entry per level: the label to show and the path clicking it navigates to.
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> studioContentBreadcrumb(
        const std::string& folder);

    /**
     * @brief Draws the Content Browser in whichever view its state names.
     *
     * @param frame The frame.
     * @param bounds The panel's content rectangle.
     * @param context The editor, for its asset database and for the selection. Not const: a click
     *        selects, the way `studioOutlinerPanel` selects an entity. The selected asset used to
     *        be an out-parameter the shell kept beside the one `StudioContext` already had, so the
     *        native browser and the native Details panel could not see each other's idea of what
     *        was selected -- see `STUDIO-07045`.
     * @param state The view, the folder, the expansion and the card size.
     * @return What happened.
     */
    StudioContentBrowserResult studioContentBrowser(StudioFrame& frame, const UiRect& bounds,
                                                    StudioContext& context,
                                                    StudioContentBrowserState& state);
}
