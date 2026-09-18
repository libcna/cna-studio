// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Assets/ThumbnailCache.hpp
 * @brief Thumbnails, generated off the frame and cancelled when nobody is looking any more.
 *
 * `plan.md` STUDIO-09003.
 *
 * ### The shape, and why it is this shape
 *
 * Drawing asks and never waits: @ref StudioThumbnailCache::find is a hash lookup that touches no
 * filesystem and starts nothing. A browser scrolling through a hundred thousand assets calls it
 * forty times a frame and must not pay for a single `stat` — the counted gates in
 * `StudioLargeProjectTests` exist to keep that true, and a thumbnail cache is exactly the feature
 * that would quietly put file access back on the draw path.
 *
 * Wanting is separate from asking, and belongs to the binder rather than to the panel. The browser
 * already knows which assets are on screen — that is `studioContentCardWindow`'s window — so it
 * *reports* them and `StudioShellPanels` hands the set to @ref StudioThumbnailCache::setWanted.
 * Panels report, the binder acts (`docs/ARCHITECTURE.md` §10.1), and the draw path stays const.
 *
 * ### What cancellation actually means here
 *
 * Scrolling past an asset before its thumbnail is made is the common case, not the exceptional one:
 * a user flicking through a folder of two thousand textures wants the forty they stop on, and the
 * nineteen hundred they flew past are work nobody will ever look at. So an asset dropping out of
 * the wanted set cancels its job, and the cancellation is the point of the feature rather than
 * tidiness — without it the queue is a record of everywhere the user has been.
 *
 * ### Two keys, because they answer different questions (`STUDIO-09004`)
 *
 * **The stamp decides whether to look**: size, modification time, source path, and a fingerprint of
 * the importer settings, all read from the record without asking the filesystem anything. It is
 * free on the frames where nothing changed, which is what lets `pump` run every poll. Same rule as
 * `StudioAssetDocumentCache`, plus the settings — because a reimport changes what a thumbnail
 * should look like without touching the file, and a cache keyed only on the file would go on
 * showing the old picture.
 *
 * **The content decides whether to decode**: a SHA-256 of the file's bytes, computed on the worker
 * because reading a file is exactly what a poll must not do. Two consequences, and the second is
 * the one that made this worth a task of its own:
 *
 * - Two assets holding identical bytes share one decode and one thumbnail. A project with a
 *   texture copied into three folders pays once.
 * - A file rewritten to the same length within the same second is *noticed*. The stamp cannot see
 *   that — size and modification time are all it has — and the content key can. The stamp still
 *   decides when to look, so the hole is narrowed to "a rewrite the watcher did not notice at all",
 *   which is the watcher's problem rather than the cache's.
 *
 * A *failure* is cached too. A file that is not really a PNG would otherwise be decoded again on
 * every pump, for ever, which is the case a cache exists to stop.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/StudioJobs.hpp"
