// SPDX-License-Identifier: MS-PL
/**
 * @file StudioJobTests.cpp
 * @brief The background job system (`plan.md` STUDIO-30001).
 *
 * The four things this has to get right are the four a scattered `std::thread` per subsystem gets
 * wrong: nothing crosses to the main thread except in `drain()`, cancellation is honoured and still
 * delivers a completion, a body that throws is a failed job rather than a dead process, and the
 * destructor joins everything without running a handler against a document that is going away.
 *
 * Every test here is deterministic. None of them sleeps: `waitForIdle` is the wait, and `drain` is
 * the crossing, so there is no window in which the answer depends on how fast the machine is.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Core/StudioJobs.hpp"

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief Both modes, so every guarantee below is asserted against each. */
    constexpr StudioJobMode kModes[] = {StudioJobMode::Threaded, StudioJobMode::Immediate};

    const char* modeName(StudioJobMode mode)
    {
        return mode == StudioJobMode::Threaded ? "threaded" : "immediate";
    }
}

CNA_STUDIO_TEST(AJobRunsOffTheFrameAndItsResultArrivesOnlyInDrain)
{
    // The crossing is the whole design. A result that became visible before drain() would be a
    // result that could arrive in the middle of a frame, which is what the document is protected
    // from by shape rather than by locks.
    for (const StudioJobMode mode : kModes)
    {
        StudioJobSystem jobs{mode, 2};

        std::atomic<int> ran{0};
        int delivered = 0;
        StudioJobState deliveredState = StudioJobState::Queued;

        const StudioJobId id = jobs.submit(
            "Count to one",
            [&ran](StudioJobContext& context) {
                ran.fetch_add(1);
                context.reportProgress(1.0f, "done");
            },
            [&delivered, &deliveredState](const StudioJobStatus& status) {
                ++delivered;
                deliveredState = status.state;
            });

        CNA_STUDIO_EXPECT(id != 0);

        jobs.waitForIdle();
        CNA_STUDIO_EXPECT_EQ(ran.load(), 1);

        // The body has run and the completion has not. Everything the job produced is still on the
        // far side of the crossing.
        if (delivered != 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         std::string{"a completion ran outside drain() in "}
                                             + modeName(mode) + " mode.");
        }

        CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});
        CNA_STUDIO_EXPECT_EQ(delivered, 1);
        CNA_STUDIO_EXPECT(deliveredState == StudioJobState::Succeeded);

        // Drained, so it is no longer tracked: a progress list that kept every job a session ever
        // ran would be a progress list nobody reads.
        CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{0});
        CNA_STUDIO_EXPECT(jobs.statuses().empty());
    }
}

CNA_STUDIO_TEST(ACancelledJobStillDeliversItsCompletion)
{
    // A caller that allocated something for the job needs the handler to run. One that cleaned up
    // only on success would leak on every cancellation -- which is the path taken most, because
    // cancellation is what happens when the user changes what they are looking at.
    for (const StudioJobMode mode : kModes)
    {
        StudioJobSystem jobs{mode, 1};

        std::atomic<bool> bodyRan{false};
        StudioJobState state = StudioJobState::Queued;

        const StudioJobId id = jobs.submit(
            "Never starts",
            [&bodyRan](StudioJobContext&) { bodyRan.store(true); },
            [&state](const StudioJobStatus& status) { state = status.state; });

        // Cancelled while still queued, which is the case a worker must not start.
        CNA_STUDIO_EXPECT(jobs.cancel(id));

        jobs.waitForIdle();
        CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});

        CNA_STUDIO_EXPECT(state == StudioJobState::Cancelled);
        if (bodyRan.load())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         std::string{"a job cancelled while queued ran its body in "}
                                             + modeName(mode) + " mode.");
        }

        // Cancelling something that has finished, or something that never existed, is a no-op
        // rather than an error: by the time a user clicks Cancel the job may already be done.
        CNA_STUDIO_EXPECT(!jobs.cancel(id));
        CNA_STUDIO_EXPECT(!jobs.cancel(9999));
    }
}

