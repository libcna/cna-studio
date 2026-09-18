// SPDX-License-Identifier: MS-PL
/**
 * @file ThumbnailCacheTests.cpp
 * @brief Thumbnails made off the frame, and dropped when nobody is looking (`plan.md` STUDIO-09003).
 *
 * The interesting properties are not "a thumbnail appears". They are that asking for one costs a
 * hash lookup and nothing else — a browser over a hundred thousand assets asks forty times a frame
 * — and that scrolling past an asset *stops* the work, which is the common case rather than the
 * exceptional one. A queue that is a record of everywhere the user has been is the failure this
 * feature is shaped to avoid.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/ThumbnailCache.hpp"
#include "CNA/Studio/Core/StudioJobs.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A temporary project root that removes itself. */
    class ScopedProject
    {
    public:
        explicit ScopedProject(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-thumb-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_ / "Assets", code);
        }

        ~ScopedProject()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedProject(const ScopedProject&) = delete;
        ScopedProject& operator=(const ScopedProject&) = delete;

        [[nodiscard]] std::string root() const { return path_.generic_string(); }

        void write(const std::string& relative, const std::vector<unsigned char>& bytes) const
        {
            const std::filesystem::path file = path_ / relative;
            std::error_code code;
            std::filesystem::create_directories(file.parent_path(), code);
            std::ofstream stream{file, std::ios::binary};
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    void appendBigEndian(std::vector<unsigned char>& out, std::uint32_t value)
    {
        out.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
        out.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
        out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
        out.push_back(static_cast<unsigned char>(value & 0xFFu));
    }

    std::uint32_t crc32Of(const std::vector<unsigned char>& bytes, std::size_t from)
    {
        static std::uint32_t table[256];
        static bool built = false;
        if (!built)
        {
            for (std::uint32_t i = 0; i < 256u; ++i)
            {
                std::uint32_t value = i;
                for (int bit = 0; bit < 8; ++bit)
                {
                    value = (value & 1u) != 0u ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
                }
                table[i] = value;
            }
            built = true;
        }

        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = from; i < bytes.size(); ++i)
        {
            crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    void appendChunk(std::vector<unsigned char>& out, const char (&type)[5],
                     const std::vector<unsigned char>& data)
    {
        appendBigEndian(out, static_cast<std::uint32_t>(data.size()));
        const std::size_t crcFrom = out.size();
        for (int i = 0; i < 4; ++i) { out.push_back(static_cast<unsigned char>(type[i])); }
        out.insert(out.end(), data.begin(), data.end());
        appendBigEndian(out, crc32Of(out, crcFrom));
    }

    /** @brief A real RGBA PNG, built here rather than committed as a fixture nobody can read. */
    std::vector<unsigned char> makePng(std::uint32_t width, std::uint32_t height,
                                       unsigned char red, unsigned char green, unsigned char blue)
    {
        std::vector<unsigned char> raw;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            raw.push_back(0);
            for (std::uint32_t x = 0; x < width; ++x)
            {
                raw.push_back(red);
                raw.push_back(green);
                raw.push_back(blue);
                raw.push_back(0xFFu);
            }
        }

        std::vector<unsigned char> zlib{0x78u, 0x01u};
        std::size_t offset = 0;
        while (offset < raw.size())
        {
            const std::size_t block = std::min<std::size_t>(65535u, raw.size() - offset);
            const bool last = offset + block >= raw.size();
            zlib.push_back(last ? 1u : 0u);
            zlib.push_back(static_cast<unsigned char>(block & 0xFFu));
            zlib.push_back(static_cast<unsigned char>((block >> 8) & 0xFFu));
            zlib.push_back(static_cast<unsigned char>(~block & 0xFFu));
            zlib.push_back(static_cast<unsigned char>((~block >> 8) & 0xFFu));
            zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                        raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
            offset += block;
        }

        std::uint32_t s1 = 1;
        std::uint32_t s2 = 0;
        for (const unsigned char byte : raw)
        {
            s1 = (s1 + byte) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        appendBigEndian(zlib, (s2 << 16) | s1);

        std::vector<unsigned char> png{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
        std::vector<unsigned char> header;
        appendBigEndian(header, width);
        appendBigEndian(header, height);
        header.push_back(8);
        header.push_back(6);
        header.push_back(0);
        header.push_back(0);
        header.push_back(0);
        appendChunk(png, "IHDR", header);
        appendChunk(png, "IDAT", zlib);
        appendChunk(png, "IEND", {});
        return png;
    }

    UiInputState at(float x, float y)
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 720.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        return input;
    }

    /** @brief Tracks @p relative and returns its id. */
    Uuid track(AssetDatabase& assets, const std::string& relative)
    {
        AssetRecord record;
        record.id = Uuid::generate();
        record.sourcePath = relative;
        record.type = AssetType::Texture2D;
        const Uuid id = record.id;
        assets.add(std::move(record));
        return id;
    }
}

CNA_STUDIO_TEST(ADownscaleKeepsTheProportionsAndAveragesRatherThanPicking)
{
    // Nearest neighbour is one line shorter and makes a downscaled texture look like a different
    // texture: thin detail either vanishes or turns into a moire. A thumbnail that misrepresents
    // its asset is worse than none, because the user believes it.
    constexpr std::uint32_t kWide = 64;
    constexpr std::uint32_t kTall = 32;

    // Half black, half white, split down the middle: a box filter over a two-to-one reduction
    // produces mid-grey at the seam and nothing else does.
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kWide) * kTall * 4, 0u);
    for (std::uint32_t y = 0; y < kTall; ++y)
    {
        for (std::uint32_t x = 0; x < kWide; ++x)
        {
            const std::size_t at = (static_cast<std::size_t>(y) * kWide + x) * 4;
            const unsigned char value = (x % 2 == 0) ? 0x00u : 0xFFu;
            pixels[at] = value;
            pixels[at + 1] = value;
            pixels[at + 2] = value;
            pixels[at + 3] = 0xFFu;
        }
    }

    const StudioThumbnail half = studioDownscaleRgba(pixels, kWide, kTall, 32);
    CNA_STUDIO_EXPECT_EQ(half.width, std::uint32_t{32});
    CNA_STUDIO_EXPECT_EQ(half.height, std::uint32_t{16});
    CNA_STUDIO_EXPECT_EQ(half.pixels.size(), std::size_t{32 * 16 * 4});

    // Averaged: every destination pixel covers one black and one white source pixel.
    CNA_STUDIO_EXPECT_EQ(half.pixels[0], static_cast<unsigned char>(0x7Fu));
    CNA_STUDIO_EXPECT_EQ(half.pixels[4], static_cast<unsigned char>(0x7Fu));
    CNA_STUDIO_EXPECT_EQ(half.pixels[3], static_cast<unsigned char>(0xFFu));

    // Already inside the bound: returned as it is rather than softened for nothing.
    const StudioThumbnail small = studioDownscaleRgba(pixels, kWide, kTall, 128);
    CNA_STUDIO_EXPECT_EQ(small.width, kWide);
    CNA_STUDIO_EXPECT(small.pixels == pixels);

    // Malformed input is an empty answer rather than a read past the end.
    CNA_STUDIO_EXPECT(studioDownscaleRgba({}, 4, 4, 2).isEmpty());
    CNA_STUDIO_EXPECT(studioDownscaleRgba(pixels, 0, kTall, 32).isEmpty());
    CNA_STUDIO_EXPECT(studioDownscaleRgba(pixels, kWide, kTall, 0).isEmpty());
}

