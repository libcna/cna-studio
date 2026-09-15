// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/StudioRecovery.hpp
 * @brief The crash-recovery *flow*, so both Studio UIs run the same one.
 *
 * `plan.md` STUDIO-07020's inventory listed crash recovery as unanswered natively, and it was: the
 * snapshot format, the store and the atomic write have always been shared, but the parts that
 * decide *when* to write one, when to drop it, and what to do with one found at start-up lived in
 * `StudioApplication` — the Dear ImGui host. The native shell runs through a different host and so
 * had neither half: no snapshots written, and nothing offering the ones a previous session left.
 *
 * Duplicating the flow into the second host would be two answers to "is this work safe", and the
 * one that drifts is always the one nobody is looking at. So it is one object, driven by whichever
 * host is running.
 *
 * ### It logs rather than returning prose
 *
 * Every message here is written through the context's log, which both UIs already read. A version
 * that returned strings for the caller to display would mean each host phrasing the same facts, and
 * the phrasing is the part that matters: "autosave is suspended" has to say why and what to do
 * about it, and it has to say it once.
 *
 * ### What it deliberately does not do
 *
 * There is no crash handler, for the reason `RecoveryStore` gives: the reliable half of crash
 * recovery is the part that runs before the crash.
 */

#include "CNA/Studio/Project/RecoveryStore.hpp"

#include <optional>
#include <string>

namespace CNA::Studio
{
    class StudioContext;

    /**
     * @brief Periodic snapshots of the open scene, and whatever a previous session left behind.
     *
     * Holds the store, the timer and the snapshot found at start-up. One per running editor.
     */
    class StudioRecoverySession
    {
    public:
        /** @brief How often a snapshot is written by default, in seconds. */
        static constexpr double kDefaultIntervalSeconds = 30.0;

        /**
         * @brief Constructs a session over @p context, writing to the default directory.
         * @param context The editor's document and log. Not owned; must outlive this.
         */
        explicit StudioRecoverySession(StudioContext& context);

        StudioRecoverySession(const StudioRecoverySession&) = delete;
        StudioRecoverySession& operator=(const StudioRecoverySession&) = delete;

        /**
         * @brief Writes snapshots to @p directory instead of the default.
         * @param directory Where snapshots live. Empty leaves the default in place.
         */
        void setDirectory(const std::string& directory);

        /**
         * @brief Sets how often a snapshot is written.
         * @param seconds Interval; zero or less turns snapshots off entirely.
         */
        void setIntervalSeconds(double seconds) { intervalSeconds_ = seconds; }

        /** @brief How often a snapshot is written. Zero or less means never. */
        [[nodiscard]] double intervalSeconds() const { return intervalSeconds_; }

        /**
         * @brief Looks for work a previous session left for the open project.
         *
         * Called after a project opens. Says so in the log when it finds something, as a warning:
         * the alternative reading of the state is that the last session ended without saving, and
         * either way there is work on disk that the document in front of the user does not have.
         *
         * @return True when something was found and is now being offered.
         */
        bool scan();

        /** @brief Whether work from a previous session is waiting to be recovered or discarded. */
        [[nodiscard]] bool hasRecoverable() const { return recoverable_.has_value(); }

        /** @brief The waiting snapshot, or nullptr. */
        [[nodiscard]] const RecoverySnapshot* recoverable() const
        {
            return recoverable_ ? &*recoverable_ : nullptr;
        }

        /**
         * @brief Loads the waiting snapshot into the open scene.
         *
         * The file on disk is left alone and the history is cleared and marked unsaved, because the
         * recovered document has never been written anywhere — saying otherwise would let the user
         * close the editor believing it had.
         *
         * A recovery that fails keeps the snapshot: it is not a reason to delete the only copy of
         * the work it was holding.
         *
         * @return True when the scene now holds the recovered work.
         */
        bool recover();

        /**
         * @brief Throws the waiting snapshot away.
         * @return True when there was one.
         */
        bool discard();

        /**
         * @brief Advances the timer, writing a snapshot when one is due.
         *
         * Also drops the snapshot as soon as the document matches its file: leaving it would make
         * the next start-up offer a recovery of work that is already saved, which trains users to
         * dismiss the offer without reading it.
         *
         * @param deltaSeconds Seconds since the last call.
         */
        void update(double deltaSeconds);

        /** @brief Drops the snapshot for the open scene, e.g. because it has just been saved. */
        void discardForCurrentScene();

        /** @brief The store, for a host that needs to point at its directory. */
        [[nodiscard]] RecoveryStore& store() { return store_; }

        /** @brief The store. */
        [[nodiscard]] const RecoveryStore& store() const { return store_; }

        /** @brief Forgets what has been reported, so a fresh project starts quiet. */
        void reset();

    private:
        StudioContext& context_;
        RecoveryStore store_{getDefaultRecoveryDirectory()};

        double intervalSeconds_ = kDefaultIntervalSeconds;
        double elapsedSeconds_ = 0.0;

        std::optional<RecoverySnapshot> recoverable_;

        /** @brief Whether this session has a snapshot of its own on disk. */
        bool written_ = false;

        /** @brief Whether the "autosave is suspended" warning has already been said. */
        bool suspensionReported_ = false;

        /** @brief Whether a failed write has already been complained about. */
        bool failureReported_ = false;
    };
}
