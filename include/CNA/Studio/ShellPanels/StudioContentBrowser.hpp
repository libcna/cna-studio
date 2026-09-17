// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioContentBrowser.hpp
 * @brief The Content Browser — the project's assets, as folders and files.
 *
 * `plan.md` STUDIO-07008, STUDIO-09001.
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
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <cstdint>
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
     * @brief Flattens the asset database into tree rows, honouring @p state.
     *
     * Exposed separately so a test can assert on the shape of the tree without a frame — the two
     * things this panel does, deciding what the tree *is* and drawing it, fail independently and a
     * screenshot cannot tell them apart.
     *
     * @param assets The database.
     * @param selected The currently selected asset, marked in the rows.
     * @param state Which folders are open.
     * @return Rows in display order: folders before files, each group in path order.
     */
    [[nodiscard]] std::vector<StudioTreeRow> studioContentRows(const AssetDatabase& assets,
                                                               const Uuid& selected,
                                                               const StudioTreeState& state);

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
                                                                     const Uuid& selected);

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
