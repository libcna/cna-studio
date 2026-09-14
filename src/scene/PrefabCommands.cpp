// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/PrefabCommands.hpp"

#include <algorithm>
#include <unordered_map>

#include "CNA/Editor/Scene/EntityJson.hpp"
#include "CNA/Editor/Scene/PrefabDocument.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns the prefab entity @p entity was instantiated from, or the nil Uuid. */
        Uuid linkedPrefabEntity(const EditorEntity& entity)
        {
            const auto found = entity.getEditorState().find(PrefabKeys::kPrefabEntity);
            if (found == entity.getEditorState().end()) { return Uuid{}; }
            return Uuid::parse(found->second.get<std::string>());
        }

        /** @brief Collects @p rootId and every descendant, parents first. */
        std::vector<Uuid> collectSubtree(const SceneDocument& scene, const Uuid& rootId)
        {
            std::vector<Uuid> ids;
            if (scene.findEntity(rootId) == nullptr) { return ids; }

            ids.push_back(rootId);
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                for (const Uuid& childId : scene.getChildren(ids[index])) { ids.push_back(childId); }
            }
            return ids;
        }

        /**
         * @brief Returns true when the two subtrees are not byte-identical once serialised.
         *
         * Compared through the shared entity codec rather than field by field: that codec is what
         * decides what an entity *is* on disk, so anything it does not write is by definition not
         * a difference worth an undo entry.
         */
        bool subtreeDiffers(const std::vector<EditorEntity>& left, const std::vector<EditorEntity>& right)
        {
            if (left.size() != right.size()) { return true; }

            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (Json::write(entityToJson(left[index]), false)
                    != Json::write(entityToJson(right[index]), false))
                {
                    return true;
                }
            }
            return false;
        }

        /** @brief Returns the union of the property names on @p left and @p right. */
        std::vector<std::string> unionOfPropertyNames(const EditorComponent& left,
                                                      const EditorComponent& right)
        {
            std::vector<std::string> names;
            for (const auto& [name, value] : left.getProperties())
            {
                (void)value;
                names.push_back(name);
            }
            for (const auto& [name, value] : right.getProperties())
            {
                (void)value;
                if (std::find(names.begin(), names.end(), name) == names.end()) { names.push_back(name); }
            }
            std::sort(names.begin(), names.end());
            return names;
        }
    }

    const char* toString(PrefabOverride::Kind kind)
    {
        switch (kind)
        {
            case PrefabOverride::Kind::Property: return "changed";
            case PrefabOverride::Kind::AddedEntity: return "added";
            case PrefabOverride::Kind::RemovedEntity: return "removed";
        }
        return "changed";
    }

    Uuid getPrefabAssetOf(const SceneDocument& scene, const Uuid& entityId)
    {
        const EditorEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr) { return Uuid{}; }

        const auto found = entity->getEditorState().find(PrefabKeys::kPrefabAsset);
        if (found == entity->getEditorState().end()) { return Uuid{}; }
        return Uuid::parse(found->second.get<std::string>());
    }

    Uuid findInstanceRoot(const SceneDocument& scene, const Uuid& entityId)
    {
        Uuid current = entityId;

        // Bounded by the entity count: the scene forbids cycles, but a bound costs nothing and
        // turns a corrupted document into a wrong answer rather than a hang.
        for (std::size_t step = 0; step <= scene.getEntityCount(); ++step)
        {
            if (!current.isValid()) { return Uuid{}; }
            if (getPrefabAssetOf(scene, current).isValid()) { return current; }

            const EditorEntity* entity = scene.findEntity(current);
            if (entity == nullptr) { return Uuid{}; }
            current = entity->getParentId();
        }
        return Uuid{};
    }

    std::vector<PrefabOverride> findPrefabOverrides(const SceneDocument& scene,
                                                    const Uuid& instanceRootId,
                                                    const PrefabDocument& prefab,
                                                    const ComponentRegistry& registry)
    {
        std::vector<PrefabOverride> overrides;

        const std::vector<Uuid> subtree = collectSubtree(scene, instanceRootId);
        if (subtree.empty()) { return overrides; }

        std::vector<Uuid> seenPrefabEntities;

        for (const Uuid& entityId : subtree)
        {
            const EditorEntity* instance = scene.findEntity(entityId);
            if (instance == nullptr) { continue; }

            const Uuid prefabEntityId = linkedPrefabEntity(*instance);
            const EditorEntity* original =
                prefabEntityId.isValid() ? prefab.findEntity(prefabEntityId) : nullptr;

            if (original == nullptr)
            {
                // No link, or a link to an entity the prefab no longer has. Either way the user is
                // looking at something the prefab does not describe.
                PrefabOverride added;
                added.kind = PrefabOverride::Kind::AddedEntity;
                added.entityId = entityId;
                added.entityName = instance->getName();
                overrides.push_back(std::move(added));
                continue;
            }

            seenPrefabEntities.push_back(prefabEntityId);

            if (instance->getName() != original->getName())
            {
                PrefabOverride renamed;
                renamed.entityId = entityId;
                renamed.entityName = instance->getName();
                renamed.propertyName = "name";
                renamed.instanceValue = PropertyValue{instance->getName()};
                renamed.prefabValue = PropertyValue{original->getName()};
                overrides.push_back(std::move(renamed));
            }

            for (const EditorComponent& component : instance->getComponents())
            {
                const EditorComponent* originalComponent = original->findComponent(component.getTypeId());
                if (originalComponent == nullptr)
                {
                    // A component the instance has and the prefab does not. Reported as a property
                    // override on the component itself rather than as a fourth kind: what the user
                    // does about it -- revert, or apply -- is the same either way.
                    PrefabOverride extra;
                    extra.entityId = entityId;
                    extra.entityName = instance->getName();
                    extra.componentTypeId = component.getTypeId();
                    extra.propertyName = "<component>";
                    overrides.push_back(std::move(extra));
                    continue;
                }

                // Through the descriptor on both sides: "unset" and "set to the default" are
                // deliberately indistinguishable in the document model, so comparing stored values
                // alone would report an override every time one side had written a default out and
                // the other had not -- which is exactly what a round trip through a file does.
                const ComponentDescriptor* descriptor = registry.find(component.getTypeId());

                for (const std::string& name : unionOfPropertyNames(component, *originalComponent))
                {
                    const PropertyValue mine = component.getPropertyOrDefault(name, descriptor);
                    const PropertyValue theirs = originalComponent->getPropertyOrDefault(name, descriptor);
                    if (mine == theirs) { continue; }

                    PrefabOverride changed;
                    changed.entityId = entityId;
                    changed.entityName = instance->getName();
                    changed.componentTypeId = component.getTypeId();
                    changed.propertyName = name;
                    changed.instanceValue = mine;
                    changed.prefabValue = theirs;
                    overrides.push_back(std::move(changed));
                }
            }

            for (const EditorComponent& original2 : original->getComponents())
            {
                if (instance->findComponent(original2.getTypeId()) != nullptr) { continue; }

                PrefabOverride missing;
                missing.entityId = entityId;
                missing.entityName = instance->getName();
                missing.componentTypeId = original2.getTypeId();
                missing.propertyName = "<component removed>";
                overrides.push_back(std::move(missing));
            }
        }

        for (const EditorEntity& original : prefab.getEntities())
        {
            if (std::find(seenPrefabEntities.begin(), seenPrefabEntities.end(), original.getId())
                != seenPrefabEntities.end())
            {
                continue;
            }

            PrefabOverride removed;
            removed.kind = PrefabOverride::Kind::RemovedEntity;
            removed.entityName = original.getName();
            overrides.push_back(std::move(removed));
        }

        return overrides;
    }

    InstantiatePrefabCommand::InstantiatePrefabCommand(SceneDocument& document,
                                                       const PrefabDocument& prefab,
                                                       Uuid prefabAssetId,
                                                       Uuid parentId)
        : document_(&document), prefabName_(prefab.getName())
    {
        if (prefab.isEmpty()) { return; }
        if (parentId.isValid() && document.findEntity(parentId) == nullptr) { return; }

        // Fresh ids: two instances of one prefab are two different entities, and reusing the
        // prefab's ids would make the second instantiation collide with the first.
        std::unordered_map<Uuid, Uuid> idByPrefabId;
        for (const EditorEntity& original : prefab.getEntities())
        {
            idByPrefabId.emplace(original.getId(), Uuid::generate());
        }

        for (const EditorEntity& original : prefab.getEntities())
        {
            EditorEntity copy = original;
            copy.setId(idByPrefabId.at(original.getId()));

            const auto mappedParent = idByPrefabId.find(original.getParentId());
            copy.setParentId(mappedParent != idByPrefabId.end() ? mappedParent->second : parentId);

            // The link, on every entity, so an override can be found later without depending on
            // names or on sibling order -- both of which the user is free to change.
            copy.setEditorState(PrefabKeys::kPrefabEntity, PropertyValue{original.getId().toString()});

            if (original.getId() == prefab.getRootId())
            {
                copy.setEditorState(PrefabKeys::kPrefabAsset, PropertyValue{prefabAssetId.toString()});
                rootId_ = copy.getId();
            }

            entities_.push_back(std::move(copy));
        }

        valid_ = !entities_.empty();
    }

    void InstantiatePrefabCommand::execute()
    {
        if (!valid_) { return; }

        // Parents first, which captureFromScene guaranteed, so every parent exists by the time its
        // children are added.
        for (const EditorEntity& entity : entities_) { document_->addEntity(entity); }
    }

    void InstantiatePrefabCommand::undo()
    {
        if (!valid_) { return; }
        document_->removeEntityRecursive(rootId_);
    }

    std::string InstantiatePrefabCommand::getDescription() const
    {
        return "Instantiate prefab '" + prefabName_ + "'";
    }

    RevertPrefabInstanceCommand::RevertPrefabInstanceCommand(SceneDocument& document,
                                                             const Uuid& instanceRootId,
                                                             const PrefabDocument& prefab)
        : document_(&document), rootId_(instanceRootId), prefabName_(prefab.getName())
    {
        const EditorEntity* root = document.findEntity(instanceRootId);
        if (root == nullptr || prefab.isEmpty()) { return; }
        if (!getPrefabAssetOf(document, instanceRootId).isValid()) { return; }

        for (const Uuid& entityId : collectSubtree(document, instanceRootId))
        {
            if (const EditorEntity* entity = document.findEntity(entityId)) { before_.push_back(*entity); }
        }

        // Reuse the instance's own entity ids wherever the link still resolves. Anything else would
        // break every reference into the instance -- an EntityReference on a sibling, the current
        // selection -- for entities that did not actually change identity.
        std::unordered_map<Uuid, Uuid> idByPrefabId;
        for (const EditorEntity& existing : before_)
        {
            const Uuid prefabEntityId = linkedPrefabEntity(existing);
            if (prefabEntityId.isValid() && prefab.findEntity(prefabEntityId) != nullptr)
            {
                idByPrefabId.emplace(prefabEntityId, existing.getId());
            }
        }
        for (const EditorEntity& original : prefab.getEntities())
        {
            idByPrefabId.emplace(original.getId(), Uuid::generate());
        }

        const Uuid parentOfRoot = root->getParentId();
        const Uuid prefabAssetId = getPrefabAssetOf(document, instanceRootId);

        for (const EditorEntity& original : prefab.getEntities())
        {
            EditorEntity copy = original;
            copy.setId(idByPrefabId.at(original.getId()));

            const auto mappedParent = idByPrefabId.find(original.getParentId());
            copy.setParentId(mappedParent != idByPrefabId.end() ? mappedParent->second : parentOfRoot);

            copy.setEditorState(PrefabKeys::kPrefabEntity, PropertyValue{original.getId().toString()});
            if (original.getId() == prefab.getRootId())
            {
                copy.setEditorState(PrefabKeys::kPrefabAsset, PropertyValue{prefabAssetId.toString()});
            }

            after_.push_back(std::move(copy));
        }

        // The root has to keep its id whatever else happens: it is what the selection, the
        // hierarchy's expansion state and any reference to the instance point at.
        if (!after_.empty() && after_.front().getId() != instanceRootId)
        {
            const Uuid oldRootId = after_.front().getId();
            after_.front().setId(instanceRootId);
            for (EditorEntity& entity : after_)
            {
                if (entity.getParentId() == oldRootId) { entity.setParentId(instanceRootId); }
            }
        }

        valid_ = !after_.empty() && subtreeDiffers(before_, after_);
    }

    void RevertPrefabInstanceCommand::replaceSubtree(const std::vector<EditorEntity>& entities)
    {
        document_->removeEntityRecursive(rootId_);
        for (const EditorEntity& entity : entities) { document_->addEntity(entity); }
    }

    void RevertPrefabInstanceCommand::execute()
    {
        if (valid_) { replaceSubtree(after_); }
    }

    void RevertPrefabInstanceCommand::undo()
    {
        if (valid_) { replaceSubtree(before_); }
    }

    std::string RevertPrefabInstanceCommand::getDescription() const
    {
        return "Revert prefab instance '" + prefabName_ + "'";
    }
}
