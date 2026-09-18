// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Core/StudioJobs.hpp
 * @brief Work that happens off the frame, with progress, cancellation and a deterministic handoff.
 *
 * `plan.md` STUDIO-30001.
 *
 * Studio has several jobs it must stop doing on the frame: generating thumbnails, walking the
 * project to build the dependency index, searching for a missing asset's file, importing. Each of
 * them is a directory walk or a decode, and each of them is currently paid by whoever asked, in the
 * middle of a frame.
 *
 * The obvious answer -- a `std::thread` per subsystem -- is the one this exists to prevent. Threads
 * scattered across subsystems mean no two of them agree about cancellation, none of them are joined
 * on shutdown, and every one of them is one careless line away from touching the document while the
 * UI is drawing it. So there is one job system, and it is shaped so that the dangerous thing is not
 * available rather than merely discouraged.
 *
 * ### The worker never touches the document
 *
 * A job body receives a @ref StudioJobContext and nothing else. It computes a result and returns;
 * it does not hold a `StudioContext`, a `SceneDocument` or an `AssetDatabase`, because it has no
 * way to be given one. What happens *to* the document is the completion handler's job, and a
 * completion handler runs on the main thread, inside @ref StudioJobSystem::drain, between frames.
 *
 * That is the whole of "no document mutation races": not a lock, not a convention, but the fact
 * that the code which could race has nothing to race against.
 *
 * ### `drain()` is the only crossing, and it is deterministic
 *
 * Nothing a worker produced becomes visible to the rest of Studio except inside `drain()`. A test
 * therefore never sleeps and never races: it submits, waits for idle, drains, and asserts. A
 * subsystem that wanted its result *now* is a subsystem that wanted to be synchronous, and
 * @ref StudioJobMode::Immediate is the honest way to say so.
 *
 * ### Cancellation is cooperative, because the alternative does not exist
 *
 * There is no portable way to stop a thread part-way that leaves its memory in a state anybody can
 * reason about. A cancelled job is one that has been *asked* to stop and checks; a job that never
 * checks runs to completion and its result is discarded. Long bodies are expected to poll, and the
 * ones Studio has -- directory walks and per-file loops -- have an obvious place to.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief Identifies one submitted job. Zero is never issued and means "no job". */
    using StudioJobId = std::uint64_t;

    /** @brief Where a job is in its life. */
    enum class StudioJobState : std::uint8_t
    {
        /** @brief Submitted, not yet picked up. */
        Queued,
        /** @brief A worker is running it. */
        Running,
        /** @brief It returned without throwing and without being cancelled. */
        Succeeded,
        /** @brief It threw, or reported a failure. @ref StudioJobStatus::error says what. */
        Failed,
        /** @brief It was asked to stop and did. */
        Cancelled,
    };

    /** @brief Returns a stable English name for @p state, for logs and tests. */
    [[nodiscard]] const char* toString(StudioJobState state);

    /** @brief What a job looks like from the outside, as of the moment it was asked. */
    struct StudioJobStatus
    {
        StudioJobId id = 0;

        /** @brief What to call it in a progress list: "Generating thumbnails". */
        std::string name;

        StudioJobState state = StudioJobState::Queued;

        /**
         * @brief How far through, from 0 to 1, or negative when the job cannot say.
         *
         * Negative is a real answer and a better one than a bar that sits at zero: a walk that does
         * not know how many files it will find should say "working" rather than imply a fraction it
         * made up.
         */
        float progress = -1.0f;

        /** @brief What it is doing right now: "Reading Assets/Textures/player.png". */
        std::string message;

        /** @brief Why it failed. Empty unless @ref state is @ref StudioJobState::Failed. */
        std::string error;

        /** @brief Whether the job has stopped, whichever way it stopped. */
        [[nodiscard]] bool isFinished() const
        {
            return state == StudioJobState::Succeeded || state == StudioJobState::Failed
                || state == StudioJobState::Cancelled;
        }
    };

    /**
     * @brief What a job body is given, and the only thing it is given.
     *
     * Deliberately narrow. A body that could reach the document would be a body that could race
     * with the frame, and no amount of care at the call sites makes that safe -- so the reach is
     * not there to be taken.
     */
    class StudioJobContext
    {
    public:
        /**
         * @brief Whether the job has been asked to stop.
         *
         * A long body polls this. One that does not will run to completion and have its result
         * discarded, which is correct but wasteful -- and on a project-sized walk, slow enough to
         * notice on shutdown.
         */
        [[nodiscard]] bool isCancelled() const { return cancelled_->load(std::memory_order_relaxed); }

        /**
         * @brief Reports how far through the job is and what it is doing.
         *
         * A *snapshot*, not an event: the latest call wins and earlier ones are simply overwritten.
         * A queue of progress messages would fall behind a fast loop and then deliver the backlog
         * to a progress bar that has nothing left to say.
         *
         * @param fraction 0 to 1, or negative when the job cannot say.
         * @param message What it is doing. Optional; an empty message leaves the previous one.
         */
        void reportProgress(float fraction, std::string message = {});

        /**
         * @brief Fails the job with a reason.
         *
         * The body should return promptly afterwards. A body that reports a failure and carries on
         * is a body whose result nobody will look at.
         */
        void fail(std::string reason);

        /** @brief Whether @ref fail has been called. */
        [[nodiscard]] bool hasFailed() const { return failed_->load(std::memory_order_relaxed); }

    private:
        friend class StudioJobSystem;

        struct Shared;
        explicit StudioJobContext(std::shared_ptr<Shared> shared);

        std::shared_ptr<Shared> shared_;
        const std::atomic<bool>* cancelled_ = nullptr;
        const std::atomic<bool>* failed_ = nullptr;
    };

    /** @brief Whether the system runs work on threads or on the caller. */
    enum class StudioJobMode : std::uint8_t
    {
        /** @brief Workers run bodies off the frame. What Studio uses. */
        Threaded,

        /**
         * @brief Bodies run inside @ref StudioJobSystem::drain, on the calling thread.
         *
         * For tests and for a build that must be single-threaded. Every observable behaviour is the
         * same -- progress, cancellation, completion ordering -- which is what makes it a usable
         * substitute rather than a second code path that drifts.
         */
        Immediate,
    };

    /**
     * @brief One place background work happens, and one place its results come back.
     *
     * Not a general thread pool: a general pool would let a caller hand it a lambda that captures
     * the scene, which is the one thing this exists to make impossible. Jobs report progress and
     * hand back a *result*; deciding what that result means to the document is the completion
     * handler's job, and it runs on the main thread.
     */
    class StudioJobSystem
    {
    public:
        /** @brief A job's body. Runs on a worker (or inside `drain()` in immediate mode). */
        using Work = std::function<void(StudioJobContext&)>;

        /**
         * @brief What runs on the main thread when a job stops, whichever way it stopped.
         *
         * Called from @ref drain, so it may touch the document, push a command, or submit another
         * job. It is called for a cancelled job too -- a handler that cleaned up after itself only
         * on success would leak on every cancellation, which is the path taken most.
         */
        using Completion = std::function<void(const StudioJobStatus&)>;

        /**
         * @brief The most jobs that may be outstanding at once (`plan.md` STUDIO-30002).
         *
         * Outstanding means submitted and not yet drained — queued, running, or finished and
         * waiting for its completion — because that is the number that bounds what the system
         * holds: a body, a completion handler, whatever each captured, and a row in the progress
         * list. A bound on the *queue* alone would still let a caller that never drains grow
         * without limit, which is the same failure wearing a different name.
         *
         * A thousand and twenty-four is not a ration. It is far more outstanding work than any
         * screenful can produce and still a progress list a person could scroll; the point is that
         * a watcher which notices a hundred thousand changed files cannot turn that into a hundred
         * thousand live jobs before anybody notices.
         */
        static constexpr std::size_t kDefaultQueueLimit = 1024;

        /**
         * @brief Starts the system.
         *
         * @param mode Threaded, or immediate for tests and single-threaded builds.
         * @param workers How many threads. Zero asks the machine, leaving one core for the frame,
         *        and never asks for fewer than one. Ignored in immediate mode.
         * @param queueLimit The most outstanding jobs. Zero takes @ref kDefaultQueueLimit.
         */
        explicit StudioJobSystem(StudioJobMode mode = StudioJobMode::Threaded,
                                 std::size_t workers = 0,
                                 std::size_t queueLimit = kDefaultQueueLimit);

        /**
         * @brief Cancels everything, joins every worker, and drops undrained completions.
         *
         * Undrained completions are *dropped rather than run*: they exist to touch the document,
         * and at teardown the document may already be gone. A handler that ran here would be the
         * kind of shutdown crash that reproduces on one machine in ten.
         */
        ~StudioJobSystem();

        StudioJobSystem(const StudioJobSystem&) = delete;
        StudioJobSystem& operator=(const StudioJobSystem&) = delete;

        /**
         * @brief Queues @p work under @p name.
         *
         * Refused, rather than queued, when @ref getQueueLimit jobs are already outstanding
         * (`plan.md` STUDIO-30002). Backpressure here has to mean "not now" rather than "wait":
         * submission happens on the main thread, and a call that blocked until a worker was free
         * would stall the frame — which is the one thing a background job system exists to prevent.
         * Dropping the work silently is worse still, so the refusal is *visible*: a nil id back,
         * and @ref getRefusedCount goes up.
         *
         * A caller that is a per-frame poll simply offers the work again next frame, which is
         * backpressure without a queue and without a wait. A caller that is not should ask
         * @ref canAccept before building something expensive to submit.
         *
         * @param name What to call it in a progress list.
         * @param work The body. Must not capture anything the main thread may destroy or mutate.
         * @param onFinished Run on the main thread from @ref drain. Optional.
         * @return The job's id, or zero when the system is shutting down or already full. Those
         *         are different answers to the same signal, and @ref canAccept tells them apart:
         *         full is temporary, shutting down is not.
         */
        StudioJobId submit(std::string name, Work work, Completion onFinished = {});

        /**
         * @brief Whether @ref submit would accept work right now.
         *
         * False while full *and* false once shutting down, so a caller that only wants to know
         * whether to bother building a job can ask this one question.
         */
        [[nodiscard]] bool canAccept() const;

        /** @brief The most jobs that may be outstanding at once. */
        [[nodiscard]] std::size_t getQueueLimit() const;

        /**
         * @brief How many submissions have been refused because the system was full.
         *
         * Counted rather than logged. Backpressure that never engages is a bound nobody has
         * tested, and backpressure that engages constantly is a caller submitting work faster than
         * it can be done — both are things a test and a diagnostics panel want to be able to see,
         * and neither shows up in a log nobody reads.
         */
        [[nodiscard]] std::uint64_t getRefusedCount() const;

        /** @brief How many jobs are outstanding: queued, running, or waiting for @ref drain. */
        [[nodiscard]] std::size_t getOutstandingCount() const;

        /**
         * @brief Asks @p id to stop.
         *
         * A queued job is cancelled immediately and never runs its body; a running one is asked and
         * stops when it next checks. Either way its completion still arrives, with the state set to
         * cancelled -- a caller that allocated something for the job needs the handler to run.
         *
         * @return False when @p id is unknown or has already finished.
         */
        bool cancel(StudioJobId id);

        /** @brief Asks every unfinished job to stop. @return How many were asked. */
        std::size_t cancelAll();

        /**
         * @brief Runs the completion handlers of jobs that have finished. **Main thread only.**
         *
         * The one crossing. Call it once a frame; nothing a worker produced is visible to the rest
         * of Studio until it has been called.
         *
         * In immediate mode this is also where bodies run -- one per call, so a frame that submits
         * a hundred jobs does not become a frame that runs a hundred jobs.
         *
         * A completion may submit another job, and work produced *by* a drain belongs to the next
         * one: the loop is bounded by what was already waiting when it started, even with no
         * budget, so a self-feeding chain cannot run to its end inside one frame.
         *
         * @param budget The most completions to run in one call, so a burst of finished jobs is
         *        spread over frames rather than producing one long one. Zero means "everything that
         *        was already waiting".
         * @return How many completions ran.
         */
        std::size_t drain(std::size_t budget = 0);

        /**
         * @brief Blocks until nothing is queued or running. **Tests and shutdown only.**
         *
         * Not for a frame: waiting for background work on the main thread is the thing background
         * work exists to avoid. It does not run completions -- @ref drain still does that, which is
         * what keeps the handoff deterministic even here.
         */
        void waitForIdle();

        /** @brief The status of @p id, or nothing when it is unknown or already drained. */
        [[nodiscard]] StudioJobStatus find(StudioJobId id) const;

        /** @brief Every job that has not been drained yet, in submission order. */
        [[nodiscard]] std::vector<StudioJobStatus> statuses() const;

        /** @brief How many jobs are queued or running. */
        [[nodiscard]] std::size_t getActiveCount() const;

        /** @brief How many finished jobs are waiting for @ref drain. */
        [[nodiscard]] std::size_t getPendingCompletionCount() const;

        /** @brief How many worker threads there are. Zero in immediate mode. */
        [[nodiscard]] std::size_t getWorkerCount() const;

        /** @brief Which mode this system is in. */
        [[nodiscard]] StudioJobMode getMode() const { return mode_; }

    private:
        struct Impl;

        StudioJobMode mode_;
        std::unique_ptr<Impl> impl_;
    };
}
