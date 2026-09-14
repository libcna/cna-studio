// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/AssetBrowserPanel.hpp
 * @brief The project's assets as a folder tree, with filtering, renaming and moving.
 */

#include <string>

#include "CNA/Editor/Assets/AssetTree.hpp"
#include "CNA/Editor/Panels/EditorPanel.hpp"

namespace CNA::Editor
{
    /**
     * @brief Shows the tracked assets, and lets them be filtered, renamed and moved.
     *
     * The folder tree is derived from the source paths rather than stored (see AssetTree.hpp), so
     * moving an asset changes one string and nothing has to be kept in sync.
     *
     * Renaming and moving are filesystem operations, not document ones: the asset keeps its Uuid,
     * so no scene is touched (ANALYSIS.md decision D-08). Both go through the undo stack anyway,
     * because everything that changes a document does (D-06) and the asset database is a document.
     */
    class AssetBrowserPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

    private:
        /** @brief Draws one folder and everything below it. */
        void drawFolder(const AssetFolder& folder, bool isRoot);

        /** @brief Draws one asset row: selection, drag source, context menu, rename field. */
        void drawAsset(const Uuid& assetId);

        /**
         * @brief Draws @p record's preview, when the viewport can produce one.
         *
         * Only image assets have one, and only the CNA-backed viewport can make it -- the null
         * one returns nothing, which is why a headless run simply shows no pictures rather than
         * needing a separate code path.
         */
        /** @brief Writes a new `.cnamaterial` and rescans so it gets an id (ED-403). */
        void createMaterial();

        void drawThumbnail(const AssetRecord& record);

        /** @brief Side of the square a thumbnail is fitted into, in pixels. */
        static constexpr float kThumbnailExtent = 18.0f;

        /** @brief Runs whatever the tree asked for, once it has finished drawing. */
        void applyPendingMove();

        /** @brief Payload type for an asset dragged out of the browser. */
        static constexpr const char* kAssetDragType = "asset";

        /** @brief Case-insensitive substring kept against the whole relative path. */
        std::string filter_;

        /**
         * @brief A move requested while drawing, applied afterwards.
         *
         * Deferred because a move rewrites the very paths the tree was built from, and the
         * recursion is walking that tree.
         */
        Uuid moveAssetId_;
        std::string moveDestination_;

        /** @brief The asset whose row is currently a text field, if any. */
        Uuid renamingAsset_;
        std::string renameBuffer_;
        bool renameNeedsFocus_ = false;
    };
}
