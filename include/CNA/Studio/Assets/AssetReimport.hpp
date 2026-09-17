// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetReimport.hpp
 * @brief Re-reading an asset from disk without losing what the user chose about it.
 *
 * `plan.md` STUDIO-10001, STUDIO-09010.
 *
 * ### Settings and facts are different things, and only one of them a reimport may touch
 *
 * An asset's sidecar holds two kinds of entry, both shaped as importer settings because
 * `ComponentDescriptor` already describes both (`AssetImporters.hpp`):
 *
 * - **Settings** — what the *user* chose: a model's `scaleFactor`, a texture's `generateMipmaps`.
 *   A reimport must not touch these. Losing them is the classic asset-pipeline failure: somebody
 *   re-exports a mesh from Blender and every import setting in the project silently reverts.
 * - **Facts** — what the *file* says: a texture's pixel size, a model's triangle count. A reimport
 *   exists to refresh exactly these, and they are marked read-only by the importer that declares
 *   them.
 *
 * So a reimport rewrites the facts, leaves everything else alone, and records when it ran.
 *
 * ### A reimport is not undoable, and that is deliberate
 *
 * Every *document* change in Studio is a command (D-06). A reimport is not a document change: it
 * re-reads what is already on disk. Undoing one would put back facts describing a version of the
 * file that no longer exists — a sidecar claiming a texture is 512×512 when the file on disk is
 * 1024×1024. There is nothing the user could want from that, and the thing they might want back —
 * their settings — was never touched.
 *
 * ### Whether a reimport is due costs nothing to ask
 *
 * `AssetRecord` carries two stamps: what the watcher last saw on disk, and what the last import
 * read. Comparing them is arithmetic, so the Content Browser can mark every row that needs
 * reimporting without a syscall — which is the whole of what `STUDIO-30015` bought.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
{
    /**
     * @brief Whether @p record's file has changed **since it was imported**.
     *
     * The *news*: somebody re-exported the mesh, or edited the texture, and what Studio knows about
     * it is now out of date. False for an asset that has never been imported, because nothing has
     * changed about a file nobody has read — a freshly scanned project is not two thousand assets
     * in an alarming state.
     *
     * This is what the Content Browser marks a row with, and it costs nothing to ask: both stamps
     * are in the record already, one kept by the watcher and one by the last import.
     */
    [[nodiscard]] bool studioSourceChangedSinceImport(const AssetRecord& record);

    /**
     * @brief Whether a reimport of @p record would do anything.
     *
     * Broader than @ref studioSourceChangedSinceImport: it also takes in the asset nobody has ever
     * imported, whose facts have therefore never been read. That is the right set for "import
     * everything that needs it" and the wrong one for a badge on a row.
     *
     * A missing file is neither, because there is nothing to import; that is the Content Browser's
     * "missing" state and a different problem with a different fix (`STUDIO-09013`).
     */
    [[nodiscard]] bool studioNeedsReimport(const AssetRecord& record);

    /** @brief Every tracked asset @ref studioNeedsReimport says is due, ordered by path. */
    [[nodiscard]] std::vector<Uuid> studioAssetsNeedingReimport(const AssetDatabase& assets);

    /** @brief What a reimport did. */
    struct StudioReimportResult
    {
        /** @brief How many assets were re-read. */
        std::size_t reimported = 0;

        /** @brief How many were skipped because their file is not on disk. */
        std::size_t missing = 0;

        /** @brief How many had their facts change as a result. */
        std::size_t factsChanged = 0;

        /** @brief What could not be done, one line each. */
        std::vector<std::string> warnings;
    };

    /**
     * @brief Re-reads @p ids from disk, refreshing their facts and keeping every setting.
     *
     * @param assets The database. Sidecars are rewritten for whatever changed.
     * @param ids The assets to reimport. Unknown ids are ignored.
     * @return What happened.
     */
    StudioReimportResult studioReimportAssets(AssetDatabase& assets, const std::vector<Uuid>& ids);

    /** @brief Reimports every asset @ref studioAssetsNeedingReimport reports. */
    StudioReimportResult studioReimportOutdatedAssets(AssetDatabase& assets);
}
