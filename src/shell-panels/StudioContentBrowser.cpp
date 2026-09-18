// SPDX-License-Identifier: MS-PL
/**
 * @file StudioContentBrowser.cpp
 * @brief The Content Browser.
 */

#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"

#include "CNA/Studio/Assets/AssetCommands.hpp"
#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetReimport.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <memory>
#include <utility>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <cmath>
#include <tuple>
#include <map>
#include <set>

namespace CNA::Studio
{
    namespace
    {
        /** @brief A theme metric as a float, scaled. Spelled the same way in every panel here. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /**
         * @brief What an empty browser says, for whichever kind of empty it is.
         *
         * Four states, not one. "No assets" in a project that has two hundred of them is the sort
         * of message that makes a user think the database is broken, and "this folder is empty"
         * while a filter is on sends them looking in the wrong place.
         *
         * Shared by both presentations because it is one answer about one folder: the list and the
         * grid disagreeing about why there is nothing here would be the browser contradicting
         * itself as the user pressed the view toggle.
         */
        std::string_view studioContentEmptyMessage(const StudioContext& context,
                                                   const StudioContentBrowserState& state)
        {
            if (!context.hasProject()) { return "No project is open."; }
            if (state.query.isNarrowed()) { return "Nothing matches."; }
            if (state.folder.empty()) { return "This project has no assets yet."; }
            return "This folder is empty.";
        }

        // Forward-declared so the entry point above them can read as the decision it is -- a bar,
        // then one of two presentations -- rather than as two large functions with a switch buried
        // at the bottom of the second.
        StudioContentBrowserResult studioContentFolderPane(StudioFrame& frame, UiRect& area,
                                                       StudioContext& context,
                                                       StudioContentBrowserState& state,
                                                       StudioContentBrowserResult result)
    {
        const StudioTheme& theme = frame.theme();
        const float separator = metricOf(theme, StudioMetric::SeparatorThickness);
        const float minimum = metricOf(theme, StudioMetric::RowHeight) * 3.0f;

        const std::vector<StudioTreeRow> rows =
            studioContentFolderRows(context.getAssets(), state.folder, state.folderTree,
                                    state.shortcuts);
        result.folderRowsTotal = rows.size();

        // Hidden rather than squeezed. A browser docked into a narrow strip is more useful as
        // content alone than as two things too thin to read, and `folderPaneWidth == 0` is how a
        // user says so by dragging.
        const float wanted = state.folderPaneWidth * theme.scale();
        if (wanted < minimum || area.width < minimum * 3.0f) { return result; }

        UiRect pane = area.splitLeft(std::min(wanted, area.width * 0.5f));
        UiRect grip = area.splitLeft(std::min(area.width, std::max(separator, 3.0f)));

        frame.ids().push("folderpane");

        const StudioTreeResult tree =
            studioTreeView(frame, pane, rows, state.folderTree,
                           context.hasProject() ? std::string_view{"This project has no folders."}
                                                : std::string_view{"No project is open."});
        result.folderRowsDrawn = tree.rowsDrawn;

        if (tree.clicked.has_value())
        {
            // The root row carries the empty path, which is what `folder` holds for the project
            // root -- so navigating is one assignment rather than a special case at every reader.
            const std::string& id = rows[*tree.clicked].id;
            state.folder = (id == kStudioContentRootRowId) ? std::string{} : id;
            result.folder = state.folder;
        }

        const StudioSplitterResult drag =
            studioSplitter(frame, frame.ids().make("width"), grip, StudioSplitterAxis::Horizontal);
        if (drag.delta != 0.0f)
        {
            // Below the minimum it collapses to nothing rather than sticking at an unreadable
            // width: dragging a pane shut is a gesture people expect to work, and one that stopped
            // at forty pixels would look like the drag had broken.
            const float next = state.folderPaneWidth + drag.delta / theme.scale();
            state.folderPaneWidth = next < minimum / theme.scale() ? 0.0f : next;
        }

        frame.ids().pop();
        return result;
    }

    StudioContentBrowserResult studioContentList(StudioFrame& frame, const UiRect& bounds,
                                                     StudioContext& context,
                                                     StudioContentBrowserState& state,
                                                     StudioContentBrowserResult result);
        StudioContentBrowserResult studioContentGrid(StudioFrame& frame, const UiRect& bounds,
                                                     StudioContext& context,
                                                     StudioContentBrowserState& state,
                                                     StudioContentBrowserResult result);

        /**
         * @brief Draws the folder tree down the left of @p area and takes its width out of it.
         *
         * @param frame The frame.
         * @param area The browser's body below the bar; narrowed by the pane and its splitter.
         * @param context The open project's assets.
         * @param state The browser's retained state; the folder and the pane width are written.
         * @param result Carried through and filled in with what the pane drew.
         */
        StudioContentBrowserResult studioContentFolderPane(StudioFrame& frame, UiRect& area,
                                                           StudioContext& context,
                                                           StudioContentBrowserState& state,
                                                           StudioContentBrowserResult result);

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

        /** @brief @p text lower-cased, ASCII only, which is what a name search needs. */
        std::string lowered(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char character : text)
            {
                out.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
            return out;
        }

