// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/AssetReimport.hpp"

#include "CNA/Studio/Assets/AssetImporters.hpp"

namespace CNA::Studio
{
    bool studioSourceChangedSinceImport(const AssetRecord& record)
    {
        // Nothing to import. A file that is not there is the Content Browser's "missing" state,
        // which has its own fix (STUDIO-09013); calling it "out of date" would offer an action
        // that cannot work.
        if (!record.sourcePresent) { return false; }

        // Never imported, so nothing has *changed*. Marking every row of a freshly scanned project
        // would be two thousand assets in an alarming state on the first open, which is noise
        // rather than news.
        if (record.importedSize == 0 && record.importedModifiedTime == 0) { return false; }

        return record.sourceSize != record.importedSize
            || record.sourceModifiedTime != record.importedModifiedTime;
    }

    bool studioNeedsReimport(const AssetRecord& record)
    {
        if (!record.sourcePresent) { return false; }
        if (record.importedSize == 0 && record.importedModifiedTime == 0) { return true; }
        return studioSourceChangedSinceImport(record);
    }

    std::vector<Uuid> studioAssetsNeedingReimport(const AssetDatabase& assets)
    {
        std::vector<Uuid> ids;

        // getAll() is ordered by path, so the list a user reads is the order the browser shows.
        for (const AssetRecord* record : assets.getAll())
        {
            if (studioNeedsReimport(*record)) { ids.push_back(record->id); }
        }
        return ids;
    }

    StudioReimportResult studioReimportAssets(AssetDatabase& assets, const std::vector<Uuid>& ids)
    {
        StudioReimportResult result;

        for (const Uuid& id : ids)
        {
            const AssetRecord* record = assets.find(id);
            if (record == nullptr) { continue; }

            if (!record->sourcePresent)
            {
                ++result.missing;
                result.warnings.push_back("'" + record->sourcePath + "' is not on disk");
                continue;
            }

            // The facts, and only the facts. Every other entry in the sidecar is the user's, and a
            // reimport that reset them would be the classic asset-pipeline failure: somebody
            // re-exports a mesh and every import setting in the project silently reverts.
            if (applyImporterFacts(assets, id)) { ++result.factsChanged; }

            AssetRecord* mutableRecord = assets.findMutable(id);
            if (mutableRecord == nullptr) { continue; }

            // Stamped with what the file *is*, so the next "does this need reimporting" is
            // arithmetic rather than a syscall. Taken from the record rather than from disk,
            // because the record is what a scan and the watcher keep pointed at the file -- and
            // asking the filesystem here would put a stat back into the path that exists to
            // have none.
            mutableRecord->importedSize = mutableRecord->sourceSize;
            mutableRecord->importedModifiedTime = mutableRecord->sourceModifiedTime;
            assets.writeSidecar(id);

            ++result.reimported;
        }

        return result;
    }

    StudioReimportResult studioReimportOutdatedAssets(AssetDatabase& assets)
    {
        return studioReimportAssets(assets, studioAssetsNeedingReimport(assets));
    }
}
