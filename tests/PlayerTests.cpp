// SPDX-License-Identifier: MS-PL
/**
 * @file PlayerTests.cpp
 * @brief Tests for the runtime bridge: the transport, the player's state machine, and a real
 *        editor-spawns-player round trip over a real socket.
 *
 * The last of those is the important one. Everything else here could pass while play mode remained
 * broken; `StudioLaunchesARealPlayerProcessAndTalksToIt` starts the actual `cna-player` binary,
 * connects to it over loopback TCP, exchanges real messages and shuts it down.
 */

#include "TestHarness.hpp"

#include <algorithm>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "CNA/Studio/Player/PlayerHost.hpp"
#include "CNA/Studio/RuntimeBridge/MessageChannel.hpp"
#include "CNA/Studio/RuntimeBridge/BackendComparison.hpp"
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"

using namespace CNA::Studio;

namespace
{
    std::filesystem::path makeScratchDirectory(const std::string& name)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / ("cna-studio-tests-" + name + "-" + Uuid::generate().toString());
        std::filesystem::create_directories(directory);
        return directory;
    }

    /** @brief Writes a minimal but complete project with one scene and one entity. */
    struct ScratchProject
    {
        std::filesystem::path directory;
        std::string projectPath;
        Uuid entityId;

        explicit ScratchProject(const std::string& name)
        {
            directory = makeScratchDirectory(name);

            Project project = Project::createDefault("BridgeGame", directory.generic_string());
            project.setStartupScene("Scenes/Level01.cnascene");
            std::string errorMessage;
            const bool projectSaved = project.saveToFile({}, &errorMessage);
            (void)projectSaved;
            projectPath = project.getFilePath();

            ComponentRegistry registry;
            registerBuiltinComponents(registry);

            SceneDocument scene;
            scene.setName("Level01");
            StudioEntity player{Uuid::generate(), "Player"};
            StudioComponent transform{BuiltinComponentIds::kTransform};
            transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
            player.addComponent(std::move(transform));
            entityId = scene.addEntity(std::move(player));
            const bool sceneSaved =
                scene.saveToFile((directory / "Scenes" / "Level01.cnascene").generic_string(), &errorMessage);
            (void)sceneSaved;
        }

        ~ScratchProject()
        {
            std::error_code errorCode;
            std::filesystem::remove_all(directory, errorCode);
        }
    };

    /**
     * @brief Returns the directory holding cna-player.
     *
     * Supplied by CMake as a generator expression over the real target, rather than guessed from
     * the test binary's own location -- the two live in different directories, and a guess would
     * silently turn the end-to-end bridge test into a no-op that always passes.
     */
    std::filesystem::path playerDirectory()
    {
        return std::filesystem::path{CNA_STUDIO_TEST_PLAYER_DIR};
    }
}

CNA_STUDIO_TEST(MessageChannelBindsAnEphemeralPort)
{
    MessageChannel channel;
    CNA_STUDIO_EXPECT(channel.listen(0));

    // Port 0 asks the OS to choose. Reading it back matters: that value is what reaches the
    // player's command line, and a fixed port would collide with whatever else is running.
    CNA_STUDIO_EXPECT(channel.getPort() != 0);
    CNA_STUDIO_EXPECT(channel.getState() == ChannelState::Listening);

    channel.close();
    CNA_STUDIO_EXPECT(channel.getState() == ChannelState::Closed);
}

