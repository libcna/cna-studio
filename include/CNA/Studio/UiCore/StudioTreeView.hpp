// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioTreeView.hpp
 * @brief A scrolling, selectable tree of rows, knowing nothing about what they are.
 *
 * `plan.md` STUDIO-03034.
 *
 * The second reusable thing a ported panel needed, after scrolling. Deliberately a *view* over rows
 * a caller flattens rather than a walker over somebody's data structure: the outliner shows a scene
 * graph, the content browser will show a directory, and a widget that knew about either would have
 * to learn about both. It takes a flat list with a depth per row, which is what a tree looks like
 * once it has been drawn.
 *
 * ### The caller flattens, the widget owns nothing but geometry
 *
 * Expansion is the caller's state, passed in as a @ref StudioTreeState. That is not ceremony: the
 * caller has to consult it anyway to decide which rows to flatten, and a widget that owned it would
 * mean asking the widget a question before it has been called. The widget reports what the user
 * clicked; the caller applies it.
 *
 * ### Rows are virtualised
 *
 * A scene with fifty thousand entities must cost what a scene with five costs. Only the rows on
 * screen are measured, laid out or drawn — the clip would stop the pixels either way, and only
 * culling stops the work.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief One visible row of a tree. */
    struct StudioTreeRow
    {
        /**
         * @brief Stable identity, opaque to the widget.
         *
         * Used for the row's widget id and for the expansion state, so a tree keeps its shape when
         * the rows around it change. A scene uses the entity's UUID; a file tree would use a path.
         */
        std::string id;

        /** @brief What the row says. */
        std::string label;

        /** @brief Something to the right of the label, drawn dimmed: a type, a count, a size. */
        std::string detail;

        /** @brief How far in the row is indented. Zero is a root. */
        int depth = 0;

        /** @brief Whether the row can be expanded. Decides whether it gets a disclosure triangle. */
        bool hasChildren = false;

        /** @brief Whether the row is part of the current selection. */
        bool selected = false;

        /** @brief False to draw the row dimmed, for something hidden or disabled. */
        bool enabled = true;
    };

    /**
     * @brief Which rows of a tree are open.
     *
     * A set of ids rather than a flag per row, so the answer survives the rows being rebuilt from
     * scratch every frame — which is what an immediate-mode caller does.
     */
    class StudioTreeState
    {
    public:
        /**
         * @brief Whether @p id is open.
         *
         * Defaults to open. A tree that starts entirely collapsed shows the user one line and
         * makes them work to discover that their scene has anything in it.
         *
         * @param id Row id.
         * @return True when its children should be flattened.
         */
        [[nodiscard]] bool isExpanded(std::string_view id) const
        {
            return collapsed_.find(std::string{id}) == collapsed_.end();
        }

        /**
         * @brief Opens or closes @p id.
         * @param id Row id.
         * @param expanded Whether it should be open.
         */
        void setExpanded(std::string_view id, bool expanded)
        {
            if (expanded) { collapsed_.erase(std::string{id}); }
            else { collapsed_.insert(std::string{id}); }
        }

        /** @brief Opens everything. */
        void expandAll() { collapsed_.clear(); }

        /** @brief How many rows are closed. */
        [[nodiscard]] std::size_t collapsedCount() const { return collapsed_.size(); }

    private:
        // Collapsed rather than expanded, so the default is open and an unknown id needs no entry.
        std::set<std::string> collapsed_;
    };

    /** @brief What the user did to a tree this frame. */
    struct StudioTreeResult
    {
        /** @brief Index of the row whose disclosure triangle was clicked. Input pass only. */
        std::optional<std::size_t> toggled;

        /** @brief Index of the row that was clicked. Input pass only. */
        std::optional<std::size_t> clicked;

        /** @brief Whether the click asked to add to the selection rather than replace it. */
        bool additive = false;

        /** @brief How many rows were actually drawn. */
        std::size_t rowsDrawn = 0;
    };

    /**
     * @brief Draws a flattened tree, scrolling and selectable.
     *
     * @param frame The frame.
     * @param bounds Area the tree occupies.
     * @param rows The visible rows, in display order.
     * @param state Expansion state; updated when a disclosure triangle is clicked.
     * @param emptyMessage Shown when @p rows is empty. An empty tree that explains itself is the
     *        difference between "nothing here" and "something is broken".
     * @return What the user did.
     */
    StudioTreeResult studioTreeView(StudioFrame& frame, const UiRect& bounds,
                                    const std::vector<StudioTreeRow>& rows,
                                    StudioTreeState& state,
                                    std::string_view emptyMessage = {});
}
