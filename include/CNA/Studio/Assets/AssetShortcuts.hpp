// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetShortcuts.hpp
 * @brief The assets a user keeps coming back to: the ones they starred, and the ones they just used.
 *
 * `plan.md` STUDIO-09007.
 *
 * A real project's asset tree is deep, and the working set is small: the four textures this week's
 * work is about, and whatever was touched five minutes ago. Without somewhere to keep them, every
 * return trip is the same walk down the same folders — which is why every professional content
 * browser has both of these and why neither is a feature anybody asks for by name.
 *
 * ### Favourites are chosen, recent is observed
 *
 * They are kept apart rather than merged into one "quick access" list, because they answer
 * different questions. A favourite is a *decision* and stays until it is unmade; a recent entry is
 * a side effect and is pushed out by the next thing. A list that mixed them would lose a
 * deliberate choice to a morning's browsing.
 *
 * ### This is user state, not project data
 *
 * It lives beside the user's other Studio state rather than in the project, because it is about one
 * person's week rather than about the game — and because a project file that changed whenever
 * somebody clicked an asset would make every branch conflict on it. That is the same reason the
 * recent-projects list is where it is.
 *
 * ### Ids, not paths
 *
 * A favourite survives the file being moved or renamed, exactly as a scene's reference does (D-08).
 * A list of paths would quietly rot as the project is tidied, which is the one thing a *favourite*
 * must not do.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
{
    class AssetDatabase;

    /** @brief One project's starred and recently used assets. */
    struct StudioAssetShortcuts
    {
        /** @brief Starred assets, in the order they were starred. */
        std::vector<Uuid> favourites;

        /** @brief Recently used assets, most recent first. */
        std::vector<Uuid> recent;

        /** @brief Whether @p id is starred. */
        [[nodiscard]] bool isFavourite(const Uuid& id) const;

        /**
         * @brief Stars @p id, or unstars it when it already is.
         * @return True when it is starred afterwards.
         */
        bool toggleFavourite(const Uuid& id);

        /**
         * @brief Moves @p id to the front of the recent list.
         *
         * @param id The asset. A nil id does nothing, which is what clearing the selection is.
         * @param limit How many entries to keep.
         * @return True when the list changed, so a caller knows whether to write the file.
         */
        bool remember(const Uuid& id, std::size_t limit);

        /**
         * @brief Drops ids the database no longer has.
         *
         * A favourite pointing at a deleted asset is a row that cannot be clicked; a recent one is
         * worse, because it appeared without being asked for. Run on load, so a project that was
         * tidied elsewhere does not open with a list of ghosts.
         *
         * @param assets The database to check against.
         * @return How many entries were dropped.
         */
        std::size_t prune(const AssetDatabase& assets);

        /** @brief Whether there is nothing in either list. */
        [[nodiscard]] bool isEmpty() const { return favourites.empty() && recent.empty(); }
    };

    /**
     * @brief One project's shortcuts, read from and written to one file.
     *
     * Constructed with a path rather than finding its own, so a test can point it at a temporary
     * directory and the application at the user's — the same shape as `StudioRecentProjectsStore`.
     */
    class StudioAssetShortcutStore
    {
    public:
        /** @brief The document version, so a later shape change can be recognised rather than guessed. */
        static constexpr int kFileVersion = 1;

        /**
         * @brief How many recent entries are kept.
         *
         * Bounded because this is a convenience rather than a history: a list nobody scrolls is a
         * list that costs its space and pays nothing.
         */
        static constexpr std::size_t kMaximumRecent = 24;

        /**
         * @brief Where this project's shortcuts live for this user, or empty with nowhere to write.
         *
         * Named after the project *and* a hash of its full path: the name is what makes the
         * directory readable by a person who opens it, and the hash is what keeps two projects
         * called `Game` in different places from sharing a file.
         *
         * @param projectFilePath Absolute path of the `.cnaproject`.
         */
        [[nodiscard]] static std::string defaultPathFor(const std::string& projectFilePath);

        /** @brief Constructs a store over @p path. */
        explicit StudioAssetShortcutStore(std::string path) : path_(std::move(path)) {}

        /** @brief The file this store reads and writes. */
        [[nodiscard]] const std::string& getPath() const { return path_; }

        /**
         * @brief Reads the lists.
         *
         * A file that is missing, unreadable or malformed reads as empty rather than as a failure:
         * a corrupt convenience file must not stop a project opening.
         */
        [[nodiscard]] StudioAssetShortcuts load() const;

        /**
         * @brief Writes the lists, creating the directory if it is not there.
         * @param shortcuts What to write.
         * @param outProblem Receives the reason on failure.
         * @return Whether the file was written.
         */
        bool save(const StudioAssetShortcuts& shortcuts, std::string* outProblem = nullptr) const;

    private:
        std::string path_;
    };
}
