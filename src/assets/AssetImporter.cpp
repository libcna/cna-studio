// SPDX-License-Identifier: MS-PL
/**
 * @file AssetImporter.cpp
 * @brief The importer registry.
 *
 * Only the registry. The built-in importers live in `AssetImporters.cpp`, beside the readers and
 * the libraries they need -- which is the whole point of the interface: nothing above it names a
 * parser.
 *
 * `plan.md` STUDIO-10002.
 */

#include "CNA/Studio/Assets/AssetImporter.hpp"


namespace CNA::Studio
{
    bool StudioImporterRegistry::add(std::unique_ptr<StudioAssetImporter> importer)
    {
        if (!importer) { return false; }

        // A duplicate id is refused rather than replacing: two importers answering to one id is a
        // build that behaves differently depending on which was registered last, and a plugin
        // colliding with a built-in should be told rather than quietly winning.
        if (find(importer->id()) != nullptr) { return false; }

        importers_.push_back(std::move(importer));
        return true;
    }

    const StudioAssetImporter* StudioImporterRegistry::forType(AssetType type) const
    {
        // First claim wins, in registration order. Deterministic on purpose: an ambiguity resolved
        // by hash order is one that behaves differently on another machine.
        for (const std::unique_ptr<StudioAssetImporter>& importer : importers_)
        {
            if (importer->handles(type)) { return importer.get(); }
        }
        return nullptr;
    }

    const StudioAssetImporter* StudioImporterRegistry::find(std::string_view importerId) const
    {
        for (const std::unique_ptr<StudioAssetImporter>& importer : importers_)
        {
            if (importer->id() == importerId) { return importer.get(); }
        }
        return nullptr;
    }

    std::vector<std::string> StudioImporterRegistry::getIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(importers_.size());
        for (const std::unique_ptr<StudioAssetImporter>& importer : importers_)
        {
            ids.emplace_back(importer->id());
        }
        return ids;
    }

}
