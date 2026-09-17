// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/AssetDependencies.hpp"

#include <algorithm>
#include <functional>
#include <utility>

#include "CNA/Studio/Assets/MaterialDocument.hpp"
#include "CNA/Studio/Scene/PrefabDocument.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/StudioEntity.hpp"

namespace CNA::Studio
{
    namespace
    {
        /** @brief The last segment of a project-relative path. */
        std::string fileNameOf(const std::string& path)
        {
            const std::size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    std::string AssetUsage::describe() const
    {
        std::string text = fileNameOf(holderPath);
        if (!entityName.empty()) { text += "  ·  " + entityName; }
        if (!propertyName.empty())
        {
            // The property alone, not the component type id. "CNA.SpriteRenderer.texture" is
            // accurate and unreadable; the icon beside the row already says what kind of thing this
            // is, and two components on one entity both naming a texture is rare enough that the
            // ambiguity costs less than the noise would.
            text += "." + propertyName;
        }
        return text;
    }

    void AssetDependencyIndex::clear()
    {
        usagesByAsset_.clear();
        referencesByHolder_.clear();
    }

    void AssetDependencyIndex::add(const Uuid& target, AssetUsage usage)
    {
        if (!target.isValid()) { return; }

        std::vector<Uuid>& outgoing = referencesByHolder_[usage.holderId];
        if (std::find(outgoing.begin(), outgoing.end(), target) == outgoing.end())
        {
            outgoing.push_back(target);
        }

        usagesByAsset_[target].push_back(std::move(usage));
    }

    void AssetDependencyIndex::forget(const Uuid& holderId)
    {
        referencesByHolder_.erase(holderId);

        // Walked rather than indexed by holder as well: a third map kept in step with the two that
        // matter is a third thing to get wrong, and forgetting a holder happens once per edited
        // scene rather than once per lookup.
        for (auto entry = usagesByAsset_.begin(); entry != usagesByAsset_.end();)
        {
            std::vector<AssetUsage>& usages = entry->second;
            usages.erase(std::remove_if(usages.begin(), usages.end(),
                                        [&holderId](const AssetUsage& usage) {
                                            return usage.holderId == holderId;
                                        }),
                         usages.end());

            if (usages.empty()) { entry = usagesByAsset_.erase(entry); }
            else { ++entry; }
        }
    }

    namespace
    {
        /**
         * @brief Records every asset reference on @p entities against @p holder.
         *
         * The entities' *stored* properties rather than their descriptors': a component whose
         * plugin failed to load keeps its data, and that file is the one most likely to be broken.
         */
        std::size_t collectEntityReferences(const std::vector<StudioEntity>& entities,
                                            const AssetUsage& holder,
                                            const std::function<void(const Uuid&, AssetUsage)>& add)
        {
            std::size_t found = 0;
            for (const StudioEntity& entity : entities)
            {
                for (const StudioComponent& component : entity.getComponents())
                {
                    for (const auto& [name, value] : component.getProperties())
                    {
                        if (value.getType() != PropertyType::AssetReference) { continue; }

                        const Uuid target = value.get<PropertyValue::AssetReference>().id;

                        // A nil reference is an empty slot, not an edge. A sprite that has not been
                        // given a texture yet points at nothing, and a graph that recorded it would
                        // answer "what uses nothing?" with the whole project.
                        if (!target.isValid()) { continue; }

                        AssetUsage usage = holder;
                        usage.entityId = entity.getId();
                        usage.entityName = entity.getName();
                        usage.componentTypeId = component.getTypeId();
                        usage.propertyName = name;
                        add(target, std::move(usage));
                        ++found;
                    }
                }
            }
            return found;
        }
    }

    AssetDependencyScan AssetDependencyIndex::build(const AssetDatabase& assets,
                                                    const ComponentRegistry& registry)
    {
        clear();

        AssetDependencyScan scan;
        const auto record = [this](const Uuid& target, AssetUsage usage) {
            add(target, std::move(usage));
        };

        for (const AssetRecord* asset : assets.getAll())
        {
            AssetUsage holder;
            holder.holderId = asset->id;
            holder.holderPath = asset->sourcePath;
            holder.holderType = asset->type;

            // What the importer said this asset came with -- a model naming its textures. Recorded
            // before the file is read, because it is true whether or not the file can be.
            for (const Uuid& dependency : asset->dependencies)
            {
                if (dependency == asset->id) { continue; }
                AssetUsage usage = holder;
                add(dependency, std::move(usage));
                ++scan.referencesFound;
            }

            const std::string absolute = assets.resolvePath(asset->sourcePath);

            switch (asset->type)
            {
                case AssetType::Scene:
                {
                    SceneDocument scene;
                    const SceneLoadResult loaded = scene.loadFromFile(absolute, registry);
                    if (!loaded.succeeded)
                    {
                        scan.warnings.push_back("could not read scene '" + asset->sourcePath
                                                + "': " + loaded.errorMessage);
                        break;
                    }
                    ++scan.filesRead;
                    scan.referencesFound +=
                        collectEntityReferences(scene.getEntities(), holder, record);
                    break;
                }
                case AssetType::Prefab:
                {
                    PrefabDocument prefab;
                    const PrefabLoadResult loaded = prefab.loadFromFile(absolute, registry);
                    if (!loaded.succeeded)
                    {
                        scan.warnings.push_back("could not read prefab '" + asset->sourcePath
                                                + "': " + loaded.errorMessage);
                        break;
                    }
                    ++scan.filesRead;
                    scan.referencesFound +=
                        collectEntityReferences(prefab.getEntities(), holder, record);
                    break;
                }
                case AssetType::Material:
                {
                    MaterialDocument material;
                    if (loadMaterialDocument(assets, asset->id, material)
                        != MaterialLoadProblem::None)
                    {
                        scan.warnings.push_back("could not read material '" + asset->sourcePath
                                                + "'");
                        break;
                    }
                    ++scan.filesRead;

                    for (const auto& [field, target] :
                         {std::pair{"diffuseTexture", material.diffuseTexture},
                          std::pair{"normalTexture", material.normalTexture},
                          std::pair{"metallicRoughnessTexture", material.metallicRoughnessTexture},
                          std::pair{"emissiveTexture", material.emissiveTexture}})
                    {
                        if (!target.isValid()) { continue; }
                        AssetUsage usage = holder;
                        usage.propertyName = field;
                        add(target, std::move(usage));
                        ++scan.referencesFound;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // Ordered by where the reference is rather than by when it was found, so the list a user
        // reads is stable across rebuilds and groups the references that are in one file together.
        for (auto& [id, usages] : usagesByAsset_)
        {
            (void)id;
            std::stable_sort(usages.begin(), usages.end(),
                             [](const AssetUsage& a, const AssetUsage& b) {
                                 return a.holderPath < b.holderPath;
                             });
        }

        return scan;
    }

    void AssetDependencyIndex::observeScene(const SceneDocument& scene, const Uuid& sceneAssetId,
                                            const std::string& sourcePath)
    {
        // An unsaved new scene is not yet an asset anything can reference, and giving its
        // references a nil holder would file them all under one key that means "nowhere".
        if (!sceneAssetId.isValid()) { return; }

        forget(sceneAssetId);

        AssetUsage holder;
        holder.holderId = sceneAssetId;
        holder.holderPath = sourcePath;
        holder.holderType = AssetType::Scene;

        (void)collectEntityReferences(scene.getEntities(), holder,
                                      [this](const Uuid& target, AssetUsage usage) {
                                          add(target, std::move(usage));
                                      });

        for (auto& [id, usages] : usagesByAsset_)
        {
            (void)id;
            std::stable_sort(usages.begin(), usages.end(),
                             [](const AssetUsage& a, const AssetUsage& b) {
                                 return a.holderPath < b.holderPath;
                             });
        }
    }

    std::vector<AssetUsage> AssetDependencyIndex::referencedBy(const Uuid& id) const
    {
        const auto found = usagesByAsset_.find(id);
        return found == usagesByAsset_.end() ? std::vector<AssetUsage>{} : found->second;
    }

    std::vector<Uuid> AssetDependencyIndex::referencesTo(const Uuid& id) const
    {
        const auto found = referencesByHolder_.find(id);
        return found == referencesByHolder_.end() ? std::vector<Uuid>{} : found->second;
    }
}
