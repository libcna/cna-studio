// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetCommands.hpp
 * @brief Undoable operations on the asset database.
 *
 * The asset database is a document like the scene, so changes to it are commands and go through
 * the same history (ANALYSIS.md decision D-06). An editor where some edits undo and others quietly
 * do not is worse than one where nothing undoes: the user cannot tell which they are looking at
 * until Ctrl+Z does the wrong thing.
 */

#include <memory>
#include <string>
#include <string_view>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/MaterialDocument.hpp"
#include "CNA/Studio/Core/StudioCommand.hpp"
#include "CNA/Studio/Core/PropertyValue.hpp"

namespace CNA::Studio
{
    /**
     * @brief What is wrong with @p name as an asset file name, or an empty string when nothing is.
     *
     * `plan.md` STUDIO-09009. Checked before a rename rather than after, because the filesystem's
     * own answer arrives too late to be useful: by then the user has typed a name, pressed Enter
     * and watched nothing happen.
     *
     * The rules are the *strictest* of the platforms a project might be checked out on, not the
     * host's. A project renamed on Linux that cannot be cloned on Windows is a defect that the
     * person who made it will never see -- so `<>:"|?*`, a trailing dot or space, and the reserved
     * device names are refused everywhere. Path separators are refused too: renaming is not moving,
     * and a name with a slash in it is a user who meant to drag.
     *
     * @param name The proposed file name, extension included.
     * @return A sentence for the user, or an empty string when the name is usable.
     */
    [[nodiscard]] std::string describeStudioAssetNameProblem(std::string_view name);

    /**
     * @brief @p path with its last segment replaced by @p name.
     *
     * Renaming is a move whose destination happens to be the same folder, which is why there is no
     * `RenameAssetCommand`: one operation with one set of consequences should not have two
     * implementations that can disagree about whether the id survives.
     *
     * @param path A project-relative path, forward slashes.
     * @param name The new file name.
     * @return The new project-relative path.
     */
    [[nodiscard]] std::string studioAssetPathRenamedTo(std::string_view path, std::string_view name);

    /**
     * @brief A path beside @p path that no tracked asset occupies: `Crate.gltf` -> `Crate 2.gltf`.
     *
     * The extension is kept, because it is what decides the asset's type on the next scan: a copy
     * of a texture called `Crate 2` with the `.png` dropped is a copy that stops being a texture.
     *
     * @param assets The database, consulted for what is taken.
     * @param path The path being copied.
     * @return An unused project-relative path.
     */
    [[nodiscard]] std::string studioAssetUnusedPathLike(const AssetDatabase& assets,
                                                        std::string_view path);

    /**
     * @brief Moves or renames an asset's file, keeping its id.
     *
     * Undoable like every other document change (D-06), and undo is simply the move back. No scene
     * is touched in either direction, which is the property that makes an asset folder safe to
     * tidy: references are Uuids, so where a file sits is not something a scene knows.
     */
    class MoveAssetCommand final : public StudioCommand
    {
    public:
        MoveAssetCommand(AssetDatabase& assets, Uuid assetId, std::string newRelativePath);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns false when the asset is unknown or the destination is unusable. */
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

    /**
     * @brief The command that moves every asset under @p folder to @p newFolder.
     *
     * `plan.md` STUDIO-09009. A folder is not a thing the database tracks -- folders exist exactly
     * because assets have paths -- so moving one is moving its contents, and there is nothing else
     * it could mean. One @ref CompositeCommand, so the whole folder comes back on one Ctrl+Z
     * rather than on one press per file in it.
     *
     * Nothing is copied and no id changes, so no scene is touched, exactly as for a single move.
     *
     * @param assets The database.
     * @param folder The folder's project-relative path.
     * @param newFolder Where it goes.
     * @param error Set when the answer is null. Optional.
     * @return The command, or null when the folder is empty, the destination is taken, or a name
     *         in @p newFolder is unusable.
     */
    [[nodiscard]] std::unique_ptr<StudioCommand> studioMoveFolderCommand(AssetDatabase& assets,
                                                                          std::string_view folder,
                                                                          std::string_view newFolder,
                                                                          std::string* error = nullptr);

    /**
     * @brief Copies an asset's file, giving the copy a new id.
     *
     * `plan.md` STUDIO-09009. A *new* id, unlike a move: two files with one id would mean the
     * database could not say which one a scene is referencing, and the first scan to notice would
     * pick whichever it walked last. So a duplicate is a new asset that happens to start with the
     * same bytes, and nothing that referenced the original references the copy.
     *
     * The id is generated once, in the constructor, so that undo-then-redo restores *the same*
     * copy rather than a second one -- a redo that minted a fresh id would leave every reference
     * the user had since made to the copy pointing at nothing.
     */
    class DuplicateAssetCommand final : public StudioCommand
    {
    public:
        DuplicateAssetCommand(AssetDatabase& assets, Uuid assetId);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns false when the asset is unknown or its file cannot be read. */
        [[nodiscard]] bool isValid() const { return valid_; }

