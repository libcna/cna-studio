// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SceneCommands.hpp"

#include "CNA/Editor/Scene/BuiltinComponents.hpp"

#include <unordered_map>

namespace CNA::Editor
{
    namespace
    {
        /** @brief Finds a component, creating nothing. Returns nullptr when entity or type is absent. */
        EditorComponent* findComponent(SceneDocument& document, const Uuid& entityId, std::string_view typeId)
        {
            EditorEntity* entity = document.findEntity(entityId);
            return entity != nullptr ? entity->findComponent(typeId) : nullptr;
        }
    }

    CreateEntityCommand::CreateEntityCommand(SceneDocument& document, EditorEntity entity)
        : document_(&document), entity_(std::move(entity))
    {
        // The id is assigned now rather than at execute() time so that the caller can select the
        // entity straight away, and so that redo restores the *same* id -- otherwise every redo
        // would break references pointing at the entity.
        if (!entity_.getId().isValid()) { entity_.setId(Uuid::generate()); }
    }

    void CreateEntityCommand::execute()
    {
        document_->addEntity(entity_);
    }

    void CreateEntityCommand::undo()
    {
        // Removing recursively is correct even though execute() added exactly one entity: by the
        // time undo runs, later commands may have parented other entities under it. Those are
        // owned by their own commands, which will be undone after this one.
        document_->removeEntityRecursive(entity_.getId());
    }

    std::string CreateEntityCommand::getDescription() const
    {
        return "Create entity '" + entity_.getName() + "'";
    }

    DeleteEntityCommand::DeleteEntityCommand(SceneDocument& document, Uuid entityId)
        : document_(&document), entityId_(entityId)
    {
        if (const EditorEntity* entity = document_->findEntity(entityId_))
        {
            entityName_ = entity->getName();
        }
    }

    void DeleteEntityCommand::execute()
    {
        removed_ = document_->removeEntityRecursive(entityId_);
    }

    void DeleteEntityCommand::undo()
    {
        // removeEntityRecursive returns parents first, so re-adding in order never leaves a child
        // pointing at a parent that is not back yet.
        for (const EditorEntity& entity : removed_) { document_->addEntity(entity); }
        removed_.clear();
    }

    std::string DeleteEntityCommand::getDescription() const
    {
        return "Delete entity '" + entityName_ + "'";
    }

    DuplicateEntityCommand::DuplicateEntityCommand(SceneDocument& document, Uuid sourceId)
        : document_(&document), sourceId_(sourceId)
    {
        const EditorEntity* source = document_->findEntity(sourceId_);
        if (source == nullptr) { return; }

        sourceName_ = source->getName();

        // Breadth-first from the source, so the clones come out parents first and execute() can
        // add them in order without ever adding a child whose parent is not there yet.
        std::unordered_map<Uuid, Uuid> idMap;
        std::vector<Uuid> pending{sourceId_};

        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            const EditorEntity* original = document_->findEntity(pending[index]);
            if (original == nullptr) { continue; }

            EditorEntity clone = *original;
            const Uuid cloneId = Uuid::generate();
            idMap.emplace(original->getId(), cloneId);
            clone.setId(cloneId);

            if (index == 0)
            {
                // The copy is a sibling of the original, and says so in its name. Descendants keep
                // theirs: their path through the hierarchy already tells them apart.
                clone.setName(sourceName_ + " Copy");
            }
            else
            {
                // Every descendant's parent was cloned before it, so the mapping is always there.
                clone.setParentId(idMap.at(original->getParentId()));
            }

            clones_.push_back(std::move(clone));

            for (const Uuid& childId : document_->getChildren(original->getId()))
            {
                pending.push_back(childId);
            }
        }

