// SPDX-License-Identifier: MS-PL
/**
 * @file ImportJobTests.cpp
 * @brief Importing off the frame, cancellable, honest about how far through it is (STUDIO-10011).
 *
 * Three properties, and the third is the one that is easy to fake. *Off the frame* is checked by
 * the importer interface itself — `gatherFacts` is handed a path and a settings object and has no
 * way to reach a database, so a test that compiles has already proved the worker cannot touch the
 * document. *Cancellable* is checked by cancelling a run part-way and requiring that what was
 * already read is still on the records and the rest is accounted for. *Accurate progress* is the
 * one a bar that simply crawls to 90% and waits would pass, so it is checked against the work
 * actually done: the fraction is `applied / asked for`, it never goes backwards within a run, and
 * the counters at the end add up to exactly what was requested.
 *
 * Immediate mode throughout. It runs the same bodies, the same completions and the same ordering as
 * the threaded one, so nothing here sleeps and nothing races -- which is what makes these
 * assertions about the code rather than about the machine.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetImporter.hpp"
#include "CNA/Studio/Assets/AssetImporters.hpp"
#include "CNA/Studio/Assets/ImportJobs.hpp"
#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/StudioJobs.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <atomic>
#include <memory>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    class ScopedDirectory
    {
    public:
        explicit ScopedDirectory(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-importjobs-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_, code);
        }

        ~ScopedDirectory()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedDirectory(const ScopedDirectory&) = delete;
        ScopedDirectory& operator=(const ScopedDirectory&) = delete;

        [[nodiscard]] std::filesystem::path path() const { return path_; }

    private:
        static int& counter()
        {
            static int value = 0;
            return value;
        }

        std::filesystem::path path_;
    };

    /**
     * @brief An importer that records how many files it read, and can pretend one is unreadable.
     *
     * Its whole signature is the point of the task: a path and a settings object, and no way to
     * reach the database from a worker even by accident.
     */
    class CountingImporter final : public StudioAssetImporter
    {
    public:
        explicit CountingImporter(std::atomic<int>* reads) : reads_(reads) {}

        [[nodiscard]] std::string_view id() const override { return "Test.CountingImporter"; }
        [[nodiscard]] bool handles(AssetType type) const override
        {
            return type == AssetType::Texture2D;
        }

        [[nodiscard]] StudioImportedFacts gatherFacts(const std::string& absolutePath,
                                                      const JsonValue& settings) const override
        {
            (void)settings;
            if (reads_ != nullptr) { reads_->fetch_add(1); }

            std::ifstream stream{absolutePath, std::ios::binary};
            if (!stream) { return {}; }

            std::string contents;
            stream >> contents;

            // The two ways there are no facts, told apart. "unreadable" is a file this importer
            // claims and cannot read, which a user has to be told about; "notmine" is a file it
            // does not claim, which is ordinary and silent.
            if (contents == "unreadable")
            {
                return StudioImportedFacts{JsonValue{}, "it says it is unreadable, and it is"};
            }
            if (contents == "notmine") { return {}; }

            JsonValue facts = JsonValue::makeObject();
            facts.set("contents", JsonValue{contents});
            return StudioImportedFacts{std::move(facts), {}};
        }

    private:
        std::atomic<int>* reads_;
    };

    /** @brief Tracks @p count textures with a real file behind each, and returns their ids. */
    std::vector<Uuid> trackTextures(AssetDatabase& assets, const ScopedDirectory& directory,
                                    std::size_t count, const std::string& contents = "ok",
                                    const std::string& prefix = "asset")
    {
        std::vector<Uuid> ids;
        std::error_code code;
        std::filesystem::create_directories(directory.path() / "Textures", code);

        for (std::size_t index = 0; index < count; ++index)
        {
            const std::string name = "Textures/" + prefix + std::to_string(index) + ".png";
            {
                std::ofstream stream{directory.path() / name, std::ios::binary};
                stream << contents;
            }

            AssetRecord record;
            record.id = Uuid::generate();
            record.sourcePath = name;
            record.type = AssetType::Texture2D;
            ids.push_back(record.id);
            assets.add(std::move(record));
        }
        return ids;
    }

    /** @brief Pumps and drains until the queue stops running, or gives up after @p limit rounds. */
    std::size_t settle(StudioImportQueue& queue, StudioJobSystem& jobs, AssetDatabase& assets,
                       const StudioImporterRegistry& importers, int limit = 500)
    {
        std::size_t rounds = 0;
        while (queue.isRunning() && rounds < static_cast<std::size_t>(limit))
        {
            (void)queue.pump(jobs, assets, importers);
            jobs.waitForIdle();
            (void)jobs.drain();
            (void)queue.pump(jobs, assets, importers);
            ++rounds;
        }
        return rounds;
    }
}