        /**
         * @brief How well @p record answers @p search: lower is better, -1 is no match.
         *
         * Ranked rather than merely filtered, because a search over name, type *and* path matches
         * a great deal and the ordering is what makes the result usable. A name that starts with
         * what was typed is what the user meant; a path that happens to contain it is a guess.
         */
        int searchRank(const AssetRecord& record, const std::string& needle)
        {
            const std::string path = lowered(record.sourcePath);
            const std::string name = lowered(splitPath(record.sourcePath).second);
            const std::string type = lowered(toString(record.type));

            if (name.rfind(needle, 0) == 0) { return 0; }
            if (name.find(needle) != std::string::npos) { return 1; }
            if (type.rfind(needle, 0) == 0) { return 2; }
            if (path.find(needle) != std::string::npos) { return 3; }
            if (type.find(needle) != std::string::npos) { return 4; }
            return -1;
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

    /**
     * @brief The picture for an asset kind.
     *
     * `STUDIO-35030`. A content browser whose rows differ only in a right-aligned grey word is one
     * a user reads rather than scans, and scanning is the whole reason a project has folders.
     *
     * Shared by the list, the grid and the Details panel's asset inspector (`STUDIO-07045`), which
     * is the point of it being one function: two presentations of one database that disagreed
     * about what a texture looks like would be worse than either alone. The mapping is
     * deliberately coarse -- a sound effect and a song get the same speaker, because the question
     * a user asks of an icon is "is this audio", and the detail line already answers which.
     */
    StudioIcon studioAssetIcon(AssetType type)
    {
        switch (type)
        {
            case AssetType::Texture2D:   return StudioIcon::Texture;
            case AssetType::Model:       return StudioIcon::Mesh;
            case AssetType::Material:    return StudioIcon::Material;
            case AssetType::SoundEffect:
            case AssetType::Song:        return StudioIcon::Audio;
            case AssetType::Scene:       return StudioIcon::Scene;
            case AssetType::Prefab:      return StudioIcon::Prefab;
            case AssetType::Effect:      return StudioIcon::Material;
            case AssetType::SpriteFont:  return StudioIcon::File;
            case AssetType::RawData:
            case AssetType::Unknown:     break;
        }
        // A tracked file no importer claims is still a file, and a blank where every other
        // row has a picture reads as a row that failed to load rather than as one nobody
        // imports.
        return StudioIcon::File;
    }

    std::string_view studioContentViewName(StudioContentView view)
    {
        switch (view)
        {
            case StudioContentView::List: return "list";
            case StudioContentView::Grid: return "grid";
        }
        return "";
    }

    std::vector<std::pair<std::string, std::string>> studioContentBreadcrumb(
        const std::string& folder)
    {
        // Named rather than shown as an empty string: "Project / Assets" is a place, and
        // "/ Assets" is a path fragment somebody has to reconstruct in their head.
        //
        // "Project" rather than "Assets", although the root usually *contains* a folder called
        // Assets -- which is exactly why: a crumb reading "Assets / Assets / Textures" is a user
        // wondering which of the two they are in.
        std::vector<std::pair<std::string, std::string>> crumbs{{"Project", std::string{}}};

        std::string path;
        std::size_t start = 0;
        while (start <= folder.size())
        {
            const std::size_t slash = folder.find('/', start);
            const std::string segment = folder.substr(start, slash - start);
            if (!segment.empty())
            {
                path = path.empty() ? segment : path + "/" + segment;
                crumbs.emplace_back(segment, path);
            }
            if (slash == std::string::npos) { break; }
            start = slash + 1;
        }
        return crumbs;
    }

    std::vector<StudioTreeRow> studioContentFolderRows(const AssetDatabase& assets,
                                                       const std::string& folder,
                                                       const StudioTreeState& state,
                                                       const StudioAssetShortcuts& shortcuts)
    {
        // How many assets sit *directly* in each folder, and which folders exist at all. Both come
        // from the database's own indexes rather than from a walk over every record (STUDIO-09016):
        // this runs once per row on every frame, and an O(project) pass here is what a hundred
        // thousand assets cannot afford.
        //
        // Direct rather than cumulative: a count that included descendants would make `Assets` read
        // as holding everything in the project, which is true and useless -- the number a user
        // wants beside a folder is how much they will see when they click it.
        const std::map<std::string, std::size_t>& directCount = assets.getFolderCounts();
        const std::vector<std::string> folderList = assets.getFolderPaths();
        const std::set<std::string> folders{folderList.begin(), folderList.end()};

        const auto depthOf = [](const std::string& path) {
            return static_cast<int>(std::count(path.begin(), path.end(), '/'));
        };

        std::vector<StudioTreeRow> rows;

        // The two shortcut lists, above the project (STUDIO-09007). Above rather than below,
        // because they are where a user goes *first* and a pane that put them under two hundred
        // folders would be one where nobody found them. Each appears only when it has something in
        // it: an empty "Favourites" row teaches the user that the feature does nothing.
        for (const auto& [id, label, icon, ids] :
             {std::tuple{kStudioContentFavouritesRowId, "Favourites", StudioIcon::Info,
                         &shortcuts.favourites},
              std::tuple{kStudioContentRecentRowId, "Recent", StudioIcon::Undo,
                         &shortcuts.recent}})
        {
            if (ids->empty()) { continue; }

            StudioTreeRow row;
            row.id = std::string{id};
            row.label = label;
            row.icon = icon;
            row.depth = 0;
            row.detail = std::to_string(ids->size());
            row.selected = folder == id;
            rows.push_back(std::move(row));
        }

        // The project root, which is a row rather than a gesture. A tree whose only way back to
        // the top is collapsing everything is a tree people navigate by clicking the breadcrumb,
        // which makes half of this pane decoration.
        StudioTreeRow root;
        root.id = kStudioContentRootRowId;
        root.label = "Project";
        root.icon = StudioIcon::Folder;
        root.depth = 0;
        root.hasChildren = !folders.empty();
        root.selected = folder.empty();
        if (const auto found = directCount.find(std::string{}); found != directCount.end())
        {
            root.detail = std::to_string(found->second);
        }
        rows.push_back(std::move(root));

        // Sorted order interleaves parents with their children correctly, because `Assets` sorts
        // before `Assets/Textures` and both before `Assets2` -- the same property the list relies
        // on, and the reason neither builds an actual tree of nodes.
        if (!state.isExpanded(std::string{kStudioContentRootRowId})) { return rows; }

        for (const std::string& path : folders)
        {
            if (hiddenByCollapse(path, state)) { continue; }

            StudioTreeRow row;
            row.id = path;
            row.label = leafName(path);
            row.icon = StudioIcon::Folder;

            // One deeper than the list's, because the `Project` root is above them all here and
            // is not a row the list has.
            row.depth = depthOf(path) + 1;
            row.selected = path == folder;

            // A folder with nothing directly in it still has a triangle when something is under
            // it: the alternative is a leaf that turns out to have children, which is the one
            // thing a tree must never do.
            row.hasChildren = std::any_of(folders.begin(), folders.end(),
                [&](const std::string& other) { return other.rfind(path + "/", 0) == 0; });

            if (const auto found = directCount.find(path); found != directCount.end())
            {
                row.detail = std::to_string(found->second);
            }

            rows.push_back(std::move(row));
        }
        return rows;
    }

    /**
     * @brief The asset kinds this project actually holds, in a stable order.
     *
     * From the database rather than from the enum: a filter offering ten kinds a project has none
     * of is a filter nobody reads, and one that changes length as a project grows is one that
     * teaches its own positions.
     */
    std::vector<AssetType> studioContentTypesPresent(const AssetDatabase& assets)
    {
        std::set<std::string> names;
        std::map<std::string, AssetType> byName;
        for (const AssetRecord* record : assets.getAll())
        {
            if (record == nullptr) { continue; }
            names.insert(toString(record->type));
            byName[toString(record->type)] = record->type;
        }

        std::vector<AssetType> types;
        types.reserve(names.size());
        for (const std::string& name : names) { types.push_back(byName[name]); }
        return types;
    }

    std::string_view studioContentSortName(StudioContentSort sort)
    {
        switch (sort)
        {
            case StudioContentSort::Name: return "name";
            case StudioContentSort::Type: return "type";
        }
        return "";
    }

    bool studioContentMatches(const AssetRecord& record, std::string_view search)
    {
        if (search.empty()) { return true; }
        return searchRank(record, lowered(search)) >= 0;
    }

    /**
     * @brief Everything in the project that answers @p query, as cards.
     *
     * The folder is left behind on purpose (`STUDIO-09005`). Searching within one folder is the
     * behaviour that makes people type a name, see nothing, and conclude an asset is gone when it
     * is one folder over — and the location line on each card is what stops a flat result list
     * being ambiguous about which `player.png` was found.
     */
    std::vector<StudioContentCard> studioContentSearchCards(const AssetDatabase& assets,
                                                            const StudioContentQuery& query,
                                                            const Uuid& selected,
                                                            const StudioAssetShortcuts& shortcuts)
    {
        const std::string needle = lowered(query.search);

        std::vector<std::pair<int, const AssetRecord*>> ranked;
        for (const AssetRecord* record : assets.getAll())
        {
            if (record == nullptr) { continue; }
            if (query.type.has_value() && record->type != *query.type) { continue; }

            const int rank = searchRank(*record, needle);
            if (rank >= 0) { ranked.emplace_back(rank, record); }
        }

        // Rank first, then the requested order within a rank. A result list that sorted purely by
        // name would bury an exact match under everything whose path happens to contain the word.
        std::sort(ranked.begin(), ranked.end(), [&](const auto& lhs, const auto& rhs) {
            if (lhs.first != rhs.first) { return lhs.first < rhs.first; }
            if (query.sort == StudioContentSort::Type && lhs.second->type != rhs.second->type)
            {
                // string_view, for the reason the browsing path gives: `<` on two `const char*`
                // compares pointers.
                return std::string_view{toString(lhs.second->type)}
                     < std::string_view{toString(rhs.second->type)};
            }
            return lhs.second->sourcePath < rhs.second->sourcePath;
        });

        if (query.descending) { std::reverse(ranked.begin(), ranked.end()); }

        std::vector<StudioContentCard> cards;
        cards.reserve(ranked.size());
        for (const auto& [rank, record] : ranked)
        {
            (void)rank;

            StudioContentCard card;
            card.assetId = record->id;
            card.label = splitPath(record->sourcePath).second;
            card.detail = toString(record->type);
            card.icon = studioAssetIcon(record->type);
            card.selected = record->id == selected;

            // Where it is, which is the whole reason a result is readable. `Project` rather than
            // an empty string for an asset at the root: "/ player.png" is a path fragment somebody
            // has to reconstruct.
            const std::string directory = splitPath(record->sourcePath).first;
            card.location = directory.empty() ? "Project" : directory;

            if (assets.isMissing(record->id))
            {
                card.missing = true;
                card.detail = "missing";
                card.icon = StudioIcon::Warning;
                card.iconRole = StudioColorRole::Warning;
            }
            else if (studioSourceChangedSinceImport(*record))
            {
                // Said beside the kind rather than instead of it: what the asset *is* does not stop
                // being true because its file has moved on. Arithmetic on two stamps the record
                // already carries, so marking every row costs no syscall (STUDIO-30015).
                card.needsReimport = true;
                card.detail += "  ·  out of date";
            }
            card.favourite = shortcuts.isFavourite(record->id);
            if (card.favourite && !card.missing)
            {
                // Said with *colour* rather than with a star glyph. The shipped typeface is
                // rasterised on demand and has no `U+2605`, so a star would be a tofu box beside
                // every favourite -- the same reason the search field has no magnifier, and the
                // same task that fixes both (STUDIO-04019 font fallback). A column of its own was
                // the other option and is worse: one that is empty on ninety-nine rows in a
                // hundred costs width and says nothing.
                card.iconRole = StudioColorRole::Accent;
            }
            cards.push_back(std::move(card));
        }
        return cards;
    }

    namespace
    {
        /**
         * @brief Whether a listing's order can be answered position by position.
         *
         * True for an ordinary browse: subfolders alphabetically, then files in path order, which
         * is the order the path index is already in. False the moment something has to be
         * *considered* to know what is at position n -- a kind filter, a sort by kind, a reversal.
         */
        bool isPositionalOrder(const StudioContentQuery& query)
        {
            return query.search.empty() && !query.type.has_value()
                && query.sort == StudioContentSort::Name && !query.descending;
        }

        /** @brief The immediate subfolders of @p folder, in path order, from the path index. */
        std::vector<std::string> immediateSubfolders(const AssetDatabase& assets,
                                                     const std::string& folder)
        {
            std::vector<std::string> subfolders;

            const std::string prefix = folder.empty() ? std::string{} : folder + "/";

            // The *folder* index, not the path index (STUDIO-30022). Walking asset paths meant
            // stepping over every file in the folder to find the folders among them -- files and
            // folders interleave alphabetically, so no seek skips them -- which made answering
            // "what is in here" cost what is in here. It was eight thousand steps and eight
            // thousand substrings, twice a frame, inside the one function whose entire purpose is
            // to answer without touching a record.
            const std::map<std::string, std::size_t>& folders = assets.getFolderTotals();

            for (auto entry = folders.lower_bound(prefix); entry != folders.end();)
            {
                const std::string& path = entry->first;
                if (path.compare(0, prefix.size(), prefix) != 0) { break; }

                // The folder itself, which sits in this map too and is not its own child. Only
                // reachable at the project root, whose prefix is empty and whose key is as well.
                if (path.size() <= prefix.size()) { ++entry; continue; }

                const std::size_t slash = path.find('/', prefix.size());

                // Recorded once and the whole of it skipped. `'/' + 1` is the first character that
                // sorts after every path inside it, so one seek replaces walking however deep the
                // tree under there goes.
                const std::string child =
                    slash == std::string::npos ? path : path.substr(0, slash);
                subfolders.push_back(child);
                entry = folders.lower_bound(child + static_cast<char>('/' + 1));
            }
            return subfolders;
        }

        /** @brief A folder card for @p path, with the count a card answers with. */
        StudioContentCard folderCard(const AssetDatabase& assets, const std::string& path)
        {
            StudioContentCard card;
            card.folder = path;
            card.label = leafName(path);
            card.icon = StudioIcon::Folder;

            // Everything underneath, which is the question a *card* answers: "is opening this worth
            // the click" (STUDIO-07008). The pane beside it shows the direct count, which answers
            // "how much will I see when I click" (STUDIO-09001) -- two questions asked in two
            // places, and both decided deliberately.
            const std::size_t contents = assets.getTotalAssetCount(path);
            card.detail = std::to_string(contents) + (contents == 1 ? " item" : " items");
            return card;
        }

        /** @brief An asset card for @p record, with everything a listing says about it. */
        StudioContentCard assetCard(const AssetDatabase& assets, const AssetRecord& record,
                                    const Uuid& selected, const StudioAssetShortcuts& shortcuts)
        {
            StudioContentCard card;
            card.assetId = record.id;
            card.label = splitPath(record.sourcePath).second;
            card.detail = toString(record.type);
            card.icon = studioAssetIcon(record.type);
            card.selected = record.id == selected;

            if (assets.isMissing(record.id))
            {
                // Coloured, unlike every other card. A missing asset is the one whose *state*
                // matters more than its kind, and the warning colour is what makes it findable in
                // a folder of two hundred without reading any of them.
                card.missing = true;
                card.detail = "missing";
                card.icon = StudioIcon::Warning;
                card.iconRole = StudioColorRole::Warning;
            }
            else if (studioSourceChangedSinceImport(record))
            {
                card.needsReimport = true;
                card.detail += "  ·  out of date";
            }

            card.favourite = shortcuts.isFavourite(record.id);
            if (card.favourite && !card.missing)
            {
                // Said with *colour* rather than with a star glyph: the shipped typeface has no
                // `U+2605`, and a tofu box beside every favourite is worse than none -- the same
                // reason the search field has no magnifier (STUDIO-04019).
                card.iconRole = StudioColorRole::Accent;
            }
            return card;
        }
    }

    std::size_t studioContentCardCount(const AssetDatabase& assets, const std::string& folder,
                                       const StudioContentQuery& query,
                                       const StudioAssetShortcuts& shortcuts)
    {
        if (isPositionalOrder(query) && !studioContentIsShortcutFolder(folder))
        {
            // From the database's own counts: the folders under this one, and what is directly in
            // it. No card is built and no record is touched.
            return immediateSubfolders(assets, folder).size() + assets.getDirectAssetCount(folder);
        }

        // Anything that filters has to look at what it is filtering.
        return studioContentCards(assets, folder, Uuid{}, query, shortcuts).size();
    }

    std::vector<StudioContentCard> studioContentCardWindow(const AssetDatabase& assets,
                                                           const std::string& folder,
                                                           const Uuid& selected,
                                                           const StudioContentQuery& query,
                                                           const StudioAssetShortcuts& shortcuts,
                                                           std::size_t first, std::size_t count,
                                                           std::size_t* outCardsBuilt)
    {
        const auto built = [outCardsBuilt](std::size_t cards) {
            if (outCardsBuilt != nullptr) { *outCardsBuilt = cards; }
        };
        built(0);

        if (count == 0) { return {}; }

        if (!isPositionalOrder(query) || studioContentIsShortcutFolder(folder))
        {
            // The order cannot be answered by position, so the listing is built and sliced. Slow
            // and correct beats fast and wrong: sorting by something requires looking at
            // everything.
            std::vector<StudioContentCard> all =
                studioContentCards(assets, folder, selected, query, shortcuts);

            // The whole listing, because the whole listing is what was constructed. Reporting the
            // slice here would make the counter say the one thing it exists to disprove.
            built(all.size());
            if (first >= all.size()) { return {}; }

            const std::size_t last =
                count == std::string::npos ? all.size() : std::min(all.size(), first + count);
            return {std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(first)),
                    std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(last))};
        }