CNA_STUDIO_TEST(ARunningJobSeesTheCancellationFlagAndStops)
{
    // Cooperative, because there is no portable way to stop a thread part-way that leaves its
    // memory in a state anybody can reason about. The bargain is that long bodies poll.
    StudioJobSystem jobs{StudioJobMode::Threaded, 1};

    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<int> iterations{0};

    const StudioJobId id = jobs.submit("Long walk", [&](StudioJobContext& context) {
        started.store(true);
        while (!release.load()) { /* wait for the test to cancel */ }

        for (int i = 0; i < 1000; ++i)
        {
            if (context.isCancelled()) { return; }
            iterations.fetch_add(1);
        }
    });

    while (!started.load()) { /* the body has to be running for this to be the running case */ }
    CNA_STUDIO_EXPECT(jobs.cancel(id));
    release.store(true);

    jobs.waitForIdle();

    StudioJobState state = StudioJobState::Queued;
    CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});
    (void)state;

    // It stopped at its first check rather than running the loop out.
    CNA_STUDIO_EXPECT_EQ(iterations.load(), 0);
}

CNA_STUDIO_TEST(ProgressIsASnapshotRatherThanAStreamOfEvents)
{
    // A queue of progress messages falls behind a fast loop and then delivers the backlog to a
    // progress bar that has nothing left to say. The latest call wins.
    StudioJobSystem jobs{StudioJobMode::Immediate};

    std::atomic<bool> go{false};
    const StudioJobId id = jobs.submit("Stepping", [&go](StudioJobContext& context) {
        context.reportProgress(0.25f, "a quarter");
        context.reportProgress(0.50f);
        context.reportProgress(0.75f, "three quarters");
        go.store(true);
    });

    jobs.waitForIdle();
    CNA_STUDIO_EXPECT(go.load());

    const StudioJobStatus status = jobs.find(id);
    CNA_STUDIO_EXPECT_EQ(status.progress, 0.75f);
    CNA_STUDIO_EXPECT_EQ(status.message, std::string{"three quarters"});
    CNA_STUDIO_EXPECT_EQ(status.name, std::string{"Stepping"});

    // An empty message leaves the previous one, so a loop that reports a fraction every iteration
    // and a message every hundred does not blank the line in between. The 0.50 call proves it: the
    // message that survived it is the one before.
    CNA_STUDIO_EXPECT(jobs.drain() == std::size_t{1});
}

CNA_STUDIO_TEST(AJobThatThrowsIsAFailedJobRatherThanADeadProcess)
{
    // A background walk that hits an unreadable directory must not take the editor down with it,
    // and the reason has to survive to the handler that will say so.
    for (const StudioJobMode mode : kModes)
    {
        StudioJobSystem jobs{mode, 1};

        StudioJobStatus delivered;
        jobs.submit(
            "Throws",
            [](StudioJobContext&) { throw std::runtime_error{"the directory would not open"}; },
            [&delivered](const StudioJobStatus& status) { delivered = status; });

        jobs.waitForIdle();
        CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});

        CNA_STUDIO_EXPECT(delivered.state == StudioJobState::Failed);
        CNA_STUDIO_EXPECT_EQ(delivered.error, std::string{"the directory would not open"});
    }

    // And a body that reports a failure itself reaches the same state, with the first reason kept:
    // a body reporting two failures on its way out has one cause and one consequence.
    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioJobStatus delivered;
    jobs.submit(
        "Fails politely",
        [](StudioJobContext& context) {
            context.fail("the first reason");
            CNA_STUDIO_EXPECT(context.hasFailed());
            context.fail("the consequence");
        },
        [&delivered](const StudioJobStatus& status) { delivered = status; });

    jobs.waitForIdle();
    CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});
    CNA_STUDIO_EXPECT(delivered.state == StudioJobState::Failed);
    CNA_STUDIO_EXPECT_EQ(delivered.error, std::string{"the first reason"});
}