        valid_ = !clones_.empty();
    }

    Uuid DuplicateEntityCommand::getEntityId() const
    {
        return clones_.empty() ? Uuid{} : clones_.front().getId();
    }

    void DuplicateEntityCommand::execute()
    {
        for (const EditorEntity& clone : clones_) { document_->addEntity(clone); }
    }

    void DuplicateEntityCommand::undo()
    {
        // Recursive removal from the copied root takes the whole subtree, which is exactly what
        // execute() added. The clones stay held here so redo restores the same ids.
        document_->removeEntityRecursive(getEntityId());
    }

    std::string DuplicateEntityCommand::getDescription() const
    {
        return "Duplicate entity '" + sourceName_ + "'";
    }

    RenameEntityCommand::RenameEntityCommand(SceneDocument& document, Uuid entityId, std::string newName)
        : document_(&document), entityId_(entityId), newName_(std::move(newName))
    {
        if (const EditorEntity* entity = document_->findEntity(entityId_)) { oldName_ = entity->getName(); }
    }

    void RenameEntityCommand::execute()
    {
        if (EditorEntity* entity = document_->findEntity(entityId_)) { entity->setName(newName_); }
    }

    void RenameEntityCommand::undo()
    {
        if (EditorEntity* entity = document_->findEntity(entityId_)) { entity->setName(oldName_); }
    }

    std::string RenameEntityCommand::getDescription() const
    {
        return "Rename '" + oldName_ + "' to '" + newName_ + "'";
    }

    std::string RenameEntityCommand::getMergeKey() const
    {
        return "rename:" + entityId_.toString();
    }

    bool RenameEntityCommand::mergeWith(const EditorCommand& newer)
    {
        const auto* other = dynamic_cast<const RenameEntityCommand*>(&newer);
        if (other == nullptr || other->entityId_ != entityId_) { return false; }
        // Keep this command's original name (the undo target) and adopt the newer final name.
        newName_ = other->newName_;
        return true;
    }

    ReparentEntityCommand::ReparentEntityCommand(SceneDocument& document, Uuid entityId, Uuid newParentId)
        : document_(&document), entityId_(entityId), newParentId_(newParentId)
    {
        if (const EditorEntity* entity = document_->findEntity(entityId_)) { oldParentId_ = entity->getParentId(); }
    }

    void ReparentEntityCommand::execute()
    {
        document_->reparentEntity(entityId_, newParentId_);
    }

    void ReparentEntityCommand::undo()
    {
        document_->reparentEntity(entityId_, oldParentId_);
    }

    std::string ReparentEntityCommand::getDescription() const
    {
        const EditorEntity* entity = document_->findEntity(entityId_);
        const std::string name = entity != nullptr ? entity->getName() : entityId_.toString();
        return newParentId_.isValid() ? "Reparent '" + name + "'" : "Unparent '" + name + "'";
    }

    SetPropertyCommand::SetPropertyCommand(SceneDocument& document,
                                           Uuid entityId,
                                           std::string componentTypeId,
                                           std::string propertyName,
                                           PropertyValue newValue)
        : document_(&document),
          entityId_(entityId),
          componentTypeId_(std::move(componentTypeId)),
          propertyName_(std::move(propertyName)),
          newValue_(std::move(newValue))
    {
        if (const EditorComponent* component = findComponent(*document_, entityId_, componentTypeId_))
        {
            hadOldValue_ = component->hasProperty(propertyName_);
            oldValue_ = component->getProperty(propertyName_);
        }
    }

    void SetPropertyCommand::execute()
    {
        if (EditorComponent* component = findComponent(*document_, entityId_, componentTypeId_))
        {
            component->setProperty(propertyName_, newValue_);
        }
    }

    void SetPropertyCommand::undo()
    {
        EditorComponent* component = findComponent(*document_, entityId_, componentTypeId_);
        if (component == nullptr) { return; }

        // Restoring "the property was not present at all" matters: a component loaded from a
        // scene file that omitted an optional field must be able to go back to omitting it, or
        // an undone edit would silently start writing a field the file never had.
        if (hadOldValue_) { component->setProperty(propertyName_, oldValue_); }
        else { component->removeProperty(propertyName_); }
    }

    std::string SetPropertyCommand::getDescription() const
    {
        return "Set " + componentTypeId_ + "." + propertyName_ + " = " + newValue_.toDisplayString();
    }

    std::string SetPropertyCommand::getMergeKey() const
    {
        return "property:" + entityId_.toString() + ":" + componentTypeId_ + ":" + propertyName_;
    }

    bool SetPropertyCommand::mergeWith(const EditorCommand& newer)
    {
        const auto* other = dynamic_cast<const SetPropertyCommand*>(&newer);
        if (other == nullptr) { return false; }
        if (other->entityId_ != entityId_ || other->componentTypeId_ != componentTypeId_
            || other->propertyName_ != propertyName_)
        {
            return false;
        }
        // Adopt the newer final value; keep this command's original old value as the undo target.
        newValue_ = other->newValue_;
        return true;
    }

    namespace
    {
        /** @brief Returns @p entityId's transform component, or nullptr. */
        EditorComponent* findTransform(SceneDocument& document, const Uuid& entityId)
        {
            EditorEntity* entity = document.findEntity(entityId);
            if (entity == nullptr) { return nullptr; }
            return entity->findComponent(BuiltinComponentIds::kTransform);
        }
    }

    TransformEntitiesCommand::TransformEntitiesCommand(SceneDocument& document,
                                                       std::vector<EntityTransformEdit> edits,
                                                       std::string mergeKey)
        : document_(&document), edits_(std::move(edits)), mergeKey_(std::move(mergeKey))
    {
    }

    void TransformEntitiesCommand::captureOldValues()
    {
        if (captured_) { return; }
        captured_ = true;

        // Captured at the first execute() rather than at construction, and only for the fields this
        // command actually writes: undoing must restore exactly what execute() found, and nothing
        // else.
        for (const EntityTransformEdit& edit : edits_)
        {
            const EditorComponent* transform = findTransform(*document_, edit.entityId);
            if (transform == nullptr) { continue; }

            EntityTransformEdit old;
            old.entityId = edit.entityId;
            if (edit.position) { old.position = transform->getProperty("position").get<EditorVector3>(); }
            if (edit.rotation)
            {
                old.rotation = transform->getProperty("rotation").get<EditorQuaternion>();
            }
            if (edit.scale)
            {
                old.scale = transform->getProperty("scale").get<EditorVector3>(EditorVector3{1.0f, 1.0f, 1.0f});
            }
            oldValues_.push_back(std::move(old));
        }
    }

    void TransformEntitiesCommand::execute()
    {
        captureOldValues();

        for (const EntityTransformEdit& edit : edits_)
        {
            EditorComponent* transform = findTransform(*document_, edit.entityId);
            if (transform == nullptr) { continue; }

            if (edit.position) { transform->setProperty("position", PropertyValue{*edit.position}); }
            if (edit.rotation) { transform->setProperty("rotation", PropertyValue{*edit.rotation}); }
            if (edit.scale) { transform->setProperty("scale", PropertyValue{*edit.scale}); }
        }
    }

    void TransformEntitiesCommand::undo()
    {
        for (const EntityTransformEdit& old : oldValues_)
        {
            EditorComponent* transform = findTransform(*document_, old.entityId);
            if (transform == nullptr) { continue; }

            if (old.position) { transform->setProperty("position", PropertyValue{*old.position}); }
            if (old.rotation) { transform->setProperty("rotation", PropertyValue{*old.rotation}); }
            if (old.scale) { transform->setProperty("scale", PropertyValue{*old.scale}); }
        }
    }

    std::string TransformEntitiesCommand::getDescription() const
    {
        return "Transform " + std::to_string(edits_.size()) + " entities";
    }

    bool TransformEntitiesCommand::mergeWith(const EditorCommand& newer)
    {
        const auto* other = dynamic_cast<const TransformEntitiesCommand*>(&newer);
        if (other == nullptr) { return false; }

        // Adopt the newer final values and keep this command's captured originals, so one drag
        // undoes to where it started however many frames it took. The *set* of entities is the
        // newer one's: a drag cannot change it, and if some other path ever did, following the
        // newer command is the only answer that leaves the document consistent.
        edits_ = other->edits_;
        return true;
    }

    RelinkAssetCommand::RelinkAssetCommand(SceneDocument& document, Uuid oldAssetId, Uuid newAssetId)
        : document_(&document), oldAssetId_(oldAssetId), newAssetId_(newAssetId)
    {
        if (!oldAssetId_.isValid() || oldAssetId_ == newAssetId_) { return; }

        // The targets are found once, at construction, and held. Re-scanning in execute() would
        // make redo rewrite a different set than the one undo restored, because by then the
        // properties no longer hold the old id.
        for (const EditorEntity& entity : document_->getEntities())
        {
            for (const EditorComponent& component : entity.getComponents())
            {
                for (const auto& [name, value] : component.getProperties())
                {
                    if (value.getType() != PropertyType::AssetReference) { continue; }
                    if (value.get<PropertyValue::AssetReference>().id != oldAssetId_) { continue; }

                    targets_.push_back(Target{entity.getId(), component.getTypeId(), name});
                }
            }
        }
    }

    void RelinkAssetCommand::execute()
    {
        for (const Target& target : targets_)
        {
            if (EditorComponent* component =
                    findComponent(*document_, target.entityId, target.componentTypeId))
            {
                component->setProperty(target.propertyName,
                                       PropertyValue{PropertyValue::AssetReference{newAssetId_}});
            }
        }
    }

    void RelinkAssetCommand::undo()
    {
        for (const Target& target : targets_)
        {
            if (EditorComponent* component =
                    findComponent(*document_, target.entityId, target.componentTypeId))
            {
                component->setProperty(target.propertyName,
                                       PropertyValue{PropertyValue::AssetReference{oldAssetId_}});
            }
        }
    }

    std::string RelinkAssetCommand::getDescription() const
    {
        const std::string count = std::to_string(targets_.size());
        return newAssetId_.isValid() ? "Relink " + count + " reference(s)"
                                     : "Clear " + count + " broken reference(s)";
    }

    AddComponentCommand::AddComponentCommand(SceneDocument& document,
                                             const ComponentRegistry& registry,
                                             Uuid entityId,
                                             std::string componentTypeId)
        : document_(&document), entityId_(entityId), componentTypeId_(std::move(componentTypeId))
    {
        const ComponentDescriptor* descriptor = registry.find(componentTypeId_);
        const EditorEntity* entity = document_->findEntity(entityId_);
        if (descriptor == nullptr || entity == nullptr) { return; }
        if (descriptor->unique && entity->findComponent(componentTypeId_) != nullptr) { return; }

        prototype_ = EditorComponent{componentTypeId_};
        prototype_.applyDefaults(*descriptor);
        valid_ = true;
    }

    void AddComponentCommand::execute()
    {
        if (!valid_) { return; }
        if (EditorEntity* entity = document_->findEntity(entityId_)) { entity->addComponent(prototype_); }
    }

    void AddComponentCommand::undo()
    {
        if (!valid_) { return; }
        EditorEntity* entity = document_->findEntity(entityId_);
        if (entity == nullptr) { return; }

        // Remove the *last* instance of the type, which is the one execute() appended. For a
        // unique component there is only one; for a repeatable one this is what keeps undo
        // paired with the matching add.
        for (std::size_t index = entity->getComponents().size(); index > 0; --index)
        {
            if (entity->getComponents()[index - 1].getTypeId() == componentTypeId_)
            {
                entity->removeComponentAt(index - 1);
                return;
            }
        }
    }

    std::string AddComponentCommand::getDescription() const
    {
        return "Add component " + componentTypeId_;
    }

    RemoveComponentCommand::RemoveComponentCommand(SceneDocument& document,
                                                   const ComponentRegistry& registry,
                                                   Uuid entityId,
                                                   std::string componentTypeId)
        : document_(&document), entityId_(entityId), componentTypeId_(std::move(componentTypeId))
    {
        const EditorEntity* entity = document_->findEntity(entityId_);
        if (entity == nullptr) { return; }

        const ComponentDescriptor* descriptor = registry.find(componentTypeId_);
        if (descriptor != nullptr && descriptor->required) { return; }

        const std::size_t index = entity->indexOfComponent(componentTypeId_);
        if (index == static_cast<std::size_t>(-1)) { return; }

        removedIndex_ = index;
        removed_ = entity->getComponents()[index];
        valid_ = true;
    }

    RemoveComponentCommand::RemoveComponentCommand(SceneDocument& document,
                                                   const ComponentRegistry& registry,
                                                   Uuid entityId,
                                                   std::size_t componentIndex)
        : document_(&document), entityId_(entityId)
    {
        const EditorEntity* entity = document_->findEntity(entityId_);
        if (entity == nullptr) { return; }
        if (componentIndex >= entity->getComponents().size()) { return; }

        const EditorComponent& component = entity->getComponents()[componentIndex];
        componentTypeId_ = component.getTypeId();

        const ComponentDescriptor* descriptor = registry.find(componentTypeId_);
        if (descriptor != nullptr && descriptor->required) { return; }

        removedIndex_ = componentIndex;
        removed_ = component;
        valid_ = true;
    }

    void RemoveComponentCommand::execute()
    {
        if (!valid_) { return; }
        if (EditorEntity* entity = document_->findEntity(entityId_)) { entity->removeComponentAt(removedIndex_); }
    }

    void RemoveComponentCommand::undo()
    {
        if (!valid_) { return; }
        EditorEntity* entity = document_->findEntity(entityId_);
        if (entity == nullptr) { return; }

        // Restore at the original index so the inspector's component order survives undo.
        std::vector<EditorComponent>& components = entity->getComponents();
        const std::size_t index = std::min(removedIndex_, components.size());
        components.insert(components.begin() + static_cast<std::ptrdiff_t>(index), removed_);
    }

    std::string RemoveComponentCommand::getDescription() const
    {
        return "Remove component " + componentTypeId_;
    }
}

