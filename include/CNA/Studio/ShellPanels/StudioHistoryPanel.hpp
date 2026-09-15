// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioHistoryPanel.hpp
 * @brief The History panel — the undo stack as a list you can jump around in.
 *
 * `plan.md` STUDIO-07013.
 *
 * ### Rows are positions, not entries
 *
 * Row *i* is the document after *i* commands, so there is one more row than there are entries. That
 * extra row — the document as it was opened — is the one a user reaching for "put it back how it
 * was" is actually aiming at, and a list of entries alone can take them everywhere except there.
 *
 * ### Navigating is undo and redo, not a jump
 *
 * Clicking a row runs the commands between here and there, one at a time, through the same
 * `CommandHistory` that Ctrl+Z uses. A panel that set the cursor directly would leave the document
 * and the history describing different things — and every command that refuses would be skipped
 * silently instead of stopping the walk.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief What the History panel showed and what the user asked for. */
    struct StudioHistoryResult
    {
        /** @brief How many rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many positions there are: one more than the number of entries. */
        std::size_t positions = 0;

        /** @brief The position the user clicked, when it is not the current one. Input pass only. */
        std::optional<std::size_t> navigateTo;
    };

    /**
     * @brief Builds the history rows for @p context.
     *
     * Separate from drawing so a test can assert on what the list *says* — which entries are
     * undone, which is saved, where the cursor is — without a frame.
     *
     * @param context The editor context.
     * @return One row per position, oldest first.
     */
    [[nodiscard]] std::vector<StudioTreeRow> studioHistoryRows(const StudioContext& context);

    /**
     * @brief Draws the History panel.
     * @param frame The frame.
     * @param bounds Where the panel's content goes.
     * @param context The editor context.
     * @param state Expansion state; the list is flat, so this only carries scrolling company.
     * @return What was shown and what the user asked for.
     */
    StudioHistoryResult studioHistoryPanel(StudioFrame& frame, const UiRect& bounds,
                                           const StudioContext& context, StudioTreeState& state);

    /**
     * @brief Walks @p context's history to @p position, one command at a time.
     *
     * @param context The editor context.
     * @param position The target cursor.
     * @return How many commands actually ran. Fewer than asked means one refused, which stops the
     *         walk rather than spinning: `undo()` and `redo()` report failure rather than throwing.
     */
    std::size_t studioNavigateHistory(StudioContext& context, std::size_t position);
}