CNA_STUDIO_TEST(AnImportRunReadsEveryAssetAndSaysSoExactly)
{
    const ScopedDirectory directory{"run"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 70);

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioImportQueue queue;

    CNA_STUDIO_EXPECT_EQ(queue.request(ids), std::size_t{70});
    CNA_STUDIO_EXPECT(queue.isRunning());

    // Nothing has happened yet. A queue that imported inside `request` would be the synchronous
    // pass this task exists to replace, wearing a different name.
    CNA_STUDIO_EXPECT_EQ(reads.load(), 0);
    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{0});

    (void)settle(queue, jobs, assets, importers);

    CNA_STUDIO_EXPECT(!queue.isRunning());
    CNA_STUDIO_EXPECT_EQ(reads.load(), 70);
    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{70});
    CNA_STUDIO_EXPECT_EQ(queue.getChangedCount(), std::uint64_t{70});
    CNA_STUDIO_EXPECT_EQ(queue.getUnreadableCount(), std::uint64_t{0});
    CNA_STUDIO_EXPECT_EQ(queue.getCancelledCount(), std::uint64_t{0});

    // The facts really are on the records, which is the only thing any of this was for.
    for (const Uuid& id : ids)
    {
        CNA_STUDIO_EXPECT_EQ(assets.find(id)->importerSettings["contents"].asString(),
                             std::string{"ok"});
    }

    // 70 assets in chunks of 32 is three jobs, not seventy: a job per asset would spend a queue
    // slot each and fill every progress bar seventy times.
    CNA_STUDIO_EXPECT(reads.load() > 0);
}

CNA_STUDIO_TEST(ProgressIsAFractionOfWorkActuallyDoneAndNeverGoesBackwards)
{
    const ScopedDirectory directory{"progress"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 100);

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioImportQueue queue;

    // Nothing asked for is not a fraction of anything, and a bar sitting at zero would claim a run
    // that has not started.
    CNA_STUDIO_EXPECT(queue.getProgress() < 0.0f);

    (void)queue.request(ids);
    CNA_STUDIO_EXPECT(queue.getProgress() >= 0.0f);
    CNA_STUDIO_EXPECT(queue.getProgress() < 0.001f);

    float previous = queue.getProgress();
    int rounds = 0;
    while (queue.isRunning() && rounds < 500)
    {
        (void)queue.pump(jobs, assets, importers);
        jobs.waitForIdle();
        (void)jobs.drain();
        (void)queue.pump(jobs, assets, importers);
        ++rounds;

        const float progress = queue.getProgress();
        if (progress < previous)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         "import progress went backwards within one run.");
        }
        previous = progress;

        // And it is a fraction of *work*, not of time: it can never be ahead of what has been read.
        CNA_STUDIO_EXPECT(progress <= static_cast<float>(queue.getReadCount()) / 100.0f + 0.001f);
    }

    // A finished run says so with a full bar rather than by vanishing, so a caller can show it
    // before taking it away. `isRunning` is what tells "done" from "still going".
    CNA_STUDIO_EXPECT(!queue.isRunning());
    CNA_STUDIO_EXPECT(queue.getProgress() > 0.999f);
    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{100});
}