        std::vector<StudioContentCard> window;

        // Folders first, because a user navigating is looking for a folder and a user browsing is
        // looking at assets -- and the first of those is the one interrupted by scanning past two
        // hundred textures.
        const std::vector<std::string> subfolders = immediateSubfolders(assets, folder);

        std::size_t index = 0;
        for (const std::string& path : subfolders)
        {
            if (window.size() == count) { built(window.size()); return window; }
            if (index++ < first) { continue; }
            window.push_back(folderCard(assets, path));
        }

        // Then the files, in path order, walked from the folder's own range. Reaching position
        // `first` costs `first` steps of an iterator rather than `first` cards, which is the
        // difference between a folder of a hundred thousand being browsable and not.
        const std::string prefix = folder.empty() ? std::string{} : folder + "/";
        const std::map<std::string, Uuid>& pathIndex = assets.getPathIndex();

        for (auto entry = pathIndex.lower_bound(prefix); entry != pathIndex.end();)
        {
            if (window.size() == count) { break; }

            const std::string& path = entry->first;
            if (path.compare(0, prefix.size(), prefix) != 0) { break; }

            const std::string rest = path.substr(prefix.size());
            const std::size_t slash = rest.find('/');
            if (slash != std::string::npos)
            {
                entry = pathIndex.lower_bound(prefix + rest.substr(0, slash)
                                              + static_cast<char>('/' + 1));
                continue;
            }

            if (index++ >= first)
            {
                if (const AssetRecord* record = assets.find(entry->second); record != nullptr)
                {
                    window.push_back(assetCard(assets, *record, selected, shortcuts));
                }
            }
            ++entry;
        }

        built(window.size());
        return window;
    }

