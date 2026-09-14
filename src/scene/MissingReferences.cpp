// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/MissingReferences.hpp"

#include <algorithm>

#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    const char* toString(MissingReference::Reason reason)
    {
        switch (reason)
        {
            case MissingReference::Reason::NotInDatabase: return "not in the asset database";
            case MissingReference::Reason::FileMissing: return "source file is gone";
        }
        return "unresolvable";
    }

    std::vector<MissingReference> findMissingReferences(const SceneDocument& scene,
                                                        const AssetDatabase& assets)
    {
        std::vector<MissingReference> missing;

        for (const EditorEntity& entity : scene.getEntities())
        {
            for (const EditorComponent& component : entity.getComponents())
            {
                // The entity's *stored* properties, not its descriptor's. A component whose plugin
                // failed to load keeps its data and gets its references checked like any other --
                // and that scene is the one most likely to be broken.
                for (const auto& [name, value] : component.getProperties())
                {
                    if (value.getType() != PropertyType::AssetReference) { continue; }

                    const Uuid assetId = value.get<PropertyValue::AssetReference>().id;

                    // A nil reference is an empty slot, not a broken one. A sprite that has not
                    // been given a texture yet is an ordinary state, not a fault to report.
                    if (!assetId.isValid()) { continue; }

                    if (assets.find(assetId) == nullptr)
                    {
                        missing.push_back(MissingReference{entity.getId(), entity.getName(),
                                                           component.getTypeId(), name, assetId,
                                                           MissingReference::Reason::NotInDatabase});
                    }
                    else if (assets.isMissing(assetId))
                    {
                        missing.push_back(MissingReference{entity.getId(), entity.getName(),
                                                           component.getTypeId(), name, assetId,
                                                           MissingReference::Reason::FileMissing});
                    }
                }
            }
        }

        return missing;
    }

    std::vector<Uuid> collectMissingAssetIds(const std::vector<MissingReference>& references)
    {
        // First-seen order rather than sorted: the report lists references in document order, and
        // a grouping that jumped around relative to the list under it would be harder to read than
        // one that simply follows it.
        std::vector<Uuid> ids;
        for (const MissingReference& reference : references)
        {
            if (std::find(ids.begin(), ids.end(), reference.assetId) == ids.end())
            {
                ids.push_back(reference.assetId);
            }
        }
        return ids;
    }
}
