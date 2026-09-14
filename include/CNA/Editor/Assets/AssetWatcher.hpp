// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Assets/AssetWatcher.hpp
 * @brief Notices assets changed outside the editor, so the viewport stops showing stale art.
 *
 * **Polling, not a native watcher.** inotify, kqueue and ReadDirectoryChangesW are three separate
 * implementations with three separate failure modes, and every one of them still needs a polling
 * fallback for network and container mounts -- which is exactly where a team's assets often live.
 * Re-stating the tracked files costs one syscall each and happens twice a second at most; on a
 * project large enough for that to matter, the interval is a knob. Native watchers become worth
 * their cost when a project reaches tens of thousands of assets, and can be added behind this same
 * interface without any caller changing.
 *
 * The clock is passed in rather than read, so a test can advance time exactly and never has to
 * sleep. A watcher whose tests sleep is a watcher whose tests are flaky on a loaded machine.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    /** @brief What a poll found. */
    struct AssetWatchResult
    {
        /** @brief Tracked assets whose file changed on disk. */
        std::vector<Uuid> changed;

        /** @brief Tracked assets whose file has gone. */
        std::vector<Uuid> removed;

        /** @brief Tracked assets whose file has come back. */
        std::vector<Uuid> restored;

        /** @brief True when a poll actually ran this call, whether or not it found anything. */
        bool polled = false;

        [[nodiscard]] bool hasChanges() const
        {
            return !changed.empty() || !removed.empty() || !restored.empty();
        }
    };

    /**
     * @brief Watches the tracked assets for changes made outside the editor.
     *
     * Detects modification and disappearance of files the database already knows about. New files
     * are a scan's job, not a watcher's: discovering one means reading a sidecar, assigning an id
     * and deciding whether it is a move, and that is a decision the database owns.
     */
    class AssetWatcher
    {
    public:
        /** @brief Seconds between polls. Values below zero are clamped to zero. */
        void setInterval(double seconds);
        [[nodiscard]] double getInterval() const { return interval_; }

        /**
         * @brief Advances the clock by @p deltaSeconds and polls when one is due.
         *
         * Updates the stamps of whatever it reports, so a change is reported exactly once rather
         * than on every poll until something else fixes it.
         */
        AssetWatchResult poll(AssetDatabase& assets, double deltaSeconds);

        /** @brief Forces the next poll() to run regardless of the interval. */
        void requestImmediatePoll() { elapsed_ = interval_; }

    private:
        double interval_ = 0.5;
        double elapsed_ = 0.0;
    };
}
