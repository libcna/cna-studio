// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/EditorCommand.hpp"

namespace CNA::Editor
{
    void CommandHistory::execute(std::unique_ptr<EditorCommand> command, MergePolicy policy)
    {
        if (!command) { return; }

        command->execute();

        if (policy == MergePolicy::MergeWithPrevious && mergeChainOpen_ && cursor_ > 0
            && cursor_ == commands_.size())
        {
            EditorCommand& previous = *commands_[cursor_ - 1];
            const std::string mergeKey = command->getMergeKey();
            if (!mergeKey.empty() && previous.getMergeKey() == mergeKey && previous.mergeWith(*command))
            {
                // Merged: the previous entry now owns the combined change, so the newer command
                // object is dropped here. Its execute() already ran, which is exactly what the
                // caller wanted -- only the *history entry* was folded away, not the effect.
                return;
            }
        }

        // Any new entry invalidates the redo tail.
        commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(cursor_), commands_.end());
        if (savedCursor_ > static_cast<std::ptrdiff_t>(cursor_))
        {
            // The saved state lived in the tail that was just discarded and is now unreachable:
            // no sequence of undo/redo can return to it, so the document can never again be
            // "not dirty" until it is saved afresh.
            savedCursor_ = -1;
        }

        commands_.push_back(std::move(command));
        cursor_ = commands_.size();

        // A command that landed opens the chain: whatever follows it *within the same interaction*
        // may fold into it. endInteraction() closes it again.
        mergeChainOpen_ = true;
        trimToLimit();
    }

    bool CommandHistory::undo()
    {
        if (!canUndo()) { return false; }
        --cursor_;
        commands_[cursor_]->undo();
        return true;
    }

    bool CommandHistory::redo()
    {
        if (!canRedo()) { return false; }
        commands_[cursor_]->execute();
        ++cursor_;
        return true;
    }

    std::string CommandHistory::getUndoDescription() const
    {
        return canUndo() ? commands_[cursor_ - 1]->getDescription() : std::string{};
    }

    std::string CommandHistory::getRedoDescription() const
    {
        return canRedo() ? commands_[cursor_]->getDescription() : std::string{};
    }

    std::string CommandHistory::getDescriptionAt(std::size_t index) const
    {
        return index < commands_.size() ? commands_[index]->getDescription() : std::string{};
    }

    const EditorCommand* CommandHistory::getCommandAt(std::size_t index) const
    {
        return index < commands_.size() ? commands_[index].get() : nullptr;
    }

    void CommandHistory::clear()
    {
        commands_.clear();
        cursor_ = 0;
        savedCursor_ = 0;
    }

    void CommandHistory::setLimit(std::size_t limit)
    {
        limit_ = limit;
        trimToLimit();
    }

    void CommandHistory::trimToLimit()
    {
        if (limit_ == 0 || commands_.size() <= limit_) { return; }

        const std::size_t excess = commands_.size() - limit_;
        commands_.erase(commands_.begin(), commands_.begin() + static_cast<std::ptrdiff_t>(excess));
        cursor_ = cursor_ >= excess ? cursor_ - excess : 0;
        savedCursor_ = savedCursor_ >= static_cast<std::ptrdiff_t>(excess)
                           ? savedCursor_ - static_cast<std::ptrdiff_t>(excess)
                           : -1;
    }
}
