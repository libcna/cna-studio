// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/HistoryPanel.hpp
 * @brief The undo stack, made visible and navigable.
 *
 * Ctrl+Z answers "take back the last thing", which is the only question a keyboard shortcut can
 * ask. The question users actually have is "take me back to before I broke it", and the honest
 * answer to that needs the list -- fifteen presses of Ctrl+Z with no idea how many are left is
 * how a person loses work they meant to keep.
 *
 * Nothing here is new bookkeeping. `CommandHistory` already records every entry, its label, the
 * cursor and the position the document was last saved at; the panel is a view over state that has
 * existed since Phase 0 (plan.md ED-905).
 *
 * Clicking an entry *moves the cursor to it* -- it undoes or redoes until it gets there. It does
 * not remove that one entry from the middle of the history, which is a different feature with a
 * much worse failure mode: a command's undo is only valid against the state its execute() left
 * behind, so plucking one out of the middle would ask every later command to reverse a state it
 * never saw.
 */

#include <cstddef>

#include "CNA/Editor/Panels/EditorPanel.hpp"

namespace CNA::Editor
{
    /**
     * @brief Lists the undo history oldest-first and navigates to any point in it.
     *
     * Undone entries stay on the list rather than disappearing: they are exactly what a user
     * wants to get back to, and a list that hid them would answer "redo is broken".
     *
     * Navigation goes through EditorActions::undo/redo rather than straight into CommandHistory,
     * so that a jump does everything a Ctrl+Z does -- pruning the selection today, and whatever
     * else that path grows tomorrow. A panel that reimplemented undo slightly differently is a bug
     * users report as "undo is broken".
     */
    class HistoryPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

    private:
        /** @brief Undoes or redoes until the cursor reaches @p targetCursor. */
        void navigateTo(std::size_t targetCursor);
    };
}
