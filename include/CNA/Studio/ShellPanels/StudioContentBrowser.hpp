// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioContentBrowser.hpp
 * @brief The Content Browser — the project's assets, as folders and files.
 *
 * `plan.md` STUDIO-07008.
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
#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
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
    };

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
     * @brief Draws the Content Browser.
     *
     * @param frame The frame.
     * @param bounds The panel's content rectangle.
     * @param context The editor, for its asset database.
     * @param state Folder expansion, owned by the caller so it survives the frame.
     * @param selected The selected asset; updated when the user clicks a file.
     * @return What happened.
     */
    StudioContentBrowserResult studioContentBrowser(StudioFrame& frame, const UiRect& bounds,
                                                    const StudioContext& context,
                                                    StudioTreeState& state, Uuid& selected);
}