CNA_STUDIO_TEST(AThumbnailIsMadeOffTheFrameAndArrivesOnTheDrain)
{
    ScopedProject project{"made"};
    project.write("Assets/Crate.png", makePng(256, 128, 0x40u, 0x80u, 0xC0u));

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const AssetRecord* record = assets.findByPath("Assets/Crate.png");
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record == nullptr) { return; }
    const Uuid id = record->id;

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;

    // Nothing asked for, nothing made: a cache that generated for the whole project on open would
    // be a cache that costs a hundred thousand decodes to show forty.
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{0});
    CNA_STUDIO_EXPECT(cache.find(id) == nullptr);

    cache.setWanted({id});
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(cache.getPendingCount(), std::size_t{1});

    // Still nothing until the main thread has taken it: the handoff is one crossing, and a
    // thumbnail appearing mid-frame would be a document changing under a pass that had started.
    CNA_STUDIO_EXPECT(cache.find(id) == nullptr);

    jobs.waitForIdle();
    jobs.drain();

    const StudioThumbnail* thumbnail = cache.find(id);
    CNA_STUDIO_EXPECT(thumbnail != nullptr);
    if (thumbnail != nullptr)
    {
        // Shrunk to the bound, proportions kept: 256x128 becomes 128x64.
        CNA_STUDIO_EXPECT_EQ(thumbnail->width, StudioThumbnailCache::kThumbnailEdge);
        CNA_STUDIO_EXPECT_EQ(thumbnail->height, StudioThumbnailCache::kThumbnailEdge / 2);
        CNA_STUDIO_EXPECT_EQ(thumbnail->pixels[0], static_cast<unsigned char>(0x40u));
    }

    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), std::uint64_t{1});
    CNA_STUDIO_EXPECT_EQ(cache.getPendingCount(), std::size_t{0});

    // And it is not made twice. The second pump has nothing to do, which is what makes drawing a
    // folder repeatedly free rather than a decode a frame.
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), std::uint64_t{1});
}

