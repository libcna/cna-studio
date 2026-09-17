// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/RecentProjects.hpp
 * @brief The list the Project Hub opens on, and whether each row still points at anything.
 *
 * `plan.md` STUDIO-08002.
 *
 * ### Why validity is a field rather than a filter
 *
 * `STUDIO-08002`'s acceptance is that *a moved or deleted project is shown as unavailable rather
 * than failing on click*. Both halves matter, and the obvious implementation gets the first one
 * wrong: dropping missing entries when the list is read makes a project on an unmounted drive
 * disappear, so somebody who unplugs a disk loses their history rather than seeing it greyed out
 * until they plug it back in. A row that is present and unavailable is also the only place there
 * is to say *why*.
 *
 * ### Why the paths are checked when the list is read, not when it is written
 *
 * A stored `available` flag is a cache of the filesystem, and the filesystem changes while Studio
 * is not running — which is precisely the case this feature exists for. So the file stores paths
 * and times, and availability is answered at the moment somebody looks.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief One row of the recent-projects list. */
    struct StudioRecentProject
    {
        /** @brief Absolute path of the `.cnaproject` file. */
        std::string path;

        /** @brief The project's name when it was last opened, for a row whose file has gone. */
        std::string name;

        /** @brief Seconds since the epoch when it was last opened. */
        std::int64_t openedAt = 0;

        /** @brief Whether the file is there now. Answered when the list is read. */
        bool available = false;

        /** @brief Why it is unavailable, as a sentence. Empty when @ref available. */
        std::string problem;

        /** @brief The directory the project lives in, for showing under the name. */
        [[nodiscard]] std::string directory() const;
    };

    /**
     * @brief The recent-projects list, read from and written to one file.
     *
     * Constructed with a path rather than finding its own, so a test can point it at a temporary
     * directory and the application at the user's. @ref defaultPath is the convention.
     */
    class StudioRecentProjectsStore
    {
    public:
        /** @brief The file name, under the user's Studio configuration directory. */
        static constexpr const char* kFileName = "recent-projects.json";

        /**
         * @brief How many entries are kept.
         *
         * Bounded because this list is a convenience, not a history: a hub showing two hundred rows
         * is one nobody scrolls, and the oldest of them are projects whose directories no longer
         * exist.
         */
        static constexpr std::size_t kMaximumEntries = 20;

        /** @brief Where the list lives for this user, or empty with nowhere to write. */
        [[nodiscard]] static std::string defaultPath();

        /** @brief Constructs a store over @p path. */
        explicit StudioRecentProjectsStore(std::string path) : path_(std::move(path)) {}

        /** @brief The file this store reads and writes. */
        [[nodiscard]] const std::string& getPath() const { return path_; }

        /**
         * @brief Reads the list, newest first, with each row's availability answered.
         *
         * A file that is missing, unreadable or malformed reads as an empty list rather than a
         * failure: a corrupt convenience file must not stop Studio opening.
         */
        [[nodiscard]] std::vector<StudioRecentProject> load() const;

        /**
         * @brief Records that @p projectFilePath was opened, moving it to the front.
         *
         * @param projectFilePath Absolute path of the `.cnaproject`.
         * @param name The project's name, for a row whose file later goes away.
         * @param now Seconds since the epoch, supplied so a test is not at the mercy of a clock.
         * @param outProblem Receives the reason on failure.
         * @return Whether the list was written.
         */
        bool remember(const std::string& projectFilePath, const std::string& name,
                      std::int64_t now, std::string* outProblem = nullptr) const;

        /**
         * @brief Removes @p projectFilePath from the list.
         *
         * Offered because the one thing to do about a row pointing at a project that is gone for
         * good is to take it off the list, and a user who cannot do that is left with a hub that
         * is mostly wrong.
         *
         * @return Whether the list was written.
         */
        bool forget(const std::string& projectFilePath, std::string* outProblem = nullptr) const;

        /** @brief Removes the file, so the next start has no history. */
        bool clear() const;

    private:
        [[nodiscard]] bool write(const std::vector<StudioRecentProject>& entries,
                                 std::string* outProblem) const;

        std::string path_;
    };

    /**
     * @brief Answers whether @p projectFilePath can be opened, and says why when it cannot.
     *
     * Separate from the store so the Hub can ask about a path the user has just chosen from a file
     * dialog, which is not in the list yet.
     *
     * @param projectFilePath Absolute path of a `.cnaproject`.
     * @return Empty when it can be opened; otherwise a sentence naming what is wrong.
     */
    [[nodiscard]] std::string describeStudioProjectAvailability(const std::string& projectFilePath);
}
