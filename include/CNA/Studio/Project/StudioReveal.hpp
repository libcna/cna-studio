// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/StudioReveal.hpp
 * @brief Showing a file in the desktop's own file manager.
 *
 * `plan.md` STUDIO-09011.
 *
 * The moment this exists for: an asset is in Studio and the next thing to do with it is not in
 * Studio — open it in Krita, drop it into a chat, check what git thinks of it. Without this the
 * user copies the path out of the inspector and pastes it into a file manager, which is a thing
 * people do dozens of times a day and complain about once.
 *
 * ### What each platform can actually do, which is not the same thing
 *
 * - **Windows**: `explorer /select,<path>` opens the folder *and highlights the file*.
 * - **macOS**: `open -R <path>` does the same through Finder.
 * - **Linux**: there is no portable way to highlight a file. `xdg-open` takes a *directory*, and
 *   the desktop's own file manager opens it. Some managers support a selection through D-Bus and
 *   no two of them agree, so the honest answer is the containing folder.
 *
 * That difference is reported rather than hidden: @ref StudioRevealCommand says whether the file
 * itself will be selected, so a caller can say "Show in Folder" where it cannot promise more.
 */

#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief What this platform would run to reveal a path. */
    struct StudioRevealCommand
    {
        /** @brief The executable and its arguments. Empty when the platform is not supported. */
        std::vector<std::string> argv;

        /**
         * @brief Whether the file itself is highlighted, rather than only its folder opened.
         *
         * False on Linux, where no portable way to highlight one exists. A caller that promised
         * otherwise would be promising something the desktop will not do.
         */
        bool selectsTheFile = false;

        /** @brief Whether there is anything to run. */
        [[nodiscard]] bool isValid() const { return !argv.empty(); }
    };

    /**
     * @brief The command this platform would use to reveal @p absolutePath, without running it.
     *
     * Separate from running it so that *what gets run* is testable on a machine with no desktop —
     * which is every machine this project's tests run on. A reveal that quoted its path wrongly
     * would otherwise be discovered by a user rather than by CI.
     *
     * @param absolutePath The file, or the directory, to show.
     * @return The command, or an invalid one on a platform with no answer.
     */
    [[nodiscard]] StudioRevealCommand studioRevealCommand(const std::string& absolutePath);

    /**
     * @brief Asks the desktop to show @p absolutePath, and does not wait for it.
     *
     * Detached on purpose: a file manager is a long-lived application the user is about to work in,
     * and an editor that waited for it — or that kept it as a child to be killed on exit — would be
     * an editor that closes the window they just opened.
     *
     * @param absolutePath The file or directory to show.
     * @param errorMessage Set when the answer is false. Optional.
     * @return False when the path does not exist, the platform has no answer, or the launch failed.
     */
    bool studioRevealInFileManager(const std::string& absolutePath,
                                   std::string* errorMessage = nullptr);
}
