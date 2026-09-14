// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/PrefabWorkflow.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <system_error>

#include "CNA/Editor/Scene/PrefabCommands.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns @p name with anything a file name cannot carry replaced by '-'. */
        std::string toFileName(std::string_view name)
        {
            std::string sanitised;
            for (const char character : name)
            {
                const bool unsafe = character == '/' || character == '\\' || character == ':'
                                 || character == '*' || character == '?' || character == '"'
                                 || character == '<' || character == '>' || character == '|'
                                 || static_cast<unsigned char>(character) < 0x20;
                sanitised += unsafe ? '-' : character;
            }

            // An entity may legitimately be called "  " or "". A file may not.
            const std::size_t first = sanitised.find_first_not_of(" \t");
            const std::size_t last = sanitised.find_last_not_of(" \t.");
            if (first == std::string::npos || last == std::string::npos) { return "Prefab"; }

            sanitised = sanitised.substr(first, last - first + 1);
            return sanitised.empty() ? "Prefab" : sanitised;
        }

        /**
         * @brief Returns a project-relative path in @p directory that no file occupies.
         *
         * Suffixed rather than overwritten. "Create prefab" that silently replaced the prefab of
         * the same name would destroy work in a way undo could not fully repair -- the file's
         * previous contents are not in the undo stack.
         */
        std::string uniquePath(const AssetDatabase& assets,
                               const std::string& directory,
                               const std::string& baseName)
        {
            for (int attempt = 1; attempt < 1000; ++attempt)
            {
                const std::string suffix = attempt == 1 ? "" : " " + std::to_string(attempt);
                const std::string candidate =
                    directory + "/" + baseName + suffix + PrefabDocument::kExtension;

                std::error_code errorCode;
                if (!std::filesystem::exists(assets.resolvePath(candidate), errorCode)
                    && assets.findByPath(candidate) == nullptr)
                {
                    return candidate;
                }
            }
            return {};
        }

        /** @brief Collects @p rootId and every descendant from @p scene, parents first. */
        std::vector<EditorEntity> copySubtree(const SceneDocument& scene, const Uuid& rootId)
        {
            std::vector<Uuid> ids{rootId};
            std::vector<EditorEntity> entities;

            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                const EditorEntity* entity = scene.findEntity(ids[index]);
                if (entity == nullptr) { continue; }

                entities.push_back(*entity);
                for (const Uuid& childId : scene.getChildren(ids[index])) { ids.push_back(childId); }
            }
            return entities;
        }
    }

    CreatePrefabCommand::CreatePrefabCommand(SceneDocument& scene,
                                             AssetDatabase& assets,
                                             const Uuid& rootId,
                                             std::string relativeDirectory)
        : scene_(&scene), assets_(&assets), rootId_(rootId)
    {
        const EditorEntity* root = scene.findEntity(rootId);
        if (root == nullptr)
        {
            error_ = "no such entity";
            return;
        }

        // Nesting a prefab inside a prefab is a real feature and a large one; refusing it plainly
        // is better than producing a file whose instances cannot be reverted correctly.
        if (findInstanceRoot(scene, rootId).isValid())
        {
            error_ = "'" + root->getName() + "' is already part of a prefab instance";
            return;
        }

        if (!prefab_.captureFromScene(scene, rootId, root->getName()))
        {
            error_ = "cannot capture '" + root->getName() + "'";
            return;
        }

        relativePath_ = uniquePath(assets, relativeDirectory, toFileName(root->getName()));
        if (relativePath_.empty())
        {
            error_ = "cannot find an unused file name for '" + root->getName() + "'";
            return;
        }

        assetId_ = Uuid::generate();
        before_ = copySubtree(scene, rootId);
        valid_ = true;
    }

    void CreatePrefabCommand::execute()
    {
        if (!valid_) { return; }

        const std::string absolute = assets_->resolvePath(relativePath_);
        if (!prefab_.saveToFile(absolute, &error_)) { return; }

        AssetRecord record;
        record.id = assetId_;
        record.sourcePath = relativePath_;
        record.type = AssetType::Prefab;
        assets_->add(record);
        assets_->writeSidecar(assetId_);

        // The original becomes an instance. Creating a prefab that left the entity it was made from
        // unlinked would mean the first edit afterwards silently did not reach the prefab, which is
        // how users learn not to trust the feature.
        for (const EditorEntity& captured : prefab_.getEntities())
        {
            EditorEntity* live = scene_->findEntity(captured.getId());
            if (live == nullptr) { continue; }

            live->setEditorState(PrefabKeys::kPrefabEntity, PropertyValue{captured.getId().toString()});
            if (captured.getId() == rootId_)
            {
                live->setEditorState(PrefabKeys::kPrefabAsset, PropertyValue{assetId_.toString()});
            }
        }
    }

    void CreatePrefabCommand::undo()
    {
        if (!valid_) { return; }

        // The subtree goes back exactly as it was, which is what removes the links -- rather than
        // erasing the two keys by name and hoping nothing else touched them meanwhile.
        scene_->removeEntityRecursive(rootId_);
        for (const EditorEntity& entity : before_) { scene_->addEntity(entity); }

        assets_->removeRecord(assetId_);

        // The file and its sidecar go too. They are the ones this command wrote a moment ago and
        // nothing has referenced them since; leaving them behind would make a second "create
        // prefab" pick the suffixed name for no reason the user can see.
        std::error_code errorCode;
        const std::string absolute = assets_->resolvePath(relativePath_);
        std::filesystem::remove(absolute, errorCode);
        std::filesystem::remove(absolute + AssetDatabase::kSidecarExtension, errorCode);
    }

    std::string CreatePrefabCommand::getDescription() const
    {
        return "Create prefab '" + prefab_.getName() + "'";
    }

    ApplyPrefabInstanceCommand::ApplyPrefabInstanceCommand(SceneDocument& scene,
                                                           const AssetDatabase& assets,
                                                           const ComponentRegistry& registry,
                                                           const Uuid& instanceRootId)
        : scene_(&scene)
    {
        const Uuid assetId = getPrefabAssetOf(scene, instanceRootId);
        if (!assetId.isValid())
        {
            error_ = "not a prefab instance";
            return;
        }

        const AssetRecord* record = assets.find(assetId);
        if (record == nullptr)
        {
            error_ = "the prefab asset is no longer in the database";
            return;
        }

        absolutePath_ = assets.resolvePath(record->sourcePath);

        const PrefabLoadResult loaded = before_.loadFromFile(absolutePath_, registry);
        if (!loaded.succeeded)
        {
            error_ = "cannot read '" + record->sourcePath + "': " + loaded.errorMessage;
            return;
        }

        prefabName_ = before_.getName();

        // Captured from the *live* subtree, so this is exactly what the user is looking at. The
        // prefab keeps its own id: this is a new version of the same prefab, not a new prefab.
        if (!after_.captureFromScene(scene, instanceRootId, before_.getName()))
        {
            error_ = "cannot capture the instance";
            return;
        }
        after_.setPrefabId(before_.getPrefabId());

        // The captured entities carry the *instance's* ids. Storing them would break every link the
        // instance holds -- each one names a prefab entity, and after the write there would be no
        // entity by that name -- so every entity in the instance would read back as one the prefab
        // does not describe. Map them back through the links they already carry.
        std::unordered_map<Uuid, Uuid> prefabIdByInstanceId;
        for (const EditorEntity& entity : after_.getEntities())
        {
            const auto link = entity.getEditorState().find(PrefabKeys::kPrefabEntity);
            const Uuid linked = link != entity.getEditorState().end()
                                    ? Uuid::parse(link->second.get<std::string>())
                                    : Uuid{};

            // An entity the user added has no link and no counterpart in the prefab, so it gets a
            // fresh prefab id -- and the live entity is linked to it when this executes, or the
            // very next comparison would report it as an addition again against a prefab that now
            // contains it.
            if (linked.isValid())
            {
                prefabIdByInstanceId.emplace(entity.getId(), linked);
                continue;
            }

            const Uuid fresh = Uuid::generate();
            prefabIdByInstanceId.emplace(entity.getId(), fresh);
            newLinks_.emplace_back(entity.getId(), fresh);
        }

        std::vector<EditorEntity> mapped = after_.getEntities();
        for (EditorEntity& entity : mapped)
        {
            entity.setId(prefabIdByInstanceId.at(entity.getId()));

            const auto parent = prefabIdByInstanceId.find(entity.getParentId());
            entity.setParentId(parent != prefabIdByInstanceId.end() ? parent->second : Uuid{});

            // A prefab does not carry instance bookkeeping. Leaving it in would make every future
            // instance born claiming to be an instance of something else (D-07).
            entity.removeEditorState(PrefabKeys::kPrefabEntity);
            entity.removeEditorState(PrefabKeys::kPrefabAsset);
        }
        after_.setEntities(std::move(mapped));

        valid_ = true;
    }

    bool ApplyPrefabInstanceCommand::write(const PrefabDocument& prefab)
    {
        return prefab.saveToFile(absolutePath_, &error_);
    }

    void ApplyPrefabInstanceCommand::execute()
    {
        if (!valid_ || !write(after_)) { return; }

        for (const auto& [instanceId, prefabEntityId] : newLinks_)
        {
            if (EditorEntity* entity = scene_->findEntity(instanceId))
            {
                entity->setEditorState(PrefabKeys::kPrefabEntity, PropertyValue{prefabEntityId.toString()});
            }
        }
    }

    void ApplyPrefabInstanceCommand::undo()
    {
        if (!valid_ || !write(before_)) { return; }

        for (const auto& [instanceId, prefabEntityId] : newLinks_)
        {
            (void)prefabEntityId;
            if (EditorEntity* entity = scene_->findEntity(instanceId))
            {
                entity->removeEditorState(PrefabKeys::kPrefabEntity);
            }
        }
    }

    std::string ApplyPrefabInstanceCommand::getDescription() const
    {
        return "Apply to prefab '" + prefabName_ + "'";
    }
}