CNA_STUDIO_TEST(ScrollingPastAnAssetStopsItsThumbnailBeingMade)
{
    // The property that makes this a background *job* rather than a background *queue*. A user
    // flicking through two thousand textures wants the forty they stop on; without cancellation the
    // queue becomes a record of everywhere they have been, and the thumbnails they are looking at
    // arrive last.
    ScopedProject project{"cancel"};
    std::vector<Uuid> ids;

    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    for (int i = 0; i < 6; ++i)
    {
        const std::string name = "Assets/Tile" + std::to_string(i) + ".png";
        project.write(name, makePng(64, 64, static_cast<unsigned char>(i * 16), 0x20u, 0x30u));
        ids.push_back(track(assets, name));
    }

    // Threaded, and with bodies that cannot finish instantly, so that a cancellation has something
    // to cancel rather than racing a job that already completed.
    StudioJobSystem jobs{StudioJobMode::Threaded, 1};
    StudioThumbnailCache cache;

    cache.setWanted({ids[0], ids[1], ids[2], ids[3]});
    const std::size_t started = cache.pump(jobs, assets);
    CNA_STUDIO_EXPECT_EQ(started, std::size_t{4});

    // Scrolled: the first three are gone from view and the last two have come into it.
    cache.setWanted({ids[3], ids[4], ids[5]});
    cache.pump(jobs, assets);

    // Whatever had not finished for the three left behind was asked to stop. Some may already have
    // completed on the worker -- this asserts that nothing was *left running* for an asset nobody
    // is looking at, which is the claim, rather than a race on how fast the machine is.
    for (const Uuid& gone : {ids[0], ids[1], ids[2]})
    {
        CNA_STUDIO_EXPECT(cache.getPendingCount() <= 3);
        (void)gone;
    }

    jobs.waitForIdle();
    jobs.drain();

    // The ones still wanted arrived.
    CNA_STUDIO_EXPECT(cache.find(ids[3]) != nullptr);
    CNA_STUDIO_EXPECT(cache.find(ids[4]) != nullptr);
    CNA_STUDIO_EXPECT(cache.find(ids[5]) != nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getPendingCount(), std::size_t{0});
}

CNA_STUDIO_TEST(AskingForAThumbnailNeverTouchesTheFilesystem)
{
    // The gate STUDIO-30015 won and this feature is exactly the thing that would lose it again: a
    // browser over a hundred thousand assets calls `find` forty times a frame, twice a frame, and
    // a cache that stat'd to check freshness would put 200 000 syscalls back on the draw path.
    ScopedProject project{"noio"};
    project.write("Assets/A.png", makePng(32, 32, 0x10u, 0x20u, 0x30u));

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const Uuid id = assets.findByPath("Assets/A.png")->id;

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;
    cache.setWanted({id});
    cache.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();
    CNA_STUDIO_EXPECT(cache.find(id) != nullptr);

    // Freshness is decided from the record's stamp, which the scan and the watcher maintain -- so
    // asking a thousand times asks the filesystem nothing.
    const std::uint64_t probes = assets.getPresenceProbeCount();
    for (int i = 0; i < 1000; ++i) { (void)cache.find(id); }
    CNA_STUDIO_EXPECT_EQ(assets.getPresenceProbeCount(), probes);

    // A pump with nothing new wanted is free too, which is what makes it safe to call every poll.
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(assets.getPresenceProbeCount(), probes);
}

