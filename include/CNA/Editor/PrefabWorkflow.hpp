// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/PrefabWorkflow.hpp
 * @brief The prefab operations that touch files and the asset database.
 *
 * plan.md ED-300, upper half. `Scene/PrefabCommands.hpp` holds everything that works on documents
 * alone; these two need the `.cnaprefab` file and its asset record as well, so they live above both
 * `cna-editor-scene` and `cna-editor-assets` -- neither of which may depend on the other.
 *
 * Both write the file as part of executing, and undo it as part of undoing. That is the same stance
 * `SetImporterSettingCommand` takes toward a sidecar: an editor where the undo stack and the
 * filesystem disagree about what exists is worse than one that does not offer undo at all.
 */

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Core/EditorCommand.hpp"
#include "CNA/Editor/Scene/EditorEntity.hpp"
#include "CNA/Editor/Scene/PrefabDocument.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /**
     * @brief Turns an entity subtree into a `.cnaprefab` and makes the original an instance of it.
     *
     * The original becoming an instance is the point. Creating a prefab that left the entity it was
     * made from unlinked would mean the very first edit after "create prefab" silently did not
     * reach the prefab -- which is how users learn not to trust the feature.
     */
    class CreatePrefabCommand final : public EditorCommand
    {
    public:
        /**
         * @param scene The scene holding the subtree; must outlive this command.
         * @param assets Where the new prefab is registered; must outlive this command.
         * @param rootId The subtree to capture.
         * @param relativeDirectory Project-relative directory for the file, e.g. "Assets/Prefabs".
         */
        CreatePrefabCommand(SceneDocument& scene,
                            AssetDatabase& assets,
                            const Uuid& rootId,
                            std::string relativeDirectory);

        /** @brief Returns false when the entity is unknown or is already a prefab instance. */
        [[nodiscard]] bool isValid() const { return valid_; }

        /** @brief Returns the project-relative path the prefab will be written to. */
        [[nodiscard]] const std::string& getRelativePath() const { return relativePath_; }

        /** @brief Returns the asset id the prefab is registered under. */
        [[nodiscard]] const Uuid& getAssetId() const { return assetId_; }

        /** @brief Returns why execute() failed, or an empty string. */
        [[nodiscard]] const std::string& getError() const { return error_; }

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

    private:
        SceneDocument* scene_;
        AssetDatabase* assets_;
        Uuid rootId_;
        Uuid assetId_;
        std::string relativePath_;
        PrefabDocument prefab_;

        /** @brief The subtree as it was before it became an instance, so undo restores it exactly. */
        std::vector<EditorEntity> before_;
        std::string error_;
        bool valid_ = false;
    };

    /**
     * @brief Writes an instance's current state back into the prefab it came from.
     *
     * The instance stops having any overrides, by construction: it *is* the prefab now. Other
     * instances of the same prefab are left alone until something reverts them -- updating every
     * instance in every scene would mean opening every scene, which an editor cannot do behind the
     * user's back.
     */
    class ApplyPrefabInstanceCommand final : public EditorCommand
    {
    public:
        ApplyPrefabInstanceCommand(SceneDocument& scene,
                                   const AssetDatabase& assets,
                                   const ComponentRegistry& registry,
                                   const Uuid& instanceRootId);

        /** @brief Returns false when the entity is not an instance, or the prefab cannot be read. */
        [[nodiscard]] bool isValid() const { return valid_; }

        /** @brief Returns why the command is invalid or failed, or an empty string. */
        [[nodiscard]] const std::string& getError() const { return error_; }

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

    private:
        bool write(const PrefabDocument& prefab);

        SceneDocument* scene_ = nullptr;
        std::string absolutePath_;

        /**
         * @brief Entities the user added, and the prefab ids they gained by being applied.
         *
         * They had no link before -- that is what made them additions -- and they need one
         * afterwards, or the very next comparison would report them as additions again against a
         * prefab that now contains them.
         */
        std::vector<std::pair<Uuid, Uuid>> newLinks_;

        PrefabDocument before_;
        PrefabDocument after_;
        std::string prefabName_;
        std::string error_;
        bool valid_ = false;
    };
}
