// SPDX-License-Identifier: MS-PL
/**
 * @file StudioNotifications.cpp
 * @brief What is showing, for how long, and what goes in the log.
 */

#include "CNA/Studio/UiCore/StudioNotifications.hpp"

#include <algorithm>

namespace CNA::Studio
{
    const char* toString(StudioNotificationSeverity severity)
    {
        switch (severity)
        {
            case StudioNotificationSeverity::Info: return "info";
            case StudioNotificationSeverity::Success: return "done";
            case StudioNotificationSeverity::Warning: return "warn";
            case StudioNotificationSeverity::Error: return "error";
        }
        return "info";
    }

    float studioNotificationLifetime(StudioNotificationSeverity severity)
    {
        switch (severity)
        {
            case StudioNotificationSeverity::Info:
            case StudioNotificationSeverity::Success:
                return StudioNotificationCenter::kShortSeconds;

            // Longer, not sticky. A warning is something the user may want to act on and may not;
            // holding the corner of the editor until it is acknowledged would make every one of
            // them a small interruption, and they are the kind of thing there are many of.
            case StudioNotificationSeverity::Warning:
                return StudioNotificationCenter::kLongSeconds;

            // The whole reason to raise a failure is that nobody was watching. One that waited
            // four seconds and left is a failure the user meets again by other means.
            case StudioNotificationSeverity::Error:
                return -1.0f;
        }
        return StudioNotificationCenter::kShortSeconds;
    }

    LogSeverity studioNotificationLogSeverity(StudioNotificationSeverity severity)
    {
        switch (severity)
        {
            case StudioNotificationSeverity::Info:
            case StudioNotificationSeverity::Success: return LogSeverity::Info;
            case StudioNotificationSeverity::Warning: return LogSeverity::Warning;
            case StudioNotificationSeverity::Error: return LogSeverity::Error;
        }
        return LogSeverity::Info;
    }

    void StudioNotificationCenter::post(StudioNotification notification)
    {
        const float seconds = notification.seconds != 0.0f
            ? notification.seconds
            : studioNotificationLifetime(notification.severity);
        notification.sticky = seconds < 0.0f;
        notification.remaining = notification.sticky ? 0.0 : static_cast<double>(seconds);

        if (log_ != nullptr)
        {
            // Both lines as one entry, so the log's own repeat-collapsing sees the whole message:
            // two entries would collapse the title and leave the details of each attempt.
            std::string line = notification.title;
            if (!notification.detail.empty()) { line += " -- " + notification.detail; }
            log_->append(studioNotificationLogSeverity(notification.severity), line);
        }

        history_.push_back(notification);
        if (history_.size() > kMaxHistory)
        {
            history_.erase(history_.begin(),
                           history_.begin()
                               + static_cast<std::ptrdiff_t>(history_.size() - kMaxHistory));
        }

        // Replaced where it stands rather than moved to the end. A toast that jumped to the bottom
        // of the stack each time it was re-posted would move out from under the pointer that was
        // reaching for its button.
        if (!notification.id.empty())
        {
            const auto existing = std::find_if(posted_.begin(), posted_.end(),
                [&](const StudioNotification& shown) { return shown.id == notification.id; });
            if (existing != posted_.end())
            {
                *existing = std::move(notification);
                return;
            }
        }

        posted_.push_back(std::move(notification));
    }

    void StudioNotificationCenter::post(StudioNotificationSeverity severity, std::string title,
                                        std::string detail)
    {
        StudioNotification notification;
        notification.severity = severity;
        notification.title = std::move(title);
        notification.detail = std::move(detail);
        post(std::move(notification));
    }

    bool StudioNotificationCenter::dismiss(std::string_view id)
    {
        if (id.empty()) { return false; }

        const auto found = std::find_if(posted_.begin(), posted_.end(),
            [&](const StudioNotification& shown) { return shown.id == id; });
        if (found == posted_.end()) { return false; }

        posted_.erase(found);
        return true;
    }

    bool StudioNotificationCenter::dismissAt(std::size_t index)
    {
        // Indexed into what the shell *drew*, which is the tail of what is posted. A shell that had
        // to know that would be a shell that gets it wrong the day the cap changes.
        const std::size_t hidden = hiddenCount();
        if (index >= posted_.size() - hidden) { return false; }

        posted_.erase(posted_.begin() + static_cast<std::ptrdiff_t>(hidden + index));
        return true;
    }

    void StudioNotificationCenter::dismissAll() { posted_.clear(); }

    std::size_t StudioNotificationCenter::tick(double deltaSeconds)
    {
        if (deltaSeconds <= 0.0) { return 0; }

        const std::size_t before = posted_.size();
        for (StudioNotification& notification : posted_)
        {
            if (notification.isSticky()) { continue; }
            notification.remaining -= deltaSeconds;
        }

        posted_.erase(std::remove_if(posted_.begin(), posted_.end(),
                          [](const StudioNotification& notification) {
                              return !notification.isSticky() && notification.remaining <= 0.0;
                          }),
                      posted_.end());
        return before - posted_.size();
    }

    std::size_t StudioNotificationCenter::hiddenCount() const
    {
        return posted_.size() > kMaxVisible ? posted_.size() - kMaxVisible : 0;
    }

    std::vector<StudioNotification> StudioNotificationCenter::showing() const
    {
        // The newest, not the oldest. A burst pushes earlier ones off, errors included: they are in
        // the history and in the log, and a stack that could grow without bound would cover the
        // editor with the thing it was reporting *about*.
        return {posted_.begin() + static_cast<std::ptrdiff_t>(hiddenCount()), posted_.end()};
    }
}
