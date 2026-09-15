// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioWorkspaceStore.hpp
 * @brief Remembers the workspace layout between runs.
 *
 * `plan.md` STUDIO-05014.
 *
 * A dock layout a user spent ten minutes arranging and Studio forgets on exit is a dock layout they
 * will not arrange again. `StudioShell` already serializes its workspace (`STUDIO-05008`) and
 * already survives a document it cannot read (`STUDIO-05011`, `STUDIO-05012`); this is the half
 * that puts the document somewhere and finds it again.
 *
 * ### What this class knows, and what it deliberately does not
 *
 * It knows about a file, an enclosing format version, and the ways writing to disk goes wrong. It
 * knows nothing about panels, leaves or splits — reconciling a stored arrangement against the
 * panels a build actually has is `StudioShell::loadLayout`'s job, and doing it in both places would
 * mean two answers to the same question. The seam between them is a `JsonValue`.
 *
 * ### Losing a layout is never losing work
 *
 * Every failure here is recoverable by definition: the worst outcome is the default arrangement,
 * which is one menu item away in any case. So nothing throws, nothing refuses to start, and nothing
 * is silent — a user whose panels moved deserves to know why.
 *
 * That principle decides the write path too. The document is written to a temporary beside itself
 * and renamed over the original, so a Studio killed mid-save leaves the previous layout intact
 * rather than a half-written file. It costs one rename and removes the only way this could lose
 * something.
 */

#pragma once

#include "CNA/Studio/Core/Json.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief One arrangement the user saved under a name. */
    struct StudioNamedLayout
    {
        /** @brief What the user called it, e.g. "Animation". */
        std::string name;

        /** @brief The arrangement, for `StudioShell::loadLayout`. */
        JsonValue layout;
    };

    /** @brief What reading the stored layout produced. */
    struct StudioWorkspaceDocument
    {
        /** @brief The stored arrangement, for `StudioShell::loadLayout`. Null when there is none. */
        JsonValue layout;

        /**
         * @brief The arrangements the user saved under names, ordered by name.
         *
         * Ordered rather than in the order they were saved, because they are shown as a menu and a
         * menu whose rows move when one is added is a menu people learn to read every time instead
         * of reaching straight for the one they want.
         */
        std::vector<StudioNamedLayout> named;

        /** @brief Whether a usable document was found and read. */
        bool found = false;

        /**
         * @brief Why the stored layout was not usable, when it was not.
         *
         * Empty on a clean read *and* on a first run — not finding a file the user has never saved
         * is not a problem to report, and reporting it would train them to ignore the channel that
         * reports the real ones.
         */
        std::string problem;
    };

    /**
     * @brief Reads and writes one workspace layout file.
     *
     * Constructed with a path rather than finding its own, so a test can point it at a temporary
     * directory and the application can point it at the user's. @ref defaultPath is the convention.
     */
    class StudioWorkspaceStore
    {
    public:
        /** @brief The file name, under the user's Studio configuration directory. */
        static constexpr const char* kFileName = "workspace.json";

        /**
         * @brief The version of the enclosing document, independent of the layout inside it.
         *
         * Separate from `StudioDockTree::kLayoutVersion` on purpose: this one changes when the
         * *envelope* changes — when the file learns to hold named layouts as well as the current
         * one, say — and the tree's changes when the arrangement's own shape does.
         */
        static constexpr int kFileVersion = 2;

        /**
         * @brief The longest a saved layout's name may be.
         *
         * Bounded because the name is a menu row and a key in a file somebody may open, and a
         * thousand-character one is neither.
         */
        static constexpr std::size_t kMaximumNameLength = 64;

        /**
         * @brief Returns @p name trimmed and checked, or an empty string when it is unusable.
         *
         * One place decides, because a name that the menu accepts and the file rejects is a save
         * that appears to work and is gone at the next start.
         *
         * @param name What the user typed.
         * @return The name to store, or empty when it cannot be one.
         */
        [[nodiscard]] static std::string sanitizeName(std::string_view name);

        /**
         * @brief Where the layout lives for this user.
         * @return An absolute path, or empty on a machine with nowhere to write.
         */
        [[nodiscard]] static std::string defaultPath();

        /** @brief Constructs a store over @p path. */
        explicit StudioWorkspaceStore(std::string path) : path_(std::move(path)) {}

        /** @brief The file this store reads and writes. */
        [[nodiscard]] const std::string& getPath() const { return path_; }

        /**
         * @brief Writes @p layout as the current arrangement, keeping the saved ones.
         *
         * Read-modify-write, and deliberately so: this runs on exit, and a Studio that rewrote the
         * whole file from what it happened to hold in memory would throw away a layout saved by a
         * second Studio running beside it. The rename is still atomic, so an interrupted save
         * leaves the previous file rather than half of a new one.
         *
         * @param layout A document from `StudioShell::saveLayout`.
         * @param outProblem Receives the reason on failure.
         * @return Whether the layout was stored.
         */
        [[nodiscard]] bool save(const JsonValue& layout, std::string* outProblem = nullptr) const;

        /**
         * @brief Saves @p layout under @p name, replacing one of the same name.
         *
         * @param name What to call it. Trimmed; refused when @ref sanitizeName rejects it.
         * @param layout A document from `StudioShell::saveLayout`.
         * @param outProblem Receives the reason on failure.
         * @return Whether it was stored.
         */
        [[nodiscard]] bool saveNamed(std::string_view name, const JsonValue& layout,
                                     std::string* outProblem = nullptr) const;

        /**
         * @brief Removes the layout saved under @p name.
         * @param name The layout to remove.
         * @param outProblem Receives the reason on failure.
         * @return Whether one was removed.
         */
        [[nodiscard]] bool removeNamed(std::string_view name,
                                       std::string* outProblem = nullptr) const;

        /**
         * @brief Reads the stored layout.
         * @return The document, and why it was not usable when it was not.
         */
        [[nodiscard]] StudioWorkspaceDocument load() const;

        /**
         * @brief Removes the stored layout, so the next start uses the default arrangement.
         * @return Whether anything was removed.
         */
        bool forget() const;

    private:
        /** @brief Reads the whole file, writes it back with @p change applied. */
        bool rewrite(const std::function<bool(JsonValue&)>& change, std::string* outProblem) const;

        /** @brief Writes @p document over the file, through a temporary and a rename. */
        bool writeDocument(const JsonValue& document, std::string* outProblem) const;

        std::string path_;
    };
}
