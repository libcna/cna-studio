// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/ImportJobs.hpp
 * @brief Importing off the frame, cancellable, and honest about how far through it is.
 *
 * `plan.md` STUDIO-10011.
 *
 * ### What this replaces
 *
 * `applyImporterFacts(assets)` reads every tracked file on the calling thread. For a texture that
 * is a header read; for a model it is a whole glTF parse, because a triangle count is the sum of
 * every primitive's and there is no header that states it. A project with a thousand models
 * therefore froze the editor for as long as it took, with no progress and no way to stop — and
 * `StudioAssetReload` ran the whole pass again every time the watcher saw *one* file change.
 *
 * ### How the work is split
 *
 * A job body is given a @ref StudioJobContext and nothing else (`StudioJobs.hpp`), which is the
 * rule that makes background work safe rather than merely encouraged. So an import is cut where
 * that rule cuts it:
 *
 * - **On the worker:** read a file at a path, with a copy of its settings, and produce facts.
 *   That is `StudioAssetImporter::gatherFacts`, which is why it takes a path and a `JsonValue`
 *   instead of an `AssetDatabase&`.
 * - **On the main thread, in `drain()`:** put those facts on the record, compare before writing,
 *   write the sidecar. That is @ref studioApplyImporterFacts.
 *
 * ### Why chunks rather than one job or one job per asset
 *
 * One job for the whole run would apply two thousand sidecar writes in a single completion, which
 * is the long frame this exists to prevent, just moved. One job per asset would make the progress
 * fraction per-asset — every bar full, two thousand times — and spend a queue slot each
 * (`STUDIO-30002` bounds those). A chunk is both: a completion short enough to fit in a frame, and
 * a run whose progress is a real fraction of a total known when it started.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetImporter.hpp"