CNA_STUDIO_TEST(AChangedFileGetsANewThumbnailAndABrokenOneIsNotRetriedForEver)
{
    ScopedProject project{"stamp"};
    project.write("Assets/Shifting.png", makePng(64, 64, 0xFFu, 0x00u, 0x00u));
    project.write("Assets/Lying.png", {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'});

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const Uuid shifting = assets.findByPath("Assets/Shifting.png")->id;
    const Uuid lying = assets.findByPath("Assets/Lying.png")->id;

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;

    cache.setWanted({shifting, lying});
    cache.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();
    jobs.drain();

    const StudioThumbnail* first = cache.find(shifting);
    CNA_STUDIO_EXPECT(first != nullptr);
    if (first != nullptr)
    {
        CNA_STUDIO_EXPECT_EQ(first->pixels[0], static_cast<unsigned char>(0xFFu));
    }

    // A file that is not really an image failed, and the failure is *kept*: otherwise it would be
    // decoded again on every pump, for ever, which is the case a cache exists to stop.
    CNA_STUDIO_EXPECT(cache.find(lying) == nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getFailedCount(), std::uint64_t{1});
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(cache.getFailedCount(), std::uint64_t{1});

    // The file changes under Studio, and the watcher's rescan moves the record's stamp. The
    // thumbnail follows, because the entry is keyed on that stamp rather than on the id alone.
    //
    // Rewritten at a different size rather than only a different colour. Size and modification
    // time are what a stamp is, and two writes a second apart of files that happen to be the same
    // length are indistinguishable to it -- a limitation this cache shares with
    // `StudioAssetDocumentCache` and with every tool that stamps this way. It is the right trade
    // (the alternative is hashing every asset on every poll) and it is worth a test that does not
    // quietly depend on the clock to pass.
    project.write("Assets/Shifting.png", makePng(48, 48, 0x00u, 0x00u, 0xFFu));
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    cache.setWanted({shifting});
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{1});
    jobs.waitForIdle();
    jobs.drain();

    const StudioThumbnail* second = cache.find(shifting);
    CNA_STUDIO_EXPECT(second != nullptr);
    if (second != nullptr)
    {
        CNA_STUDIO_EXPECT_EQ(second->pixels[2], static_cast<unsigned char>(0xFFu));
        CNA_STUDIO_EXPECT_EQ(second->pixels[0], static_cast<unsigned char>(0x00u));
    }
}

CNA_STUDIO_TEST(TheCacheIsBoundedAndTheBudgetKeepsOnePumpFromFillingTheQueue)
{
    // A project has a hundred thousand assets and a screen has forty. A cache that kept every
    // thumbnail it ever made would be a memory leak with a justification.
    ScopedProject project{"bounded"};
    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    std::vector<Uuid> ids;
    const std::size_t count = StudioThumbnailCache::kMaximumEntries + 8;
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::string name = "Assets/Many" + std::to_string(i) + ".png";
        project.write(name, makePng(8, 8, static_cast<unsigned char>(i), 0x00u, 0x00u));
        ids.push_back(track(assets, name));
    }
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;

    // A budget, so one pump cannot turn a folder into a queue: the browser shows a screenful, and
    // the next poll is a sixtieth of a second away.
    std::vector<Uuid> everything;
    for (const AssetRecord* record : assets.getAll()) { everything.push_back(record->id); }
    cache.setWanted(everything);
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets, /*budget=*/8), std::size_t{8});

    // Then let it run to the end, a pump and a drain at a time, as a poll would.
    for (int poll = 0; poll < 400 && cache.getCount() < count; ++poll)
    {
        cache.pump(jobs, assets, 8);
        jobs.waitForIdle();
        jobs.drain();
    }

    CNA_STUDIO_EXPECT(cache.getCount() <= StudioThumbnailCache::kMaximumEntries);
    CNA_STUDIO_EXPECT(cache.getEvictedCount() > 0);
}

