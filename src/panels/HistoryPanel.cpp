// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/HistoryPanel.hpp"

#include <string>

#include "CNA/Editor/EditorContext.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief The row standing for the document as it was before any command ran. */
        constexpr const char* kBaseLabel = "Opened";
    }

    void HistoryPanel::draw()
    {
        if (!ui_.beginPanel("History", DockSide::Right)) { ui_.endPanel(); return; }

        const CommandHistory& history = context_.getHistory();
        const std::size_t count = history.getCount();
        const std::size_t cursor = history.getCursor();
        const std::ptrdiff_t savedCursor = history.getSavedCursor();

        // Stated even when empty. A blank panel reads as broken; "nothing to undo yet" reads as
        // the truth.
        if (count == 0)
        {
            ui_.text("Nothing to undo yet.");
            ui_.endPanel();
            return;
        }

        ui_.text(std::to_string(cursor) + " of " + std::to_string(count) + " applied" +
                 (history.getLimit() > 0 ? ", keeping " + std::to_string(history.getLimit()) : ""));
        ui_.separator();

        // Rows are positions, not entries: row i is the state after i commands, so there is one
        // more row than there are entries. That extra row -- the document as opened -- is the one
        // a user reaching for "put it back how it was" is actually aiming at, and without it the
        // list could take them everywhere except there.
        std::size_t requested = cursor;
        bool navigate = false;

        for (std::size_t position = 0; position <= count; ++position)
        {
            std::string label = position == cursor ? "> " : "  ";
            label += position == 0 ? std::string{kBaseLabel} : history.getDescriptionAt(position - 1);

            // Everything past the cursor has been undone and is waiting to be redone. Marked
            // rather than hidden: those entries are precisely what a user is trying to get back to.
            if (position > cursor) { label += "  (undone)"; }
            if (savedCursor >= 0 && position == static_cast<std::size_t>(savedCursor))
            {
                label += "  (saved)";
            }

            const UiTreeNodeResult row =
                ui_.treeNode("history-" + std::to_string(position), label, position == cursor, true);

            // Applied after the loop: navigating runs commands, which changes the very list being
            // drawn. Half the rows would describe one history and half another.
            if (row.clicked && position != cursor)
            {
                requested = position;
                navigate = true;
            }
        }

        ui_.endPanel();

        if (navigate) { navigateTo(requested); }
    }

    void HistoryPanel::navigateTo(std::size_t targetCursor)
    {
        const CommandHistory& history = context_.getHistory();

        // Bounded by the entry count on both sides: undo() and redo() report failure rather than
        // throwing, and a loop that trusted the cursor to move would spin forever on a command
        // that refused.
        for (std::size_t guard = 0; guard <= history.getCount(); ++guard)
        {
            const std::size_t cursor = history.getCursor();
            if (cursor == targetCursor) { return; }

            const std::size_t before = cursor;
            if (cursor > targetCursor) { actions_.undo(); }
            else { actions_.redo(); }

            if (history.getCursor() == before) { return; }
        }
    }
}