        /** @brief Returns why the command is invalid, or why the last attempt failed. */
        [[nodiscard]] const std::string& getError() const { return error_; }

        /** @brief The copy's id, so the caller can select what it just made. */
        [[nodiscard]] const Uuid& getCopyId() const { return copyId_; }

        /** @brief Where the copy went. */
        [[nodiscard]] const std::string& getCopyPath() const { return copyPath_; }

    private:
        AssetDatabase* assets_;
        Uuid sourceId_;
        Uuid copyId_;
        std::string sourcePath_;
        std::string copyPath_;
        std::string error_;
        bool valid_ = false;
    };

    /**
     * @brief Deletes an asset's file and its sidecar, and forgets its record.
     *
     * `plan.md` STUDIO-09009. Undoable, which for a delete means the *bytes* are captured: the
     * file is gone from disk, so an undo with nothing to write back would be a menu entry that
     * lies. They are read in the constructor, before anything is removed, so a file that cannot be
     * read produces an invalid command rather than a deletion that cannot be undone.
     *
     * Undo restores the asset **under the same id**, which is the only answer that works. A scene
     * referencing the asset is not touched by the delete -- the reference dangles, and the
     * Content Browser shows the asset as missing until the undo -- and an undo that produced a new
     * id would leave every one of those references pointing at nothing for ever.
     *
     * It does *not* ask first. The confirmation is the undo stack: a dialog before an operation
     * that is already reversible teaches the user to dismiss dialogs, and the one that matters
     * later gets dismissed too.
     */
    class DeleteAssetCommand final : public StudioCommand
    {
    public:
        DeleteAssetCommand(AssetDatabase& assets, Uuid assetId);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns false when the asset is unknown or its file cannot be read. */
        [[nodiscard]] bool isValid() const { return valid_; }

        /** @brief Returns why the command is invalid, or why the last attempt failed. */
        [[nodiscard]] const std::string& getError() const { return error_; }

    private:
        AssetDatabase* assets_;

        /** @brief The whole record, replayed by undo so importer settings survive too. */
        AssetRecord record_;

        /** @brief The file's bytes, captured before the delete because afterwards there are none. */
        std::string contents_;

        /** @brief The sidecar's bytes, so a hand-edited sidecar comes back as it was. */
        std::string sidecar_;
        bool hadSidecar_ = false;

        std::string error_;
        bool valid_ = false;
    };

    /**
     * @brief Sets one importer setting on one asset, and rewrites its sidecar.
     *
     * Merges on asset + setting, so dragging a slider produces one undo entry that returns to the
     * value the drag started from -- the same policy the inspector's scene properties use.
     */
    /**
     * @brief Writes a `.cnamaterial` file (plan.md ED-403).
     *
     * A command, like every other document change (D-06), even though what it edits is a file
     * rather than the open scene. The precedent is ED-300's prefab Apply, which also writes a file
     * and also undoes: an editor where some edits undo and others quietly do not is worse than one
     * where nothing does, because the user has to remember which is which.
     *
     * Undo rewrites the previous contents rather than deleting the file, which is the only correct
     * answer for an *edit*. Deleting would be right for a create and wrong here, and the two are
     * distinguishable: `existedBefore` records which this was.
     *
     * Merges per field, so dragging a colour is one undo entry and changing the roughness
     * afterwards is a second -- the same bargain `SetSceneEnvironmentCommand` strikes.
     */
    class SetMaterialCommand final : public StudioCommand
    {
    public:
        SetMaterialCommand(std::string absolutePath, MaterialDocument material,
                           std::string fieldName);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override;
        bool mergeWith(const StudioCommand& newer) override;

        /** @brief False when the file could not be written, so the caller can say so. */
        [[nodiscard]] bool succeeded() const { return succeeded_; }

    private:
        std::string absolutePath_;
        MaterialDocument newMaterial_;

        /** @brief The bytes that were there before, replayed verbatim by undo. */
        std::string previousText_;
        bool existedBefore_ = false;
        bool succeeded_ = false;
        std::string fieldName_;
    };

    class SetImporterSettingCommand final : public StudioCommand
    {
    public:
        SetImporterSettingCommand(AssetDatabase& assets,
                                  Uuid assetId,
                                  std::string settingName,
                                  PropertyValue newValue);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override;
        bool mergeWith(const StudioCommand& newer) override;

        /** @brief Returns false when the asset is unknown. */
        [[nodiscard]] bool isValid() const { return valid_; }

    private:
        /** @brief Writes @p value into the record and persists the sidecar. */
        void apply(const PropertyValue& value) const;

        AssetDatabase* assets_;
        Uuid assetId_;
        std::string settingName_;
        PropertyValue newValue_;
        PropertyValue oldValue_;
        bool hadOldValue_ = false;
        bool valid_ = false;
    };
}
