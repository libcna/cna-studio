// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Assets/AssetTree.hpp
 * @brief The asset browser's folder tree, derived from the tracked source paths.
 *
 * The database stores a flat set of records keyed by id, because that is what identity is
 * (ANALYSIS.md decision D-08). Folders exist only in the paths, so the tree is *derived* rather
 * than stored: nothing has to be kept in sync, and moving an asset changes one string.
 *
 * Kept here rather than in the panel, and free of any UI, because the parts worth getting right --
 * how paths become a hierarchy, what a filter keeps, how the result is ordered -- are all
 * arithmetic on strings and can be checked without a window.
 */

#include <string>
#include <string_view>
#include <vector>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    /** @brief One folder in the derived tree. */
    struct AssetFolder
    {
        /** @brief This folder's own name. Empty for the root. */
        std::string name;

        /** @brief The full relative path to this folder. Empty for the root. */
        std::string path;

        /** @brief Sub-folders, ordered by name. */
        std::vector<AssetFolder> folders;

        /** @brief Assets directly in this folder, ordered by source path. */
        std::vector<Uuid> assets;

        /** @brief Returns the number of assets here and in every sub-folder. */
        [[nodiscard]] std::size_t getTotalAssetCount() const;

        /** @brief Returns true when neither this folder nor any below it holds an asset. */
        [[nodiscard]] bool isEmpty() const { return getTotalAssetCount() == 0; }
    };

    /**
     * @brief Builds the folder tree for @p assets, keeping only what matches @p filter.
     *
     * The filter is a case-insensitive substring test against the whole relative path, so typing
     * a folder name keeps everything under it and typing part of a file name finds it wherever it
     * lives. An empty filter keeps everything.
     *
     * Folders left with nothing in them are dropped rather than shown empty: a filter whose result
     * is a tree of empty folders tells the user nothing about where the matches are.
     */
    [[nodiscard]] AssetFolder buildAssetTree(const AssetDatabase& assets, std::string_view filter = {});

    /**
     * @brief Returns @p path's last component, e.g. "player.png" for "Assets/Textures/player.png".
     */
    [[nodiscard]] std::string assetFileName(std::string_view path);

    /**
     * @brief Returns @p path without its last component, without a trailing slash.
     *
     * Empty when the path has no directory part, which is what a file at the project root has.
     */
    [[nodiscard]] std::string assetDirectory(std::string_view path);

    /**
     * @brief Joins a directory and a file name into a relative asset path.
     *
     * An empty directory yields @p fileName alone rather than a path beginning with a slash, which
     * would look absolute and resolve somewhere else entirely.
     */
    [[nodiscard]] std::string joinAssetPath(std::string_view directory, std::string_view fileName);
}