CNA_STUDIO_TEST(MessageChannelCarriesMessagesBothWays)
{
    MessageChannel server;
    CNA_STUDIO_EXPECT(server.listen(0));

    MessageChannel client;
    CNA_STUDIO_EXPECT(client.connect(server.getPort()));

    // Both ends are non-blocking, so the handshake completes over several polls rather than in
    // one call. That is the same loop the editor runs once per frame.
    bool connected = false;
    for (int attempt = 0; attempt < 200 && !connected; ++attempt)
    {
        server.poll();
        client.poll();
        connected = server.isConnected() && client.isConnected();
        if (!connected) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    }
    CNA_STUDIO_EXPECT(connected);

    CNA_STUDIO_EXPECT(server.send(StudioMessage::makeLoadScene("Scenes/Level01.cnascene")));

    std::vector<StudioMessage> received;
    for (int attempt = 0; attempt < 200 && received.empty(); ++attempt)
    {
        const std::vector<StudioMessage> batch = client.poll();
        received.insert(received.end(), batch.begin(), batch.end());
        if (received.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    }

    CNA_STUDIO_EXPECT_EQ(received.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(received.front().type == StudioMessageType::LoadScene);
    CNA_STUDIO_EXPECT_EQ(received.front().payload["scenePath"].asString(),
                         std::string{"Scenes/Level01.cnascene"});
    CNA_STUDIO_EXPECT_EQ(client.getDroppedCount(), std::uint64_t{0});
}

CNA_STUDIO_TEST(MessageChannelReportsAFailedConnect)
{
    MessageChannel client;
    // Port 1 on loopback: privileged and nothing is listening, so this cannot succeed.
    client.connect(1);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        client.poll();
        if (client.getState() == ChannelState::Failed) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_STUDIO_EXPECT(!client.isConnected());
}

CNA_STUDIO_TEST(PlayerHostOpensAProjectAndItsStartupScene)
{
    const ScratchProject project{"playerhost"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));
    CNA_STUDIO_EXPECT_EQ(host.getScene().getName(), std::string{"Level01"});
    CNA_STUDIO_EXPECT(host.getScene().findEntity(project.entityId) != nullptr);
    CNA_STUDIO_EXPECT(host.getPlayState() == PlayState::Running);

    const StudioMessage ready = host.makeReady("SOFTWARE");
    CNA_STUDIO_EXPECT(ready.type == StudioMessageType::Ready);
    // The backend is the one thing the editor cannot infer: it is fixed at compile time, so the
    // Ready message is how the editor learns which build it actually got.
    CNA_STUDIO_EXPECT_EQ(ready.payload["backend"].asString(), std::string{"SOFTWARE"});
    CNA_STUDIO_EXPECT_EQ(ready.payload["entityCount"].asInt(), 1);
}

CNA_STUDIO_TEST(PlayerHostHonoursPauseStepAndResume)
{
    const ScratchProject project{"playerstep"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    CNA_STUDIO_EXPECT(host.tick());
    CNA_STUDIO_EXPECT_EQ(host.getFrameCount(), std::uint64_t{1});

    StudioMessage pause;
    pause.type = StudioMessageType::Pause;
    host.handle(pause, outbox);
    CNA_STUDIO_EXPECT(host.getPlayState() == PlayState::Paused);

    // Paused means paused: ticking must not advance, or the game would drift ahead of what the
    // user is looking at in the inspector.
    CNA_STUDIO_EXPECT(!host.tick());
    CNA_STUDIO_EXPECT_EQ(host.getFrameCount(), std::uint64_t{1});

    StudioMessage step;
    step.type = StudioMessageType::StepFrame;
    host.handle(step, outbox);
    CNA_STUDIO_EXPECT(host.tick());
    CNA_STUDIO_EXPECT_EQ(host.getFrameCount(), std::uint64_t{2});
    // One step, one frame -- not a resume.
    CNA_STUDIO_EXPECT(!host.tick());

    StudioMessage resume;
    resume.type = StudioMessageType::Resume;
    host.handle(resume, outbox);
    CNA_STUDIO_EXPECT(host.tick());
    CNA_STUDIO_EXPECT_EQ(host.getFrameCount(), std::uint64_t{3});
}

CNA_STUDIO_TEST(PlayerHostIgnoresStepWhileRunning)
{
    const ScratchProject project{"playernostep"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    StudioMessage step;
    step.type = StudioMessageType::StepFrame;
    host.handle(step, outbox);
    host.handle(step, outbox);

    // Stepping while running is meaningless; honouring it would make the game jump ahead.
    host.tick();
    CNA_STUDIO_EXPECT_EQ(host.getFrameCount(), std::uint64_t{1});
}

CNA_STUDIO_TEST(PlayerHostAppliesLiveSetProperty)
{
    const ScratchProject project{"playerlive"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    host.handle(StudioMessage::makeSetProperty(project.entityId, BuiltinComponentIds::kTransform,
                                               "position", PropertyValue{StudioVector3{7.0f, 8.0f, 9.0f}}),
                outbox);

    const StudioComponent* transform =
        host.getScene().findEntity(project.entityId)->findComponent(BuiltinComponentIds::kTransform);
    CNA_STUDIO_EXPECT_EQ(transform->getProperty("position").get<StudioVector3>().x, 7.0f);
    CNA_STUDIO_EXPECT_EQ(transform->getProperty("position").get<StudioVector3>().z, 9.0f);
}

CNA_STUDIO_TEST(PlayerHostReportsRatherThanCrashingOnBadRequests)
{
    const ScratchProject project{"playerbad"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    host.handle(StudioMessage::makeSetProperty(Uuid::generate(), BuiltinComponentIds::kTransform,
                                               "position", PropertyValue{StudioVector3{}}),
                outbox);
    host.handle(StudioMessage::makeSetProperty(project.entityId, "Nope.Missing",
                                               "x", PropertyValue{1.0f}),
                outbox);
    host.handle(StudioMessage::makeLoadScene(""), outbox);

    CNA_STUDIO_EXPECT_EQ(outbox.size(), std::size_t{3});
    for (const StudioMessage& reply : outbox)
    {
        CNA_STUDIO_EXPECT(reply.type == StudioMessageType::ReportLog);
    }
}

CNA_STUDIO_TEST(AScreenshotIsQueuedForTheGraphicsHalfRatherThanAnsweredOnTheSpot)
{
    const ScratchProject project{"playershot"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    StudioMessage request = StudioMessage::makeScreenshot("/tmp/frame.png");
    request.requestId = 42;
    host.handle(request, outbox);

    // Nothing goes back yet. Only the CNA-linked loop can read a back buffer, and a ScreenshotReady
    // sent from here would tell the editor a file exists before anything had been written to it --
    // a lie nothing later on the wire would correct.
    CNA_STUDIO_EXPECT(outbox.empty());

    std::vector<PlayerHost::ScreenshotRequest> queued = host.takeScreenshotRequests();
    CNA_STUDIO_EXPECT_EQ(queued.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(queued.front().path, std::string{"/tmp/frame.png"});
    CNA_STUDIO_EXPECT_EQ(queued.front().requestId, std::uint64_t{42});

    // Drained, so the graphics half cannot take the same capture twice.
    CNA_STUDIO_EXPECT(host.takeScreenshotRequests().empty());

    // The reply carries the request id back, so it can be matched with several in flight, and says
    // whether the file was written rather than leaving the editor to look for it.
    const StudioMessage written = PlayerHost::makeScreenshotReply(queued.front());
    CNA_STUDIO_EXPECT(written.type == StudioMessageType::ScreenshotReady);
    CNA_STUDIO_EXPECT_EQ(written.requestId, std::uint64_t{42});
    CNA_STUDIO_EXPECT(written.payload["written"].asBoolean(false));

    const StudioMessage failed = PlayerHost::makeScreenshotReply(queued.front(), "no device");
    CNA_STUDIO_EXPECT(!failed.payload["written"].asBoolean(true));
    CNA_STUDIO_EXPECT_EQ(failed.payload["error"].asString(), std::string{"no device"});
}

CNA_STUDIO_TEST(AScreenshotWithNoPathIsReportedRatherThanQueued)
{
    const ScratchProject project{"playershotpath"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    host.handle(StudioMessage::makeScreenshot(""), outbox);

    // Queuing a capture with nowhere to write it would produce a reply saying it failed, one frame
    // later, for a reason the editor could have been told immediately.
    CNA_STUDIO_EXPECT(host.takeScreenshotRequests().empty());
    CNA_STUDIO_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(outbox.front().type == StudioMessageType::ReportLog);
}

CNA_STUDIO_TEST(PlayerHostReportsAProtocolVersionMismatch)
{
    const ScratchProject project{"playerversion"};

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    StudioMessage hello = StudioMessage::makeHello(project.directory.generic_string());
    hello.payload.set("protocolVersion", JsonValue{kStudioProtocolVersion + 99});
    host.handle(hello, outbox);

    // Reported, not fatal: the editor can then tell the user which build to rebuild, which it
    // could not do from a silently dropped connection.
    CNA_STUDIO_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(!host.isHandshakeComplete());

    outbox.clear();
    host.handle(StudioMessage::makeHello(project.directory.generic_string()), outbox);
    CNA_STUDIO_EXPECT(host.isHandshakeComplete());
}

// ---------------------------------------------------------------------------------------------
// Backend comparison (ED-510)
// ---------------------------------------------------------------------------------------------

namespace
{
    /** @brief Builds a solid image, so a test can say exactly what two frames differ by. */
    ImageBuffer makeSolidImage(int width, int height, std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue)
    {
        ImageBuffer image;
        image.width = width;
        image.height = height;
        image.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 255);

        for (std::size_t pixel = 0; pixel < image.getPixelCount(); ++pixel)
        {
            image.pixels[pixel * 4 + 0] = red;
            image.pixels[pixel * 4 + 1] = green;
            image.pixels[pixel * 4 + 2] = blue;
            image.pixels[pixel * 4 + 3] = 255;
        }
        return image;
    }

    /** @brief Sets one pixel, for the tests that care about *where* two images differ. */
    void setPixel(ImageBuffer& image, int x, int y, std::uint8_t red, std::uint8_t green,
                  std::uint8_t blue)
    {
        const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)
                             + static_cast<std::size_t>(x)) * 4;
        image.pixels[offset + 0] = red;
        image.pixels[offset + 1] = green;
        image.pixels[offset + 2] = blue;
    }
}

CNA_STUDIO_TEST(IdenticalFramesCompareEqualAndTinyDifferencesAreWithinTolerance)
{
    const ImageBuffer reference = makeSolidImage(8, 8, 100, 149, 237);

    const ImageDifference same = compareImages(reference, reference, 0);
    CNA_STUDIO_EXPECT(same.comparable);
    CNA_STUDIO_EXPECT(same.matches());
    CNA_STUDIO_EXPECT_EQ(same.totalPixels, std::size_t{64});

    // Two backends drawing the same scene routinely differ by a step or two in a channel. A
    // comparison with no tolerance reports every backend as different from every other, which is
    // true and useless.
    const ImageBuffer nearly = makeSolidImage(8, 8, 101, 149, 238);
    CNA_STUDIO_EXPECT(compareImages(reference, nearly, 2).matches());
    CNA_STUDIO_EXPECT(!compareImages(reference, nearly, 0).matches());

    // The largest delta is reported even when everything is within tolerance: it is the number
    // that says whether two backends are *identical* or merely close enough.
    CNA_STUDIO_EXPECT_EQ(compareImages(reference, nearly, 2).maxChannelDelta, 1);
}

CNA_STUDIO_TEST(ADifferenceReportsHowManyPixelsAndWhere)
{
    const ImageBuffer reference = makeSolidImage(10, 10, 0, 0, 0);
    ImageBuffer other = reference;
    setPixel(other, 3, 4, 255, 255, 255);
    setPixel(other, 6, 8, 255, 255, 255);

    const ImageDifference difference = compareImages(reference, other);
    CNA_STUDIO_EXPECT_EQ(difference.differingPixels, std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(difference.maxChannelDelta, 255);

    // The bounding box is usually the diagnosis: a band along one edge is a viewport or scissor
    // problem, a scattering over one sprite is a filtering one.
    CNA_STUDIO_EXPECT_EQ(difference.boundingBox.x, 3);
    CNA_STUDIO_EXPECT_EQ(difference.boundingBox.y, 4);
    CNA_STUDIO_EXPECT_EQ(difference.boundingBox.width, 4);
    CNA_STUDIO_EXPECT_EQ(difference.boundingBox.height, 5);
}

CNA_STUDIO_TEST(ImagesOfDifferentSizesAreIncomparableRatherThanDifferent)
{
    // Not the same fact, and not the same action: a size mismatch means the capture went wrong,
    // not that the backends disagree about how to draw.
    const ImageDifference difference =
        compareImages(makeSolidImage(4, 4, 0, 0, 0), makeSolidImage(4, 5, 0, 0, 0));

    CNA_STUDIO_EXPECT(!difference.comparable);
    CNA_STUDIO_EXPECT(!difference.matches());
    CNA_STUDIO_EXPECT(!difference.incomparableReason.empty());
    CNA_STUDIO_EXPECT(compareImages(ImageBuffer{}, ImageBuffer{}).incomparableReason.size() > 0);
}

CNA_STUDIO_TEST(TheDifferenceImageMarksTheDifferingPixelsOnADimmedCopy)
{
    const ImageBuffer reference = makeSolidImage(4, 4, 200, 200, 200);
    ImageBuffer other = reference;
    setPixel(other, 1, 1, 0, 0, 0);

    const ImageBuffer marked = makeDifferenceImage(reference, other);
    CNA_STUDIO_EXPECT(marked.isWellFormed());

    // Magenta where they differ: it appears in no rendered scene by accident, so it cannot be
    // mistaken for part of the picture.
    const std::size_t differing = (1 * 4 + 1) * 4;
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(marked.pixels[differing + 0]), 255);
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(marked.pixels[differing + 1]), 0);
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(marked.pixels[differing + 2]), 255);

    // And a dimmed copy everywhere else, because the matching picture is the context that makes
    // the marked pixels mean anything.
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(marked.pixels[0]), 50);
}

CNA_STUDIO_TEST(AComparisonNeedsMoreThanOneBackend)
{
    const ScratchProject project{"comparefew"};

    ComparisonRequest request;
    request.projectPath = project.projectPath;
    request.outputDirectory = (project.directory / "comparison").generic_string();
    request.builds = {PlayerBuild{"software", "/nowhere/cna-player-software"}};

    // One player is not a comparison, and saying so beats launching it and reporting the useless
    // truth that a backend matches itself.
    CNA_STUDIO_EXPECT(!describeComparisonProblem(request).empty());

    BackendComparison comparison;
    CNA_STUDIO_EXPECT(!comparison.start(request, {}));
    CNA_STUDIO_EXPECT(comparison.getState() == ComparisonState::Failed);
    CNA_STUDIO_EXPECT(!comparison.getError().empty());
}

CNA_STUDIO_TEST(AComparisonReportsPlayersThatCannotBeLaunched)
{
    const ScratchProject project{"comparemissing"};

    ComparisonRequest request;
    request.projectPath = project.projectPath;
    request.outputDirectory = (project.directory / "comparison").generic_string();
    request.builds = {PlayerBuild{"ghost", "/nowhere/cna-player-ghost"},
                      PlayerBuild{"phantom", "/nowhere/cna-player-phantom"}};
    request.warmupFrames = 0;

    // Refused at once rather than after the timeout: a comparison with nothing to compare has
    // already answered, and making the user wait thirty seconds for that answer helps nobody.
    BackendComparison comparison;
    CNA_STUDIO_EXPECT(!comparison.start(request, {}));
    CNA_STUDIO_EXPECT(comparison.getState() == ComparisonState::Failed);
    CNA_STUDIO_EXPECT(!comparison.getError().empty());

    // Each entry says what happened to *it*, rather than the run reporting one opaque failure:
    // with several backends the interesting case is the one that would not launch among several
    // that did, and that only reads correctly if every entry carries its own reason.
    CNA_STUDIO_EXPECT_EQ(comparison.getEntries().size(), std::size_t{2});
    for (const ComparisonEntry& entry : comparison.getEntries())
    {
        CNA_STUDIO_EXPECT(!entry.errorMessage.empty());
        CNA_STUDIO_EXPECT(!entry.captured);
    }
    CNA_STUDIO_EXPECT(!comparison.allBackendsAgree());
}

CNA_STUDIO_TEST(AComparisonCarriesOnWithTheBackendsThatDidLaunch)
{
    // One backend missing is exactly the kind of thing a comparison exists to find, and it must
    // not take the others down with it -- the run continues, and the reference moves to a player
    // that actually started rather than staying on the one that did not.
    const std::vector<PlayerBuild> discovered = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_STUDIO_EXPECT(!discovered.empty());
    if (discovered.empty()) { return; }

    const ScratchProject project{"comparepartial"};

    ComparisonRequest request;
    request.projectPath = project.projectPath;
    request.outputDirectory = (project.directory / "comparison").generic_string();
    request.builds = {PlayerBuild{"ghost", "/nowhere/cna-player-ghost"},
                      PlayerBuild{"real", discovered.front().executablePath}};
    request.warmupFrames = 0;

    BackendComparison comparison;
    CNA_STUDIO_EXPECT(comparison.start(request, {}));

    CNA_STUDIO_EXPECT_EQ(comparison.getEntries().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(!comparison.getEntries()[0].errorMessage.empty());
    CNA_STUDIO_EXPECT(!comparison.getEntries()[0].isReference);
    CNA_STUDIO_EXPECT(comparison.getEntries()[1].isReference);

    double now = 0.0;
    for (int step = 0; step < 800 && comparison.getState() != ComparisonState::Finished
                       && comparison.getState() != ComparisonState::Failed;
         ++step)
    {
        now += 0.01;
        comparison.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CNA_STUDIO_EXPECT(comparison.getState() == ComparisonState::Finished);
}

CNA_STUDIO_TEST(TwoRealPlayersAreComparedAgainstEachOther)
{
    // The end-to-end case, with the one player build this repository produces standing in for two.
    // Pointing both entries at the same binary is not a cheat: what is being tested is the
    // sequence -- launch, handshake, ask each for the same frame, read both back, compare -- and
    // that sequence does not know or care that the two paths are equal. Whether two *different*
    // backends agree is a question about CNA, and it needs two backend builds installed.
    const ScratchProject project{"comparereal"};

    const std::vector<PlayerBuild> discovered = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_STUDIO_EXPECT(!discovered.empty());
    if (discovered.empty()) { return; }

    ComparisonRequest request;
    request.projectPath = project.projectPath;
    request.outputDirectory = (project.directory / "comparison").generic_string();
    request.builds = {discovered.front(), discovered.front()};
    request.warmupFrames = 0;
    request.timeoutSeconds = 20.0;

    // Synthetic images rather than real captures: a test binary has no graphics device, so it
    // cannot decode a PNG -- which is exactly why the reader is injected in the first place. Every
    // path here is the real one apart from the two bytes at the very end. The second entry's stem
    // is made unique by the run itself, which is what lets the reader tell them apart.
    const ImageReader reader = [](const std::string& path) {
        ImageBuffer image = makeSolidImage(4, 4, 10, 20, 30);

        // The *stem*, not the whole path: the scratch directory's name contains a Uuid, and a Uuid
        // that happened to contain "-2" made both captures look like the second one -- which made
        // this test pass or fail depending on random hex.
        if (std::filesystem::path{path}.stem().generic_string().ends_with("-2"))
        {
            setPixel(image, 0, 0, 200, 20, 30);
        }
        return image;
    };

    BackendComparison comparison;
    CNA_STUDIO_EXPECT(comparison.start(request, reader));

    double now = 0.0;
    for (int step = 0; step < 2000 && comparison.getState() != ComparisonState::Finished; ++step)
    {
        now += 0.01;
        comparison.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_STUDIO_EXPECT(comparison.getState() == ComparisonState::Finished);
    CNA_STUDIO_EXPECT_EQ(comparison.getEntries().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(!comparison.getReferenceBackend().empty());

    // Two players were launched, handshaken and asked for the same frame over real sockets. What
    // they answer depends on how this repository was built, and both answers are correct:
    const bool captured = comparison.getEntries().front().captured;
    if (captured)
    {
        // Built with CNA: each player really wrote a PNG. The injected reader gives the second one
        // a different pixel, so the run must report a disagreement -- proof it compares what came
        // back rather than assuming a match.
        for (const ComparisonEntry& entry : comparison.getEntries())
        {
            CNA_STUDIO_EXPECT(entry.captured);
            CNA_STUDIO_EXPECT(entry.errorMessage.empty());
        }
        CNA_STUDIO_EXPECT(!comparison.allBackendsAgree());
        CNA_STUDIO_EXPECT_EQ(comparison.getEntries()[1].difference.differingPixels, std::size_t{1});
    }
    else
    {
        // Built without CNA: the player has no device to capture from and says so, rather than
        // leaving the editor waiting for a reply that will never come. That refusal is the whole
        // reason `screenshotReady` carries `written` at all.
        for (const ComparisonEntry& entry : comparison.getEntries())
        {
            CNA_STUDIO_EXPECT(!entry.errorMessage.empty());
        }
    }
}

CNA_STUDIO_TEST(PlayerBuildDiscoveryFindsTheInstalledBinaries)
{
    // Discovery is a real feature, not a detail: because CNA fixes its backend at compile time,
    // "preview on Software" means "launch cna-player-software", and the editor must offer only
    // the backends whose binaries actually exist.
    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());

    // At least one player must be discoverable, and every entry must name a real file. The
    // *backend* deliberately is not asserted: without CNA the binary is the un-suffixed
    // `cna-player` (reported as "default"), and with CNA it is `cna-player-<backend>`. Pinning
    // either would make this test pass in one configuration and fail in the other for no reason.
    CNA_STUDIO_EXPECT(!builds.empty());
    for (const PlayerBuild& build : builds)
    {
        CNA_STUDIO_EXPECT(!build.backend.empty());
        CNA_STUDIO_EXPECT(!build.executablePath.empty());
        CNA_STUDIO_EXPECT(std::filesystem::exists(build.executablePath));
    }

    CNA_STUDIO_EXPECT_EQ(discoverPlayerBuilds("/definitely/not/a/directory").size(), std::size_t{0});
}

CNA_STUDIO_TEST(StudioLaunchesARealPlayerProcessAndTalksToIt)
{
    // The end-to-end case: a real process, a real socket, real messages. Everything else in this
    // file could pass while play mode remained broken.
    const ScratchProject project{"bridge"};

    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_STUDIO_EXPECT(!builds.empty());
    if (builds.empty()) { return; }

    PlayerProcess player;
    CNA_STUDIO_EXPECT(player.start(builds.front(), project.projectPath));
    if (!player.isRunning() && player.getExitReason() == PlayerExitReason::FailedToStart)
    {
        ::CnaStudioTest::reportFailure(__FILE__, __LINE__, "cannot start player: " + player.getError());
        return;
    }

    bool sawReady = false;
    bool sawSceneLog = false;
    bool sentLoad = false;

    for (int attempt = 0; attempt < 400 && !(sawReady && sawSceneLog); ++attempt)
    {
        for (const StudioMessage& message : player.poll())
        {
            if (message.type == StudioMessageType::Ready) { sawReady = true; }
            if (message.type == StudioMessageType::ReportLog
                && message.payload["text"].asString().find("loaded scene") != std::string::npos)
            {
                sawSceneLog = true;
            }
        }

        if (sawReady && !sentLoad)
        {
            CNA_STUDIO_EXPECT(player.send(StudioMessage::makeLoadScene("Scenes/Level01.cnascene")));
            sentLoad = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_STUDIO_EXPECT(sawReady);

    // The player reports the backend it was compiled with -- "NONE" without CNA, the real backend
    // name with it. What matters is that *something* was reported: that is the mechanism the
    // editor uses to detect a mismatched launch, and an empty value would break it silently.
    CNA_STUDIO_EXPECT(!player.getReportedBackend().empty());
    CNA_STUDIO_EXPECT(sawSceneLog);

    player.stop();
    CNA_STUDIO_EXPECT(!player.isRunning());
    CNA_STUDIO_EXPECT(player.getExitReason() != PlayerExitReason::FailedToStart);
}

CNA_STUDIO_TEST(PlayerProcessReportsAMissingBinaryRatherThanHanging)
{
    // The failure happens after the fork, in the child, where nothing can be returned -- so it is
    // carried back over a pipe and reported from start(). That it comes back *here* rather than
    // as a process that started and vanished is the whole point: the editor never shows a Stop
    // button, never waits for a connection, and can name the path that was not there.
    const ScratchProject project{"bridgemissing"};

    PlayerBuild missing;
    missing.backend = "nonexistent";
    missing.executablePath = (project.directory / "cna-player-nonexistent").generic_string();

    PlayerProcess player;
    CNA_STUDIO_EXPECT(!player.start(missing, project.projectPath));

    CNA_STUDIO_EXPECT(!player.isRunning());
    CNA_STUDIO_EXPECT_EQ(std::string{toString(player.getExitReason())},
                         std::string{"failed to start"});
    CNA_STUDIO_EXPECT(player.getError().find(missing.executablePath) != std::string::npos);

    // And polling a launch that never happened is harmless rather than a wait for a connection
    // that will never arrive.
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        CNA_STUDIO_EXPECT(player.poll().empty());
        CNA_STUDIO_EXPECT(!player.isRunning());
    }
}

CNA_STUDIO_TEST(APlayerThatFailsIsDistinguishedFromOneThatFinished)
{
    // "The game exited" and "the game crashed" are different things to tell a user, and the
    // process's own status is the only honest source: the connection drops in both cases, so
    // reading the answer off the channel would report every segfault as a clean exit.
    //
    // Stood in for by /bin/true and /bin/false, which ignore their arguments and differ in
    // exactly one thing -- the code they return.
    if (!std::filesystem::exists("/bin/true") || !std::filesystem::exists("/bin/false")) { return; }

    const ScratchProject project{"bridgestatus"};

    const auto runToCompletion = [&](const char* executable) {
        PlayerProcess player;
        CNA_STUDIO_EXPECT(player.start(PlayerBuild{"default", executable}, project.projectPath));
        for (int attempt = 0; attempt < 400 && player.isRunning(); ++attempt)
        {
            player.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CNA_STUDIO_EXPECT(!player.isRunning());
        return std::string{toString(player.getExitReason())};
    };

    CNA_STUDIO_EXPECT_EQ(runToCompletion("/bin/true"), std::string{"exited"});
    CNA_STUDIO_EXPECT_EQ(runToCompletion("/bin/false"), std::string{"crashed"});
}

CNA_STUDIO_TEST(PlayerProcessIntroducesItselfOnceConnected)
{
    const ScratchProject project{"handshake"};

    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_STUDIO_EXPECT(!builds.empty());
    if (builds.empty()) { return; }

    PlayerProcess player;
    CNA_STUDIO_EXPECT(player.start(builds.front(), project.projectPath));

    bool sawReady = false;
    for (int attempt = 0; attempt < 400 && !(sawReady && player.isHelloSent()); ++attempt)
    {
        for (const StudioMessage& message : player.poll())
        {
            if (message.type == StudioMessageType::Ready) { sawReady = true; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // The player treats a missing Hello as an incomplete handshake and says nothing about it, so
    // forgetting to send one produces a session that looks connected and is quietly degraded.
    CNA_STUDIO_EXPECT(sawReady);
    CNA_STUDIO_EXPECT(player.isHelloSent());

    player.stop();
}

CNA_STUDIO_TEST(PlayerHostReloadsOneAssetByIdAndReportsWhatItDid)
{
    const ScratchProject project{"reloadasset"};

    // An asset the player has never seen: the reload has to rescan before it looks the id up, or
    // it would answer "unknown" about a file that is right there.
    const std::filesystem::path assetPath = project.directory / "Assets" / "hero.png";
    std::filesystem::create_directories(assetPath.parent_path());
    {
        std::ofstream stream{assetPath, std::ios::binary | std::ios::trunc};
        stream << "hero";
    }

    PlayerHost host;
    CNA_STUDIO_EXPECT(host.openProject(project.projectPath));

    // Learn the id the same way the editor would: by scanning the same directory.
    AssetDatabase editorSide;
    editorSide.setProjectRoot(project.directory.generic_string());
    CNA_STUDIO_EXPECT(editorSide.scan("Assets").succeeded);
    const AssetRecord* editorRecord = editorSide.findByPath("Assets/hero.png");
    CNA_STUDIO_EXPECT(editorRecord != nullptr);
    if (editorRecord == nullptr) { return; }
    const Uuid assetId = editorRecord->id;

    PlayerHost::Outbox outbox;
    host.handle(StudioMessage::makeReloadAsset(assetId), outbox);

    bool reported = false;
    for (const StudioMessage& message : outbox)
    {
        if (message.payload["text"].asString().find("reloaded 'Assets/hero.png'") != std::string::npos)
        {
            reported = true;
        }
    }
    CNA_STUDIO_EXPECT(reported);

    // The graphics half drains this to drop what it has cached. A list, not a flag: it runs on its
    // own schedule and must not miss a reload that arrived between two of its frames.
    const std::vector<Uuid> reloaded = host.takeReloadedAssets();
    CNA_STUDIO_EXPECT_EQ(reloaded.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(reloaded.front() == assetId);
    CNA_STUDIO_EXPECT(host.takeReloadedAssets().empty());

    // An id the player does not know is a timing difference, not a broken session.
    outbox.clear();
    host.handle(StudioMessage::makeReloadAsset(Uuid::generate()), outbox);
    CNA_STUDIO_EXPECT(host.takeReloadedAssets().empty());

    bool warned = false;
    for (const StudioMessage& message : outbox)
    {
        if (message.payload["severity"].asString() == "warn"
            && message.payload["text"].asString().find("unknown asset") != std::string::npos)
        {
            warned = true;
        }
    }
    CNA_STUDIO_EXPECT(warned);
}

CNA_STUDIO_TEST(AnInputSnapshotSurvivesTheWireAndIsMappedIntoThePlayersWindow)
{
    PlayerInputSnapshot sent;
    sent.keys = {"W", "Space"};
    sent.mouseX = 100.0f;
    sent.mouseY = 50.0f;
    sent.surfaceWidth = 400.0f;
    sent.surfaceHeight = 200.0f;
    sent.leftButton = true;
    sent.wheel = -2.0f;

    const std::optional<StudioMessage> decoded =
        StudioMessage::decode(StudioMessage::makeInput(sent).encode());
    CNA_STUDIO_EXPECT(decoded.has_value());
    if (!decoded) { return; }

    CNA_STUDIO_EXPECT(decoded->type == StudioMessageType::Input);
    CNA_STUDIO_EXPECT(PlayerInputSnapshot::fromJson(decoded->payload) == sent);

    // The mapping is the whole point of sending the surface with the pointer: a quarter of the way
    // across the editor's panel is a quarter of the way across the game's window, whatever the two
    // are measured in.
    const PlayerInputSnapshot mapped = sent.mapToSurface(1280.0f, 720.0f);
    CNA_STUDIO_EXPECT(std::abs(mapped.mouseX - 320.0f) < 0.01f);
    CNA_STUDIO_EXPECT(std::abs(mapped.mouseY - 180.0f) < 0.01f);
    CNA_STUDIO_EXPECT(mapped.isKeyDown("W") && !mapped.isKeyDown("A"));

    // No pointer stays no pointer. Mapping it would put the cursor in a corner of the game window
    // and leave it there, which is worse than saying nothing.
    PlayerInputSnapshot keysOnly;
    keysOnly.keys = {"Escape"};
    CNA_STUDIO_EXPECT(!keysOnly.hasPointer());
    CNA_STUDIO_EXPECT(keysOnly.mapToSurface(1280.0f, 720.0f) == keysOnly);
}

CNA_STUDIO_TEST(ThePlayerMapsForwardedInputAndReportsWhatItHolds)
{
    PlayerHost host;

    PlayerInputSnapshot sent;
    sent.keys = {"D"};
    sent.mouseX = 470.5f;
    sent.mouseY = 270.5f;
    sent.surfaceWidth = 941.0f;
    sent.surfaceHeight = 541.0f;
    sent.rightButton = true;

    // Before any frame has run, the host does not know how big its window is. It stores what it
    // was sent rather than mapping by a zero, and says so by reporting the sender's surface back.
    PlayerHost::Outbox outbox;
    host.handle(StudioMessage::makeInput(sent), outbox);
    CNA_STUDIO_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(host.getInput() == sent);

    outbox.clear();
    host.setSurfaceSize(1280, 720);
    host.handle(StudioMessage::makeInput(sent), outbox);

    CNA_STUDIO_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(outbox.front().type == StudioMessageType::ReportInput);

    const PlayerInputSnapshot held = host.getInput();
    CNA_STUDIO_EXPECT(std::abs(held.surfaceWidth - 1280.0f) < 0.01f);
    CNA_STUDIO_EXPECT(std::abs(held.mouseX - 470.5f * (1280.0f / 941.0f)) < 0.01f);
    CNA_STUDIO_EXPECT(held.isKeyDown("D"));
    CNA_STUDIO_EXPECT(held.rightButton);

    // The reply carries exactly what the player holds, so the editor can show the game's own view
    // rather than an echo of what it sent.
    CNA_STUDIO_EXPECT(PlayerInputSnapshot::fromJson(outbox.front().payload) == held);
}