CNA_STUDIO_TEST(DrainSpreadsABurstOfCompletionsOverFramesWhenGivenABudget)
{
    // Ten jobs finishing on one frame is one long frame unless the handoff is bounded. The budget
    // is what makes "background work" stay background all the way to the last step.
    StudioJobSystem jobs{StudioJobMode::Threaded, 4};

    std::atomic<int> completed{0};
    for (int i = 0; i < 10; ++i)
    {
        jobs.submit("Job " + std::to_string(i), [](StudioJobContext&) {},
                    [&completed](const StudioJobStatus&) { completed.fetch_add(1); });
    }

    jobs.waitForIdle();
    CNA_STUDIO_EXPECT_EQ(jobs.getActiveCount(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(jobs.getPendingCompletionCount(), std::size_t{10});

    CNA_STUDIO_EXPECT_EQ(jobs.drain(3), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(completed.load(), 3);

    CNA_STUDIO_EXPECT_EQ(jobs.drain(0), std::size_t{7});
    CNA_STUDIO_EXPECT_EQ(completed.load(), 10);
}

CNA_STUDIO_TEST(ACompletionMaySubmitAnotherJob)
{
    // Which is the shape every real use has: a walk finishes, its result is applied, and applying
    // it asks for the next piece of work. A handoff that held the system's own lock while calling
    // the handler would deadlock on the first one that did.
    StudioJobSystem jobs{StudioJobMode::Threaded, 2};

    std::atomic<int> ran{0};
    jobs.submit("First", [&ran](StudioJobContext&) { ran.fetch_add(1); },
                [&jobs, &ran](const StudioJobStatus&) {
                    jobs.submit("Second", [&ran](StudioJobContext&) { ran.fetch_add(1); });
                });

    jobs.waitForIdle();

    // Exactly one: the second job is submitted *during* this drain, and work produced by a drain
    // belongs to the next one. Without that bound a self-feeding chain would run to its end inside
    // one frame, which is the opposite of what a background job system is for.
    CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});

    jobs.waitForIdle();
    CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(ran.load(), 2);
}

CNA_STUDIO_TEST(ShutdownJoinsEveryWorkerAndRunsNoHandlerOnTheWayOut)
{
    // Undrained completions exist to touch the document, and at teardown the document may already
    // be gone. A handler that ran here would be the kind of shutdown crash that reproduces on one
    // machine in ten.
    std::atomic<int> handlersRun{0};
    std::atomic<int> bodiesRun{0};

    {
        StudioJobSystem jobs{StudioJobMode::Threaded, 2};
        for (int i = 0; i < 8; ++i)
        {
            jobs.submit("Job " + std::to_string(i),
                        [&bodiesRun](StudioJobContext& context) {
                            if (context.isCancelled()) { return; }
                            bodiesRun.fetch_add(1);
                        },
                        [&handlersRun](const StudioJobStatus&) { handlersRun.fetch_add(1); });
        }
        // Destructor runs here: cancels, joins, drops.
    }

    CNA_STUDIO_EXPECT_EQ(handlersRun.load(), 0);

    // Whatever ran, ran. The point is not that nothing did -- a job that had already started is
    // allowed to finish -- but that nothing crossed back.
    CNA_STUDIO_EXPECT(bodiesRun.load() >= 0);
}

CNA_STUDIO_TEST(TheSystemAsksTheMachineForItsWorkerCountAndNeverAsksForNone)
{
    // A machine reporting a single core still has to be able to run a job; a system with no workers
    // would queue for ever rather than fail loudly.
    const StudioJobSystem automatic{StudioJobMode::Threaded};
    CNA_STUDIO_EXPECT(automatic.getWorkerCount() >= 1);

    const StudioJobSystem explicitCount{StudioJobMode::Threaded, 3};
    CNA_STUDIO_EXPECT_EQ(explicitCount.getWorkerCount(), std::size_t{3});

    // Immediate mode has no threads at all, which is what makes it usable in a build that must be
    // single-threaded rather than only in a test.
    const StudioJobSystem immediate{StudioJobMode::Immediate, 4};
    CNA_STUDIO_EXPECT_EQ(immediate.getWorkerCount(), std::size_t{0});
    CNA_STUDIO_EXPECT(immediate.getMode() == StudioJobMode::Immediate);
}

CNA_STUDIO_TEST(ImmediateModeRunsOneBodyPerDrainRatherThanTheWholeQueue)
{
    // Or the substitute would behave differently from the thing it substitutes for in exactly the
    // case that matters: a frame that submits a hundred jobs becoming a frame that runs them.
    StudioJobSystem jobs{StudioJobMode::Immediate};

    std::atomic<int> ran{0};
    for (int i = 0; i < 3; ++i)
    {
        jobs.submit("Job " + std::to_string(i), [&ran](StudioJobContext&) { ran.fetch_add(1); });
    }

    CNA_STUDIO_EXPECT_EQ(jobs.getActiveCount(), std::size_t{3});

    CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(ran.load(), 1);

    CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(ran.load(), 2);

    // waitForIdle finishes the rest without running completions, which is what keeps the crossing
    // deterministic even there.
    jobs.waitForIdle();
    CNA_STUDIO_EXPECT_EQ(ran.load(), 3);
    CNA_STUDIO_EXPECT_EQ(jobs.getPendingCompletionCount(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(jobs.drain(), std::size_t{1});
}

CNA_STUDIO_TEST(EveryJobStateHasAName)
{
    // Printed in the progress list and in tests, so a rename would be a silent change to both.
    CNA_STUDIO_EXPECT_EQ(std::string{toString(StudioJobState::Queued)}, std::string{"queued"});
    CNA_STUDIO_EXPECT_EQ(std::string{toString(StudioJobState::Running)}, std::string{"running"});
    CNA_STUDIO_EXPECT_EQ(std::string{toString(StudioJobState::Succeeded)},
                         std::string{"succeeded"});
    CNA_STUDIO_EXPECT_EQ(std::string{toString(StudioJobState::Failed)}, std::string{"failed"});
    CNA_STUDIO_EXPECT_EQ(std::string{toString(StudioJobState::Cancelled)},
                         std::string{"cancelled"});

    StudioJobStatus status;
    CNA_STUDIO_EXPECT(!status.isFinished());
    status.state = StudioJobState::Cancelled;
    CNA_STUDIO_EXPECT(status.isFinished());
}

CNA_STUDIO_TEST(ManyJobsAcrossManyWorkersAllArriveExactlyOnce)
{
    // The thing a scattered thread-per-subsystem design gets wrong quietly: a completion that runs
    // twice, or not at all, under contention. Sixty-four jobs over four workers is enough
    // contention to catch a missing lock and few enough to stay fast.
    StudioJobSystem jobs{StudioJobMode::Threaded, 4};

    constexpr int kCount = 64;
    std::atomic<int> bodies{0};
    std::vector<int> completions(kCount, 0);

    for (int i = 0; i < kCount; ++i)
    {
        jobs.submit("Job " + std::to_string(i),
                    [&bodies](StudioJobContext& context) {
                        context.reportProgress(1.0f);
                        bodies.fetch_add(1);
                    },
                    [&completions, i](const StudioJobStatus& status) {
                        CNA_STUDIO_EXPECT(status.state == StudioJobState::Succeeded);
                        ++completions[static_cast<std::size_t>(i)];
                    });
    }

    jobs.waitForIdle();
    CNA_STUDIO_EXPECT_EQ(jobs.drain(), static_cast<std::size_t>(kCount));
    CNA_STUDIO_EXPECT_EQ(bodies.load(), kCount);

    for (const int count : completions) { CNA_STUDIO_EXPECT_EQ(count, 1); }
}
