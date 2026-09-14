// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/FormatMigration.hpp
 * @brief Upgrading a document written by an older build, one version at a time.
 *
 * plan.md ED-902. Version *gating* has existed since Phase 0 -- every loader reads `formatVersion`,
 * refuses a file from the future and refuses one with no version at all. What did not exist is the
 * other half: a file from the past was read by today's permissive loader, which is fine only while
 * nothing has ever changed. The moment a field is renamed, that loader silently reads a default
 * where the user's value used to be, and the damage is written back on the next save.
 *
 * The shape here is deliberately the boring one: **a chain of single-version steps**. Version 3
 * becomes 4, then 4 becomes 5. No step ever knows about more than one transition, which is what
 * keeps the twelfth migration the same size as the first -- the alternative, one function per
 * (from, to) pair, grows quadratically and is where migration frameworks go to die.
 *
 * Steps run on the *parsed JSON*, before any of it reaches a document type. That is the only place
 * they can run: by the time a `SceneDocument` exists, the fields the old file used are already
 * gone, and a migration would be reconstructing them from a lossy reading.
 *
 * @note Every format in this repository is at version 1, so every migrator here is empty. That is
 *       the intended state -- the mechanism exists so that the first real change is a small,
 *       tested, reviewable addition rather than an emergency. Registering a step is not a licence
 *       to bump a version; formats stay backward compatible unless the plan explicitly says
 *       otherwise.
 */

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "CNA/Editor/Core/Json.hpp"

namespace CNA::Editor
{
    /**
     * @brief One step, upgrading a document from version @c fromVersion to @c fromVersion + 1.
     *
     * The step edits @p document in place. It does not have to touch `formatVersion`: the migrator
     * stamps that after the step returns, so a step cannot leave the document claiming a version it
     * did not reach.
     */
    struct FormatMigration
    {
        /** @brief The version this step reads. It always produces @c fromVersion + 1. */
        int fromVersion = 0;

        /** @brief What the step changes, in one phrase. Reported to the user when it runs. */
        std::string description;

        /**
         * @brief Performs the upgrade.
         *
         * @return False when the document cannot be upgraded, with the reason in @c errorMessage.
         *         Failing is a legitimate outcome -- a file whose old form is genuinely ambiguous
         *         is better refused than guessed at, because the guess gets written back on save.
         */
        std::function<bool(JsonValue& document, std::string& errorMessage)> apply;
    };

    /** @brief What happened when a document was run through a migrator. */
    struct FormatMigrationResult
    {
        bool succeeded = false;

        /** @brief The version the document arrived at. */
        int fromVersion = 0;

        /** @brief The version it left at; equal to @c fromVersion when nothing ran. */
        int toVersion = 0;

        /** @brief The description of each step that ran, oldest first. Empty when nothing ran. */
        std::vector<std::string> applied;

        /** @brief Why the upgrade stopped, when it did. Empty on success. */
        std::string errorMessage;

        /** @brief True when at least one step ran. */
        [[nodiscard]] bool changedAnything() const { return !applied.empty(); }
    };

    /**
     * @brief The ordered migration chain for one file format.
     *
     * Owns the version gate as well as the upgrades, so that every format refuses a file from the
     * future in the same words and every loader delegates rather than reimplementing the check.
     */
    class FormatMigrator
    {
    public:
        /**
         * @param formatName How the format is named in messages, e.g. "scene".
         * @param currentVersion The version this build writes.
         */
        FormatMigrator(std::string formatName, int currentVersion)
            : formatName_(std::move(formatName)), currentVersion_(currentVersion)
        {
        }

        [[nodiscard]] const std::string& getFormatName() const { return formatName_; }
        [[nodiscard]] int getCurrentVersion() const { return currentVersion_; }
        [[nodiscard]] std::size_t getMigrationCount() const { return migrations_.size(); }

        /**
         * @brief Registers a step upgrading @p fromVersion to @p fromVersion + 1.
         *
         * @return False when @p fromVersion is not below the current version, when @p apply is
         *         empty, or when a step for that version is already registered. Refusing a
         *         duplicate is what makes the chain a chain: two steps reading the same version
         *         would make the outcome depend on registration order.
         */
        bool addMigration(int fromVersion, std::string description,
                          std::function<bool(JsonValue&, std::string&)> apply);

        /**
         * @brief Upgrades @p document in place to the current version.
         *
         * Reads `formatVersion` from the document itself. A document already at the current version
         * is left untouched and reported as a success with nothing applied.
         */
        [[nodiscard]] FormatMigrationResult migrate(JsonValue& document) const;

    private:
        std::string formatName_;
        int currentVersion_ = 1;
        std::vector<FormatMigration> migrations_;
    };
}
