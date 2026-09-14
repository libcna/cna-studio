// SPDX-License-Identifier: MS-PL

#include "CNA/Editor/Assets/MeshCache.hpp"

#include "CNA/Editor/Core/PropertyValue.hpp"

namespace CNA::Editor
{
    const MeshData* MeshCache::get(const AssetDatabase& assets, const Uuid& assetId)
    {
        if (!assetId.isValid()) { return nullptr; }

        if (const auto found = entries_.find(assetId); found != entries_.end())
        {
            return found->second->usable ? &found->second->mesh : nullptr;
        }

        const AssetRecord* record = assets.find(assetId);

        // An id the database has never heard of is *not* remembered, and that is the one exception
        // to the rule below. "Not in the database" is a moving answer -- an asset being imported,
        // or a scene opened before its project finished scanning -- and caching it would leave a
        // model permanently invisible after the scan that would have found it. Re-asking costs a
        // hash lookup rather than a file read, which is the right price for not being stuck.
        if (record == nullptr) { return nullptr; }

        auto entry = std::make_unique<Entry>();

        if (record->type == AssetType::Model)
        {
            // The asset's own settings, so what the 3D view draws is the model at the size the
            // inspector says it is. Reading the file with a default scale while the sidecar says
            // 100 would put the viewport and the inspector into open disagreement about one model.
            ModelImportSettings settings;
            const JsonValue& storedScale = record->importerSettings["scaleFactor"];
            if (!storedScale.isNull())
            {
                settings.scaleFactor =
                    PropertyValue::fromJson(storedScale, PropertyType::Float).get<float>();
            }

            ModelImportResult imported = loadModel(assets.resolvePath(record->sourcePath), settings);
            if (imported.succeeded && !imported.mesh.isEmpty())
            {
                entry->mesh = std::move(imported.mesh);
                entry->usable = true;
            }
        }

        // Stored either way. An entry that failed is a remembered "no", which is what stops a
        // missing or broken file from being re-parsed once per entity per frame for as long as it
        // stays that way.
        const MeshData* result = entry->usable ? &entry->mesh : nullptr;
        entries_.emplace(assetId, std::move(entry));
        return result;
    }

    void MeshCache::invalidate(const Uuid& assetId) { entries_.erase(assetId); }

    void MeshCache::clear() { entries_.clear(); }

    std::size_t MeshCache::getVertexCount() const
    {
        std::size_t total = 0;
        for (const auto& [id, entry] : entries_)
        {
            if (entry->usable) { total += entry->mesh.getVertexCount(); }
        }
        return total;
    }
}