    std::vector<StudioContentCard> studioContentCards(const AssetDatabase& assets,
                                                       const std::string& folder,
                                                       const Uuid& selected,
                                                       const StudioContentQuery& query,
                                                       const StudioAssetShortcuts& shortcuts)
    {
        if (!query.search.empty())
        {
            return studioContentSearchCards(assets, query, selected, shortcuts);
        }

        // The two pseudo-folders (STUDIO-09007). A *list* rather than a folder: the ids are in the
        // order the user starred them, or the order they were used, and sorting them by name would
        // throw away the only thing those orders are for.
        if (studioContentIsShortcutFolder(folder))
        {
            const std::vector<Uuid>& ids = folder == kStudioContentFavouritesRowId
                ? shortcuts.favourites
                : shortcuts.recent;

            std::vector<StudioContentCard> listed;
            listed.reserve(ids.size());
            for (const Uuid& id : ids)
            {
                const AssetRecord* record = assets.find(id);

                // An entry whose asset has gone is simply not shown. It is pruned on load, so one
                // being here at all means the asset went away during this session -- and a row
                // that cannot be clicked is worse than one that is not there.
                if (record == nullptr) { continue; }
                if (query.type.has_value() && record->type != *query.type) { continue; }

                StudioContentCard card;
                card.assetId = record->id;
                card.label = splitPath(record->sourcePath).second;
                card.detail = toString(record->type);
                card.icon = studioAssetIcon(record->type);
                card.selected = record->id == selected;
                card.favourite = shortcuts.isFavourite(record->id);
            if (card.favourite && !card.missing)
            {
                // Said with *colour* rather than with a star glyph. The shipped typeface is
                // rasterised on demand and has no `U+2605`, so a star would be a tofu box beside
                // every favourite -- the same reason the search field has no magnifier, and the
                // same task that fixes both (STUDIO-04019 font fallback). A column of its own was
                // the other option and is worse: one that is empty on ninety-nine rows in a
                // hundred costs width and says nothing.
                card.iconRole = StudioColorRole::Accent;
            }

                // Where it is, always, unlike a folder listing: these came from all over the
                // project and a name on its own does not say which `player.png` this is.
                const std::string directory = splitPath(record->sourcePath).first;
                card.location = directory.empty() ? "Project" : directory;

                if (assets.isMissing(record->id))
                {
                    card.missing = true;
                    card.detail = "missing";
                    card.icon = StudioIcon::Warning;
                    card.iconRole = StudioColorRole::Warning;
                }
                listed.push_back(std::move(card));
            }
            return listed;
        }

        std::vector<StudioContentCard> cards;

        // Immediate children only, in both halves. A grid of every asset under a folder is a wall,
        // and the folder a user is *in* is the unit they think in -- which is the whole difference
        // between this presentation and the tree beside it.
        //
        // Walked over the *folder's own range* of the path index rather than over every record
        // (STUDIO-09016). The index is ordered by path, so a folder's contents are contiguous in
        // it, and a subtree is skipped by seeking past its last possible key -- which makes showing
        // a folder cost what that folder holds rather than what the project holds.
        std::set<std::string> subfolders;
        std::vector<const AssetRecord*> files;

        {
            const std::string prefix = folder.empty() ? std::string{} : folder + "/";
            const std::map<std::string, Uuid>& index = assets.getPathIndex();

            auto entry = index.lower_bound(prefix);
            while (entry != index.end())
            {
                const std::string& path = entry->first;
                if (path.compare(0, prefix.size(), prefix) != 0) { break; }

                const std::string rest = path.substr(prefix.size());
                const std::size_t slash = rest.find('/');

                if (slash == std::string::npos)
                {
                    if (const AssetRecord* record = assets.find(entry->second); record != nullptr)
                    {
                        files.push_back(record);
                    }
                    ++entry;
                    continue;
                }

                // A subfolder: recorded once, and the whole of it skipped. `'/' + 1` is the first
                // character that sorts after every path inside it, so one seek replaces walking
                // however many thousand assets are under there.
                const std::string child = prefix + rest.substr(0, slash);
                subfolders.insert(child);
                entry = index.lower_bound(child + static_cast<char>('/' + 1));
            }
        }

        // Folders first: a user navigating is looking for a folder and a user browsing is looking
        // at assets, and the first of those is the one interrupted by having to scan past two
        // hundred textures.
        for (const std::string& path : subfolders)
        {
            StudioContentCard card;
            card.folder = path;
            card.label = leafName(path);
            card.icon = StudioIcon::Folder;

            // Everything underneath, which is the question a *card* answers: "is opening this
            // worth the click" (STUDIO-07008). The pane beside it shows the direct count, which
            // answers "how much will I see when I click" (STUDIO-09001) -- two questions asked in
            // two places, and both decided deliberately.
            //
            // From the index rather than from a walk per subfolder, which was quadratic and is
            // what made the grid unusable at a hundred thousand assets (STUDIO-09016).
            const std::size_t contents = assets.getTotalAssetCount(path);
            card.detail = std::to_string(contents) + (contents == 1 ? " item" : " items");
            cards.push_back(std::move(card));
        }

        // Filtering applies to files and never to folders (`STUDIO-09006`): a filter that hid the
        // folders as well would leave a user filtered to textures unable to reach the folder the
        // textures are in, which is filtering them out of their own project.
        if (query.type.has_value())
        {
            files.erase(std::remove_if(files.begin(), files.end(),
                                       [&](const AssetRecord* record) {
                                           return record->type != *query.type;
                                       }),
                        files.end());
        }

        std::sort(files.begin(), files.end(), [&](const AssetRecord* a, const AssetRecord* b) {
            if (query.sort == StudioContentSort::Type && a->type != b->type)
            {
                // Through string_view: `toString` returns a `const char*`, and `<` on two of those
                // compares *pointers* -- an order that is not alphabetical, not stable across
                // builds, and not even the same twice within one.
                return std::string_view{toString(a->type)} < std::string_view{toString(b->type)};
            }
            return a->sourcePath < b->sourcePath;
        });

        // Only the files. Folders stay alphabetical and stay first whatever the order: they are
        // navigation, and navigation that reorders itself is navigation people stop trusting.
        if (query.descending) { std::reverse(files.begin(), files.end()); }

        for (const AssetRecord* record : files)
        {
            StudioContentCard card;
            card.assetId = record->id;
            card.label = splitPath(record->sourcePath).second;
            card.detail = toString(record->type);
            card.icon = studioAssetIcon(record->type);
            card.selected = record->id == selected;

            if (assets.isMissing(record->id))
            {
                // Coloured, unlike every other card. A missing asset is the one whose *state*
                // matters more than its kind, and the warning colour is what makes it findable in
                // a folder of two hundred without reading any of them.
                card.missing = true;
                card.detail = "missing";
                card.icon = StudioIcon::Warning;
                card.iconRole = StudioColorRole::Warning;
            }
            else if (studioSourceChangedSinceImport(*record))
            {
                // Said beside the kind rather than instead of it: what the asset *is* does not stop
                // being true because its file has moved on. Arithmetic on two stamps the record
                // already carries, so marking every row costs no syscall (STUDIO-30015).
                card.needsReimport = true;
                card.detail += "  ·  out of date";
            }
            card.favourite = shortcuts.isFavourite(record->id);
            if (card.favourite && !card.missing)
            {
                // Said with *colour* rather than with a star glyph. The shipped typeface is
                // rasterised on demand and has no `U+2605`, so a star would be a tofu box beside
                // every favourite -- the same reason the search field has no magnifier, and the
                // same task that fixes both (STUDIO-04019 font fallback). A column of its own was
                // the other option and is worse: one that is empty on ninety-nine rows in a
                // hundred costs width and says nothing.
                card.iconRole = StudioColorRole::Accent;
            }
            cards.push_back(std::move(card));
        }

        return cards;
    }

    // --- Rename, move, duplicate and delete (STUDIO-09009) ---------------------------------------
    //
    // Each of them builds a command and hands it to the context, so all four undo, and all four
    // undo the same way as every other document change (ANALYSIS.md decision D-06). None of them
    // touches a scene: a reference is a Uuid, and the id survives a move, a rename and a restore
    // from the undo stack alike -- which is the property STUDIO-09009 exists to guarantee.

    namespace
    {
        /** @brief The last segment of @p path: the file or folder name, with no path above it. */
        std::string lastSegment(std::string_view path)
        {
            const std::size_t slash = path.find_last_of('/');
            return std::string{slash == std::string_view::npos ? path : path.substr(slash + 1)};
        }

        /** @brief What an asset or a folder is called, which is what a rename field starts with. */
        std::string studioContentDisplayName(const AssetDatabase& assets, const Uuid& asset,
                                             const std::string& folder)
        {
            if (!folder.empty()) { return lastSegment(folder); }
            const AssetRecord* record = assets.find(asset);
            return record == nullptr ? std::string{} : lastSegment(record->sourcePath);
        }