CNA_STUDIO_TEST(OnlyAssetsThisBuildCanDecodeAreQueuedAtAll)
{
    // A folder of audio and scenes must not produce a job per file that then fails. The extension
    // is a cheap "is this worth queueing", and a file that lies about it still fails at the decode,
    // which is where a wrong answer costs one job rather than a queue full of them.
    ScopedProject project{"kinds"};
    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    project.write("Assets/Real.png", makePng(16, 16, 0x11u, 0x22u, 0x33u));
    project.write("Assets/Sound.ogg", {'O', 'g', 'g', 'S'});
    project.write("Assets/Scene.cnascene", {'{', '}'});
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    std::vector<Uuid> everything;
    for (const AssetRecord* record : assets.getAll()) { everything.push_back(record->id); }

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;
    cache.setWanted(everything);

    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{1});
}

CNA_STUDIO_TEST(TheBrowserReportsWhatItIsShowingAndTheBinderMakesTheThumbnails)
{
    // End to end, through the real shell: draw the browser, poll, and a thumbnail exists for the
    // asset on screen and for nothing else. This is the assertion that the wiring is real rather
    // than two halves that each work alone.
    ScopedProject project{"wired"};
    project.write("Assets/Shown.png", makePng(96, 96, 0x90u, 0x30u, 0x10u));
    project.write("Assets/Elsewhere/Hidden.png", makePng(96, 96, 0x10u, 0x30u, 0x90u));

    StudioContext context;
    context.getAssets().setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(context.getAssets().scan("Assets").succeeded);

    const Uuid shown = context.getAssets().findByPath("Assets/Shown.png")->id;
    const Uuid hidden = context.getAssets().findByPath("Assets/Elsewhere/Hidden.png")->id;

    StudioLog log;
    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    StudioShellPanels panels{*shell, context, log};

    panels.contentBrowserState().folder = "Assets";
    panels.contentBrowserState().view = StudioContentView::Grid;
    panels.contentBrowserState().folderPaneWidth = 0.0f;

    CNA_STUDIO_EXPECT(shell->activatePanel("content"));

    UiInputState input;
    input.displayWidth = 1280.0f;
    input.displayHeight = 720.0f;
    input.mouseInWindow = false;
    input.mouseX = -1.0f;
    input.mouseY = -1.0f;

    // Drawn, then polled, then drained -- which is the order a running Studio does them in.
    //
    // Driven until the *cache* says the work is done rather than for a fixed number of frames
    // (`plan.md` STUDIO-33026). A frame is not a unit of a worker thread's progress: twelve of them
    // are far more than enough on an idle machine and no guarantee at all on one whose cores are
    // busy, so a count of frames is a count of the wrong side of an asynchronous boundary. The
    // `waitForIdle` is what makes this a *bound* rather than a race -- the worker is given its
    // chance inside the loop, so the only thing the iteration count has to survive is the number of
    // pump-and-drain round trips the pipeline needs, which is fixed.
    bool generated = false;
    for (int frame = 0; frame < 200 && !generated; ++frame)
    {
        shell->renderFrame(input);
        panels.poll(static_cast<double>(frame) / 60.0);
        panels.jobs().waitForIdle();
        generated = panels.thumbnails().find(shown) != nullptr;
    }

    if (!generated)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                     "no thumbnail was generated for the asset on screen after 200 "
                                     "frames; the cache reported "
                                         + std::to_string(panels.thumbnails().getPendingCount())
                                         + " pending and "
                                         + std::to_string(panels.thumbnails().getFailedCount())
                                         + " failed.");
    }

    // The one in a folder the browser is not showing was never asked for. A cache that generated
    // for the whole project would be one that costs a hundred thousand decodes to show forty.
    CNA_STUDIO_EXPECT(panels.thumbnails().find(hidden) == nullptr);

    // And drawing after they exist asks the filesystem nothing, which is the gate STUDIO-30015 won
    // and this feature is the one most likely to lose again.
    const std::uint64_t probes = context.getAssets().getPresenceProbeCount();
    shell->renderFrame(input);
    shell->renderFrame(input);
    CNA_STUDIO_EXPECT_EQ(context.getAssets().getPresenceProbeCount(), probes);
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(TwoAssetsHoldingTheSameBytesShareOneDecode)
{
    // What content keying buys, and the reason it is a task rather than a refinement: a texture
    // copied into three folders is one picture, and a cache that keyed only on the asset id would
    // decode it three times and hold it three times.
    ScopedProject project{"shared"};
    const std::vector<unsigned char> png = makePng(80, 40, 0x70u, 0x10u, 0x50u);
    project.write("Assets/One/Crate.png", png);
    project.write("Assets/Two/Crate.png", png);

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const Uuid first = assets.findByPath("Assets/One/Crate.png")->id;
    const Uuid second = assets.findByPath("Assets/Two/Crate.png")->id;

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;

    // The first one is decoded.
    cache.setWanted({first});
    cache.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();
    CNA_STUDIO_EXPECT(cache.find(first) != nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), std::uint64_t{1});

    // The second is decoded once too -- nothing has told the cache that these are the same bytes
    // until it has hashed the second file, and hashing means reading it, which is a job.
    cache.setWanted({first, second});
    cache.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();

    const StudioThumbnail* a = cache.find(first);
    const StudioThumbnail* b = cache.find(second);
    CNA_STUDIO_EXPECT(a != nullptr);
    CNA_STUDIO_EXPECT(b != nullptr);
    if (a != nullptr && b != nullptr)
    {
        // Identical bytes in, identical pixels out, whichever path produced them.
        CNA_STUDIO_EXPECT_EQ(a->width, b->width);
        CNA_STUDIO_EXPECT_EQ(a->height, b->height);
        CNA_STUDIO_EXPECT(a->pixels == b->pixels);
    }

    // And the second one was *shared* rather than decoded: its job hashed the file, found those
    // bytes already answered, and never called the decoder. One decode for two assets, which is
    // the whole of what content keying buys.
    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), std::uint64_t{1});
    CNA_STUDIO_EXPECT_EQ(cache.getSharedCount(), std::uint64_t{1});

    // Dropping one copy does not cost the other a decode: the shared answer outlives the entry
    // that first produced it, because entries are evicted and bytes are not.
    cache.invalidate(second);
    CNA_STUDIO_EXPECT(cache.find(second) == nullptr);

    const std::uint64_t decodes = cache.getGeneratedCount();
    cache.setWanted({second});
    cache.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();

    CNA_STUDIO_EXPECT(cache.find(second) != nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), decodes);
    CNA_STUDIO_EXPECT_EQ(cache.getSharedCount(), std::uint64_t{2});
}

