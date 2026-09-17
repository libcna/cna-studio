// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetRelink.hpp
 * @brief Finding a missing asset's file again, and pointing the record back at it.
 *
 * `plan.md` STUDIO-09013.
 *
 * A tracked asset whose source file has gone is not an error the database can fix by itself, and it
 * is deliberately not dropped (`AssetDatabase::scan`): a scene references it by id, so forgetting
 * the record would turn a fixable problem into a broken scene. What is left is a row in the Content
 * Browser marked missing and a line in the Problems panel — true, and not a repair.
 *
 * ### The two repairs are different operations and must not be confused
 *
 * - **The file moved, and nothing has re-imported it.** The id is still the right id; only the path
 *   is wrong. @ref RelinkAssetFileCommand points the record at the file again, and **no scene is
 *   touched** — which is the whole reason references are ids.
 * - **The file moved, and a scan has already given it a new id.** There are now two records and the
 *   scenes point at the old one. The repair is `RelinkAssetCommand`, which rewrites the references
 *   to the new id — a scene edit, because the scenes really are wrong.
 *
 * Offering one where the other is needed produces two records for one file, or a scene rewritten
 * when nothing was wrong with it. So @ref RelinkCandidate says which kind each suggestion is, and
 * the commands refuse the case they are not for.
 */

#include <string>
#include <vector>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/StudioCommand.hpp"
#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
{
    /** @brief One way a missing asset could be found again. */
    struct RelinkCandidate
    {
        /** @brief Which repair this suggestion is, and therefore which command applies it. */
        enum class Kind
        {
            /**
             * @brief A file on disk that nothing tracks. Point the record at it.
             *
             * The preferred answer whenever it exists: the asset keeps its id, so every scene that
             * referenced it is correct again without being edited.
             */
            UntrackedFile,

            /**
             * @brief A tracked asset that looks like the one that went. Relink the references.
             *
             * Needed when a scan has already given the returned file a new id: two records exist,
             * and the scenes are pointing at the wrong one. This repair *does* edit scenes.
             */
            TrackedAsset,
        };

        Kind kind = Kind::UntrackedFile;

        /** @brief The candidate's project-relative path. */
        std::string path;

        /** @brief The candidate's id, for @ref Kind::TrackedAsset. Nil otherwise. */
        Uuid assetId;

        /** @brief Why this was suggested, shown beside it: "same name", "same name and kind". */
        std::string reason;
    };

    /**
     * @brief Suggestions for where @p missingId's file went, best first.
     *
     * Ranked rather than merely listed: a dialog offering fourteen files called `player.png` in no
     * particular order is a dialog that asks the user to do the search themselves.
     *
     * The ranking is by *name*, because that is what survives a file being moved, and by extension
     * after it, because the extension decides the asset's type — a `.png` offered in place of a
     * `.wav` is not a candidate however similar the names are.
     *
     * An untracked file always outranks a tracked one, because pointing the record back at its file
     * touches no scene and relinking references edits every scene that used it.
     *
     * @param assets The project's assets. Its root is walked for untracked files.
     * @param missingId The asset whose file has gone. Its record supplies the name to look for, so
     *        an id the database does not know produces nothing — there is nothing to search on.
     * @param limit How many to return at most.
     * @return The candidates, best first. Empty when nothing plausible is on disk.
     */
    [[nodiscard]] std::vector<RelinkCandidate> studioRelinkCandidates(const AssetDatabase& assets,
                                                                       const Uuid& missingId,
                                                                       std::size_t limit = 8);

    /**
     * @brief Points a tracked asset whose file has gone at the file again, keeping its id.
     *
     * `plan.md` STUDIO-09013. **No scene is touched**, in either direction: the id does not change,
     * so every reference that was broken is correct again and every reference that was correct
     * stays so. That is the same property a move has, and for the same reason (D-08).
     *
     * Refused when the asset's file is still where the record says it is — that is a *move*, and
     * `MoveAssetCommand` is the operation that moves a file rather than the one that admits it has
     * already been moved by something else. Refused, too, when the destination is already tracked:
     * two records for one file is a database that cannot say which id a scene means.
     */
    class RelinkAssetFileCommand final : public StudioCommand
    {
    public:
        RelinkAssetFileCommand(AssetDatabase& assets, Uuid assetId, std::string newRelativePath);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns false when the repair does not apply; see @ref getError. */
        [[nodiscard]] bool isValid() const { return valid_; }

        /** @brief Returns why the command is invalid, or why the last attempt failed. */
        [[nodiscard]] const std::string& getError() const { return error_; }

    private:
        AssetDatabase* assets_;
        Uuid assetId_;
        std::string oldPath_;
        std::string newPath_;
        std::string error_;
        bool valid_ = false;
    };
}
