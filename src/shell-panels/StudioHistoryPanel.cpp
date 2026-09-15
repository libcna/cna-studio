// SPDX-License-Identifier: MS-PL
/**
 * @file StudioHistoryPanel.cpp
 * @brief The undo stack as a list.
 */

#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"

#include "CNA/Studio/StudioContext.hpp"

#include <string>

namespace CNA::Studio
{
    namespace
    {
        /** @brief The row standing for the document as it was before any command ran. */
        constexpr std::string_view kBaseLabel = "Opened";
    }

    std::vector<StudioTreeRow> studioHistoryRows(const StudioContext& context)
    {
        const CommandHistory& history = context.getHistory();
        const std::size_t count = history.getCount();
        const std::size_t cursor = history.getCursor();
        const std::ptrdiff_t savedCursor = history.getSavedCursor();

        std::vector<StudioTreeRow> rows;
        rows.reserve(count + 1);

        for (std::size_t position = 0; position <= count; ++position)
        {
            StudioTreeRow row;
            row.id = "position:" + std::to_string(position);
            row.label = position == 0 ? std::string{kBaseLabel}
                                      : history.getDescriptionAt(position - 1);
            row.selected = position == cursor;

            // Everything past the cursor has been undone and is waiting to be redone. Marked
            // rather than hidden: those entries are precisely what a user is trying to get back
            // to, and a list that hides them is a list with no forward direction.
            if (position > cursor)
            {
                row.detail = "undone";
                row.muted = true;
            }
            if (savedCursor >= 0 && position == static_cast<std::size_t>(savedCursor))
            {
                // The one row that answers "where was this when I last saved it", which is the
                // question behind most uses of an undo list.
                row.detail = row.detail.empty() ? "saved" : row.detail + ", saved";
                row.detailRole = StudioColorRole::Success;
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    std::size_t studioNavigateHistory(StudioContext& context, std::size_t position)
    {
        CommandHistory& history = context.getHistory();
        std::size_t ran = 0;

        // Bounded by the entry count on both sides: undo() and redo() report failure rather than
        // throwing, and a loop that trusted the cursor to move would spin forever on a command
        // that refused.
        for (std::size_t guard = 0; guard <= history.getCount(); ++guard)
        {
            const std::size_t cursor = history.getCursor();
            if (cursor == position) { return ran; }

            const bool moved = cursor > position ? history.undo() : history.redo();
            if (!moved || history.getCursor() == cursor) { return ran; }
            ++ran;
        }
        return ran;
    }

    StudioHistoryResult studioHistoryPanel(StudioFrame& frame, const UiRect& bounds,
                                           const StudioContext& context, StudioTreeState& state)
    {
        StudioHistoryResult result;

        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(bounds,
                                      frame.theme().color(StudioColorRole::PanelBackground));
        }
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) { return result; }

        const std::vector<StudioTreeRow> rows = studioHistoryRows(context);
        result.positions = rows.size();

        frame.ids().push("history");
        const StudioTreeResult tree = studioTreeView(
            frame, bounds, rows, state,
            "Nothing to undo yet. Every change you make appears here.");
        frame.ids().pop();

        result.rowsDrawn = tree.rowsDrawn;

        if (tree.clicked.has_value() && *tree.clicked < rows.size()
            && *tree.clicked != context.getHistory().getCursor())
        {
            // Reported rather than applied here: navigating runs commands, which changes the very
            // list being drawn. Half the rows would describe one history and half another.
            result.navigateTo = *tree.clicked;
        }
        return result;
    }
}
