// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioNotifications.hpp
 * @brief Background results the user was not looking at when they arrived.
 *
 * `plan.md` STUDIO-06014.
 *
 * ### Why an editor needs these at all
 *
 * A build takes a minute and the user goes to read something else. A player is launched and cannot
 * start. An export finishes. Each of those is a fact the user asked for and is no longer watching
 * for, and the two places Studio has to put a fact — the status bar and the log — are both places
 * you have to be *looking at* to learn anything. The status bar is one line that the next poll
 * overwrites; the log is a panel that may not even be open.
 *
 * So a result announces itself, briefly, over the corner of the workspace, and then gets out of the
 * way.
 *
 * ### An error does not get out of the way
 *
 * A toast that vanishes after four seconds is fine for "Exported" and useless for "Build failed":
 * the whole reason to raise a failure is that the user was not watching, and a failure that waited
 * four seconds and left is a failure they will meet again by other means. Errors stay until they
 * are dismissed. Everything else counts down.
 *
 * ### A toast is not the record
 *
 * It is ephemeral by design, which makes it the wrong place to keep anything. Every notification is
 * written to the `StudioLog` as it is posted, by the centre rather than by each caller, so "it told
 * me and I missed it" has somewhere to be answered. That is also why posting is the *only* way in:
 * a caller that could show a toast without logging it would be a caller whose messages disappear.
 */

#pragma once

#include "CNA/Studio/Ui/StudioLog.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief How loudly a notification is shown, and how long it stays. */
    enum class StudioNotificationSeverity
    {
        Info,
        Success,
        Warning,
        Error
    };

    /** @brief Returns the short display name of @p severity, e.g. `"error"`. */
    [[nodiscard]] const char* toString(StudioNotificationSeverity severity);

    /** @brief One announced result. */
    struct StudioNotification
    {
        /**
         * @brief Deduplication key, e.g. `"studio.build"`. Empty means every post is its own.
         *
         * Posting an id that is already showing *replaces* it where it stands and restarts its
         * countdown, rather than stacking another copy. Ten saves that each fail to write is one
         * toast saying so; ten toasts saying so is a stack that covers the editor with one fact.
         */
        std::string id;

        StudioNotificationSeverity severity = StudioNotificationSeverity::Info;

        /** @brief The headline, one line, e.g. `"Build failed"`. */
        std::string title;

        /** @brief One more line of detail, or empty. */
        std::string detail;

        /**
         * @brief A command offered as a button, e.g. `"studio.window.panel.build"`. Optional.
         *
         * The point of announcing a failure is that the user is somewhere else; making them find
         * the panel that explains it is most of the friction the toast was meant to remove.
         */
        std::string actionId;

        /** @brief The button's text. Empty falls back to the command's own label. */
        std::string actionLabel;

        /**
         * @brief How long it stays, in seconds. Zero lets @ref severity decide; negative is sticky.
         */
        float seconds = 0.0f;

        /**
         * @brief Whether it never expires on its own. Set when it is posted.
         *
         * A flag rather than a negative @ref remaining. Encoding "stays" as a negative countdown
         * reads well and is wrong the first time a countdown overshoots its last tick: a toast four
         * seconds old ticked by five becomes indistinguishable from one that was never meant to
         * expire, and then stays on the screen for ever.
         */
        bool sticky = false;

        /** @brief Seconds left before it goes. Meaningless while @ref sticky. */
        double remaining = 0.0;

        /** @brief Whether this notification never expires on its own. */
        [[nodiscard]] bool isSticky() const { return sticky; }
    };

    /**
     * @brief The notifications currently showing, and the record of those that have been.
     *
     * Not a widget: the shell draws the stack, and this decides what is in it. The separation is
     * what lets the interesting rules — expiry, deduplication, the cap, whether an error can be
     * pushed off the screen — be tested without a frame, which is the only way the timing ones can
     * be tested at all.
     */
    class StudioNotificationCenter
    {
    public:
        /** @brief How many are shown at once. The rest are counted and kept in the history. */
        static constexpr std::size_t kMaxVisible = 4;

        /** @brief How many are remembered. */
        static constexpr std::size_t kMaxHistory = 200;

        /** @brief How long an ordinary result stays, in seconds. */
        static constexpr float kShortSeconds = 4.0f;

        /** @brief How long something the user should probably act on stays, in seconds. */
        static constexpr float kLongSeconds = 9.0f;

        StudioNotificationCenter() = default;

        /**
         * @brief Sends every posted notification to @p log as well as showing it.
         *
         * @param log The log, or nullptr to stop mirroring. Not owned; must outlive this.
         */
        void setLog(StudioLog* log) { log_ = log; }

        /**
         * @brief Shows @p notification, replacing any showing one with the same id.
         *
         * @param notification What to say. Its @ref StudioNotification::remaining is computed here
         *        from @ref StudioNotification::seconds and @ref StudioNotification::severity.
         */
        void post(StudioNotification notification);

        /** @brief Convenience: posts a titled notification of @p severity with no id. */
        void post(StudioNotificationSeverity severity, std::string title, std::string detail = {});

        /**
         * @brief Removes the notification with @p id, if it is showing.
         * @param id Identifier given when it was posted.
         * @return True when one was removed.
         */
        bool dismiss(std::string_view id);

        /** @brief Removes the notification at @p index of @ref showing. */
        bool dismissAt(std::size_t index);

        /** @brief Removes every showing notification. The history is kept. */
        void dismissAll();

        /**
         * @brief Advances every countdown and removes whatever has run out.
         *
         * @param deltaSeconds Seconds since the last call. **Zero while the user is reading**: the
         *        shell passes zero whenever the pointer is over the stack, because a toast that
         *        disappeared while it was being read, or while the pointer was travelling to its
         *        button, is worse than one that never appeared.
         * @return How many expired.
         */
        std::size_t tick(double deltaSeconds);

        /** @brief Everything currently posted, oldest first. */
        [[nodiscard]] const std::vector<StudioNotification>& posted() const { return posted_; }

        /** @brief The newest @ref kMaxVisible, oldest first — what the shell draws. */
        [[nodiscard]] std::vector<StudioNotification> showing() const;

        /** @brief How many are posted but not shown, for the "and N more" line. */
        [[nodiscard]] std::size_t hiddenCount() const;

        /** @brief Everything posted since Studio started, newest last, capped at @ref kMaxHistory. */
        [[nodiscard]] const std::vector<StudioNotification>& history() const { return history_; }

        /** @brief Whether anything is showing. */
        [[nodiscard]] bool empty() const { return posted_.empty(); }

    private:
        std::vector<StudioNotification> posted_;
        std::vector<StudioNotification> history_;
        StudioLog* log_ = nullptr;
    };

    /**
     * @brief How long @p severity stays by default, in seconds. Negative means it stays.
     * @param severity Severity to ask about.
     * @return Seconds, or a negative number for sticky.
     */
    [[nodiscard]] float studioNotificationLifetime(StudioNotificationSeverity severity);

    /** @brief The log severity a notification is mirrored at. */
    [[nodiscard]] LogSeverity studioNotificationLogSeverity(StudioNotificationSeverity severity);
}
