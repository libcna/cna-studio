// SPDX-License-Identifier: MS-PL
/**
 * @file StudioAssetReload.cpp
 * @brief Noticing that an asset changed outside Studio, and letting go of what went stale.
 */

#include "CNA/Studio/StudioAssetReload.hpp"

#include "CNA/Studio/Assets/AssetImporters.hpp"
#include "CNA/Studio/Assets/AssetWatcher.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <string>

namespace CNA::Studio
{
    namespace
    {
        /** @brief The asset's source path, or its id when the record has gone with it. */
        std::string nameOf(const StudioContext& context, const Uuid& assetId)
        {
            const AssetRecord* record = context.getAssets().find(assetId);
            return record != nullptr ? record->sourcePath : assetId.toString();
        }
    }

    StudioAssetReloadResult studioPollAssetChanges(AssetWatcher& watcher, StudioContext& context,
                                                   const StudioAssetReloadSinks& sinks,
                                                   double deltaSeconds)
    {
        StudioAssetReloadResult result;

        const AssetWatchResult changes = watcher.poll(context.getAssets(), deltaSeconds);
        if (!changes.hasChanges()) { return result; }

        for (const Uuid& assetId : changes.changed)
        {
            // Dropping the cached texture is what makes the change visible. Without it the editor
            // would report the edit and go on drawing the art from before it. A mesh is the same
            // bargain in the 3D view: `MeshCache` holds an imported model until told otherwise, so
            // an edited .gltf would keep drawing the shape it had when the project was opened.
            if (sinks.invalidateRendered) { sinks.invalidateRendered(assetId); }
            context.getMeshes().invalidate(assetId);
            if (sinks.reloadInPlayer) { sinks.reloadInPlayer(assetId); }

            context.log(LogSeverity::Info,
                        "Reloaded '" + nameOf(context, assetId) + "' after an external change.");
        }

        for (const Uuid& assetId : changes.restored)
        {
            if (sinks.invalidateRendered) { sinks.invalidateRendered(assetId); }
            context.getMeshes().invalidate(assetId);
            if (sinks.reloadInPlayer) { sinks.reloadInPlayer(assetId); }

            context.log(LogSeverity::Info, "'" + nameOf(context, assetId) + "' is back.");
        }

        for (const Uuid& assetId : changes.removed)
        {
            // Forgotten rather than kept: a model whose file has gone should stop being drawn, and
            // a cache that held the last good copy would show a mesh that is no longer there.
            context.getMeshes().invalidate(assetId);

            context.log(LogSeverity::Warning,
                        "'" + nameOf(context, assetId)
                            + "' has gone missing. Anything referencing it is listed in Missing "
                              "References.");
        }

        // The pixel size of a texture that just changed is no longer the one on record -- so the
        // assets that moved are *reported* as needing a reimport rather than re-read here. Two
        // things were wrong with reading them here: it was the whole project rather than the files
        // that changed, and it was on the frame, where a glTF parse does not belong
        // (`plan.md` STUDIO-10011).
        result.needsReimport.reserve(changes.changed.size() + changes.restored.size());
        result.needsReimport.insert(result.needsReimport.end(), changes.changed.begin(),
                                    changes.changed.end());
        result.needsReimport.insert(result.needsReimport.end(), changes.restored.begin(),
                                    changes.restored.end());

        result.changed = changes.changed.size();
        result.restored = changes.restored.size();
        result.removed = changes.removed.size();
        return result;
    }
}
