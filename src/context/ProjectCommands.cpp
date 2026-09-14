// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/ProjectCommands.hpp"

#include "CNA/Editor/Scene/BuiltinComponents.hpp"

namespace CNA::Editor
{
    SetProjectLayersCommand::SetProjectLayersCommand(Project& project,
                                                     ComponentRegistry& registry,
                                                     std::vector<std::string> layers)
        : project_(&project),
          registry_(&registry),
          newLayers_(std::move(layers)),
          oldLayers_(project.getLayers())
    {
        // An empty list would leave nothing for an entity to be on. An unchanged list would put an
        // entry in the undo history that undoes to the state it is already in, which reads to the
        // user as a broken Ctrl+Z.
        valid_ = !newLayers_.empty() && newLayers_ != oldLayers_;
    }

    void SetProjectLayersCommand::execute()
    {
        if (valid_) { apply(newLayers_); }
    }

    void SetProjectLayersCommand::undo()
    {
        if (valid_) { apply(oldLayers_); }
    }

    void SetProjectLayersCommand::apply(const std::vector<std::string>& layers)
    {
        project_->setLayers(layers);
        applyProjectLayers(*registry_, layers);

        // Written through, like an importer setting. A project change that lived only in memory
        // would be lost by a crash the recovery snapshot cannot help with -- that snapshot holds
        // the scene, not the project. A failed write is left to the caller to notice: refusing the
        // edit over it would leave the registry and the project disagreeing about what exists.
        savedToDisk_ = project_->saveToFile();
    }

    std::string SetProjectLayersCommand::getDescription() const
    {
        if (newLayers_.size() > oldLayers_.size()) { return "Add layer"; }
        if (newLayers_.size() < oldLayers_.size()) { return "Remove layer"; }
        return "Rename layer";
    }

    SetProjectGridSnapCommand::SetProjectGridSnapCommand(Project& project, float step)
        : project_(&project), newStep_(step), oldStep_(project.getGridSnap())
    {
        // An unchanged value would put an entry in the history that undoes to the state it is
        // already in, which reads to the user as a broken Ctrl+Z.
        valid_ = step >= 0.0f && step != oldStep_;
    }

    void SetProjectGridSnapCommand::execute()
    {
        if (valid_) { apply(newStep_); }
    }

    void SetProjectGridSnapCommand::undo()
    {
        if (valid_) { apply(oldStep_); }
    }

    void SetProjectGridSnapCommand::apply(float step)
    {
        project_->setGridSnap(step);

        // Written through, like the layer list beside it and for the same reason: the recovery
        // snapshot holds the scene, not the project, so a project change that lived only in
        // memory is a project change a crash loses entirely.
        savedToDisk_ = project_->saveToFile();
    }

    std::string SetProjectGridSnapCommand::getDescription() const
    {
        return newStep_ > 0.0f ? "Set grid snap" : "Use the visible grid for snapping";
    }
}