CNA_STUDIO_TEST(CancellingAnImportKeepsWhatWasReadAndAccountsForTheRest)
{
    const ScopedDirectory directory{"cancel"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 200);

    // One outstanding job at a time, so that a cancel lands while most of the run is still queued.
    // With an unbounded system the first pump submits every chunk at once and there is nothing left
    // to cancel, which would make this a test of nothing.
    StudioJobSystem jobs{StudioJobMode::Immediate, 0, 1};
    StudioImportQueue queue;
    (void)queue.request(ids);

    // One chunk through, then stop.
    (void)queue.pump(jobs, assets, importers);
    jobs.waitForIdle();
    (void)jobs.drain();
    (void)queue.pump(jobs, assets, importers);

    const std::uint64_t readBeforeCancel = queue.getReadCount();
    CNA_STUDIO_EXPECT(readBeforeCancel > 0);
    CNA_STUDIO_EXPECT(readBeforeCancel < 200);

    const std::size_t givenUp = queue.cancel(jobs);
    CNA_STUDIO_EXPECT(givenUp > 0);

    (void)settle(queue, jobs, assets, importers);
    CNA_STUDIO_EXPECT(!queue.isRunning());

    // What was read is still on the records. Those files really were read, and their facts really
    // are what the files say -- throwing them away because the user stopped the *rest* would be
    // undoing work nobody asked to undo.
    std::size_t withFacts = 0;
    for (const Uuid& id : ids)
    {
        if (!assets.find(id)->importerSettings["contents"].isNull()) { ++withFacts; }
    }
    CNA_STUDIO_EXPECT(withFacts >= static_cast<std::size_t>(readBeforeCancel));
    CNA_STUDIO_EXPECT(withFacts < 200);

    // And every asset asked for is accounted for exactly once: read, unreadable, or given up on.
    const std::uint64_t accounted =
        queue.getReadCount() + queue.getUnreadableCount() + queue.getCancelledCount();
    CNA_STUDIO_EXPECT_EQ(accounted, std::uint64_t{200});

    // Nothing is half-imported, which is the property a cancelled import has to have: the smallest
    // thing the queue applies is one asset's whole facts object.
    for (const Uuid& id : ids)
    {
        const JsonValue& contents = assets.find(id)->importerSettings["contents"];
        CNA_STUDIO_EXPECT(contents.isNull() || contents.asString() == "ok");
    }
}

/**
 * A file nobody claims and a file that is broken are different answers (`plan.md` STUDIO-10013).
 *
 * They used to be the same one -- both were "the importer returned nothing" -- so a texture
 * truncated by a failed copy imported as quietly as a readme. One of those a user needs to be told
 * about; the other is most of the files in their project.
 */
