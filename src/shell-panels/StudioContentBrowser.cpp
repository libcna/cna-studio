// SPDX-License-Identifier: MS-PL
/**
 * @file StudioContentBrowser.cpp
 * @brief The Content Browser.
 */

#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Splits a project-relative path into its directory part and its file name. */
        std::pair<std::string, std::string> splitPath(const std::string& path)
        {
            const std::size_t slash = path.find_last_of('/');
            if (slash == std::string::npos) { return {std::string{}, path}; }
            return {path.substr(0, slash), path.substr(slash + 1)};
        }

        /**
         * @brief Every folder a path implies, outermost first.
         *
         * `Assets/Textures/player.png` implies `Assets` and `Assets/Textures`. Derived rather than
         * read from disk, so a folder exists exactly when something tracked is in it -- an empty
         * directory would otherwise promise a place to put things the database does not know about.
         */
        std::vector<std::string> ancestorsOf(const std::string& directory)
        {
            std::vector<std::string> folders;
            if (directory.empty()) { return folders; }

            std::size_t from = 0;
            while (true)
            {
                const std::size_t slash = directory.find('/', from);
                if (slash == std::string::npos)
                {
                    folders.push_back(directory);
                    break;
                }
                folders.push_back(directory.substr(0, slash));
                from = slash + 1;
            }
            return folders;
        }

        /** @brief The last segment of a folder path, which is what a tree row shows. */
        std::string leafName(const std::string& folder)
        {
            const std::size_t slash = folder.find_last_of('/');
            return slash == std::string::npos ? folder : folder.substr(slash + 1);
        }

        /** @brief Whether @p folder is inside a folder the user has collapsed. */
        bool hiddenByCollapse(const std::string& folder, const StudioTreeState& state)
        {
            for (const std::string& ancestor : ancestorsOf(folder))
            {
                if (ancestor == folder) { break; }
                if (!state.isExpanded(ancestor)) { return true; }
            }
            return false;
        }
    }

    std::vector<StudioTreeRow> studioContentRows(const AssetDatabase& assets,
                                                 const Uuid& selected,
                                                 const StudioTreeState& state)
    {
        // Grouped by folder and sorted, because the database's own order is insertion order and a
        // browser whose files moved about as the project was rescanned would be unusable.
        std::map<std::string, std::vector<const AssetRecord*>> byFolder;
        std::set<std::string> folders;

        for (const AssetRecord* record : assets.getAll())
        {
            if (record == nullptr) { continue; }

            const auto [directory, name] = splitPath(record->sourcePath);
            byFolder[directory].push_back(record);
            for (const std::string& ancestor : ancestorsOf(directory)) { folders.insert(ancestor); }
        }

        for (auto& [directory, records] : byFolder)
        {
            (void)directory;
            std::sort(records.begin(), records.end(),
                      [](const AssetRecord* lhs, const AssetRecord* rhs) {
                          return lhs->sourcePath < rhs->sourcePath;
                      });
        }

        std::vector<StudioTreeRow> rows;

        // One pass over the folders in sorted order interleaves parents with their contents
        // correctly, because "Assets" sorts before "Assets/Textures" and both before "Assets2".
        const auto depthOf = [](const std::string& path) {
            return static_cast<int>(std::count(path.begin(), path.end(), '/'));
        };

        const auto appendFiles = [&](const std::string& directory, int depth) {
            const auto found = byFolder.find(directory);
            if (found == byFolder.end()) { return; }

            for (const AssetRecord* record : found->second)
            {
                StudioTreeRow row;
                row.id = record->id.toString();
                row.label = splitPath(record->sourcePath).second;
                row.detail = toString(record->type);
                row.depth = depth;
                row.selected = record->id == selected;

                // A tracked asset whose file has gone is still tracked: a scene references it by
                // id, and dropping the record would turn a fixable problem into a broken scene.
                // Drawn dimmed and labelled, because "what did I break when I moved that folder"
                // is what a content browser is most often opened to answer.
                if (assets.isMissing(record->id))
                {
                    // Dimmed, not disabled. This is the row a user most needs to click: clicking
                    // it is how they find out what references the file that has gone.
                    row.muted = true;
                    row.detail = "missing";
                }

                rows.push_back(std::move(row));
            }
        };

        appendFiles(std::string{}, 0);

        for (const std::string& folder : folders)
        {
            if (hiddenByCollapse(folder, state)) { continue; }

            StudioTreeRow row;
            row.id = folder;
            row.label = leafName(folder);
            row.depth = depthOf(folder);
            row.hasChildren = true;

            const auto contents = byFolder.find(folder);
            const std::size_t count = contents == byFolder.end() ? 0 : contents->second.size();
            if (count > 0) { row.detail = std::to_string(count); }

            rows.push_back(std::move(row));

            if (state.isExpanded(folder)) { appendFiles(folder, depthOf(folder) + 1); }
        }

        return rows;
    }

    StudioContentBrowserResult studioContentBrowser(StudioFrame& frame, const UiRect& bounds,
                                                    const StudioContext& context,
                                                    StudioTreeState& state, Uuid& selected)
    {
        StudioContentBrowserResult result;

        const AssetDatabase& assets = context.getAssets();
        const std::vector<StudioTreeRow> rows = studioContentRows(assets, selected, state);
        result.rowsTotal = rows.size();
        result.missingCount = assets.getMissingAssets().size();

        const std::string_view empty = context.hasProject()
            ? std::string_view{"This project has no assets yet."}
            : std::string_view{"No project is open."};

        const StudioTreeResult tree = studioTreeView(frame, bounds, rows, state, empty);
        result.rowsDrawn = tree.rowsDrawn;

        if (tree.clicked.has_value())
        {
            // A folder's id is its path and a file's is a UUID, so the parse decides which was
            // clicked -- no second field to keep in step with the rows.
            const Uuid clicked = Uuid::parse(rows[*tree.clicked].id);
            if (clicked.isValid())
            {
                selected = clicked;
                result.selectedAsset = clicked;
            }
        }

        return result;
    }
}
