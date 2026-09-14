// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Core/UserPaths.hpp
 * @brief Where Studio keeps a user's own files, by each platform's own convention.
 *
 * `plan.md` STUDIO-05014, STUDIO-06010.
 *
 * Three kinds of user file, kept apart because the platforms keep them apart and because what
 * should happen when one is lost differs:
 *
 * - **Configuration** — the workspace layout, preferences, recent projects. Losing it costs the
 *   user their arrangement. It is the thing a person would think to back up.
 * - **State** — crash-recovery snapshots, logs, caches of things that can be recomputed. Losing it
 *   costs nothing a rebuild cannot replace, and a machine sweeping it is not a bug.
 *
 * Studio resolves both from the environment rather than hard-coding a path, so a user who has moved
 * their home directory, a CI job with a scratch `HOME`, and a test that wants neither, all get what
 * they asked for. Every resolution ends somewhere: a user with no home directory still deserves an
 * autosave, so the last fallback is the temporary directory rather than nothing.
 */

#pragma once

#include <string>

namespace CNA::Studio
{
    /**
     * @brief Directory for the user's Studio configuration.
     *
     * `$XDG_CONFIG_HOME/cna-studio`, `%APPDATA%\cna-studio`, `$HOME/.config/cna-studio`, or a
     * temporary directory, in that order of preference.
     *
     * @return An absolute path, which may not exist yet. Empty only when even a temporary directory
     *         could not be found, which is a machine with nowhere to write at all.
     */
    [[nodiscard]] std::string getStudioConfigDirectory();

    /**
     * @brief Directory for the user's Studio state: recovery snapshots, logs, caches.
     *
     * `$XDG_STATE_HOME/cna-studio`, `%LOCALAPPDATA%\cna-studio`, `%APPDATA%\cna-studio`,
     * `$HOME/.local/state/cna-studio`, or a temporary directory.
     *
     * @return An absolute path, which may not exist yet.
     */
    [[nodiscard]] std::string getStudioStateDirectory();
}