namespace CNA::Editor
{
    SetSceneEnvironmentCommand::SetSceneEnvironmentCommand(SceneDocument& document,
                                                           SceneEnvironment environment,
                                                           std::string fieldName)
        : document_(&document), newEnvironment_(environment), oldEnvironment_(document.getEnvironment()),
          fieldName_(std::move(fieldName))
    {
    }

    void SetSceneEnvironmentCommand::execute() { document_->setEnvironment(newEnvironment_); }

    void SetSceneEnvironmentCommand::undo() { document_->setEnvironment(oldEnvironment_); }

    std::string SetSceneEnvironmentCommand::getDescription() const
    {
        return "Set " + fieldName_;
    }

    std::string SetSceneEnvironmentCommand::getMergeKey() const
    {
        // The field, not just "environment": changing the fog colour after dragging its distance
        // is two things the user did and should be two things to undo.
        return "scene-environment:" + fieldName_;
    }

    bool SetSceneEnvironmentCommand::mergeWith(const EditorCommand& newer)
    {
        const auto* other = dynamic_cast<const SetSceneEnvironmentCommand*>(&newer);
        if (other == nullptr) { return false; }

        // The newer value wins and the *older* undo state is kept, so one Ctrl+Z goes back to
        // before the whole drag rather than to the middle of it.
        newEnvironment_ = other->newEnvironment_;
        return true;
    }
}
