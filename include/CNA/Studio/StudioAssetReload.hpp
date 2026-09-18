// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/StudioAssetReload.hpp
 * @brief Noticing that an asset changed outside Studio, and letting go of what went stale.
 *
 * `plan.md` STUDIO-07051.
 *
 * ### The change is not the interesting part
 *
 * `AssetWatcher` answers "what moved on disk". Doing something about it is four separate acts of
 * *forgetting* — the viewport's texture, the mesh cache's model, the running player's copy, and the
 * importer facts on the record — and every one of them is a cache that will otherwise go on
 * serving the version from before the edit. An editor that reported the change and kept drawing the
 * old art is worse than one that noticed nothing, because it has told the user it is up to date.
 *
 * ### Two sinks, because two of the four are not the context's business
 *
 * Invalidating a *rendered* texture belongs to whatever is rendering, and telling a running game
 * belongs to whatever launched it. Neither is something the context knows about, and neither is
 * the same object on the two UIs — the prototype holds a `StudioViewport` and a `PlayerProcess`
 * directly, the native shell reaches them through its panels and `StudioPlayService`. So they
 * arrive as callables and this function stays the one description of *what reloading means*.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace CNA::Studio
{
    class AssetWatcher;
    class StudioContext;
    class Uuid;

    /** @brief What a poll noticed and acted on. */
    struct StudioAssetReloadResult
    {
        /** @brief Assets whose file changed and whose caches were dropped. */
        std::size_t changed = 0;

        /** @brief Assets whose missing file came back. */
        std::size_t restored = 0;

        /** @brief Assets whose file went away. */
        std::size_t removed = 0;

        /**
         * @brief The assets whose facts are now out of date: those that changed, and those that
         *        came back.
         *
         * *Reported*, not acted on -- "panels report, the binder acts", and this is the same rule
         * one layer down. Reading a model is a whole glTF parse, which belongs on a worker
         * (`plan.md` STUDIO-10011), and this function has no job system and no business having one.
         * Its caller feeds these to a `StudioImportQueue`.
         *
         * This used to be a call to `applyImporterFacts(assets)` right here: every tracked file in
         * the project re-read, on the frame, because one file changed. On a project of a thousand
         * models that is a full parse of every one of them each time somebody saves a texture.
         */
        std::vector<Uuid> needsReimport;

        /** @brief Whether anything at all happened. */
        [[nodiscard]] bool any() const { return changed + restored + removed > 0; }
    };

    /** @brief The caches a reload has to reach that the context does not own. */
    struct StudioAssetReloadSinks
    {
        /**
         * @brief Drops a rendered texture, so the next frame fetches the new one.
         *
         * Empty on a host with no viewport — the headless preview, a test. Reloading is still worth
         * doing there: the mesh cache and the importer facts are the same either way.
         */
        std::function<void(const Uuid&)> invalidateRendered;

        /**
         * @brief Tells a running game that one of its assets changed.
         *
         * Empty when nothing is running, which is the ordinary case. The callable is expected to
         * check that for itself: sending to a stopped player is not merely useless, there is no
         * process to send to, and the failure is reported as a broken bridge.
         */
        std::function<void(const Uuid&)> reloadInPlayer;
    };

    /**
     * @brief Polls @p watcher and drops whatever the changes made stale.
     *
     * Every change is reported through @p context's log, in the asset's own source path, because
     * "something reloaded" is not a sentence a user can check against what they just edited.
     *
     * @param watcher The watcher to poll.
     * @param context The editor, for its asset database, its mesh cache and its log.
     * @param sinks The caches outside the context. Either may be empty.
     * @param deltaSeconds Seconds since the last poll, which is what paces the watcher.
     * @return How many assets changed, came back and went away.
     */
    StudioAssetReloadResult studioPollAssetChanges(AssetWatcher& watcher, StudioContext& context,
                                                   const StudioAssetReloadSinks& sinks,
                                                   double deltaSeconds);
}
