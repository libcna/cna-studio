// SPDX-License-Identifier: MS-PL
/**
 * @file StudioLog.cpp
 * @brief The bounded message log.
 */

#include "CNA/Studio/Ui/StudioLog.hpp"

namespace CNA::Studio
{
    void StudioLog::append(LogSeverity severity, std::string message)
    {
        // A message repeated four hundred times is one thing that happened four hundred times.
        // Collapsing keeps the interesting lines on screen, which is the entire purpose of a
        // console; the count is kept so nothing is hidden.
        if (!entries_.empty() && entries_.back().severity == severity
            && entries_.back().message == message)
        {
            ++entries_.back().repeats;
            return;
        }

        entries_.push_back(StudioLogEntry{severity, std::move(message), 1});

        while (entries_.size() > capacity_)
        {
            entries_.pop_front();
            ++dropped_;
        }
    }

    void StudioLog::clear()
    {
        entries_.clear();
        // The dropped count goes too. It exists to tell the user their history is incomplete, and
        // after a clear the history they are looking at is exactly what they asked for.
        dropped_ = 0;
    }

    std::size_t StudioLog::countAtLeast(LogSeverity minimumSeverity) const
    {
        std::size_t count = 0;
        for (const StudioLogEntry& entry : entries_)
        {
            if (entry.severity >= minimumSeverity) { ++count; }
        }
        return count;
    }

    std::string StudioLog::toText(LogSeverity minimumSeverity) const
    {
        std::string text;
        for (const StudioLogEntry& entry : entries_)
        {
            if (entry.severity < minimumSeverity) { continue; }

            text += '[';
            text += toString(entry.severity);
            text += "] ";
            text += entry.message;
            if (entry.repeats > 1)
            {
                text += " (x" + std::to_string(entry.repeats) + ")";
            }
            text += '\n';
        }
        return text;
    }
}