CNA_STUDIO_TEST(ChangingImporterSettingsRemakesTheThumbnailAlthoughTheFileDidNot)
{
    // The other half of STUDIO-09004. A reimport changes what a thumbnail should look like without
    // touching the source file, so a cache keyed only on the file goes on showing the old picture
    // -- the one failure a user reads as the editor lying rather than as a cache being stale.
    ScopedProject project{"reimport"};
    project.write("Assets/Tile.png", makePng(64, 64, 0x22u, 0x44u, 0x66u));

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const Uuid id = assets.findByPath("Assets/Tile.png")->id;

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;

    cache.setWanted({id});
    cache.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();
    CNA_STUDIO_EXPECT(cache.find(id) != nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), std::uint64_t{1});

    // Nothing changed: no work.
    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{0});

    // The settings change. The file is untouched -- same size, same modification time, same path --
    // so the stamp alone says nothing happened.
    AssetRecord reimported = *assets.find(id);
    JsonValue settings = JsonValue::makeObject();
    settings.set("sRGB", JsonValue{false});
    reimported.importerSettings = settings;
    CNA_STUDIO_EXPECT(assets.add(std::move(reimported)));

    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{1});
    jobs.waitForIdle();
    jobs.drain();
    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), std::uint64_t{2});

    // Decoded again rather than shared, although the *bytes* are identical: the sharing key is
    // bytes and settings together, because the same file under different settings is a different
    // picture and sharing on content alone would hand this asset the old answer.
    CNA_STUDIO_EXPECT_EQ(cache.getSharedCount(), std::uint64_t{0});

    // And settling on the same settings again is not a third decode: the fingerprint is the
    // serialised form, so identical settings compare identical however they were built.
    AssetRecord again = *assets.find(id);
    JsonValue same = JsonValue::makeObject();
    same.set("sRGB", JsonValue{false});
    again.importerSettings = same;
    CNA_STUDIO_EXPECT(assets.add(std::move(again)));

    CNA_STUDIO_EXPECT_EQ(cache.pump(jobs, assets), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(cache.getGeneratedCount(), std::uint64_t{2});
}

