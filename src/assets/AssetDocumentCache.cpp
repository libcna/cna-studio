// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/AssetDocumentCache.hpp"

#include "CNA/Studio/Core/ComponentDescriptor.hpp"

namespace CNA::Studio
{
    StudioAssetDocumentCache::Entry& StudioAssetDocumentCache::entryFor(const AssetRecord& record,
                                                                        bool& outNeedsLoad)
    {
        Entry& entry = entries_[record.id];

        // The stamp the last scan or watcher poll saw, compared without asking the filesystem
        // anything. That is what makes this free on the frames where nothing changed, and it is
        // why the watcher is what makes an external edit visible here as well as in the browser.
        //
        // The path too: an asset that moved is the same document, but an entry keyed only on the
        // stamp would survive a move to a file that happens to be the same size.
        outNeedsLoad = !entry.loaded || entry.size != record.sourceSize
                    || entry.modifiedTime != record.sourceModifiedTime
                    || entry.sourcePath != record.sourcePath;

        if (outNeedsLoad)
        {
            entry.size = record.sourceSize;
            entry.modifiedTime = record.sourceModifiedTime;
            entry.sourcePath = record.sourcePath;
            entry.material.reset();
            entry.prefab.reset();
        }
        return entry;
    }

    const MaterialDocument* StudioAssetDocumentCache::material(const AssetDatabase& assets,
                                                               const Uuid& id)
    {
        const AssetRecord* record = assets.find(id);
        if (record == nullptr || record->type != AssetType::Material) { return nullptr; }

        bool needsLoad = false;
        Entry& entry = entryFor(*record, needsLoad);

        if (needsLoad)
        {
            auto document = std::make_unique<MaterialDocument>();
            ++fileReads_;

            // A failure is cached as a failure rather than left uncached: a broken material would
            // otherwise be reopened on every frame it is selected, which is the case this exists to
            // stop and the one most likely to be sitting on somebody's screen.
            entry.loaded = true;
            if (loadMaterialDocument(assets, id, *document) == MaterialLoadProblem::None)
            {
                entry.material = std::move(document);
            }
        }

        return entry.material.get();
    }

    const PrefabDocument* StudioAssetDocumentCache::prefab(const AssetDatabase& assets,
                                                           const Uuid& id,
                                                           const ComponentRegistry& registry)
    {
        const AssetRecord* record = assets.find(id);
        if (record == nullptr || record->type != AssetType::Prefab) { return nullptr; }

        bool needsLoad = false;
        Entry& entry = entryFor(*record, needsLoad);

        if (needsLoad)
        {
            auto document = std::make_unique<PrefabDocument>();
            ++fileReads_;

            entry.loaded = true;
            if (document->loadFromFile(assets.resolvePath(record->sourcePath), registry).succeeded)
            {
                entry.prefab = std::move(document);
            }
        }

        return entry.prefab.get();
    }

    void StudioAssetDocumentCache::invalidate(const Uuid& id)
    {
        if (id.isValid()) { entries_.erase(id); }
        else { entries_.clear(); }
    }
}
