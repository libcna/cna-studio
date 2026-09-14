// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Ui/StudioLog.hpp
 * @brief The message log, as a model no UI owns.
 *
 * `plan.md` STUDIO-07005.
 *
 * The prototype kept its log inside the Dear ImGui `StudioUi`, which meant the Console panel could
 * only be drawn by the UI that also stored its contents. Porting the panel therefore starts here:
 * the log moves out, both presentations read the same one, and the migration can proceed a panel at
 * a time with both UIs showing identical output — which is the only way anybody can tell whether
 * the port is faithful.
 *
 * ### Bounded, because a log is not a database
 *
 * An editor left open for a day with a noisy import can produce millions of lines, and a log that
 * grows without limit turns that into an out-of-memory crash that loses the user's scene. The store
 * keeps a fixed number of the most recent entries and counts what it dropped, so the panel can say
 * "12,043 earlier messages" rather than quietly presenting a partial history as a complete one.
 *
 * ### Repeats are collapsed, and counted
 *
 * A message repeated four hundred times is one thing that happened four hundred times, not four
 * hundred things. Collapsing them keeps the interesting lines on screen, which is the entire
 * purpose of a console; the count is kept so nothing is hidden.
 */

#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief Severity of a console message, used for filtering and colouring.
     *
     * Declared here rather than beside `StudioUi` because it belongs to the log, and the log now
     * outlives any particular UI: both the Dear ImGui console and the Studio one read this model,
     * which is what makes the ported panel a *port* rather than a second console showing something
     * else.
     */
    enum class LogSeverity
    {
        Trace,
        Info,
        Warning,
        Error
    };

    /** @brief Returns the short display name of @p severity, e.g. "warn". */
    const char* toString(LogSeverity severity);

    /** @brief One line in the log. */
    struct StudioLogEntry
    {
        LogSeverity severity = LogSeverity::Info;
        std::string message;

        /** @brief How many times in a row this message arrived. One for an ordinary line. */
        std::size_t repeats = 1;
    };

    /**
     * @brief A bounded, UI-independent message log.
     *
     * Not thread-safe, and deliberately so: everything that logs today does so from the frame
     * thread, and a mutex whose only purpose is to be uncontended is a mutex that hides where the
     * real threading boundary should go. When a background job needs to log, it will need a queue,
     * not a lock.
     */
    class StudioLog
    {
    public:
        /** @brief How many entries are kept before the oldest are dropped. */
        static constexpr std::size_t kDefaultCapacity = 10000;

        StudioLog() = default;

        /** @brief Constructs a log holding at most @p capacity entries. Zero means the default. */
        explicit StudioLog(std::size_t capacity)
            : capacity_(capacity == 0 ? kDefaultCapacity : capacity)
        {
        }

        /**
         * @brief Appends a message, collapsing it into the previous one if identical.
         *
         * @param severity How serious it is.
         * @param message The text. Newlines are kept: a stack trace is one message.
         */
        void append(LogSeverity severity, std::string message);

        /** @brief Every retained entry, oldest first. */
        [[nodiscard]] const std::deque<StudioLogEntry>& entries() const { return entries_; }

        /** @brief How many entries were dropped to stay within capacity. */
        [[nodiscard]] std::size_t droppedCount() const { return dropped_; }

        /** @brief The most entries this log will retain. */
        [[nodiscard]] std::size_t capacity() const { return capacity_; }

        /** @brief Removes everything, including the dropped count: the user asked for a clean slate. */
        void clear();

        /**
         * @brief How many retained entries are at least @p minimumSeverity.
         * @param minimumSeverity Lowest severity to count.
         * @return The count.
         */
        [[nodiscard]] std::size_t countAtLeast(LogSeverity minimumSeverity) const;

        /**
         * @brief The log as plain text, for the clipboard or a bug report.
         *
         * Each line is `[severity] message`, with a repeat count appended where there is one, so
         * text pasted into an issue says the same thing the panel said.
         *
         * @param minimumSeverity Lowest severity to include.
         * @return The text, newline-terminated per entry.
         */
        [[nodiscard]] std::string toText(LogSeverity minimumSeverity = LogSeverity::Trace) const;

    private:
        std::deque<StudioLogEntry> entries_;
        std::size_t capacity_ = kDefaultCapacity;
        std::size_t dropped_ = 0;
    };
}