#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
{
    class AssetDatabase;

    /** @brief One generated thumbnail: RGBA, eight bits a channel, row-major, top row first. */
    struct StudioThumbnail
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<unsigned char> pixels;

        /**
         * @brief What this thumbnail was made from — the content and settings key.
         *
         * Carried on the thumbnail rather than kept privately (`plan.md` STUDIO-35041) because the
         * host that uploads these pixels to a texture is called once per visible card per frame and
         * needs to know when they have actually changed. Comparing the pixels would cost more than
         * the upload it saves; comparing an id alone would never notice a reimport.
         */
        std::string key;

        [[nodiscard]] bool isEmpty() const { return pixels.empty(); }
    };

    /**
     * @brief Shrinks @p pixels to fit inside @p maximumEdge, keeping its proportions.
     *
     * A box filter, averaging every source pixel that lands in a destination one. Nearest-neighbour
     * is one line shorter and makes a downscaled texture look like a different texture — thin
     * detail either vanishes or turns into a moiré, and a thumbnail that misrepresents its asset is
     * worse than no thumbnail, because the user believes it.
     *
     * An image already inside the bound is returned unchanged rather than resampled, so a small
     * icon is not softened for nothing.
     *
     * @param source Pixels, RGBA, `width * height * 4` bytes.
     * @param width Source width.
     * @param height Source height.
     * @param maximumEdge The longest edge the result may have.
     * @return The shrunk image, empty when the input is malformed.
     */
    [[nodiscard]] StudioThumbnail studioDownscaleRgba(const std::vector<unsigned char>& source,
                                                      std::uint32_t width, std::uint32_t height,
                                                      std::uint32_t maximumEdge);

    /**
     * @brief Thumbnails for assets, made on worker threads and kept until something changes.
     *
     * Not thread-safe: every method is the main thread's. The *work* is what happens elsewhere, and
     * it happens through `StudioJobSystem`, whose completions run on the main thread from `drain`.
     */
    class StudioThumbnailCache
    {
    public:
        /**
         * @brief The longest edge a thumbnail has.
         *
         * A hundred and twenty-eight is twice the largest card the grid draws at its biggest size,
         * so a thumbnail is still sharp on a high-density display, and 64 KB of pixels — small
         * enough that @ref kMaximumEntries of them is a bounded, unremarkable amount of memory.
         */
        static constexpr std::uint32_t kThumbnailEdge = 128;

        /**
         * @brief The most thumbnails kept at once.
         *
         * Bounded because a project has a hundred thousand assets and a screen has forty. Two
         * hundred and fifty-six is several screenfuls — enough that scrolling back up finds the
         * thumbnails still there, which is what makes a cache feel like one — and about sixteen
         * megabytes, which is not a number anybody has to think about.
         */
        static constexpr std::size_t kMaximumEntries = 256;

        /**
         * @brief The thumbnail for @p id, or null when there is not one yet.
         *
         * A hash lookup. Touches no filesystem, starts no work, and is what the draw path calls.
         * Null means "not yet", "not an image", or "never asked for" — the caller draws its icon
         * and does not need to know which.
         */
        [[nodiscard]] const StudioThumbnail* find(const Uuid& id) const;

        /**
         * @brief Says which assets are worth having thumbnails for.
         *
         * Everything not in @p ids stops being wanted, and any job running for it is cancelled on
         * the next @ref pump. Called once a poll with the browser's visible window.
         */
        void setWanted(std::vector<Uuid> ids);

        /**
         * @brief Starts and stops work so that what is running matches what is wanted.
         *
         * **Main thread.** Submits a job per wanted asset that has no current thumbnail, cancels
         * jobs for assets that are no longer wanted, and respects the job system's backpressure —
         * a refusal is simply "not now", and the next pump offers the work again (`STUDIO-30002`).
         *
         * @param jobs Where the work goes.
         * @param assets The database, for each record's path and stamp.
         * @param budget The most jobs to submit in one call, so one pump cannot fill the queue with
         *        a whole folder. Zero means the wanted set.
         * @return How many jobs were submitted.
         */
        std::size_t pump(StudioJobSystem& jobs, const AssetDatabase& assets,
                         std::size_t budget = 0);

        /** @brief Forgets @p id's thumbnail, or every one when @p id is nil. */
        void invalidate(const Uuid& id = {});

        /**
         * @brief Told when an entry is dropped, so a host holding a texture for it can let go.
         *
         * `plan.md` STUDIO-35041. The cache is bounded and the host's textures are not, so without
         * this a project of a hundred thousand images fills a GPU with pictures of folders nobody
         * is in. Called from @ref pump's completions and from @ref invalidate, on the main thread.
         */
        void setOnDropped(std::function<void(const Uuid&)> onDropped);

        /** @brief How many thumbnails are held. */
        [[nodiscard]] std::size_t getCount() const;

        /** @brief How many jobs are running or queued for this cache right now. */
        [[nodiscard]] std::size_t getPendingCount() const { return pending_.size(); }

        /**
         * @brief How many jobs have been cancelled because nobody was looking any more.
         *
         * Counted because it is the feature rather than an accident: a number that stays at zero
         * while a user scrolls means the cancellation never engaged, and nothing else would say so.
         */
        [[nodiscard]] std::uint64_t getCancelledCount() const { return cancelled_; }

        /** @brief How many decodes failed. A file that is not really an image is the usual cause. */
        [[nodiscard]] std::uint64_t getFailedCount() const { return failed_; }

        /** @brief How many thumbnails have been generated, for tests and diagnostics. */
        [[nodiscard]] std::uint64_t getGeneratedCount() const { return generated_; }

        /** @brief How many entries were dropped to stay inside @ref kMaximumEntries. */
        [[nodiscard]] std::uint64_t getEvictedCount() const { return evicted_; }

        /**
         * @brief How many thumbnails were answered from another asset's identical bytes.
         *
         * `STUDIO-09004`. Counted because a number that stays at zero in a project with duplicated
         * textures means the content key is not doing the one thing it was added for, and nothing
         * else would say so.
         */
        [[nodiscard]] std::uint64_t getSharedCount() const { return sharedHits_; }

        /**
         * @brief A fingerprint of @p settings, for the stamp that decides whether to look again.
         *
         * The serialised form rather than a structural comparison: importer settings are arbitrary
         * JSON, the cache has no business knowing what any particular importer's fields mean, and
         * "the text differs" is exactly the question being asked.
         */
        [[nodiscard]] static std::string settingsFingerprint(const JsonValue& settings);

    private:
        /** @brief One entry, with the stamp it was made at. */
        struct Entry
        {
            std::uint64_t size = 0;
            std::int64_t modifiedTime = 0;
            std::string sourcePath;

            /** @brief The importer settings this thumbnail was made under. See `STUDIO-09004`. */
            std::string settings;

            /** @brief SHA-256 of the source bytes, for sharing between identical files. */
            std::string content;

            /**
             * @brief Whether the decode succeeded.
             *
             * A failure is kept as an entry, or a file that is not really an image would be decoded
             * again on every pump for ever.
             */
            bool decoded = false;

            /** @brief When this entry was last asked for, for the eviction order. */
            std::uint64_t lastWanted = 0;

            StudioThumbnail thumbnail;
        };

        /** @brief A job in flight, and the stamp it was started for. */
        struct Pending
        {
            StudioJobId id = 0;
            std::uint64_t size = 0;
            std::int64_t modifiedTime = 0;
            std::string sourcePath;
            std::string settings;
        };

        /** @brief Whether @p entry still describes the record as the database sees it. */
        [[nodiscard]] static bool matches(const Entry& entry, std::uint64_t size,
                                          std::int64_t modifiedTime, const std::string& path,
                                          const std::string& settings);

        /** @brief Drops the least recently wanted entries until the count is inside the bound. */
        void evict();

        std::unordered_map<Uuid, Entry> entries_;
        std::unordered_map<Uuid, Pending> pending_;
        std::vector<Uuid> wanted_;

        /** @brief Increments on every setWanted, so "least recently wanted" has an order. */
        std::uint64_t clock_ = 0;

        /**
         * @brief Thumbnails by what they were made from, readable from a worker.
         *
         * Keyed on the source bytes' hash *and* the importer settings, because the same file under
         * different settings is a different picture — sharing on content alone would hand one
         * asset another's answer, which is the sort of wrong that looks right.
         *
         * Behind a mutex and held by `shared_ptr` because a job body consults it: that is what lets
         * a duplicate skip the decode entirely rather than decoding and then discovering it need
         * not have. The `shared_ptr` is the lifetime answer — a job in flight outliving the cache
         * is not a case anybody should have to reason about at a teardown.
         *
         * It holds pixels rather than pointing at an entry: entries are evicted, and a shared
         * thumbnail whose owner was dropped would leave every other copy blank.
         */
        struct SharedThumbnails
        {
            std::mutex mutex;
            std::unordered_map<std::string, StudioThumbnail> byKey;
        };

        /** @brief The key a thumbnail is shared under: what it was made from. */
        [[nodiscard]] static std::string sharingKey(const std::string& contentHash,
                                                    const std::string& settings);

        std::shared_ptr<SharedThumbnails> shared_ = std::make_shared<SharedThumbnails>();

        /** @brief Told when an entry is dropped. See setOnDropped. */
        std::function<void(const Uuid&)> onDropped_;

        std::uint64_t cancelled_ = 0;
        std::uint64_t sharedHits_ = 0;
        std::uint64_t failed_ = 0;
        std::uint64_t generated_ = 0;
        std::uint64_t evicted_ = 0;
    };
}
