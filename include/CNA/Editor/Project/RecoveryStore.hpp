// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Project/RecoveryStore.hpp
 * @brief Periodic snapshots of the open scene, so a crash costs seconds rather than an afternoon.
 *
 * plan.md ED-903, and the shape of it follows from one observation: the reliable half of crash
 * recovery is the part that runs *before* the crash. A handler that tries to serialise a document
 * from inside SIGSEGV is calling malloc and the filesystem from a signal handler with a corrupted
 * heap -- the situation in which it is least likely to work is exactly the one it exists for. A
 * snapshot written every few seconds by ordinary code, on the other hand, is already on disk when
 * the process dies, and needs nothing from the dying process at all.
 *
 * So there is no crash handler here. There is a snapshot, written atomically, discarded the moment
 * the document matches its file, and found again when the project is next opened.
 *
 * Deliberately knows nothing about SceneDocument: it takes and returns the scene as a `JsonValue`.
 * That keeps this in `cna-editor-project` alongside the other file formats, keeps it testable
 * without a document, and means a future snapshot of something *other* than a scene needs no new
 * type.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    /** @brief One unsaved document, as it stood the last time it was snapshotted. */
    struct RecoverySnapshot
    {
        /** @brief Where this snapshot lives on disk. Set by the store, ignored on write. */
        std::string filePath;

        /** @brief The `.cnaproject` that was open, so the right snapshot is offered on reopen. */
        std::string projectPath;

        /** @brief The scene's own file, or empty when the scene had never been saved. */
        std::string scenePath;

        /** @brief The scene's display name, for the message offering the recovery. */
        std::string sceneName;

        /** @brief The scene's stable id, which is also this snapshot's file name. */
        Uuid sceneId;

        /** @brief Unix time the snapshot was taken. Supplied by the caller, never read here. */
        std::int64_t savedAtSeconds = 0;

        /** @brief The document, exactly as `SceneDocument::toJson()` produced it. */
        JsonValue scene;
    };

    /**
     * @brief A directory of snapshots, one file per scene id.
     *
     * Writes are atomic: the snapshot goes to a temporary file and is renamed over the old one, so
     * a crash *during* a snapshot leaves the previous one intact. A half-written recovery file
     * would be worse than none at all -- it would fail to load at the one moment the user needs it,
     * having already convinced them their work was safe.
     */
    class RecoveryStore
    {
    public:
        /** @brief The `formatVersion` this build writes, and the highest it will read. */
        static constexpr int kFormatVersion = 1;

        /** @brief File extension for a snapshot. */
        static constexpr const char* kExtension = ".cnarecovery";

        explicit RecoveryStore(std::string directory) : directory_(std::move(directory)) {}

        [[nodiscard]] const std::string& getDirectory() const { return directory_; }

        /**
         * @brief Writes @p snapshot, replacing any previous one for the same scene id.
         *
         * @return False when the directory cannot be created or the file cannot be written, with
         *         the reason in @p errorMessage. A failure is reported rather than thrown: an
         *         editor that dies because it could not autosave has caused the loss it was
         *         installed to prevent.
         */
        bool write(const RecoverySnapshot& snapshot, std::string* errorMessage = nullptr) const;

        /** @brief Removes the snapshot for @p sceneId. Returns true when one was removed. */
        bool discard(const Uuid& sceneId) const;

        /** @brief Returns every readable snapshot, newest first. Unreadable files are skipped. */
        [[nodiscard]] std::vector<RecoverySnapshot> list() const;

        /**
         * @brief Returns the newest snapshot taken while @p projectPath was open.
         *
         * Matching on the project rather than the scene is deliberate: the scene the user was
         * editing when the editor died is not necessarily the project's startup scene, and
         * offering only the latter would silently drop the work.
         */
        [[nodiscard]] std::optional<RecoverySnapshot> findForProject(const std::string& projectPath) const;

    private:
        std::string directory_;
    };

    /**
     * @brief Returns the per-user directory snapshots belong in.
     *
     * Under the user's *state* directory, not the project: an autosave of unsaved work is not part
     * of the game, and writing it beside the project would put half-finished edits into somebody's
     * repository. Falls back through the platform conventions and finally to the temporary
     * directory, which is worse but is never nothing.
     */
    [[nodiscard]] std::string getDefaultRecoveryDirectory();

    /**
     * @brief Renders a snapshot timestamp as local `YYYY-MM-DD HH:MM`.
     *
     * Local time, not UTC, and to the minute rather than the second: the reader is deciding
     * whether the snapshot is the work they remember doing, and no one remembers seconds.
     */
    [[nodiscard]] std::string formatRecoveryTime(std::int64_t unixSeconds);
}
