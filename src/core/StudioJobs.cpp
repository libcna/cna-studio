// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Core/StudioJobs.hpp"

#include <algorithm>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace CNA::Studio
{
    const char* toString(StudioJobState state)
    {
        switch (state)
        {
            case StudioJobState::Queued:    return "queued";
            case StudioJobState::Running:   return "running";
            case StudioJobState::Succeeded: return "succeeded";
            case StudioJobState::Failed:    return "failed";
            case StudioJobState::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    /**
     * @brief Everything one job owns, shared between the worker running it and the main thread.
     *
     * Held by `shared_ptr` on both sides, so a job whose system is torn down while its body is
     * mid-loop still has somewhere to write its cancellation flag -- and the body's next check is
     * against memory that is still there.
     */
    struct StudioJobContext::Shared
    {
        StudioJobId id = 0;
        std::string name;

        StudioJobSystem::Work work;
        StudioJobSystem::Completion completion;

        std::atomic<bool> cancelled{false};
        std::atomic<bool> failed{false};

        /** @brief Progress and the two strings, which are not atomic and so need the lock. */
        mutable std::mutex mutex;
        float progress = -1.0f;
        std::string message;
        std::string error;
        StudioJobState state = StudioJobState::Queued;

        [[nodiscard]] StudioJobStatus snapshot() const
        {
            const std::lock_guard<std::mutex> lock{mutex};
            StudioJobStatus status;
            status.id = id;
            status.name = name;
            status.state = state;
            status.progress = progress;
            status.message = message;
            status.error = error;
            return status;
        }
    };

    StudioJobContext::StudioJobContext(std::shared_ptr<Shared> shared)
        : shared_(std::move(shared)),
          cancelled_(&shared_->cancelled),
          failed_(&shared_->failed)
    {
    }

    void StudioJobContext::reportProgress(float fraction, std::string message)
    {
        const std::lock_guard<std::mutex> lock{shared_->mutex};
        shared_->progress = fraction;

        // An empty message leaves the previous one, so a loop that reports a fraction every
        // iteration and a message every hundred does not blank the line in between.
        if (!message.empty()) { shared_->message = std::move(message); }
    }

    void StudioJobContext::fail(std::string reason)
    {
        {
            const std::lock_guard<std::mutex> lock{shared_->mutex};
            // The first reason wins. A body that reports two failures on its way out has one real
            // cause and one consequence, and the cause is the one that came first.
            if (shared_->error.empty()) { shared_->error = std::move(reason); }
        }
        shared_->failed.store(true, std::memory_order_relaxed);
    }

    struct StudioJobSystem::Impl
    {
        using Job = std::shared_ptr<StudioJobContext::Shared>;

        mutable std::mutex mutex;
        std::condition_variable wake;

        /** @brief Woken when a job finishes, for waitForIdle(). */
        std::condition_variable idle;

        std::deque<Job> queued;
        std::vector<Job> running;

        /** @brief Finished, waiting for drain() to run their completions on the main thread. */
        std::deque<Job> finished;

        /** @brief Every undrained job, in submission order, for statuses(). */
        std::vector<Job> tracked;

        std::vector<std::thread> workers;
        StudioJobId nextId = 1;
        bool stopping = false;

        /** @brief The most outstanding jobs. See StudioJobSystem::kDefaultQueueLimit. */
        std::size_t queueLimit = StudioJobSystem::kDefaultQueueLimit;

        /** @brief How many submissions were refused because the system was full. */
        std::uint64_t refused = 0;

        /**
         * @brief How many jobs are outstanding. Call under @ref mutex.
         *
         * `tracked` is the answer rather than `queued.size()`: a job stays tracked until its
         * completion has been drained, and it is holding a body, a handler and whatever those
         * captured for the whole of that. Bounding only the queue would leave a caller that never
         * drains growing without limit, which is the same failure wearing a different name.
         */
        [[nodiscard]] std::size_t outstanding() const { return tracked.size(); }

        /** @brief Removes @p job from @p list. */
        static void erase(std::vector<Job>& list, const Job& job)
        {
            list.erase(std::remove(list.begin(), list.end(), job), list.end());
        }

        /** @brief Sets @p job's state under its own lock. */
        static void setState(const Job& job, StudioJobState state)
        {
            const std::lock_guard<std::mutex> lock{job->mutex};
            job->state = state;
        }

        /** @brief Runs one job's body to completion and files it under `finished`. */
        void run(const Job& job)
        {
            if (!job->cancelled.load(std::memory_order_relaxed) && job->work)
            {
                StudioJobContext context{job};
                try
                {
                    job->work(context);
                }
                catch (const std::exception& error)
                {
                    // A throwing body is a failed job, not a dead process. A background walk that
                    // hit an unreadable directory must not take the editor down with it, and the
                    // reason has to survive to the completion handler that will say so.
                    context.fail(error.what());
                }
                catch (...)
                {
                    context.fail("the job threw something that is not an exception");
                }
            }

            const StudioJobState state = job->cancelled.load(std::memory_order_relaxed)
                ? StudioJobState::Cancelled
                : (job->failed.load(std::memory_order_relaxed) ? StudioJobState::Failed
                                                               : StudioJobState::Succeeded);
            setState(job, state);

            {
                const std::lock_guard<std::mutex> lock{mutex};
                erase(running, job);
                finished.push_back(job);
            }
            idle.notify_all();
        }

        void workerLoop()
        {
            while (true)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock{mutex};
                    wake.wait(lock, [this] { return stopping || !queued.empty(); });
                    if (stopping && queued.empty()) { return; }

                    job = queued.front();
                    queued.pop_front();
                    running.push_back(job);
                }

                // Marked running outside the system's lock, because it takes the *job's* lock and
                // holding two at once is how a deadlock is invented.
                setState(job, StudioJobState::Running);
                run(job);
            }
        }
    };

    StudioJobSystem::StudioJobSystem(StudioJobMode mode, std::size_t workers,
                                     std::size_t queueLimit)
        : mode_(mode), impl_(std::make_unique<Impl>())
    {
        // Zero would mean a system that refuses everything, which is never what a caller passing it
        // meant; it reads as "no opinion".
        impl_->queueLimit = queueLimit > 0 ? queueLimit : kDefaultQueueLimit;

        if (mode_ != StudioJobMode::Threaded) { return; }

        if (workers == 0)
        {
            // One core left for the frame, and never fewer than one worker: a machine that reports
            // a single core still has to be able to run a job, and a system with no workers would
            // queue for ever rather than fail loudly.
            const unsigned int cores = std::thread::hardware_concurrency();
            workers = cores > 1 ? static_cast<std::size_t>(cores - 1) : std::size_t{1};
        }

        impl_->workers.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i)
        {
            impl_->workers.emplace_back([this] { impl_->workerLoop(); });
        }
    }

    StudioJobSystem::~StudioJobSystem()
    {
        (void)cancelAll();

        {
            const std::lock_guard<std::mutex> lock{impl_->mutex};
            impl_->stopping = true;

            // Queued jobs never start. Their bodies would run to completion against a system that
            // is going away, and their completions would never be drained anyway.
            impl_->queued.clear();
        }
        impl_->wake.notify_all();

        for (std::thread& worker : impl_->workers)
        {
            if (worker.joinable()) { worker.join(); }
        }

        // Undrained completions are dropped rather than run: they exist to touch the document, and
        // at teardown the document may already be gone. Running one here would be the kind of
        // shutdown crash that reproduces on one machine in ten.
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        impl_->finished.clear();
        impl_->tracked.clear();
    }

    StudioJobId StudioJobSystem::submit(std::string name, Work work, Completion onFinished)
    {
        auto job = std::make_shared<StudioJobContext::Shared>();
        job->name = std::move(name);
        job->work = std::move(work);
        job->completion = std::move(onFinished);

        {
            const std::lock_guard<std::mutex> lock{impl_->mutex};
            if (impl_->stopping) { return 0; }

            // Refused rather than queued (STUDIO-30002), and counted so that the refusal is
            // something a test and a diagnostics panel can see. Blocking here would stall the
            // frame, which is what the whole system exists to avoid; dropping it quietly would
            // lose work nobody knows was lost.
            if (impl_->outstanding() >= impl_->queueLimit)
            {
                ++impl_->refused;
                return 0;
            }

            job->id = impl_->nextId++;
            impl_->queued.push_back(job);
            impl_->tracked.push_back(job);
        }

        impl_->wake.notify_one();
        return job->id;
    }

    bool StudioJobSystem::canAccept() const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        return !impl_->stopping && impl_->outstanding() < impl_->queueLimit;
    }

    std::size_t StudioJobSystem::getQueueLimit() const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        return impl_->queueLimit;
    }

    std::uint64_t StudioJobSystem::getRefusedCount() const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        return impl_->refused;
    }

    std::size_t StudioJobSystem::getOutstandingCount() const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        return impl_->outstanding();
    }

    bool StudioJobSystem::cancel(StudioJobId id)
    {
        Impl::Job job;
        {
            const std::lock_guard<std::mutex> lock{impl_->mutex};
            for (const Impl::Job& candidate : impl_->tracked)
            {
                if (candidate->id == id) { job = candidate; break; }
            }
            if (!job) { return false; }

            const std::lock_guard<std::mutex> jobLock{job->mutex};
            if (job->state == StudioJobState::Succeeded || job->state == StudioJobState::Failed
                || job->state == StudioJobState::Cancelled)
            {
                return false;
            }
        }

        // The flag, whatever state it is in. A queued job is picked up, sees it, and skips its body;
        // a running one sees it at its next check. Either way the completion still arrives, which
        // is what a caller that allocated something for the job depends on.
        job->cancelled.store(true, std::memory_order_relaxed);
        return true;
    }

    std::size_t StudioJobSystem::cancelAll()
    {
        std::vector<Impl::Job> jobs;
        {
            const std::lock_guard<std::mutex> lock{impl_->mutex};
            jobs = impl_->tracked;
        }

        std::size_t asked = 0;
        for (const Impl::Job& job : jobs)
        {
            if (cancel(job->id)) { ++asked; }
        }
        return asked;
    }

    std::size_t StudioJobSystem::drain(std::size_t budget)
    {
        // Immediate mode runs one body per call rather than the whole queue: a frame that submits a
        // hundred jobs must not become a frame that runs a hundred jobs, or the substitute would
        // behave differently from the thing it substitutes for in exactly the case that matters.
        if (mode_ == StudioJobMode::Immediate)
        {
            Impl::Job next;
            {
                const std::lock_guard<std::mutex> lock{impl_->mutex};
                if (!impl_->queued.empty())
                {
                    next = impl_->queued.front();
                    impl_->queued.pop_front();
                    impl_->running.push_back(next);
                }
            }
            if (next)
            {
                Impl::setState(next, StudioJobState::Running);
                impl_->run(next);
            }
        }

        // Bounded by what was already waiting, even with no budget. A completion may submit another
        // job, and in immediate mode -- or on a fast machine -- that job can finish before the loop
        // comes round again: an unbounded drain would then run a self-feeding chain to its end
        // inside one frame, which is the opposite of what a background job system is for. Work
        // produced *by* a drain belongs to the next one.
        std::size_t available = 0;
        {
            const std::lock_guard<std::mutex> lock{impl_->mutex};
            available = impl_->finished.size();
        }
        if (budget == 0 || budget > available) { budget = available; }

        std::size_t ran = 0;
        while (ran < budget)
        {
            Impl::Job job;
            {
                const std::lock_guard<std::mutex> lock{impl_->mutex};
                if (impl_->finished.empty()) { break; }
                job = impl_->finished.front();
                impl_->finished.pop_front();
                Impl::erase(impl_->tracked, job);
            }

            // Outside the lock. A completion handler may submit another job, and doing that while
            // holding the system's own lock would deadlock on the first one that did.
            if (job->completion) { job->completion(job->snapshot()); }
            ++ran;
        }
        return ran;
    }

    void StudioJobSystem::waitForIdle()
    {
        if (mode_ == StudioJobMode::Immediate)
        {
            // Nothing runs in the background, so "idle" means "everything queued has been run".
            // Bodies run in drain(), so running them here would move the crossing -- instead the
            // queue is worked through the same way drain() does, without running completions.
            while (true)
            {
                Impl::Job next;
                {
                    const std::lock_guard<std::mutex> lock{impl_->mutex};
                    if (impl_->queued.empty()) { return; }
                    next = impl_->queued.front();
                    impl_->queued.pop_front();
                    impl_->running.push_back(next);
                }
                Impl::setState(next, StudioJobState::Running);
                impl_->run(next);
            }
        }

        std::unique_lock<std::mutex> lock{impl_->mutex};
        impl_->idle.wait(lock, [this] {
            return impl_->queued.empty() && impl_->running.empty();
        });
    }

    StudioJobStatus StudioJobSystem::find(StudioJobId id) const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        for (const Impl::Job& job : impl_->tracked)
        {
            if (job->id == id) { return job->snapshot(); }
        }
        return {};
    }

    std::vector<StudioJobStatus> StudioJobSystem::statuses() const
    {
        std::vector<Impl::Job> jobs;
        {
            const std::lock_guard<std::mutex> lock{impl_->mutex};
            jobs = impl_->tracked;
        }

        // Snapshotted outside the system's lock, because a snapshot takes the job's own lock and
        // holding both at once is how a deadlock is invented.
        std::vector<StudioJobStatus> out;
        out.reserve(jobs.size());
        for (const Impl::Job& job : jobs) { out.push_back(job->snapshot()); }
        return out;
    }

    std::size_t StudioJobSystem::getActiveCount() const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        return impl_->queued.size() + impl_->running.size();
    }

    std::size_t StudioJobSystem::getPendingCompletionCount() const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        return impl_->finished.size();
    }

    std::size_t StudioJobSystem::getWorkerCount() const
    {
        const std::lock_guard<std::mutex> lock{impl_->mutex};
        return impl_->workers.size();
    }
}
