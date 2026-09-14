// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Assets/AssetCommands.hpp
 * @brief Undoable operations on the asset database.
 *
 * The asset database is a document like the scene, so changes to it are commands and go through
 * the same history (ANALYSIS.md decision D-06). An editor where some edits undo and others quietly
 * do not is worse than one where nothing undoes: the user cannot tell which they are looking at
 * until Ctrl+Z does the wrong thing.
 */

#include <string>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Assets/MaterialDocument.hpp"
#include "CNA/Editor/Core/EditorCommand.hpp"
#include "CNA/Editor/Core/PropertyValue.hpp"

namespace CNA::Editor
{
    /**
     * @brief Moves or renames an asset's file, keeping its id.
     *
     * Undoable like every other document change (D-06), and undo is simply the move back. No scene
     * is touched in either direction, which is the property that makes an asset folder safe to
     * tidy: references are Uuids, so where a file sits is not something a scene knows.
     */
    class MoveAssetCommand final : public EditorCommand
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
    class SetMaterialCommand final : public EditorCommand
    {
    public:
        SetMaterialCommand(std::string absolutePath, MaterialDocument material,
                           std::string fieldName);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override;
        bool mergeWith(const EditorCommand& newer) override;

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

    class SetImporterSettingCommand final : public EditorCommand
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
        bool mergeWith(const EditorCommand& newer) override;

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