        /** @brief Reports @p operation to the console when it failed, and returns it. */
        StudioContentOperation reported(StudioContext& context, StudioContentOperation operation)
        {
            // A refused rename that said nothing would look like a browser that had stopped
            // responding. The console is where Studio already says what it would not do.
            if (!operation.applied && !operation.message.empty())
            {
                context.log(LogSeverity::Warning, operation.message);
            }
            return operation;
        }
    }

    StudioContentOperation studioContentRename(StudioContext& context, const Uuid& asset,
                                               const std::string& folder,
                                               const std::string& newName)
    {
        if (const std::string problem = describeStudioAssetNameProblem(newName); !problem.empty())
        {
            return reported(context, {false, problem});
        }

        if (!folder.empty())
        {
            std::string error;
            std::unique_ptr<StudioCommand> command = studioMoveFolderCommand(
                context.getAssets(), folder, studioAssetPathRenamedTo(folder, newName), &error);
            if (command == nullptr) { return reported(context, {false, std::move(error)}); }

            const std::string description = command->getDescription();
            context.execute(std::move(command));
            return {true, description};
        }

        const AssetRecord* record = context.getAssets().find(asset);
        if (record == nullptr) { return reported(context, {false, "no asset with that id"}); }

        auto command = std::make_unique<MoveAssetCommand>(
            context.getAssets(), asset, studioAssetPathRenamedTo(record->sourcePath, newName));
        if (!command->isValid())
        {
            return reported(context, {false, command->getError()});
        }

        const std::string description = command->getDescription();
        context.execute(std::move(command));
        return {true, description};
    }

    StudioContentOperation studioContentMoveInto(StudioContext& context, const Uuid& asset,
                                                 const std::string& folder)
    {
        const AssetRecord* record = context.getAssets().find(asset);
        if (record == nullptr) { return reported(context, {false, "no asset with that id"}); }

        const std::size_t slash = record->sourcePath.find_last_of('/');
        const std::string name = slash == std::string::npos ? record->sourcePath
                                                            : record->sourcePath.substr(slash + 1);

        auto command = std::make_unique<MoveAssetCommand>(
            context.getAssets(), asset, folder.empty() ? name : folder + "/" + name);
        if (!command->isValid())
        {
            // Dropping a file back into the folder it is already in is a gesture, not a mistake:
            // it is refused silently rather than logged, because a console line every time someone
            // changes their mind mid-drag is noise.
            return {false, command->getError()};
        }

        const std::string description = command->getDescription();
        context.execute(std::move(command));
        return {true, description};
    }

    StudioContentOperation studioContentDuplicate(StudioContext& context, const Uuid& asset)
    {
        auto command = std::make_unique<DuplicateAssetCommand>(context.getAssets(), asset);
        if (!command->isValid())
        {
            return reported(context, {false, command->getError()});
        }

        const std::string description = command->getDescription();
        const Uuid copyId = command->getCopyId();
        context.execute(std::move(command));

        // Selected, so the next thing the user does happens to the copy. A duplicate that left the
        // original selected is one people edit by mistake, and the mistake is invisible.
        context.selectAsset(copyId);
        return {true, description};
    }

    StudioContentOperation studioContentDelete(StudioContext& context, const Uuid& asset)
    {
        auto command = std::make_unique<DeleteAssetCommand>(context.getAssets(), asset);
        if (!command->isValid())
        {
            return reported(context, {false, command->getError()});
        }

        const std::string description = command->getDescription();
        const bool wasSelected = context.getSelectedAsset() == asset;
        context.execute(std::move(command));

        // An inspector still showing a file that no longer exists is the panel telling the user
        // the delete did not work.
        if (wasSelected) { context.selectAsset(Uuid{}); }
        return {true, description};
    }

    StudioContentOperation studioContentReimport(StudioContext& context, const Uuid& asset)
    {
        const AssetRecord* record = context.getAssets().find(asset);
        if (record == nullptr) { return reported(context, {false, "no asset with that id"}); }
        if (!record->sourcePresent)
        {
            return reported(context, {false, "'" + record->sourcePath + "' is not on disk"});
        }

        const std::string path = record->sourcePath;
        const StudioReimportResult reimport = studioReimportAssets(context.getAssets(), {asset});
        if (reimport.reimported == 0)
        {
            return reported(context,
                            {false, reimport.warnings.empty() ? "nothing to reimport"
                                                              : reimport.warnings.front()});
        }

        // Said even when nothing changed, because "I pressed Reimport and the editor did nothing"
        // is indistinguishable from a broken button -- and "nothing changed" is the answer most of
        // the time, which is exactly why it has to be said out loud.
        return {true, reimport.factsChanged != 0 ? "Reimported '" + path + "'"
                                                 : "Reimported '" + path + "'; nothing changed"};
    }

    std::vector<StudioContextMenuItem> studioContentMenuItems(const AssetDatabase& assets,
                                                              const Uuid& asset,
                                                              const std::string& folder,
                                                              bool starred)
    {
        if (!folder.empty())
        {
            // A folder can be shown too, and on every platform: `xdg-open` on a directory is
            // exactly the supported case, and the other two open it as readily as they select a
            // file in it.
            return {StudioContextMenuItem{"Rename", true, "F2"},
                    StudioContextMenuItem{"Show in Folder", true, {}}};
        }

        if (!asset.isValid()) { return {}; }

        // A missing source can still be renamed -- that is metadata, and the record is what is
        // being renamed -- but there is nothing to copy and nothing to delete. Greyed rather than
        // absent: a menu that changes length depending on the file's state is one where the user
        // clicks Delete and gets Duplicate.
        const bool present = !assets.isMissing(asset);

        // Offered whenever the file is there, not only when it is out of date: "reimport this
        // anyway" is a thing people do when they suspect the editor is wrong about a file, and a
        // row that greyed out unless Studio already agreed something had changed would refuse them
        // exactly then. Whether it is *due* is the row's marker, which is a different question.
        // Starring is offered for a *missing* asset too, unlike everything else here: a favourite
        // is a note about the asset rather than an operation on its file, and the one a user most
        // wants to keep hold of is the one that has gone wrong.
        return {StudioContextMenuItem{starred ? "Remove from Favourites" : "Add to Favourites",
                                      true, {}},
                StudioContextMenuItem{},
                StudioContextMenuItem{"Rename", true, "F2"},
                StudioContextMenuItem{"Duplicate", present, "Ctrl+D"},
                StudioContextMenuItem{"Reimport", present, {}},
                StudioContextMenuItem{"Show in Folder", present, {}},
                StudioContextMenuItem{},
                StudioContextMenuItem{"Delete", present, "Delete"}};
    }

    StudioContentBrowserResult studioContentBrowser(StudioFrame& frame, const UiRect& bounds,
                                                    StudioContext& context,
                                                    StudioContentBrowserState& state)
    {
        const StudioTheme& theme = frame.theme();
        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);
        const float rowHeight = metricOf(theme, StudioMetric::RowHeight);

        UiRect area = bounds;

        // --- The bar ---------------------------------------------------------------------------
        //
        // Breadcrumb on the left, view switch on the right, above both presentations. Above rather
        // than inside either, because "where am I" and "how am I looking at it" are the two
        // questions a content browser answers before it shows anything, and a control that moved
        // when the view changed would be a control the user hunts for.
        StudioContentBrowserResult result;
        result.view = state.view;
        result.folder = state.folder;