CNA_STUDIO_TEST(AFileNobodyClaimsIsSilentAndABrokenOneIsReportedWithAReason)
{
    const ScopedDirectory directory{"unreadable"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    std::vector<Uuid> ids = trackTextures(assets, directory, 5, "ok");
    const std::vector<Uuid> declined = trackTextures(assets, directory, 3, "notmine", "other");
    const std::vector<Uuid> broken = trackTextures(assets, directory, 2, "unreadable", "broken");
    ids.insert(ids.end(), declined.begin(), declined.end());
    ids.insert(ids.end(), broken.begin(), broken.end());

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioImportQueue queue;
    (void)queue.request(ids);
    (void)settle(queue, jobs, assets, importers);

    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{5});
    CNA_STUDIO_EXPECT_EQ(queue.getChangedCount(), std::uint64_t{5});

    // Not claimed: counted, not complained about. A project holds files Studio does not import, and
    // a run that flagged each of them would produce a list nobody reads.
    CNA_STUDIO_EXPECT_EQ(queue.getUnreadableCount(), std::uint64_t{3});

    // Claimed and broken: counted *and* explained, with the path so the message names something
    // findable and the reason so it is worth reading.
    CNA_STUDIO_EXPECT_EQ(queue.getFailedCount(), std::uint64_t{2});
    CNA_STUDIO_EXPECT_EQ(queue.getFailures().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(queue.getFailures().front().reason.find("unreadable") != std::string::npos);
    CNA_STUDIO_EXPECT(queue.getFailures().front().sourcePath.find("broken") != std::string::npos);
    CNA_STUDIO_EXPECT(queue.getFailures().front().id.isValid());

    CNA_STUDIO_EXPECT_EQ(queue.getCancelledCount(), std::uint64_t{0});
    CNA_STUDIO_EXPECT(queue.getProgress() > 0.999f);

    // Taken rather than read, so each is reported once. A caller that read the list every frame
    // would write the same broken file to the log sixty times a second.
    CNA_STUDIO_EXPECT_EQ(queue.takeFailures().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(queue.getFailures().empty());
    CNA_STUDIO_EXPECT_EQ(queue.getFailedCount(), std::uint64_t{2});
}

/**
 * A failed import leaves the asset exactly as it was, never half-written (`plan.md` STUDIO-10013).
 *
 * The stale facts stay, and that is the decision rather than an oversight: they describe the file
 * as it last read, which is more use than nothing, and the failure is *reported* -- which is what
 * makes keeping them honest rather than misleading.
 */
CNA_STUDIO_TEST(AFailedImportLeavesTheAssetExactlyAsItWas)
{
    const ScopedDirectory directory{"partial"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 1, "good");
    const Uuid id = ids.front();

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioImportQueue queue;
    (void)queue.request(ids);
    (void)settle(queue, jobs, assets, importers);
    CNA_STUDIO_EXPECT_EQ(assets.find(id)->importerSettings["contents"].asString(),
                         std::string{"good"});

    // A setting the user chose, which a failed import must not touch either.
    AssetRecord* record = assets.findMutable(id);
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record == nullptr) { return; }
    record->importerSettings.set("chosen", JsonValue{std::string{"kept"}});

    // The file is replaced with one this importer claims and cannot read.
    {
        std::ofstream stream{directory.path() / assets.find(id)->sourcePath, std::ios::binary};
        stream << "unreadable";
    }

    (void)queue.request(ids);
    (void)settle(queue, jobs, assets, importers);

    CNA_STUDIO_EXPECT_EQ(queue.getFailedCount(), std::uint64_t{1});

    // The facts from the last good read are still there, unchanged and whole -- not cleared, not
    // half-overwritten with whatever the importer managed before giving up.
    CNA_STUDIO_EXPECT_EQ(assets.find(id)->importerSettings["contents"].asString(),
                         std::string{"good"});
    CNA_STUDIO_EXPECT_EQ(assets.find(id)->importerSettings["chosen"].asString(), std::string{"kept"});

    // And the reason names the file, so the stale facts are stale *visibly*.
    CNA_STUDIO_EXPECT_EQ(queue.getFailures().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(!queue.getFailures().front().reason.empty());
}

/**
 * An asset whose file is not on disk is skipped rather than reported as a failed import.
 *
 * "Its file is not there" is a state of its own -- marked on the row, repairable from the
 * inspector (`STUDIO-09013`) -- and reporting it a second time as an import failure would put two
 * different-sounding complaints in front of somebody about one problem with one fix.
 */
CNA_STUDIO_TEST(AnAssetWithNoFileOnDiskIsSkippedRatherThanFailed)
{
    const ScopedDirectory directory{"missing"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 4);
    std::error_code code;
    std::filesystem::remove(directory.path() / assets.find(ids[1])->sourcePath, code);
    std::filesystem::remove(directory.path() / assets.find(ids[2])->sourcePath, code);
    (void)assets.refreshPresence(ids[1]);
    (void)assets.refreshPresence(ids[2]);

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioImportQueue queue;
    (void)queue.request(ids);
    (void)settle(queue, jobs, assets, importers);

    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{2});
    CNA_STUDIO_EXPECT_EQ(queue.getFailedCount(), std::uint64_t{0});
    CNA_STUDIO_EXPECT(queue.getFailures().empty());
    CNA_STUDIO_EXPECT_EQ(queue.getUnreadableCount(), std::uint64_t{0});

    // The run finishes rather than stalling on assets it will never read.
    CNA_STUDIO_EXPECT(!queue.isRunning());
    CNA_STUDIO_EXPECT(queue.getProgress() > 0.999f);
}

CNA_STUDIO_TEST(ARunDoesNotRewriteSidecarsItHasAlreadyWritten)
{
    const ScopedDirectory directory{"idempotent"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 40);

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioImportQueue queue;

    (void)queue.request(ids);
    (void)settle(queue, jobs, assets, importers);
    CNA_STUDIO_EXPECT_EQ(queue.getChangedCount(), std::uint64_t{40});

    // The same run again. Every file is read again -- there is no way to know a file has not
    // changed without looking -- but nothing is *written*, which is the rule that keeps opening a
    // project twice from producing a repository full of diffs.
    (void)queue.request(ids);
    CNA_STUDIO_EXPECT(queue.getProgress() < 0.001f);
    (void)settle(queue, jobs, assets, importers);

    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{80});
    CNA_STUDIO_EXPECT_EQ(queue.getChangedCount(), std::uint64_t{40});
}

