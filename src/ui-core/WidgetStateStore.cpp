// SPDX-License-Identifier: MS-PL
/**
 * @file WidgetStateStore.cpp
 * @brief Retained widget state and its reclamation.
 */

#include "CNA/Studio/UiCore/WidgetStateStore.hpp"

#include <algorithm>

namespace CNA::Studio
{
    void WidgetStateStore::beginFrame()
    {
        ++frame_;
        if (frame_ - lastSweepFrame_ >= kSweepInterval) { sweep(); }
    }

    WidgetState& WidgetStateStore::get(WidgetId id)
    {
        Entry& entry = entries_[id.value()];
        entry.lastTouchedFrame = frame_;
        return entry.state;
    }

    void WidgetStateStore::touch(WidgetId id)
    {
        entries_[id.value()].lastTouchedFrame = frame_;
    }

    const WidgetState* WidgetStateStore::find(WidgetId id) const
    {
        const auto found = entries_.find(id.value());
        return found == entries_.end() ? nullptr : &found->second.state;
    }

    bool WidgetStateStore::forget(WidgetId id)
    {
        return entries_.erase(id.value()) > 0;
    }

    void WidgetStateStore::clear()
    {
        entries_.clear();
    }

    void WidgetStateStore::setRetentionFrames(std::uint64_t frames)
    {
        // Below two, a widget that is merely off-screen for one frame loses its state. That is not
        // a tuning choice, it is a bug, so the floor is enforced here rather than documented.
        retentionFrames_ = std::max<std::uint64_t>(frames, 2);
    }

    std::size_t WidgetStateStore::sweep()
    {
        lastSweepFrame_ = frame_;

        // Unsigned arithmetic: early in a session frame_ is smaller than the retention window, and
        // computing a cutoff by subtraction would wrap to an enormous number and reclaim
        // everything. Comparing forwards avoids the whole class of problem.
        std::size_t reclaimed = 0;
        for (auto it = entries_.begin(); it != entries_.end();)
        {
            if (it->second.lastTouchedFrame + retentionFrames_ < frame_)
            {
                it = entries_.erase(it);
                ++reclaimed;
            }
            else
            {
                ++it;
            }
        }
        return reclaimed;
    }
} // namespace CNA::Studio