        {
            UiRect bar = area.splitTop(std::min(rowHeight + spacing, area.height));
            bar = bar.inset(UiEdges{spacing, 0.0f, spacing, spacing * 0.5f});
            frame.ids().push("contentbar");

            // Right first, so a long breadcrumb runs out of room rather than pushing the switch
            // off the end -- the same reason the status bar lays itself out from the right.
            {
                const UiRect box = bar.splitRight(std::min(bar.width, rowHeight * 2.0f + spacing));
                UiRect cursor = box;
                for (const auto& [view, icon, help] :
                     {std::tuple{StudioContentView::Grid, StudioIcon::Grid, "Show assets as a grid"},
                      std::tuple{StudioContentView::List, StudioIcon::File, "Show assets as a list"}})
                {
                    const UiRect button = cursor.splitRight(std::min(cursor.width, rowHeight));

                    StudioButtonOptions options;
                    options.icon = icon;
                    options.iconOnly = true;
                    options.selected = state.view == view;
                    options.tooltip = help;
                    if (studioButton(frame, frame.ids().makeIndex(static_cast<std::int64_t>(view)),
                                     button, help, options).activated)
                    {
                        state.view = view;
                    }
                }
            }

            // The search field, next to the view switch. Wide enough to read a file name in, and
            // taken out of the bar before the breadcrumb so that a deep folder shortens the crumb
            // rather than squeezing the field to nothing (STUDIO-09005).
            {
                bar.splitRight(std::min(bar.width, spacing));

                const UiRect filterButton =
                    bar.splitRight(std::min(bar.width, rowHeight));
                StudioButtonOptions filterOptions;
                filterOptions.icon = StudioIcon::ChevronDown;
                filterOptions.iconOnly = true;
                filterOptions.selected = state.filtersOpen || state.query.type.has_value()
                                      || state.query.sort != StudioContentSort::Name
                                      || state.query.descending;
                filterOptions.tooltip = "Filter by kind and choose an order";
                if (studioButton(frame, frame.ids().make("filters"), filterButton, "Filter",
                                 filterOptions).activated)
                {
                    state.filtersOpen = !state.filtersOpen;
                }

                bar.splitRight(std::min(bar.width, spacing));

                const UiRect field =
                    bar.splitRight(std::min(bar.width * 0.5f, rowHeight * 8.0f));

                StudioTextFieldOptions options;
                options.placeholder = "Search";
                options.font = StudioFontRole::BodySmall;

                // No glyph prefix: the shipped atlas has no magnifier, and a tofu box in front of
                // a field is worse than a field with only its placeholder to explain it
                // (STUDIO-04019 is the font-fallback task that would change that).
                (void)studioTextField(frame, frame.ids().make("search"), field, state.query.search,
                                      options);
            }

            // The breadcrumb, every segment clickable. A path drawn as text would say where the
            // user is and leave going up a level to a control that does not exist.
            if (frame.isDrawPass() || frame.isInputPass())
            {
                frame.ids().push("crumbs");
                for (const auto& [label, path] : studioContentBreadcrumb(state.folder))
                {
                    if (bar.width <= 0.0f) { break; }

                    const float width = std::min(
                        std::ceil(studioLabelWidth(frame, label, StudioFontRole::BodySmall)
                                  + spacing * 2.0f),
                        bar.width);
                    const UiRect crumb = bar.splitLeft(width);

                    StudioButtonOptions options;
                    options.kind = StudioButtonKind::Ghost;
                    options.font = StudioFontRole::BodySmall;
                    if (studioButton(frame, frame.ids().make(path.empty() ? "root" : path),
                                     crumb, label, options).activated)
                    {
                        state.folder = path;
                    }

                    if (bar.width > 0.0f && !path.empty() && path != state.folder)
                    {
                        const UiRect chevron = bar.splitLeft(std::min(bar.width, rowHeight * 0.6f));
                        if (frame.isDrawPass())
                        {
                            studioDrawIcon(frame, chevron, StudioIcon::ChevronRight,
                                           theme.color(StudioColorRole::TextDisabled));
                        }
                    }
                }
                frame.ids().pop();
            }

            frame.ids().pop();
            result.folder = state.folder;
        }

        // --- Filters and order, when asked for ----------------------------------------------
        //
        // Behind a button rather than always on the bar. Four controls above a browser that most
        // often needs none of them is four controls' worth of a panel that is already the smallest
        // one in the default layout.
        if (state.filtersOpen)
        {
            UiRect row = area.splitTop(std::min(rowHeight + spacing, area.height));
            row = row.inset(UiEdges{spacing, 0.0f, spacing, spacing * 0.5f});
            frame.ids().push("contentfilters");

            const std::vector<AssetType> types = studioContentTypesPresent(context.getAssets());

            std::vector<std::string> options;
            options.reserve(types.size() + 1);
            options.emplace_back("All kinds");
            for (const AssetType type : types) { options.emplace_back(toString(type)); }

            int selected = 0;
            if (state.query.type.has_value())
            {
                const auto found = std::find(types.begin(), types.end(), *state.query.type);
                selected = found == types.end()
                    ? 0
                    : static_cast<int>(std::distance(types.begin(), found)) + 1;
            }

            const UiRect kind = row.splitLeft(std::min(row.width, rowHeight * 6.0f));
            if (studioDropdown(frame, frame.ids().make("kind"), kind, options, selected).changed)
            {
                // Index zero is "All kinds", so the filter clears rather than needing its own
                // button beside the control that set it.
                if (selected <= 0) { state.query.type.reset(); }
                else if (static_cast<std::size_t>(selected) <= types.size())
                {
                    state.query.type = types[static_cast<std::size_t>(selected) - 1];
                }
            }
            row.splitLeft(std::min(row.width, spacing));

            for (const auto& [sort, label] :
                 {std::pair{StudioContentSort::Name, "Name"},
                  std::pair{StudioContentSort::Type, "Kind"}})
            {
                const UiRect button = row.splitLeft(std::min(row.width, rowHeight * 3.0f));
                StudioButtonOptions options2;
                options2.selected = state.query.sort == sort;
                options2.font = StudioFontRole::BodySmall;
                if (studioButton(frame, frame.ids().make(label), button, label, options2).activated)
                {
                    state.query.sort = sort;
                }
            }
            row.splitLeft(std::min(row.width, spacing));

            {
                const UiRect button = row.splitLeft(std::min(row.width, rowHeight * 3.0f));
                StudioButtonOptions options2;
                options2.selected = state.query.descending;
                options2.font = StudioFontRole::BodySmall;
                options2.tooltip = "Reverse the order";
                if (studioButton(frame, frame.ids().make("reverse"), button,
                                 state.query.descending ? "Z to A" : "A to Z", options2).activated)
                {
                    state.query.descending = !state.query.descending;
                }
            }

            frame.ids().pop();
        }

        // --- The folder pane ---------------------------------------------------------------
        //
        // Beside both presentations rather than inside either (STUDIO-09001). Where a user is and
        // what they are looking at are two questions, and a navigation tree that appeared only in
        // one view would make switching views also mean switching how you move around.
        result = studioContentFolderPane(frame, area, context, state, std::move(result));

        result = state.view == StudioContentView::Grid
            ? studioContentGrid(frame, area, context, state, std::move(result))
            : studioContentList(frame, area, context, state, std::move(result));

        // --- The right-click menu (STUDIO-09009) --------------------------------------------
        //
        // Described after both presentations, so its id is the same whichever one drew: a menu
        // whose identity depended on the view would close itself the moment the view changed under
        // it. The panel's own menu rather than the shell's, because its rows are about the file
        // under the pointer and registering "Duplicate" as an application action would put it in
        // the command palette, where there is no pointer and nothing under it.
        const WidgetId menuId = frame.ids().make("contentmenu");
        if (frame.isInputPass() && result.menuRequested)
        {
            state.menuAsset = result.menuAsset;
            state.menuFolder = result.menuFolder;
            studioOpenContextMenu(frame, menuId, frame.input().mouseX, frame.input().mouseY);
        }

        const std::vector<StudioContextMenuItem> items = studioContentMenuItems(
            context.getAssets(), state.menuAsset, state.menuFolder,
            state.shortcuts.isFavourite(state.menuAsset));

        // Dispatched on the label rather than on the index, because the rows differ between an
        // asset and a folder: an index that meant Duplicate in one menu and nothing in the other
        // is the kind of off-by-one that deletes the wrong file.
        const int chosen = studioContextMenu(frame, menuId, items);
        const std::string_view action =
            chosen >= 0 && static_cast<std::size_t>(chosen) < items.size()
                ? std::string_view{items[static_cast<std::size_t>(chosen)].label}
                : std::string_view{};

        if (action == "Rename")
        {
            // Renaming begins where the name is, in whichever view is showing. The tree's rename
            // state carries it and the grid reads the same field, so switching view part-way
            // through an edit keeps the edit.
            state.tree.beginRename(
                state.menuFolder.empty() ? state.menuAsset.toString() : state.menuFolder,
                studioContentDisplayName(context.getAssets(), state.menuAsset, state.menuFolder));
        }
        else if (action == "Duplicate")
        {
            result.lastOperation = studioContentDuplicate(context, state.menuAsset);
        }
        else if (action == "Add to Favourites" || action == "Remove from Favourites")
        {
            (void)state.shortcuts.toggleFavourite(state.menuAsset);
            result.shortcutsChanged = true;
        }
        else if (action == "Reimport")
        {
            result.lastOperation = studioContentReimport(context, state.menuAsset);
        }
        else if (action == "Show in Folder")
        {
            // The asset's own path where there is one, so the file is highlighted on the platforms
            // that can; the folder's otherwise.
            if (const AssetRecord* record = context.getAssets().find(state.menuAsset);
                record != nullptr)
            {
                result.revealPath = context.getAssets().resolvePath(record->sourcePath);
            }
            else if (!state.menuFolder.empty())
            {
                result.revealPath = context.getAssets().resolvePath(state.menuFolder);
            }
        }
        else if (action == "Delete")
        {
            result.lastOperation = studioContentDelete(context, state.menuAsset);
        }