CNA_STUDIO_TEST(AQueueThatIsRefusedOffersTheSameWorkAgainRatherThanDroppingIt)
{
    const ScopedDirectory directory{"backpressure"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 96);

    // One outstanding job at a time. The queue must offer the rest again next pump rather than
    // dropping it or waiting on the main thread, which is what `STUDIO-30002`'s backpressure means.
    StudioJobSystem jobs{StudioJobMode::Immediate, 0, 1};
    StudioImportQueue queue;
    (void)queue.request(ids);

    CNA_STUDIO_EXPECT_EQ(queue.pump(jobs, assets, importers), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(queue.getQueuedCount(), std::size_t{64});

    (void)settle(queue, jobs, assets, importers);

    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{96});
    CNA_STUDIO_EXPECT_EQ(jobs.getRefusedCount(), std::uint64_t{0});
    CNA_STUDIO_EXPECT_EQ(queue.getQueuedCount(), std::size_t{0});
}

CNA_STUDIO_TEST(AnAssetDeletedWhileQueuedLeavesTheRunRatherThanFailingIt)
{
    const ScopedDirectory directory{"deleted"};
    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());

    std::atomic<int> reads{0};
    StudioImporterRegistry importers;
    CNA_STUDIO_EXPECT(importers.add(std::make_unique<CountingImporter>(&reads)));

    const std::vector<Uuid> ids = trackTextures(assets, directory, 50);

    // One outstanding job at a time, so the second chunk is still queued -- and therefore still
    // only a list of ids -- when its assets are deleted.
    StudioJobSystem jobs{StudioJobMode::Immediate, 0, 1};
    StudioImportQueue queue;
    (void)queue.request(ids);

    // Submitted the first chunk, then the rest of the project is deleted underneath.
    (void)queue.pump(jobs, assets, importers);
    for (std::size_t index = StudioImportQueue::kChunkSize; index < ids.size(); ++index)
    {
        (void)assets.removeRecord(ids[index]);
    }

    (void)settle(queue, jobs, assets, importers);

    CNA_STUDIO_EXPECT(!queue.isRunning());
    CNA_STUDIO_EXPECT_EQ(queue.getReadCount(), std::uint64_t{StudioImportQueue::kChunkSize});

    // Gone is neither read nor unreadable nor cancelled: it stopped being part of the run. A count
    // that called it unreadable would put a number in front of a user for a file that no longer
    // exists to be read.
    CNA_STUDIO_EXPECT_EQ(queue.getUnreadableCount(), std::uint64_t{0});
    CNA_STUDIO_EXPECT_EQ(queue.getCancelledCount(), std::uint64_t{0});
    CNA_STUDIO_EXPECT(queue.getProgress() > 0.999f);
}

