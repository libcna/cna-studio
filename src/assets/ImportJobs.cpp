// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/ImportJobs.hpp"

#include "CNA/Studio/Assets/AssetImporters.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace CNA::Studio
{
    namespace
    {
        /** @brief One asset, reduced to what a worker may be handed. */
        struct Request
        {
            Uuid id;

            /** @brief Already resolved, because resolving needs the database. */
            std::string absolutePath;

            /** @brief For the progress message, which should name a file a person recognises. */
            std::string sourcePath;

            /** @brief A *copy*: the main thread may edit the record while this job is in flight. */
            JsonValue settings;

            const StudioAssetImporter* importer = nullptr;
        };

        /** @brief What a worker read, handed back through a shared_ptr rather than a reference. */
        struct Outcome
        {
            Uuid id;
            JsonValue facts;
        };

        struct Payload
        {
            std::vector<Request> requests;
            std::vector<Outcome> outcomes;
        };
    }

    /** @brief A chunk whose worker has finished, waiting for a pump to put it on the records. */
    struct StudioImportQueue::Chunk
    {
        std::shared_ptr<Payload> payload;
        std::size_t size = 0;
        bool cancelled = false;

        /** @brief How many of @ref payload's outcomes have been applied. */
        std::size_t applied = 0;
    };

    StudioImportQueue::StudioImportQueue() = default;

    StudioImportQueue::~StudioImportQueue() = default;

    std::size_t StudioImportQueue::request(const std::vector<Uuid>& ids)
    {
        // A request that arrives while nothing is running starts a new run rather than continuing
        // the last one's arithmetic -- otherwise the second import of a session would open at the
        // first one's finished fraction.
        if (!isRunning())
        {
            total_ = 0;
            done_ = 0;
        }

        std::size_t added = 0;
        for (const Uuid& id : ids)
        {
            if (!id.isValid()) { continue; }
            if (std::find(queued_.begin(), queued_.end(), id) != queued_.end()) { continue; }

            queued_.push_back(id);
            ++total_;
            ++added;
        }
        return added;
    }

    std::size_t StudioImportQueue::pump(StudioJobSystem& jobs, AssetDatabase& assets,
                                        const StudioImporterRegistry& importers)
    {
        // Jobs that have been drained are no longer known to the system, so the ones still in the
        // list are the ones still outstanding. Kept tidy here rather than in the completion,
        // because a completion that erased from this vector would be doing it while `drain` walks
        // its own.
        running_.erase(std::remove_if(running_.begin(), running_.end(),
                                      [&jobs](StudioJobId id) {
                                          return jobs.find(id).id == 0;
                                      }),
                       running_.end());

        // The live message comes from whichever chunk is actually reading. Taken here, once a
        // frame, rather than pushed from the worker: `reportProgress` is a snapshot the job system
        // already keeps, and a second copy pushed across would be a second thing to get stale.
        message_.clear();
        for (const StudioJobId id : running_)
        {
            const StudioJobStatus status = jobs.find(id);
            if (status.state == StudioJobState::Running && !status.message.empty())
            {
                message_ = status.message;
                break;
            }
        }

        // Whatever came back since the last pump, put on the records now. Budgeted, because a run
        // that finished several chunks at once would otherwise spend them all in one frame.
        std::size_t applied = 0;
        while (!landed_.empty() && applied < kChunkSize)
        {
            Chunk& chunk = landed_.front();

            while (chunk.applied < chunk.payload->outcomes.size() && applied < kChunkSize)
            {
                const Outcome& outcome = chunk.payload->outcomes[chunk.applied];
                ++read_;
                if (studioApplyImporterFacts(assets, outcome.id, outcome.facts)) { ++changed_; }
                ++chunk.applied;
                ++applied;
                ++done_;
            }

            if (chunk.applied < chunk.payload->outcomes.size()) { break; }

            // Whatever the chunk produced no outcome for. A cancelled job stopped part-way and the
            // rest were never looked at; a finished one looked at all of them and some were files
            // no importer could read. Told apart, because "stopped" and "not an asset Studio
            // reads" are different answers to a user asking why a number is not what they expected.
            const std::size_t untouched = chunk.size - chunk.payload->outcomes.size();
            if (chunk.cancelled) { cancelled_ += untouched; }
            else { unreadable_ += untouched; }
            done_ += untouched;

            landed_.erase(landed_.begin());
        }

        if (!isRunning()) { message_.clear(); }

        std::size_t submitted = 0;

        while (!queued_.empty())
        {
            // Asked before building anything. A refusal is "not now" and the work stays queued for
            // the next pump, which is backpressure without a wait (`plan.md` STUDIO-30002).
            if (!jobs.canAccept()) { break; }

            auto payload = std::make_shared<Payload>();
            payload->requests.reserve(std::min(kChunkSize, queued_.size()));

            std::vector<Uuid> taken;
            while (!queued_.empty() && payload->requests.size() < kChunkSize)
            {
                const Uuid id = queued_.front();
                queued_.erase(queued_.begin());
                taken.push_back(id);

                const AssetRecord* record = assets.find(id);
                if (record == nullptr)
                {
                    // Deleted between being asked for and being read. Not an error and not work:
                    // it stops being part of the run rather than counting as read or unreadable.
                    --total_;
                    taken.pop_back();
                    continue;
                }

                Request request;
                request.id = id;
                request.absolutePath = assets.resolvePath(record->sourcePath);
                request.sourcePath = record->sourcePath;
                request.settings = record->importerSettings;
                request.importer = importers.forType(record->type);
                payload->requests.push_back(std::move(request));
            }

            if (payload->requests.empty()) { continue; }

            const std::size_t chunkSize = payload->requests.size();
            const std::size_t offset = done_ + inFlight_;
            const std::size_t runTotal = total_;

            const StudioJobId job = jobs.submit(
                "Importing " + std::to_string(chunkSize) + " asset(s)",
                [payload, offset, runTotal](StudioJobContext& context) {
                    for (std::size_t index = 0; index < payload->requests.size(); ++index)
                    {
                        // Polled between files rather than inside one: a glTF parse is where the
                        // time goes and cgltf has no way to be interrupted part-way, so this is the
                        // finest grain honestly available. A chunk therefore stops within one
                        // file's worth of work of being asked to.
                        if (context.isCancelled()) { return; }

                        const Request& request = payload->requests[index];

                        // Reported against the *run*, not the chunk, so every chunk's numbers agree
                        // with the queue's own and a progress list shows one advancing fraction
                        // rather than four bars each filling separately.
                        context.reportProgress(
                            runTotal > 0 ? static_cast<float>(offset + index)
                                               / static_cast<float>(runTotal)
                                         : -1.0f,
                            "Reading " + request.sourcePath);

                        if (request.importer == nullptr) { continue; }

                        // The one line that reads a file. Everything around it exists so that this
                        // happens here instead of on the frame.
                        JsonValue facts =
                            request.importer->gatherFacts(request.absolutePath, request.settings);
                        if (facts.isNull()) { continue; }

                        payload->outcomes.push_back(Outcome{request.id, std::move(facts)});
                    }
                },
                [this, payload, chunkSize](const StudioJobStatus& status) {
                    // Main thread, from drain(). It hands the chunk over and nothing else: the
                    // facts are put on records by the next pump, which is budgeted, so a run that
                    // finishes four chunks at once does not become one frame of a hundred and
                    // twenty-eight sidecar writes.
                    //
                    // It also means nothing here holds an `AssetDatabase&` across frames. A
                    // completion that captured one would be correct exactly as long as the database
                    // outlived the job system, which is true today and is not a property this file
                    // can check.
                    inFlight_ -= std::min(inFlight_, chunkSize);
                    landed_.push_back(
                        Chunk{payload, chunkSize, status.state == StudioJobState::Cancelled, 0});
                });

            if (job == 0)
            {
                // Refused after all -- the system filled up between `canAccept` and here, or it is
                // shutting down. The chunk goes back to the front of the queue in its original
                // order, so nothing is lost and nothing is reordered.
                queued_.insert(queued_.begin(), taken.begin(), taken.end());
                break;
            }

            running_.push_back(job);
            inFlight_ += chunkSize;
            ++submitted;
        }

        return submitted;
    }

    std::size_t StudioImportQueue::cancel(StudioJobSystem& jobs)
    {
        const std::size_t givenUp = queued_.size() + inFlight_;

        // The queued ones never become a job, so nothing else will ever count them; they leave the
        // run's total as well, so the fraction still arrives at one rather than stopping short.
        cancelled_ += queued_.size();
        total_ -= std::min(total_, queued_.size());
        queued_.clear();

        // The in-flight ones are *asked*. Their completions still arrive and their chunks are still
        // applied, so what a worker managed to read before it stopped is not thrown away -- those
        // files really were read, and their facts really are what the files say. Counting them
        // here as well would count them twice.
        for (const StudioJobId id : running_) { (void)jobs.cancel(id); }

        return givenUp;
    }

    std::size_t StudioImportQueue::getLandedCount() const { return landed_.size(); }

    bool StudioImportQueue::isRunning() const
    {
        return !queued_.empty() || inFlight_ > 0 || !landed_.empty();
    }

    float StudioImportQueue::getProgress() const
    {
        // Zero total is "nothing has ever been asked for", which is not a fraction. A *finished*
        // run keeps its numbers until the next request, so a caller gets to show a full bar before
        // taking it away -- and can tell "done" from "idle" by isRunning().
        if (total_ == 0) { return -1.0f; }
        return std::min(1.0f, static_cast<float>(done_) / static_cast<float>(total_));
    }
}