#include "CNA/Studio/Core/StudioJobs.hpp"
#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
{
    /** @brief One asset an import could not read, and why (`plan.md` STUDIO-10013). */
    struct StudioImportFailure
    {
        Uuid id;

        /** @brief The asset's project-relative path, so the message names something findable. */
        std::string sourcePath;

        /** @brief Phrased as what the editor cannot do rather than what the file did wrong. */
        std::string reason;
    };

    /**
     * @brief A run of imports: queued on the main thread, read on workers, applied on drain.
     *
     * Pumped once a frame by whoever owns it, like @ref StudioThumbnailCache. Nothing happens
     * between pumps, and nothing a worker produced is visible until one.
     */
    class StudioImportQueue
    {
    public:
        /**
         * @brief How many assets one job reads before handing back.
         *
         * Thirty-two is a completion of at most thirty-two sidecar writes, which is a short frame,
         * and a job big enough that the per-job bookkeeping is not most of the cost.
         */
        static constexpr std::size_t kChunkSize = 32;

        StudioImportQueue();

        /**
         * @brief Declared rather than implicit, because a landed chunk is only a type in the .cpp.
         *
         * `Chunk` holds the payload a worker filled in, and nothing outside this file has any
         * business knowing its shape. Declaring the destructor is what lets it stay that way.
         */
        ~StudioImportQueue();

        StudioImportQueue(const StudioImportQueue&) = delete;
        StudioImportQueue& operator=(const StudioImportQueue&) = delete;

        /**
         * @brief Adds @p ids to the run. Ids already queued or in flight are ignored.
         *
         * @return How many were newly queued.
         */
        std::size_t request(const std::vector<Uuid>& ids);

        /**
         * @brief Submits what is waiting and applies what has come back. **Main thread only.**
         *
         * Submission is bounded by what the job system will accept: a refusal means "not now"
         * (`STUDIO-30002`), and the work stays queued for the next pump rather than being dropped
         * or waited on.
         *
         * @param jobs Where the work runs. Must outlive this queue's outstanding jobs.
         * @param assets The database. Read for paths and settings, written when facts change.
         * @param importers Which importers to use. The default is the built-in set, which has
         *        static lifetime; a caller passing its own must keep it alive until this queue is
         *        idle, because a worker holds a pointer to it.
         * @return How many jobs were submitted.
         */
        std::size_t pump(StudioJobSystem& jobs, AssetDatabase& assets,
                         const StudioImporterRegistry& importers = getBuiltinAssetImporters());

        /**
         * @brief Asks every outstanding job to stop and drops whatever is still queued.
         *
         * What has already been read and applied stays applied, and that is correct rather than a
         * compromise: those files really were read, and their facts really are what the files say.
         * A cancelled import leaves assets *imported* or *not yet imported*, never half-imported —
         * the smallest thing this queue applies is one asset's whole facts object.
         *
         * A chunk whose worker had already finished is applied rather than dropped, for the same
         * reason: reading it is where the time went, and throwing the result away would waste work
         * already paid for without getting anything back.
         *
         * @return How many assets were given up on: those still queued, and those in a job that is
         *         being asked to stop. Not the ones already read and waiting to be applied.
         */
        std::size_t cancel(StudioJobSystem& jobs);

        /** @brief Whether anything is queued or in flight. */
        [[nodiscard]] bool isRunning() const;

        /**
         * @brief How far through the run is, from 0 to 1, or negative when nothing is running.
         *
         * A real fraction: the denominator is the number of assets this run was asked for, known
         * when it started, and the numerator is how many have been applied. Negative when idle
         * rather than 1.0, because "finished" and "nothing to do" are different things and only one
         * of them is worth a progress bar.
         *
         * Asking for more assets while a run is going extends it, so the fraction can fall. That is
         * the honest answer — the work really did grow — and a bar kept monotonic by hiding it
         * would be the lie this task exists to remove.
         *
         * Updated by @ref pump, so it is a value rather than a question put to the job system.
         */
        [[nodiscard]] float getProgress() const;

        /** @brief What the run is reading right now, or empty when nothing is. */
        [[nodiscard]] const std::string& getMessage() const { return message_; }

        /** @brief How many assets are waiting to be submitted. */
        [[nodiscard]] std::size_t getQueuedCount() const { return queued_.size(); }

        /** @brief How many assets are in a submitted job that has not come back yet. */
        [[nodiscard]] std::size_t getInFlightCount() const { return inFlight_; }

        /**
         * @brief How many chunks have come back and are waiting for a pump to apply them.
         *
         * Out of line, because a landed chunk is only a type in the .cpp.
         */
        [[nodiscard]] std::size_t getLandedCount() const;

        /** @brief How many files have been read, whether or not their facts changed. */
        [[nodiscard]] std::uint64_t getReadCount() const { return read_; }

        /** @brief How many assets had a fact change, and therefore a sidecar written. */
        [[nodiscard]] std::uint64_t getChangedCount() const { return changed_; }

        /**
         * @brief How many files no importer claimed.
         *
         * Not a failure and not reported as one: a project holds files Studio does not import, and
         * a run that flagged each of them would produce a list nobody reads. A file that *is* the
         * kind an importer reads and could not be read anyway is @ref getFailedCount instead
         * (`plan.md` STUDIO-10013).
         */
        [[nodiscard]] std::uint64_t getUnreadableCount() const { return unreadable_; }

        /** @brief How many files an importer claimed and then could not read. */
        [[nodiscard]] std::uint64_t getFailedCount() const { return failed_; }

        /**
         * @brief What failed and why, oldest first, since the last @ref takeFailures.
         *
         * Kept rather than logged from the worker, for the reason everything else here is kept: a
         * job body has no log to write to and no business having one. The owner drains these into
         * whatever a user actually reads.
         */
        [[nodiscard]] const std::vector<StudioImportFailure>& getFailures() const { return failures_; }

        /**
         * @brief Hands the failures over and forgets them, so each is reported once.
         *
         * A caller that read @ref getFailures every frame and logged what it found would log the
         * same broken texture sixty times a second.
         */
        [[nodiscard]] std::vector<StudioImportFailure> takeFailures();

        /** @brief How many assets were given up on because the run was cancelled. */
        [[nodiscard]] std::uint64_t getCancelledCount() const { return cancelled_; }

    private:
        struct Chunk;

        std::vector<Uuid> queued_;
        std::vector<StudioJobId> running_;
        std::vector<Chunk> landed_;
        std::size_t inFlight_ = 0;

        std::size_t total_ = 0;
        std::size_t done_ = 0;

        std::string message_;

        std::uint64_t read_ = 0;
        std::uint64_t changed_ = 0;
        std::uint64_t unreadable_ = 0;
        std::uint64_t failed_ = 0;
        std::vector<StudioImportFailure> failures_;
        std::uint64_t cancelled_ = 0;
    };
}