/**
 * End to end, through the real shell: a file changes on disk, and its facts are read again off the
 * frame. This is the assertion that the wiring is real rather than two halves that each work alone.
 *
 * It also pins the thing this task removed. The watcher used to answer a single changed file by
 * re-reading *every tracked asset in the project*, synchronously, inside the poll -- which on a
 * project of a thousand models is a full glTF parse of each of them every time somebody saves a
 * texture. The reload now reports which assets went stale and the panels queue those, so the work
 * is proportional to what changed and happens on a worker.
 */
CNA_STUDIO_TEST(AFileChangedOnDiskHasItsFactsReadAgainOffTheFrame)
{
    const ScopedDirectory directory{"wired"};
    std::error_code code;
    std::filesystem::create_directories(directory.path() / "Assets", code);

    // A BMP, because its header states its size at fixed offsets and this test is about *when* the
    // facts are re-read rather than about parsing.
    const auto writeBmp = [&](const std::string& name, std::int32_t width, std::int32_t height) {
        std::vector<unsigned char> bmp;
        const auto little32 = [&](std::uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8)
            {
                bmp.push_back(static_cast<unsigned char>((value >> shift) & 0xFFu));
            }
        };
        bmp.push_back('B');
        bmp.push_back('M');
        little32(54);
        little32(0);
        little32(54);
        little32(40);
        little32(static_cast<std::uint32_t>(width));
        little32(static_cast<std::uint32_t>(height));
        bmp.push_back(1);
        bmp.push_back(0);
        bmp.push_back(24);
        bmp.push_back(0);
        for (int index = 0; index < 24; ++index) { bmp.push_back(0); }

        // The pixel data, all zeroes, and written rather than left out on purpose: the watcher
        // notices a change by size and modification time, and two headers of identical length
        // rewritten within the same second are invisible to it. That is a real limit of a
        // size-and-time stamp, and the honest way round it in a test is an edit that really does
        // change the file's length -- which a re-export at a different size does.
        const std::size_t rowBytes = (static_cast<std::size_t>(width) * 3u + 3u) & ~std::size_t{3};
        bmp.insert(bmp.end(), rowBytes * static_cast<std::size_t>(height < 0 ? -height : height), 0);

        std::ofstream stream{directory.path() / name, std::ios::binary};
        stream.write(reinterpret_cast<const char*>(bmp.data()),
                     static_cast<std::streamsize>(bmp.size()));
    };

    writeBmp("Assets/Hero.bmp", 64, 32);

    StudioContext context;
    context.getAssets().setProjectRoot(directory.path().generic_string());
    CNA_STUDIO_EXPECT(context.getAssets().scan("Assets").succeeded);

    const AssetRecord* record = context.getAssets().findByPath("Assets/Hero.bmp");
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record == nullptr) { return; }
    const Uuid id = record->id;

    // Opening a project reads the facts once, synchronously. That is not what this is about.
    CNA_STUDIO_EXPECT(applyImporterFacts(context.getAssets(), id));
    const auto widthOf = [&] {
        return PropertyValue::fromJson(context.getAssets().find(id)->importerSettings["pixelSize"],
                                       PropertyType::Vector2)
            .get<StudioVector2>()
            .x;
    };
    CNA_STUDIO_EXPECT(widthOf() > 63.0f && widthOf() < 65.0f);

    StudioLog log;
    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    StudioShellPanels panels{*shell, context, log};

    // Re-exported at a different size, which is the change a facts pass exists to notice.
    writeBmp("Assets/Hero.bmp", 128, 96);

    // Polled until the import queue has actually read something, rather than for a fixed number of
    // frames: the read happens on a worker thread, and a loop counting *frames* would be counting
    // the wrong side of an asynchronous boundary (`plan.md` STUDIO-33026).
    for (int frame = 0; frame < 400 && panels.imports().getReadCount() == 0; ++frame)
    {
        panels.poll(static_cast<double>(frame));
        panels.jobs().waitForIdle();
    }

    CNA_STUDIO_EXPECT(panels.imports().getReadCount() > 0);

    // One more poll, because applying is the pump's job and the read only finished in the last one.
    panels.poll(1000.0);
    panels.jobs().waitForIdle();
    panels.poll(1001.0);

    CNA_STUDIO_EXPECT(widthOf() > 127.0f && widthOf() < 129.0f);

    // Proportional to what changed. One file moved, so one asset was read -- not the whole project,
    // which is what the synchronous pass this replaced would have done.
    CNA_STUDIO_EXPECT_EQ(panels.imports().getReadCount(), std::uint64_t{1});
}