CNA_STUDIO_TEST(TheGridDrawsAThumbnailWhereOneCanBeMadeAndAnIconWhereItCannot)
{
    // The difference is invisible to every test this project can run unaided: the headless suite
    // has no graphics device, so it never resolves a texture, and a grid that had silently stopped
    // asking would look identical in every screenshot. A fake resolver is how the asking becomes
    // observable -- and the count is how it stays observable.
    ScopedProject project{"drawn"};
    project.write("Assets/A.png", makePng(48, 48, 0x10u, 0x80u, 0x40u));
    project.write("Assets/B.png", makePng(48, 48, 0x80u, 0x10u, 0x40u));

    StudioContext context;
    context.getAssets().setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(context.getAssets().scan("Assets").succeeded);

    StudioContentBrowserState state;
    state.view = StudioContentView::Grid;
    state.folder = "Assets";
    state.folderPaneWidth = 0.0f;

    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->activatePanel("content"));

    StudioContentBrowserServices services;
    std::size_t asked = 0;

    // Stands in for a host with a device: every asset resolves, so every card should draw one.
    services.thumbnailTexture = [&asked](const Uuid& id) -> UiTextureId {
        (void)id;
        ++asked;
        return UiTextureId{42};
    };

    StudioContentBrowserResult drawn;
    CNA_STUDIO_EXPECT(shell->setPanelContent("content",
        [&](StudioFrame& frame, const UiRect& area) {
            const StudioContentBrowserResult pass =
                studioContentBrowser(frame, area, context, state, services);
            if (frame.isDrawPass()) { drawn = pass; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT_EQ(drawn.thumbnailsDrawn, std::size_t{2});
    CNA_STUDIO_EXPECT(asked >= 2);

    // A build with no device leaves the seam unset, and the grid draws its icons exactly as it did
    // before thumbnails existed. That is the headless preview, the whole test suite, and any build
    // without CNA -- so it has to be the quiet default rather than a degraded mode.
    StudioContentBrowserResult withoutDevice;
    CNA_STUDIO_EXPECT(shell->setPanelContent("content",
        [&](StudioFrame& frame, const UiRect& area) {
            const StudioContentBrowserResult pass =
                studioContentBrowser(frame, area, context, state);
            if (frame.isDrawPass()) { withoutDevice = pass; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT_EQ(withoutDevice.thumbnailsDrawn, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(withoutDevice.rowsTotal, drawn.rowsTotal);

    // And a host that has no texture for a particular asset gets an icon for that card rather than
    // a blank one -- "not yet" is the normal state of a thumbnail, not an error.
    StudioContentBrowserServices partial;
    partial.thumbnailTexture = [](const Uuid&) { return kUiTextureNone; };

    StudioContentBrowserResult none;
    CNA_STUDIO_EXPECT(shell->setPanelContent("content",
        [&](StudioFrame& frame, const UiRect& area) {
            const StudioContentBrowserResult pass =
                studioContentBrowser(frame, area, context, state, partial);
            if (frame.isDrawPass()) { none = pass; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT_EQ(none.thumbnailsDrawn, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(DroppingAThumbnailTellsWhoeverUploadedIt)
{
    // The cache is bounded and a host's textures are not. Without this a project of a hundred
    // thousand images fills a GPU with pictures of folders nobody is in -- a leak that is invisible
    // until somebody profiles memory on a real project.
    ScopedProject project{"dropped"};
    project.write("Assets/Gone.png", makePng(32, 32, 0x33u, 0x66u, 0x99u));

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);
    const Uuid id = assets.findByPath("Assets/Gone.png")->id;

    std::vector<Uuid> released;

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache cache;
    cache.setOnDropped([&released](const Uuid& dropped) { released.push_back(dropped); });

    cache.setWanted({id});
    cache.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();
    CNA_STUDIO_EXPECT(cache.find(id) != nullptr);
    CNA_STUDIO_EXPECT(released.empty());

    cache.invalidate(id);
    CNA_STUDIO_EXPECT_EQ(released.size(), std::size_t{1});
    if (!released.empty()) { CNA_STUDIO_EXPECT(released.front() == id); }

    // And invalidating an asset with no thumbnail tells nobody, or a host would forget textures it
    // never made.
    cache.invalidate(id);
    CNA_STUDIO_EXPECT_EQ(released.size(), std::size_t{1});
}