        return result;
    }

    namespace
    {
    StudioContentBrowserResult studioContentList(StudioFrame& frame, const UiRect& bounds,
                                                 StudioContext& context,
                                                 StudioContentBrowserState& state,
                                                 StudioContentBrowserResult result)
    {
        const StudioTheme& theme = frame.theme();
        const AssetDatabase& assets = context.getAssets();

        // The *count* first, then the window (STUDIO-09016), exactly as the grid does it. The same
        // model the grid draws (STUDIO-09002): two presentations of one folder rather than two
        // browsers sharing a panel, so switching view does not also move the user.
        const std::size_t total =
            studioContentCardCount(assets, state.folder, state.query, state.shortcuts);
        result.rowsTotal = total;

        // The count, not the list (STUDIO-30015). Building the list to call `.size()` on it was a
        // second full pass over the database with a `stat` per asset, on every pass of every frame
        // -- half the 3 000 syscalls a frame `STUDIO-30014` measured at 1 500 assets.
        result.missingCount = assets.getMissingCount();

        if (total == 0)
        {
            if (frame.isDrawPass())
            {
                studioDrawText(frame,
                               bounds.inset(UiEdges{metricOf(theme, StudioMetric::SpacingMedium)}),
                               studioContentEmptyMessage(context, state), StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }
            return result;
        }

        // Opened here rather than inside the tree, because the window can only be asked once the
        // view's position is resolved -- and that is what decides which rows are worth building.
        StudioScrollOptions scroll;
        scroll.contentHeight =
            static_cast<float>(total) * studioTreeRowHeight(theme);
        scroll.wheelStep = studioTreeRowHeight(theme) * 3.0f;

        const StudioScrollResult view =
            studioBeginScroll(frame, frame.ids().make("treescroll"), bounds, scroll);

        const StudioTreeWindow window = studioTreeWindow(view, total, theme);

        // Only the window is built. This is the line that makes a folder of a hundred thousand
        // files browsable in the list view rather than merely drawable.
        const std::vector<StudioContentCard> cards = studioContentCardWindow(
            assets, state.folder, context.getSelectedAsset(), state.query, state.shortcuts,
            window.firstRow, window.rowCount, &result.rowsBuilt);

        result.visibleAssets.reserve(cards.size());
        for (const StudioContentCard& card : cards)
        {
            if (!card.isFolder()) { result.visibleAssets.push_back(card.assetId); }
        }

        std::vector<StudioTreeRow> rows;
        rows.reserve(cards.size());
        for (const StudioContentCard& card : cards)
        {
            StudioTreeRow row;
            row.id = card.isFolder() ? card.folder : card.assetId.toString();
            row.label = card.label;

            // In a search, where it is rather than what it is: `player.png` three folders deep,
            // twice over, is what a flat result list is ambiguous about. The kind is already the
            // icon.
            row.detail = card.location.empty() ? card.detail
                                               : card.location + "  ·  " + card.detail;

            row.icon = card.icon;
            row.iconRole = card.iconRole;
            row.selected = card.selected;

            // Dimmed, not disabled. A missing asset is the row a user most needs to click:
            // clicking it is how they find out what references the file that has gone.
            row.muted = card.missing;

            // Flat, and no disclosure triangles. Indentation says "this is inside that", and
            // inside one folder everything is at the same level; entering a folder is a *click*,
            // the way it is in the grid and in every file manager, rather than an expansion that
            // would put two folders' contents on screen and make the breadcrumb wrong.
            row.depth = 0;
            row.hasChildren = false;

            if (!card.isFolder())
            {
                // Draggable onto anything that takes an asset. Folders are not -- there is
                // nothing a folder means as a property value.
                row.dragType = std::string{kStudioAssetDragType};
                row.dragValue = row.id;
            }
            else
            {
                // And a folder takes one, which is how a file is moved (STUDIO-09009). Dragging is
                // the gesture people reach for first; the menu exists for the file that is already
                // where the drag would have to start from.
                row.dropTypes = {std::string{kStudioAssetDragType}};
            }

            rows.push_back(std::move(row));
        }

        const StudioTreeResult tree = studioTreeRows(frame, view, rows, state.tree, window);
        studioEndScroll(frame);
        result.rowsDrawn = tree.rowsDrawn;

        // A rename commits into the same move that a drag would produce (STUDIO-09009). The tree
        // owns the field and the keys; what the new name *means* is the browser's to say.
        if (tree.renamed.has_value())
        {
            const StudioContentCard& card = cards[*tree.renamed];
            result.lastOperation = studioContentRename(context, card.assetId, card.folder,
                                                      tree.renamedTo);
        }

        if (tree.rightClicked.has_value())
        {
            const StudioContentCard& card = cards[*tree.rightClicked];
            result.menuAsset = card.assetId;
            result.menuFolder = card.folder;
            result.menuRequested = true;
        }

        // A drop onto a folder row moves the asset there. The same operation the menu's rename
        // performs and the same command behind it, so a moved file keeps its id either way and no
        // scene is touched.
        if (tree.dropped.has_value())
        {
            const StudioContentCard& card = cards[*tree.dropped];
            if (card.isFolder())
            {
                result.lastOperation =
                    studioContentMoveInto(context, Uuid::parse(tree.droppedValue), card.folder);
            }
        }

        if (tree.clicked.has_value())
        {
            const StudioContentCard& card = cards[*tree.clicked];
            if (card.isFolder())
            {
                state.folder = card.folder;
                result.folder = state.folder;
            }
            else
            {
                // Through the context, not into an out-parameter the shell keeps beside the one
                // StudioContext already has. Two ideas of "the selected asset" meant the native
                // Details panel could not see what the native Content Browser had selected --
                // STUDIO-07045. Selecting an asset also clears the entity selection, which is what
                // makes the inspector show one thing at a time.
                context.selectAsset(card.assetId);
                result.selectedAsset = card.assetId;

                // Recorded on *selection* rather than on some notion of "opened", because
                // selecting is what a user does to look at an asset -- and it is the only moment
                // the browser can be sure they meant that one (STUDIO-09007).
                if (state.shortcuts.remember(card.assetId, StudioAssetShortcutStore::kMaximumRecent))
                {
                    result.shortcutsChanged = true;
                }
            }
        }

        return result;
    }

    StudioContentBrowserResult studioContentGrid(StudioFrame& frame, const UiRect& bounds,
                                                 StudioContext& context,
                                                 StudioContentBrowserState& state,
                                                 StudioContentBrowserResult result)
    {
        const StudioTheme& theme = frame.theme();
        const AssetDatabase& assets = context.getAssets();

        // The *count* first, then the window (STUDIO-09016). Building the whole listing to learn
        // how long it is was the last O(project) pass in this panel: at a hundred thousand assets
        // that is a hundred thousand cards, each with its own strings, built twice a frame to show
        // forty of them.
        const std::size_t total =
            studioContentCardCount(assets, state.folder, state.query, state.shortcuts);
        result.rowsTotal = total;

        // The count, not the list (STUDIO-30015). Building the list to call `.size()` on it was a
        // second full pass over the database with a `stat` per asset, on every pass of every frame
        // -- half the 3 000 syscalls a frame `STUDIO-30014` measured at 1 500 assets.
        result.missingCount = assets.getMissingCount();

        if (total == 0)
        {
            // A folder with nothing in it is a place, not a failure, and it says which place.
            if (frame.isDrawPass())
            {
                studioDrawText(frame, bounds, studioContentEmptyMessage(context, state),
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary));
            }
            return result;
        }

        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);
        const float card = std::max(48.0f, state.cardSize * theme.scale());
        const float labelHeight = metricOf(theme, StudioMetric::RowHeight) * 1.6f;
        const float cardHeight = card + labelHeight;

        StudioScrollOptions scroll;

        // Through the shared helper rather than the same `floor` written here (STUDIO-30010): the
        // extent and the layout are computed in different places, and a grid whose two ideas of the
        // column count disagreed would scroll past its own last row.
        scroll.contentHeight =
            studioGridContentHeight(bounds.width, card, cardHeight, spacing, total);
        scroll.wheelStep = (cardHeight + spacing) * 0.5f;

        const StudioScrollResult view =
            studioBeginScroll(frame, frame.ids().make("contentgrid"), bounds, scroll);

        // The window, rather than a loop over everything that skips what it cannot see. Culling
        // inside the loop stops the drawing and still walks every card, which at a hundred thousand
        // assets is a hundred thousand iterations a pass to show forty.
        std::size_t columns = 1;
        std::size_t firstVisible = 0;
        std::size_t lastVisible = 0;
        view.visibleCells(card, cardHeight, spacing, total, columns, firstVisible, lastVisible);

        // Only the window is built. This is the line that makes a folder of a hundred thousand
        // files browsable rather than merely drawable.
        const std::vector<StudioContentCard> cards = studioContentCardWindow(
            assets, state.folder, context.getSelectedAsset(), state.query, state.shortcuts,
            firstVisible, lastVisible - firstVisible, &result.rowsBuilt);

        result.visibleAssets.reserve(cards.size());
        for (const StudioContentCard& card : cards)
        {
            if (!card.isFolder()) { result.visibleAssets.push_back(card.assetId); }
        }

        frame.ids().push("cards");
        for (std::size_t offset = 0; offset < cards.size(); ++offset)
        {
            const std::size_t index = firstVisible + offset;
            const StudioContentCard& entry = cards[offset];
            const std::size_t column = index % columns;
            const std::size_t row = index / columns;

            const UiRect box{
                view.viewport.left() + spacing
                    + static_cast<float>(column) * (card + spacing),
                view.viewport.top() + spacing - view.offsetY
                    + static_cast<float>(row) * (cardHeight + spacing),
                card, cardHeight};


            frame.ids().pushIndex(static_cast<std::int64_t>(index));

            const std::string entryId =
                entry.isFolder() ? entry.folder : entry.assetId.toString();
            const bool renaming = !state.tree.renaming().empty()
                               && state.tree.renaming() == entryId;

            // Issued once and kept, rather than asked for again where the drag needs it. Two calls
            // return the same id -- it is derived from the scope and the key -- but each one also
            // *records* it, and a key recorded twice is what the collision detector is there to
            // report. It was reporting it: one per draggable card, every frame.
            const WidgetId cardId = frame.ids().make("card");

            // A card being renamed is a text field, not a card: clicking to place the caret must
            // not also enter the folder, and dragging to select a word must not pick the asset up.
            const StudioInteraction interaction = renaming
                ? StudioInteraction{}
                : frame.interact(cardId, box, /*enabled=*/true);

            // Outside the draw pass, because the rename field below needs the same rectangle the
            // label would have used: an editor that appeared somewhere other than where the name
            // was would make the user check afterwards which card they edited.
            UiRect caption{box.left() + spacing * 0.5f, box.top() + card,
                           std::max(0.0f, box.width - spacing), labelHeight};
            const UiRect nameRow = caption.splitTop(labelHeight * 0.55f);

            if (frame.isDrawPass())
            {
                const StudioColorRole background = entry.selected
                    ? StudioColorRole::Selection
                    : (interaction.hovered ? StudioColorRole::RowHover
                                           : StudioColorRole::ControlBackground);
                frame.drawList().fillRoundedRect(box, theme.color(background),
                                                 metricOf(theme, StudioMetric::CornerRadius));
                frame.drawList().strokeRect(
                    box,
                    theme.color(entry.selected ? StudioColorRole::Accent
                                               : StudioColorRole::Border),
                    metricOf(theme, StudioMetric::BorderWidth));

                // The icon fills most of the card, which is what makes this a grid of *things*
                // rather than a grid of buttons with pictures on them. STUDIO-35041 replaces it
                // with the asset's own thumbnail where one can be made; the layout does not change
                // when it does, which is why the icon is drawn into the same rectangle.
                const float art = card * 0.56f;
                studioDrawIcon(frame,
                               UiRect{std::round(box.centerX() - art * 0.5f),
                                      std::round(box.top() + (card - art) * 0.5f), art, art},
                               entry.icon,
                               theme.color(entry.missing ? StudioColorRole::Warning
                                                         : StudioColorRole::TextSecondary));

                // Centred, and truncated rather than wrapped: two lines of file name would make
                // the cards different heights, and a grid whose rows do not line up is not a grid.
                if (!renaming)
                {
                    studioDrawText(frame, nameRow,
                                   studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                      entry.label, nameRow.width),
                                   StudioFontRole::BodySmall,
                                   theme.color(StudioColorRole::TextPrimary),
                                   StudioTextAlign::Center);
                }
                studioDrawText(frame, caption,
                               studioTruncateText(frame, theme.font(StudioFontRole::Caption),
                                                  entry.detail, caption.width),
                               StudioFontRole::Caption,
                               theme.color(entry.missing ? StudioColorRole::Warning
                                                         : StudioColorRole::TextSecondary),
                               StudioTextAlign::Center);
                ++result.cardsDrawn;
            }

            // Renamed in place, exactly as in the list (STUDIO-09009). One rename state, shared by
            // both presentations, so switching view part-way through an edit keeps the edit rather
            // than silently abandoning it.
            if (renaming)
            {
                const WidgetId renameId = frame.ids().make("rename");
                if (state.tree.renameStarting())
                {
                    frame.router().setFocus(renameId);
                    state.tree.clearRenameStarting();
                }

                StudioTextFieldOptions renameOptions;
                renameOptions.selectAllOnFocus = true;
                renameOptions.font = StudioFontRole::BodySmall;

                const StudioTextFieldResult edit = studioTextField(
                    frame, renameId, nameRow, state.tree.renameText(), renameOptions);

                if (frame.isInputPass())
                {
                    if (edit.cancelled) { state.tree.cancelRename(); }
                    else if (edit.committed || !edit.interaction.focused)
                    {
                        std::string name = state.tree.renameText();
                        state.tree.cancelRename();
                        if (!name.empty() && name != entry.label)
                        {
                            result.lastOperation = studioContentRename(
                                context, entry.isFolder() ? Uuid{} : entry.assetId,
                                entry.folder, name);
                        }
                    }
                }

                frame.ids().pop();
                continue;
            }

            if (frame.isInputPass())
            {
                // A folder opens on a single click here, unlike the tree, where a click selects and
                // the triangle opens. A grid has no triangle and no second thing to click, and a
                // folder card that needed a double click would be a folder a user opens by
                // accident on the first try and not at all on the second.
                if (interaction.clicked)
                {
                    if (entry.isFolder()) { state.folder = entry.folder; }
                    else
                    {
                        context.selectAsset(entry.assetId);
                        result.selectedAsset = entry.assetId;
                        if (state.shortcuts.remember(entry.assetId,
                                                     StudioAssetShortcutStore::kMaximumRecent))
                        {
                            result.shortcutsChanged = true;
                        }
                    }
                }

                if (interaction.rightClicked)
                {
                    result.menuAsset = entry.assetId;
                    result.menuFolder = entry.folder;
                    result.menuRequested = true;
                }
            }

            // Draggable onto anything that takes an asset, exactly as the list's rows are. A
            // browser whose two presentations differed about whether an asset can be dragged
            // would be a browser a user learns twice.
            if (!entry.isFolder())
            {
                StudioFrame::StudioDragPayload payload;
                payload.type = std::string{kStudioAssetDragType};
                payload.value = entry.assetId.toString();
                payload.label = entry.label;
                studioDragSource(frame, cardId, interaction, std::move(payload));
            }
            else
            {
                const StudioFrame::StudioDropResult drop =
                    frame.acceptDrop(frame.ids().make("drop"), box,
                                     std::string{kStudioAssetDragType});
                if (drop.hovered && frame.isDrawPass())
                {
                    frame.drawList().strokeRect(box, theme.color(StudioColorRole::Accent),
                                                metricOf(theme, StudioMetric::BorderWidth) * 2.0f);
                }
                if (drop.dropped)
                {
                    result.lastOperation =
                        studioContentMoveInto(context, Uuid::parse(drop.value), entry.folder);
                }
            }

            frame.ids().pop();
        }
        frame.ids().pop();

        studioEndScroll(frame);
        result.rowsDrawn = result.cardsDrawn;
        return result;
    }
    } // namespace
}
