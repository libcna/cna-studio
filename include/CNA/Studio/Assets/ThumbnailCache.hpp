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
 * ### What invalidates an entry, which is `STUDIO-30012`'s question again
 *
 * The record's stamp — size and modification time as the last scan or watcher poll saw them — and
 * the source path, compared without asking the filesystem anything. Same rule as
 * `StudioAssetDocumentCache`, for the same reason: it is free on the frames where nothing changed,
 * and the watcher is what makes an external edit visible.
 *
 * A *failure* is cached too. A file that is not really a PNG would otherwise be decoded again on
 * every pump, for ever, which is the case a cache exists to stop.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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

    private:
        /** @brief One entry, with the stamp it was made at. */
        struct Entry
        {
            std::uint64_t size = 0;
            std::int64_t modifiedTime = 0;
            std::string sourcePath;

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
        };

        /** @brief Whether @p entry still describes the record as the database sees it. */
        [[nodiscard]] static bool matches(const Entry& entry, std::uint64_t size,
                                          std::int64_t modifiedTime, const std::string& path);

        /** @brief Drops the least recently wanted entries until the count is inside the bound. */
        void evict();

        std::unordered_map<Uuid, Entry> entries_;
        std::unordered_map<Uuid, Pending> pending_;
        std::vector<Uuid> wanted_;

        /** @brief Increments on every setWanted, so "least recently wanted" has an order. */
        std::uint64_t clock_ = 0;

        std::uint64_t cancelled_ = 0;
        std::uint64_t failed_ = 0;
        std::uint64_t generated_ = 0;
        std::uint64_t evicted_ = 0;
    };
}
