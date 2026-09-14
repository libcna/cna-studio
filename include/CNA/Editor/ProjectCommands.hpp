// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/ProjectCommands.hpp
 * @brief Undoable changes to the open `.cnaproject`.
 *
 * The project is a document like the scene and the asset database, so a change to it goes through
 * a command (ANALYSIS.md decision D-06). An editor where some edits undo and others quietly do not
 * is worse than one where nothing does.
 *
 * These live above both `cna-editor-project` and `cna-editor-scene` rather than inside either:
 * changing the layer list has to update the component registry as well as the project, and neither
 * of those modules may depend on the other.
 */

#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/EditorCommand.hpp"
#include "CNA/Editor/Project/Project.hpp"

namespace CNA::Editor
{
    /**
     * @brief Replaces the project's render layers.
     *
     * Also re-registers `CNA.Layer` so the inspector offers the new names, and writes the project
     * file -- the same shape as `SetImporterSettingCommand`, which persists a sidecar rather than
     * waiting for a save the user has no reason to expect.
     *
     * Entities already on a layer that has been renamed away are **not** rewritten. Which of the
     * remaining layers they belonged to is the user's decision, not the command's; scene validation
     * reports them instead (`unknown-enum-value`).
     */
    class SetProjectLayersCommand final : public EditorCommand
    {
    public:
        SetProjectLayersCommand(Project& project, ComponentRegistry& registry, std::vector<std::string> layers);

        /** @brief Returns false when the new list is empty or identical to the current one. */
        [[nodiscard]] bool isValid() const { return valid_; }

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns whether the last apply() managed to write the project file. */
        [[nodiscard]] bool wasSavedToDisk() const { return savedToDisk_; }

    private:
        void apply(const std::vector<std::string>& layers);

        Project* project_;
        ComponentRegistry* registry_;
        std::vector<std::string> newLayers_;
        std::vector<std::string> oldLayers_;
        bool valid_ = false;
        bool savedToDisk_ = false;
    };

    /**
     * @brief Sets the project's snap step, undoably.
     *
     * Beside SetProjectLayersCommand rather than in the project module, for the same reason that
     * one is here: a project edit has to reach the undo stack like every other document change
     * (D-06), and the project module cannot depend on the command history.
     *
     * Simpler than its neighbour in one respect -- a step needs no registry, since nothing but the
     * viewport reads it, and no descriptor has to be re-registered when it changes.
     */
    class SetProjectGridSnapCommand final : public EditorCommand
    {
    public:
        SetProjectGridSnapCommand(Project& project, float step);

        /** @brief Returns false when the step is negative or already the current one. */
        [[nodiscard]] bool isValid() const { return valid_; }

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns whether the last apply() managed to write the project file. */
        [[nodiscard]] bool wasSavedToDisk() const { return savedToDisk_; }

    private:
        void apply(float step);

        Project* project_;
        float newStep_ = 0.0f;
        float oldStep_ = 0.0f;
        bool valid_ = false;
        bool savedToDisk_ = false;
    };
}