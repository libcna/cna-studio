// SPDX-License-Identifier: MS-PL
/**
 * @file StudioContentBrowser.cpp
 * @brief The Content Browser.
 */

#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
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
            studioContentFolderRows(context.getAssets(), state.folder, state.folderTree);
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
                                                     StudioTreeState& state,
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
                row.icon = studioAssetIcon(record->type);
                row.depth = depth;
                row.selected = record->id == selected;
                // Draggable onto anything that takes an asset: a property slot in the inspector,
                // or a broken reference in the Problems panel. Folders are not — there is nothing
                // a folder means as a property value.
                row.dragType = std::string{kStudioAssetDragType};
                row.dragValue = row.id;

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
                    // Coloured, unlike every other icon in the list. A missing asset is the one row
                    // whose *state* matters more than its kind, and the warning colour is what
                    // makes it findable in a folder of two hundred without reading any of them.
                    row.icon = StudioIcon::Warning;
                    row.iconRole = StudioColorRole::Warning;
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
            row.icon = StudioIcon::Folder;

            const auto contents = byFolder.find(folder);
            const std::size_t count = contents == byFolder.end() ? 0 : contents->second.size();
            if (count > 0) { row.detail = std::to_string(count); }

            rows.push_back(std::move(row));

            if (state.isExpanded(folder)) { appendFiles(folder, depthOf(folder) + 1); }
        }

        return rows;
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
                                                       const StudioTreeState& state)
    {
        // How many assets sit *directly* in each folder, and which folders exist at all. Direct
        // rather than cumulative: a count that included descendants would make `Assets` read as
        // holding everything in the project, which is true and useless -- the number a user wants
        // beside a folder is how much they will see when they click it.
        std::map<std::string, std::size_t> directCount;
        std::set<std::string> folders;

        for (const AssetRecord* record : assets.getAll())
        {
            if (record == nullptr) { continue; }

            const std::string directory = splitPath(record->sourcePath).first;
            ++directCount[directory];
            for (const std::string& ancestor : ancestorsOf(directory)) { folders.insert(ancestor); }
        }

        const auto depthOf = [](const std::string& path) {
            return static_cast<int>(std::count(path.begin(), path.end(), '/'));
        };

        std::vector<StudioTreeRow> rows;

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

    std::vector<StudioContentCard> studioContentCards(const AssetDatabase& assets,
                                                       const std::string& folder,
                                                       const Uuid& selected)
    {
        std::vector<StudioContentCard> cards;

        // Immediate children only, in both halves. A grid of every asset under a folder is a wall,
        // and the folder a user is *in* is the unit they think in -- which is the whole difference
        // between this presentation and the tree beside it.
        std::set<std::string> subfolders;
        std::vector<const AssetRecord*> files;

        for (const AssetRecord* record : assets.getAll())
        {
            const std::string directory = splitPath(record->sourcePath).first;
            if (directory == folder)
            {
                files.push_back(record);
                continue;
            }

            const std::string prefix = folder.empty() ? std::string{} : folder + "/";
            if (directory.rfind(prefix, 0) != 0) { continue; }

            const std::string rest = directory.substr(prefix.size());
            if (rest.empty()) { continue; }

            const std::size_t slash = rest.find('/');
            subfolders.insert(prefix + (slash == std::string::npos ? rest : rest.substr(0, slash)));
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

            std::size_t contents = 0;
            const std::string prefix = path + "/";
            for (const AssetRecord* record : assets.getAll())
            {
                const std::string directory = splitPath(record->sourcePath).first;
                if (directory == path || directory.rfind(prefix, 0) == 0) { ++contents; }
            }
            card.detail = std::to_string(contents) + (contents == 1 ? " item" : " items");
            cards.push_back(std::move(card));
        }

        std::sort(files.begin(), files.end(), [](const AssetRecord* a, const AssetRecord* b) {
            return a->sourcePath < b->sourcePath;
        });

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
            cards.push_back(std::move(card));
        }

        return cards;
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

        // --- The folder pane ---------------------------------------------------------------
        //
        // Beside both presentations rather than inside either (STUDIO-09001). Where a user is and
        // what they are looking at are two questions, and a navigation tree that appeared only in
        // one view would make switching views also mean switching how you move around.
        result = studioContentFolderPane(frame, area, context, state, std::move(result));

        if (state.view == StudioContentView::Grid)
        {
            return studioContentGrid(frame, area, context, state, result);
        }

        return studioContentList(frame, area, context, state.tree, result);
    }

    namespace
    {
    StudioContentBrowserResult studioContentList(StudioFrame& frame, const UiRect& bounds,
                                                 StudioContext& context,
                                                 StudioTreeState& state,
                                                 StudioContentBrowserResult result)
    {
        const AssetDatabase& assets = context.getAssets();
        const std::vector<StudioTreeRow> rows =
            studioContentRows(assets, context.getSelectedAsset(), state);
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
                // Through the context, not into an out-parameter the shell keeps beside the one
                // StudioContext already has. Two ideas of "the selected asset" meant the native
                // Details panel could not see what the native Content Browser had selected --
                // STUDIO-07045. Selecting an asset also clears the entity selection, which is what
                // makes the inspector show one thing at a time.
                context.selectAsset(clicked);
                result.selectedAsset = clicked;
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

        const std::vector<StudioContentCard> cards =
            studioContentCards(assets, state.folder, context.getSelectedAsset());
        result.rowsTotal = cards.size();
        result.missingCount = assets.getMissingAssets().size();

        if (cards.empty())
        {
            // A folder with nothing in it is a place, not a failure, and it says which place.
            // "No assets" in a project that has two hundred of them is the sort of message that
            // makes a user think the database is broken.
            if (frame.isDrawPass())
            {
                studioDrawText(frame, bounds,
                               context.hasProject()
                                   ? (state.folder.empty() ? "This project has no assets yet."
                                                           : "This folder is empty.")
                                   : "No project is open.",
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary));
            }
            return result;
        }

        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);
        const float card = std::max(48.0f, state.cardSize * theme.scale());
        const float labelHeight = metricOf(theme, StudioMetric::RowHeight) * 1.6f;
        const float cardHeight = card + labelHeight;

        // Columns from the width available, floored at one: a panel narrower than a card still has
        // to show it, clipped, rather than dividing by zero and drawing nothing.
        const auto columns = static_cast<std::size_t>(
            std::max(1.0f, std::floor((bounds.width - spacing) / (card + spacing))));
        const std::size_t rows = (cards.size() + columns - 1) / columns;

        StudioScrollOptions scroll;
        scroll.contentHeight = static_cast<float>(rows) * (cardHeight + spacing) + spacing;
        scroll.wheelStep = (cardHeight + spacing) * 0.5f;

        const StudioScrollResult view =
            studioBeginScroll(frame, frame.ids().make("contentgrid"), bounds, scroll);

        frame.ids().push("cards");
        for (std::size_t index = 0; index < cards.size(); ++index)
        {
            const StudioContentCard& entry = cards[index];
            const std::size_t column = index % columns;
            const std::size_t row = index / columns;

            const UiRect box{
                view.viewport.left() + spacing
                    + static_cast<float>(column) * (card + spacing),
                view.viewport.top() + spacing - view.offsetY
                    + static_cast<float>(row) * (cardHeight + spacing),
                card, cardHeight};

            // Culled against the viewport rather than left to the scissor. A project with four
            // thousand assets in one folder would otherwise describe four thousand widgets to show
            // twenty, and each one registers with the router whether or not it is visible.
            if (box.bottom() < view.viewport.top() || box.top() > view.viewport.bottom())
            {
                continue;
            }

            frame.ids().pushIndex(static_cast<std::int64_t>(index));
            const StudioInteraction interaction =
                frame.interact(frame.ids().make("card"), box, /*enabled=*/true);

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

                UiRect caption{box.left() + spacing * 0.5f, box.top() + card,
                               std::max(0.0f, box.width - spacing), labelHeight};
                const UiRect nameRow = caption.splitTop(labelHeight * 0.55f);

                // Centred, and truncated rather than wrapped: two lines of file name would make
                // the cards different heights, and a grid whose rows do not line up is not a grid.
                studioDrawText(frame, nameRow,
                               studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                  entry.label, nameRow.width),
                               StudioFontRole::BodySmall,
                               theme.color(StudioColorRole::TextPrimary), StudioTextAlign::Center);
                studioDrawText(frame, caption,
                               studioTruncateText(frame, theme.font(StudioFontRole::Caption),
                                                  entry.detail, caption.width),
                               StudioFontRole::Caption,
                               theme.color(entry.missing ? StudioColorRole::Warning
                                                         : StudioColorRole::TextSecondary),
                               StudioTextAlign::Center);
                ++result.cardsDrawn;
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
                    }
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
                studioDragSource(frame, frame.ids().make("card"), interaction, std::move(payload));
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