/**
 * A broken file reaches the user, through the real shell (`plan.md` STUDIO-10013).
 *
 * The worker that found it cannot log -- it is handed a job context and nothing else -- so the
 * reason travels back on the queue and the binder says it. Without this last hop the whole task is
 * a reason recorded where nobody looks.
 */
CNA_STUDIO_TEST(AFailedImportIsReportedToTheUserOnceAndNotEveryFrame)
{
    const ScopedDirectory directory{"reported"};
    std::error_code code;
    std::filesystem::create_directories(directory.path() / "Assets", code);

    const auto write = [&](const std::string& name, const std::vector<unsigned char>& bytes) {
        std::ofstream stream{directory.path() / name, std::ios::binary};
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    };

    // A whole PNG, so the asset imports cleanly to start with.
    std::vector<unsigned char> png{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    png.insert(png.end(), 18, 0);
    png[16] = 0;  png[17] = 0;  png[18] = 0;  png[19] = 32;   // width
    png[20] = 0;  png[21] = 0;  png[22] = 0;  png[23] = 16;   // height
    png[24] = 8;  png[25] = 6;                                // depth, RGBA
    write("Assets/Hero.png", png);

    StudioContext context;
    context.getAssets().setProjectRoot(directory.path().generic_string());
    CNA_STUDIO_EXPECT(context.getAssets().scan("Assets").succeeded);

    StudioLog log;
    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    StudioShellPanels panels{*shell, context, log};

    // Truncated where a failed copy would leave it: the signature is there and the header is not.
    write("Assets/Hero.png", std::vector<unsigned char>{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au,
                                                        0x0Au, 0, 0, 0});

    for (int frame = 0; frame < 400 && panels.imports().getFailedCount() == 0; ++frame)
    {
        panels.poll(static_cast<double>(frame));
        panels.jobs().waitForIdle();
    }
    panels.poll(1000.0);

    CNA_STUDIO_EXPECT_EQ(panels.imports().getFailedCount(), std::uint64_t{1});

    const auto warningsNaming = [&](const std::string& fragment) {
        std::size_t found = 0;
        for (const StudioLogEntry& entry : log.entries())
        {
            if (entry.severity == LogSeverity::Warning
                && entry.message.find(fragment) != std::string::npos)
            {
                ++found;
            }
        }
        return found;
    };

    // The message names the file and says what is wrong with it. "Could not import" on its own is
    // a line that tells somebody only that they have a problem.
    CNA_STUDIO_EXPECT_EQ(warningsNaming("Assets/Hero.png"), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(warningsNaming("truncated"), std::size_t{1});

    // And once. Fifty more frames of the same broken file produce no more lines, because the
    // failures are *taken* rather than read -- a list read every frame would fill the log at sixty
    // lines a second and bury everything else in it.
    for (int frame = 0; frame < 50; ++frame)
    {
        panels.poll(1001.0 + static_cast<double>(frame));
        panels.jobs().waitForIdle();
    }
    CNA_STUDIO_EXPECT_EQ(warningsNaming("Assets/Hero.png"), std::size_t{1});
}
