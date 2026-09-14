// SPDX-License-Identifier: MS-PL
/**
 * @file PlayerTests.cpp
 * @brief Tests for the runtime bridge: the transport, the player's state machine, and a real
 *        editor-spawns-player round trip over a real socket.
 *
 * The last of those is the important one. Everything else here could pass while play mode remained
 * broken; `EditorLaunchesARealPlayerProcessAndTalksToIt` starts the actual `cna-player` binary,
 * connects to it over loopback TCP, exchanges real messages and shuts it down.
 */

#include "TestHarness.hpp"

#include <algorithm>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "CNA/Editor/EditorApplication.hpp"
#include "CNA/Editor/Player/PlayerHost.hpp"
#include "CNA/Editor/RuntimeBridge/MessageChannel.hpp"
#include "CNA/Editor/RuntimeBridge/BackendComparison.hpp"
#include "CNA/Editor/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"

using namespace CNA::Editor;

namespace
{
    std::filesystem::path makeScratchDirectory(const std::string& name)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / ("cna-editor-tests-" + name + "-" + Uuid::generate().toString());
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
            EditorEntity player{Uuid::generate(), "Player"};
            EditorComponent transform{BuiltinComponentIds::kTransform};
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
        return std::filesystem::path{CNA_EDITOR_TEST_PLAYER_DIR};
    }
}

CNA_EDITOR_TEST(MessageChannelBindsAnEphemeralPort)
{
    MessageChannel channel;
    CNA_EDITOR_EXPECT(channel.listen(0));

    // Port 0 asks the OS to choose. Reading it back matters: that value is what reaches the
    // player's command line, and a fixed port would collide with whatever else is running.
    CNA_EDITOR_EXPECT(channel.getPort() != 0);
    CNA_EDITOR_EXPECT(channel.getState() == ChannelState::Listening);

    channel.close();
    CNA_EDITOR_EXPECT(channel.getState() == ChannelState::Closed);
}

CNA_EDITOR_TEST(MessageChannelCarriesMessagesBothWays)
{
    MessageChannel server;
    CNA_EDITOR_EXPECT(server.listen(0));

    MessageChannel client;
    CNA_EDITOR_EXPECT(client.connect(server.getPort()));

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
    CNA_EDITOR_EXPECT(connected);

    CNA_EDITOR_EXPECT(server.send(EditorMessage::makeLoadScene("Scenes/Level01.cnascene")));

    std::vector<EditorMessage> received;
    for (int attempt = 0; attempt < 200 && received.empty(); ++attempt)
    {
        const std::vector<EditorMessage> batch = client.poll();
        received.insert(received.end(), batch.begin(), batch.end());
        if (received.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    }

    CNA_EDITOR_EXPECT_EQ(received.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(received.front().type == EditorMessageType::LoadScene);
    CNA_EDITOR_EXPECT_EQ(received.front().payload["scenePath"].asString(),
                         std::string{"Scenes/Level01.cnascene"});
    CNA_EDITOR_EXPECT_EQ(client.getDroppedCount(), std::uint64_t{0});
}

CNA_EDITOR_TEST(MessageChannelReportsAFailedConnect)
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

    CNA_EDITOR_EXPECT(!client.isConnected());
}

CNA_EDITOR_TEST(PlayerHostOpensAProjectAndItsStartupScene)
{
    const ScratchProject project{"playerhost"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));
    CNA_EDITOR_EXPECT_EQ(host.getScene().getName(), std::string{"Level01"});
    CNA_EDITOR_EXPECT(host.getScene().findEntity(project.entityId) != nullptr);
    CNA_EDITOR_EXPECT(host.getPlayState() == PlayState::Running);

    const EditorMessage ready = host.makeReady("SOFTWARE");
    CNA_EDITOR_EXPECT(ready.type == EditorMessageType::Ready);
    // The backend is the one thing the editor cannot infer: it is fixed at compile time, so the
    // Ready message is how the editor learns which build it actually got.
    CNA_EDITOR_EXPECT_EQ(ready.payload["backend"].asString(), std::string{"SOFTWARE"});
    CNA_EDITOR_EXPECT_EQ(ready.payload["entityCount"].asInt(), 1);
}

CNA_EDITOR_TEST(PlayerHostHonoursPauseStepAndResume)
{
    const ScratchProject project{"playerstep"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    CNA_EDITOR_EXPECT(host.tick());
    CNA_EDITOR_EXPECT_EQ(host.getFrameCount(), std::uint64_t{1});

    EditorMessage pause;
    pause.type = EditorMessageType::Pause;
    host.handle(pause, outbox);
    CNA_EDITOR_EXPECT(host.getPlayState() == PlayState::Paused);

    // Paused means paused: ticking must not advance, or the game would drift ahead of what the
    // user is looking at in the inspector.
    CNA_EDITOR_EXPECT(!host.tick());
    CNA_EDITOR_EXPECT_EQ(host.getFrameCount(), std::uint64_t{1});

    EditorMessage step;
    step.type = EditorMessageType::StepFrame;
    host.handle(step, outbox);
    CNA_EDITOR_EXPECT(host.tick());
    CNA_EDITOR_EXPECT_EQ(host.getFrameCount(), std::uint64_t{2});
    // One step, one frame -- not a resume.
    CNA_EDITOR_EXPECT(!host.tick());

    EditorMessage resume;
    resume.type = EditorMessageType::Resume;
    host.handle(resume, outbox);
    CNA_EDITOR_EXPECT(host.tick());
    CNA_EDITOR_EXPECT_EQ(host.getFrameCount(), std::uint64_t{3});
}

CNA_EDITOR_TEST(PlayerHostIgnoresStepWhileRunning)
{
    const ScratchProject project{"playernostep"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    EditorMessage step;
    step.type = EditorMessageType::StepFrame;
    host.handle(step, outbox);
    host.handle(step, outbox);

    // Stepping while running is meaningless; honouring it would make the game jump ahead.
    host.tick();
    CNA_EDITOR_EXPECT_EQ(host.getFrameCount(), std::uint64_t{1});
}

CNA_EDITOR_TEST(PlayerHostAppliesLiveSetProperty)
{
    const ScratchProject project{"playerlive"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    host.handle(EditorMessage::makeSetProperty(project.entityId, BuiltinComponentIds::kTransform,
                                               "position", PropertyValue{EditorVector3{7.0f, 8.0f, 9.0f}}),
                outbox);

    const EditorComponent* transform =
        host.getScene().findEntity(project.entityId)->findComponent(BuiltinComponentIds::kTransform);
    CNA_EDITOR_EXPECT_EQ(transform->getProperty("position").get<EditorVector3>().x, 7.0f);
    CNA_EDITOR_EXPECT_EQ(transform->getProperty("position").get<EditorVector3>().z, 9.0f);
}

CNA_EDITOR_TEST(PlayerHostReportsRatherThanCrashingOnBadRequests)
{
    const ScratchProject project{"playerbad"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    host.handle(EditorMessage::makeSetProperty(Uuid::generate(), BuiltinComponentIds::kTransform,
                                               "position", PropertyValue{EditorVector3{}}),
                outbox);
    host.handle(EditorMessage::makeSetProperty(project.entityId, "Nope.Missing",
                                               "x", PropertyValue{1.0f}),
                outbox);
    host.handle(EditorMessage::makeLoadScene(""), outbox);

    CNA_EDITOR_EXPECT_EQ(outbox.size(), std::size_t{3});
    for (const EditorMessage& reply : outbox)
    {
        CNA_EDITOR_EXPECT(reply.type == EditorMessageType::ReportLog);
    }
}

CNA_EDITOR_TEST(AScreenshotIsQueuedForTheGraphicsHalfRatherThanAnsweredOnTheSpot)
{
    const ScratchProject project{"playershot"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    EditorMessage request = EditorMessage::makeScreenshot("/tmp/frame.png");
    request.requestId = 42;
    host.handle(request, outbox);

    // Nothing goes back yet. Only the CNA-linked loop can read a back buffer, and a ScreenshotReady
    // sent from here would tell the editor a file exists before anything had been written to it --
    // a lie nothing later on the wire would correct.
    CNA_EDITOR_EXPECT(outbox.empty());

    std::vector<PlayerHost::ScreenshotRequest> queued = host.takeScreenshotRequests();
    CNA_EDITOR_EXPECT_EQ(queued.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(queued.front().path, std::string{"/tmp/frame.png"});
    CNA_EDITOR_EXPECT_EQ(queued.front().requestId, std::uint64_t{42});

    // Drained, so the graphics half cannot take the same capture twice.
    CNA_EDITOR_EXPECT(host.takeScreenshotRequests().empty());

    // The reply carries the request id back, so it can be matched with several in flight, and says
    // whether the file was written rather than leaving the editor to look for it.
    const EditorMessage written = PlayerHost::makeScreenshotReply(queued.front());
    CNA_EDITOR_EXPECT(written.type == EditorMessageType::ScreenshotReady);
    CNA_EDITOR_EXPECT_EQ(written.requestId, std::uint64_t{42});
    CNA_EDITOR_EXPECT(written.payload["written"].asBoolean(false));

    const EditorMessage failed = PlayerHost::makeScreenshotReply(queued.front(), "no device");
    CNA_EDITOR_EXPECT(!failed.payload["written"].asBoolean(true));
    CNA_EDITOR_EXPECT_EQ(failed.payload["error"].asString(), std::string{"no device"});
}

CNA_EDITOR_TEST(AScreenshotWithNoPathIsReportedRatherThanQueued)
{
    const ScratchProject project{"playershotpath"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    host.handle(EditorMessage::makeScreenshot(""), outbox);

    // Queuing a capture with nowhere to write it would produce a reply saying it failed, one frame
    // later, for a reason the editor could have been told immediately.
    CNA_EDITOR_EXPECT(host.takeScreenshotRequests().empty());
    CNA_EDITOR_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(outbox.front().type == EditorMessageType::ReportLog);
}

CNA_EDITOR_TEST(PlayerHostReportsAProtocolVersionMismatch)
{
    const ScratchProject project{"playerversion"};

    PlayerHost host;
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    PlayerHost::Outbox outbox;
    EditorMessage hello = EditorMessage::makeHello(project.directory.generic_string());
    hello.payload.set("protocolVersion", JsonValue{kEditorProtocolVersion + 99});
    host.handle(hello, outbox);

    // Reported, not fatal: the editor can then tell the user which build to rebuild, which it
    // could not do from a silently dropped connection.
    CNA_EDITOR_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(!host.isHandshakeComplete());

    outbox.clear();
    host.handle(EditorMessage::makeHello(project.directory.generic_string()), outbox);
    CNA_EDITOR_EXPECT(host.isHandshakeComplete());
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

CNA_EDITOR_TEST(IdenticalFramesCompareEqualAndTinyDifferencesAreWithinTolerance)
{
    const ImageBuffer reference = makeSolidImage(8, 8, 100, 149, 237);

    const ImageDifference same = compareImages(reference, reference, 0);
    CNA_EDITOR_EXPECT(same.comparable);
    CNA_EDITOR_EXPECT(same.matches());
    CNA_EDITOR_EXPECT_EQ(same.totalPixels, std::size_t{64});

    // Two backends drawing the same scene routinely differ by a step or two in a channel. A
    // comparison with no tolerance reports every backend as different from every other, which is
    // true and useless.
    const ImageBuffer nearly = makeSolidImage(8, 8, 101, 149, 238);
    CNA_EDITOR_EXPECT(compareImages(reference, nearly, 2).matches());
    CNA_EDITOR_EXPECT(!compareImages(reference, nearly, 0).matches());

    // The largest delta is reported even when everything is within tolerance: it is the number
    // that says whether two backends are *identical* or merely close enough.
    CNA_EDITOR_EXPECT_EQ(compareImages(reference, nearly, 2).maxChannelDelta, 1);
}

CNA_EDITOR_TEST(ADifferenceReportsHowManyPixelsAndWhere)
{
    const ImageBuffer reference = makeSolidImage(10, 10, 0, 0, 0);
    ImageBuffer other = reference;
    setPixel(other, 3, 4, 255, 255, 255);
    setPixel(other, 6, 8, 255, 255, 255);

    const ImageDifference difference = compareImages(reference, other);
    CNA_EDITOR_EXPECT_EQ(difference.differingPixels, std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(difference.maxChannelDelta, 255);

    // The bounding box is usually the diagnosis: a band along one edge is a viewport or scissor
    // problem, a scattering over one sprite is a filtering one.
    CNA_EDITOR_EXPECT_EQ(difference.boundingBox.x, 3);
    CNA_EDITOR_EXPECT_EQ(difference.boundingBox.y, 4);
    CNA_EDITOR_EXPECT_EQ(difference.boundingBox.width, 4);
    CNA_EDITOR_EXPECT_EQ(difference.boundingBox.height, 5);
}

CNA_EDITOR_TEST(ImagesOfDifferentSizesAreIncomparableRatherThanDifferent)
{
    // Not the same fact, and not the same action: a size mismatch means the capture went wrong,
    // not that the backends disagree about how to draw.
    const ImageDifference difference =
        compareImages(makeSolidImage(4, 4, 0, 0, 0), makeSolidImage(4, 5, 0, 0, 0));

    CNA_EDITOR_EXPECT(!difference.comparable);
    CNA_EDITOR_EXPECT(!difference.matches());
    CNA_EDITOR_EXPECT(!difference.incomparableReason.empty());
    CNA_EDITOR_EXPECT(compareImages(ImageBuffer{}, ImageBuffer{}).incomparableReason.size() > 0);
}

CNA_EDITOR_TEST(TheDifferenceImageMarksTheDifferingPixelsOnADimmedCopy)
{
    const ImageBuffer reference = makeSolidImage(4, 4, 200, 200, 200);
    ImageBuffer other = reference;
    setPixel(other, 1, 1, 0, 0, 0);

    const ImageBuffer marked = makeDifferenceImage(reference, other);
    CNA_EDITOR_EXPECT(marked.isWellFormed());

    // Magenta where they differ: it appears in no rendered scene by accident, so it cannot be
    // mistaken for part of the picture.
    const std::size_t differing = (1 * 4 + 1) * 4;
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(marked.pixels[differing + 0]), 255);
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(marked.pixels[differing + 1]), 0);
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(marked.pixels[differing + 2]), 255);

    // And a dimmed copy everywhere else, because the matching picture is the context that makes
    // the marked pixels mean anything.
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(marked.pixels[0]), 50);
}

CNA_EDITOR_TEST(AComparisonNeedsMoreThanOneBackend)
{
    const ScratchProject project{"comparefew"};

    ComparisonRequest request;
    request.projectPath = project.projectPath;
    request.outputDirectory = (project.directory / "comparison").generic_string();
    request.builds = {PlayerBuild{"software", "/nowhere/cna-player-software"}};

    // One player is not a comparison, and saying so beats launching it and reporting the useless
    // truth that a backend matches itself.
    CNA_EDITOR_EXPECT(!describeComparisonProblem(request).empty());

    BackendComparison comparison;
    CNA_EDITOR_EXPECT(!comparison.start(request, {}));
    CNA_EDITOR_EXPECT(comparison.getState() == ComparisonState::Failed);
    CNA_EDITOR_EXPECT(!comparison.getError().empty());
}

CNA_EDITOR_TEST(AComparisonReportsPlayersThatProduceNoFrame)
{
    const ScratchProject project{"comparemissing"};

    ComparisonRequest request;
    request.projectPath = project.projectPath;
    request.outputDirectory = (project.directory / "comparison").generic_string();
    request.builds = {PlayerBuild{"ghost", "/nowhere/cna-player-ghost"},
                      PlayerBuild{"phantom", "/nowhere/cna-player-phantom"}};
    request.warmupFrames = 0;

    BackendComparison comparison;
    CNA_EDITOR_EXPECT(comparison.start(request, {}));

    // Spawning a missing binary fails in the child, not in the parent, so the failure arrives as
    // an exit rather than as a refused start. It must not hold the run open for the whole timeout:
    // a dead player has answered as definitively as a live one.
    double now = 0.0;
    for (int step = 0; step < 500 && comparison.getState() != ComparisonState::Finished; ++step)
    {
        now += 0.01;
        comparison.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_EDITOR_EXPECT(comparison.getState() == ComparisonState::Finished);
    CNA_EDITOR_EXPECT(now < 20.0);

    // Each entry says what happened to it, rather than the run reporting one opaque failure.
    CNA_EDITOR_EXPECT_EQ(comparison.getEntries().size(), std::size_t{2});
    for (const ComparisonEntry& entry : comparison.getEntries())
    {
        CNA_EDITOR_EXPECT(!entry.errorMessage.empty());
        CNA_EDITOR_EXPECT(!entry.captured);
    }
    CNA_EDITOR_EXPECT(!comparison.allBackendsAgree());
}

CNA_EDITOR_TEST(TwoRealPlayersAreComparedAgainstEachOther)
{
    // The end-to-end case, with the one player build this repository produces standing in for two.
    // Pointing both entries at the same binary is not a cheat: what is being tested is the
    // sequence -- launch, handshake, ask each for the same frame, read both back, compare -- and
    // that sequence does not know or care that the two paths are equal. Whether two *different*
    // backends agree is a question about CNA, and it needs two backend builds installed.
    const ScratchProject project{"comparereal"};

    const std::vector<PlayerBuild> discovered = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_EDITOR_EXPECT(!discovered.empty());
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
    CNA_EDITOR_EXPECT(comparison.start(request, reader));

    double now = 0.0;
    for (int step = 0; step < 2000 && comparison.getState() != ComparisonState::Finished; ++step)
    {
        now += 0.01;
        comparison.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_EDITOR_EXPECT(comparison.getState() == ComparisonState::Finished);
    CNA_EDITOR_EXPECT_EQ(comparison.getEntries().size(), std::size_t{2});
    CNA_EDITOR_EXPECT(!comparison.getReferenceBackend().empty());

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
            CNA_EDITOR_EXPECT(entry.captured);
            CNA_EDITOR_EXPECT(entry.errorMessage.empty());
        }
        CNA_EDITOR_EXPECT(!comparison.allBackendsAgree());
        CNA_EDITOR_EXPECT_EQ(comparison.getEntries()[1].difference.differingPixels, std::size_t{1});
    }
    else
    {
        // Built without CNA: the player has no device to capture from and says so, rather than
        // leaving the editor waiting for a reply that will never come. That refusal is the whole
        // reason `screenshotReady` carries `written` at all.
        for (const ComparisonEntry& entry : comparison.getEntries())
        {
            CNA_EDITOR_EXPECT(!entry.errorMessage.empty());
        }
    }
}

CNA_EDITOR_TEST(PlayerBuildDiscoveryFindsTheInstalledBinaries)
{
    // Discovery is a real feature, not a detail: because CNA fixes its backend at compile time,
    // "preview on Software" means "launch cna-player-software", and the editor must offer only
    // the backends whose binaries actually exist.
    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());

    // At least one player must be discoverable, and every entry must name a real file. The
    // *backend* deliberately is not asserted: without CNA the binary is the un-suffixed
    // `cna-player` (reported as "default"), and with CNA it is `cna-player-<backend>`. Pinning
    // either would make this test pass in one configuration and fail in the other for no reason.
    CNA_EDITOR_EXPECT(!builds.empty());
    for (const PlayerBuild& build : builds)
    {
        CNA_EDITOR_EXPECT(!build.backend.empty());
        CNA_EDITOR_EXPECT(!build.executablePath.empty());
        CNA_EDITOR_EXPECT(std::filesystem::exists(build.executablePath));
    }

    CNA_EDITOR_EXPECT_EQ(discoverPlayerBuilds("/definitely/not/a/directory").size(), std::size_t{0});
}

CNA_EDITOR_TEST(EditorLaunchesARealPlayerProcessAndTalksToIt)
{
    // The end-to-end case: a real process, a real socket, real messages. Everything else in this
    // file could pass while play mode remained broken.
    const ScratchProject project{"bridge"};

    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_EDITOR_EXPECT(!builds.empty());
    if (builds.empty()) { return; }

    PlayerProcess player;
    CNA_EDITOR_EXPECT(player.start(builds.front(), project.projectPath));
    if (!player.isRunning() && player.getExitReason() == PlayerExitReason::FailedToStart)
    {
        ::CnaEditorTest::reportFailure(__FILE__, __LINE__, "cannot start player: " + player.getError());
        return;
    }

    bool sawReady = false;
    bool sawSceneLog = false;
    bool sentLoad = false;

    for (int attempt = 0; attempt < 400 && !(sawReady && sawSceneLog); ++attempt)
    {
        for (const EditorMessage& message : player.poll())
        {
            if (message.type == EditorMessageType::Ready) { sawReady = true; }
            if (message.type == EditorMessageType::ReportLog
                && message.payload["text"].asString().find("loaded scene") != std::string::npos)
            {
                sawSceneLog = true;
            }
        }

        if (sawReady && !sentLoad)
        {
            CNA_EDITOR_EXPECT(player.send(EditorMessage::makeLoadScene("Scenes/Level01.cnascene")));
            sentLoad = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_EDITOR_EXPECT(sawReady);

    // The player reports the backend it was compiled with -- "NONE" without CNA, the real backend
    // name with it. What matters is that *something* was reported: that is the mechanism the
    // editor uses to detect a mismatched launch, and an empty value would break it silently.
    CNA_EDITOR_EXPECT(!player.getReportedBackend().empty());
    CNA_EDITOR_EXPECT(sawSceneLog);

    player.stop();
    CNA_EDITOR_EXPECT(!player.isRunning());
    CNA_EDITOR_EXPECT(player.getExitReason() != PlayerExitReason::FailedToStart);
}

CNA_EDITOR_TEST(PlayerProcessReportsAMissingBinaryRatherThanHanging)
{
    const ScratchProject project{"bridgemissing"};

    PlayerBuild missing;
    missing.backend = "nonexistent";
    missing.executablePath = (project.directory / "cna-player-nonexistent").generic_string();

    PlayerProcess player;
    player.start(missing, project.projectPath);

    // On POSIX the fork succeeds and the exec fails in the child, so the failure surfaces as the
    // process exiting rather than as a spawn error. Either way the editor must notice and must
    // not sit waiting for a connection that will never arrive.
    for (int attempt = 0; attempt < 200 && player.isRunning(); ++attempt)
    {
        player.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CNA_EDITOR_EXPECT(!player.isRunning());
}

/**
 * @brief An editor over the null UI, for driving play mode end to end.
 *
 * The real EditorApplication, not a stand-in: play mode is exactly the feature where a test double
 * would let a broken editor pass. Frames are stepped by hand so the test controls when the bridge
 * is pumped.
 */
namespace
{
    struct PlayFixture
    {
        EditorApplication application{std::make_unique<NullEditorUi>(),
                                      std::make_unique<NullEditorViewport>()};

        explicit PlayFixture(const std::string& projectPath = {})
        {
            EditorOptions options;
            options.headless = true;
            options.projectPath = projectPath;
            application.initialize(options);
        }

        [[nodiscard]] bool logContains(std::string_view needle) const
        {
            const auto& ui = static_cast<const NullEditorUi&>(
                const_cast<PlayFixture*>(this)->application.getUi());
            for (const auto& entry : ui.getLog())
            {
                if (entry.message.find(needle) != std::string::npos) { return true; }
            }
            return false;
        }

        /** @brief Steps frames, pumping the bridge, until @p predicate holds or the budget runs out. */
        template <typename Predicate>
        bool pumpUntil(Predicate predicate)
        {
            for (int attempt = 0; attempt < 400; ++attempt)
            {
                application.renderFrame();
                if (predicate()) { return true; }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return false;
        }
    };
}

CNA_EDITOR_TEST(PlayRefusesWithNoProjectAndSaysWhy)
{
    PlayFixture fixture;
    fixture.application.setPlayerBuilds(discoverPlayerBuilds(playerDirectory().generic_string()));

    fixture.application.startPlay();

    CNA_EDITOR_EXPECT(fixture.application.getPlayMode() == PlayMode::Stopped);

    // Saying *why* matters more than refusing: a Play button that does nothing at all is a bug
    // report, and the reason is something only the editor knows.
    CNA_EDITOR_EXPECT(fixture.logContains("open project"));
}

CNA_EDITOR_TEST(PlayRefusesWhenNoPlayerBuildIsInstalled)
{
    const ScratchProject project{"noplayerbuild"};

    PlayFixture fixture{project.projectPath};
    fixture.application.setPlayerBuilds({});

    fixture.application.startPlay();

    CNA_EDITOR_EXPECT(fixture.application.getPlayMode() == PlayMode::Stopped);
    CNA_EDITOR_EXPECT(fixture.logContains("No cna-player build"));
}

CNA_EDITOR_TEST(PlayControlsAreInertWhileStopped)
{
    const ScratchProject project{"inertcontrols"};
    PlayFixture fixture{project.projectPath};

    // Nothing is running, so these must be no-ops rather than acting on a null process.
    fixture.application.setPlayPaused(true);
    fixture.application.stepPlayFrame();
    fixture.application.stopPlay();

    CNA_EDITOR_EXPECT(fixture.application.getPlayMode() == PlayMode::Stopped);
}

CNA_EDITOR_TEST(TheEditorDrivesARealPlayerThroughPlayPauseStepAndStop)
{
    // The whole point of ED-245: the toolbar's operations against a real process, a real socket
    // and the real protocol. Every other test in this file could pass with the toolbar unwired.
    const ScratchProject project{"playtoolbar"};

    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_EDITOR_EXPECT(!builds.empty());
    if (builds.empty()) { return; }

    PlayFixture fixture{project.projectPath};
    fixture.application.setPlayerBuilds(builds);

    fixture.application.startPlay();
    CNA_EDITOR_EXPECT(fixture.application.getPlayMode() == PlayMode::Playing);
    if (fixture.application.getPlayMode() != PlayMode::Playing)
    {
        ::CnaEditorTest::reportFailure(__FILE__, __LINE__, "player did not start");
        return;
    }

    CNA_EDITOR_EXPECT(fixture.pumpUntil([&] { return fixture.logContains("Player ready"); }));

    fixture.application.setPlayPaused(true);
    CNA_EDITOR_EXPECT(fixture.application.getPlayMode() == PlayMode::Paused);

    // The player's own log coming back through the console is the proof the message arrived --
    // the editor changing its own state proves only that it changed its own state.
    CNA_EDITOR_EXPECT(fixture.pumpUntil([&] { return fixture.logContains("player: paused"); }));

    fixture.application.stepPlayFrame();

    fixture.application.setPlayPaused(false);
    CNA_EDITOR_EXPECT(fixture.application.getPlayMode() == PlayMode::Playing);
    CNA_EDITOR_EXPECT(fixture.pumpUntil([&] { return fixture.logContains("player: resumed"); }));

    fixture.application.stopPlay();
    CNA_EDITOR_EXPECT(fixture.application.getPlayMode() == PlayMode::Stopped);
}

CNA_EDITOR_TEST(PlayerProcessIntroducesItselfOnceConnected)
{
    const ScratchProject project{"handshake"};

    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_EDITOR_EXPECT(!builds.empty());
    if (builds.empty()) { return; }

    PlayerProcess player;
    CNA_EDITOR_EXPECT(player.start(builds.front(), project.projectPath));

    bool sawReady = false;
    for (int attempt = 0; attempt < 400 && !(sawReady && player.isHelloSent()); ++attempt)
    {
        for (const EditorMessage& message : player.poll())
        {
            if (message.type == EditorMessageType::Ready) { sawReady = true; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // The player treats a missing Hello as an incomplete handshake and says nothing about it, so
    // forgetting to send one produces a session that looks connected and is quietly degraded.
    CNA_EDITOR_EXPECT(sawReady);
    CNA_EDITOR_EXPECT(player.isHelloSent());

    player.stop();
}

CNA_EDITOR_TEST(PlayerHostReloadsOneAssetByIdAndReportsWhatItDid)
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
    CNA_EDITOR_EXPECT(host.openProject(project.projectPath));

    // Learn the id the same way the editor would: by scanning the same directory.
    AssetDatabase editorSide;
    editorSide.setProjectRoot(project.directory.generic_string());
    CNA_EDITOR_EXPECT(editorSide.scan("Assets").succeeded);
    const AssetRecord* editorRecord = editorSide.findByPath("Assets/hero.png");
    CNA_EDITOR_EXPECT(editorRecord != nullptr);
    if (editorRecord == nullptr) { return; }
    const Uuid assetId = editorRecord->id;

    PlayerHost::Outbox outbox;
    host.handle(EditorMessage::makeReloadAsset(assetId), outbox);

    bool reported = false;
    for (const EditorMessage& message : outbox)
    {
        if (message.payload["text"].asString().find("reloaded 'Assets/hero.png'") != std::string::npos)
        {
            reported = true;
        }
    }
    CNA_EDITOR_EXPECT(reported);

    // The graphics half drains this to drop what it has cached. A list, not a flag: it runs on its
    // own schedule and must not miss a reload that arrived between two of its frames.
    const std::vector<Uuid> reloaded = host.takeReloadedAssets();
    CNA_EDITOR_EXPECT_EQ(reloaded.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(reloaded.front() == assetId);
    CNA_EDITOR_EXPECT(host.takeReloadedAssets().empty());

    // An id the player does not know is a timing difference, not a broken session.
    outbox.clear();
    host.handle(EditorMessage::makeReloadAsset(Uuid::generate()), outbox);
    CNA_EDITOR_EXPECT(host.takeReloadedAssets().empty());

    bool warned = false;
    for (const EditorMessage& message : outbox)
    {
        if (message.payload["severity"].asString() == "warn"
            && message.payload["text"].asString().find("unknown asset") != std::string::npos)
        {
            warned = true;
        }
    }
    CNA_EDITOR_EXPECT(warned);
}

namespace
{
    /** @brief A null UI that can report keys held and a hovered viewport, for input forwarding. */
    class InputScriptedUi final : public NullEditorUi
    {
    public:
        std::vector<UiKey> heldKeys;
        UiImageInteraction interaction;

        [[nodiscard]] bool isKeyDown(UiKey key) const override
        {
            return std::find(heldKeys.begin(), heldKeys.end(), key) != heldKeys.end();
        }

        UiImageInteraction image(const std::string& id, UiTextureId texture, float width, float height,
                                 bool flipVertically = false) override
        {
            (void)id;
            (void)texture;
            (void)width;
            (void)height;
            (void)flipVertically;
            return interaction;
        }
    };
}

CNA_EDITOR_TEST(LiveEditsAndAssetChangesReachARunningPlayer)
{
    // End to end, over a real process and a real socket. Everything else about ED-306 and ED-307
    // could pass while the editor never sent a single message.
    const ScratchProject project{"liveedit"};

    const std::filesystem::path assetPath = project.directory / "Assets" / "hero.png";
    std::filesystem::create_directories(assetPath.parent_path());
    {
        std::ofstream stream{assetPath, std::ios::binary | std::ios::trunc};
        stream << "hero";
    }

    const std::vector<PlayerBuild> builds = discoverPlayerBuilds(playerDirectory().generic_string());
    CNA_EDITOR_EXPECT(!builds.empty());
    if (builds.empty()) { return; }

    auto scripted = std::make_unique<InputScriptedUi>();
    InputScriptedUi* ui = scripted.get();
    NullEditorUi* log = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    options.autosaveSeconds = 0.0;
    options.projectPath = project.projectPath;
    CNA_EDITOR_EXPECT(application.initialize(options));

    application.setPlayerBuilds(builds);
    application.getAssetWatcher().setInterval(0.0);
    application.startPlay();
    CNA_EDITOR_EXPECT(application.getPlayMode() == PlayMode::Playing);

    EditorContext& context = application.getContext();

    // Wait for the handshake before editing: a message sent before the player is connected is not
    // queued anywhere, and asserting on it would make this test flaky rather than wrong.
    const auto sawLog = [&](std::string_view needle) {
        for (const auto& entry : log->getLog())
        {
            if (entry.message.find(needle) != std::string::npos) { return true; }
        }
        return false;
    };

    for (int attempt = 0; attempt < 400 && !sawLog("Player ready"); ++attempt)
    {
        application.renderFrame(0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CNA_EDITOR_EXPECT(sawLog("Player ready"));

    // ED-307: an inspector edit is a command, and every command goes through the one hook.
    context.execute(std::make_unique<SetPropertyCommand>(
        context.getScene(), project.entityId, BuiltinComponentIds::kTransform, "position",
        PropertyValue{EditorVector3{40.0f, 8.0f, 0.0f}}));

    for (int attempt = 0; attempt < 400 && !sawLog("set Player.CNA.Transform.position"); ++attempt)
    {
        application.renderFrame(0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CNA_EDITOR_EXPECT(sawLog("set Player.CNA.Transform.position"));

    // ED-306: a file changed outside the editor reaches the player as a reload of that one asset.
    {
        std::ofstream stream{assetPath, std::ios::binary | std::ios::trunc};
        stream << "hero-repainted";
    }
    application.getAssetWatcher().requestImmediatePoll();

    for (int attempt = 0; attempt < 400 && !sawLog("reloaded 'Assets/hero.png'"); ++attempt)
    {
        application.renderFrame(1.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CNA_EDITOR_EXPECT(sawLog("reloaded 'Assets/hero.png'"));

    // Input forwarding, on the same running process rather than in a test of its own: a second
    // player costs a second handshake and proves nothing this one does not. Driven through the
    // viewport panel rather than by calling forwardInputToPlayer directly, because the panel
    // forwards a snapshot every frame -- a hand-sent one would be overwritten by the next frame's
    // empty one, which is the feature working rather than the test failing.
    ui->heldKeys = {UiKey::W};
    ui->interaction = UiImageInteraction{};
    ui->interaction.hovered = true;
    ui->interaction.localMouseX = 320.0f;
    ui->interaction.localMouseY = 180.0f;
    ui->interaction.leftDown = true;

    for (int attempt = 0; attempt < 400 && !application.getPlayerInput().isKeyDown("W"); ++attempt)
    {
        application.renderFrame(0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const PlayerInputSnapshot& seen = application.getPlayerInput();
    CNA_EDITOR_EXPECT(seen.isKeyDown("W"));
    CNA_EDITOR_EXPECT(seen.leftButton);

    // A headless player has no window, so it reports back the surface it was given rather than
    // inventing one -- and the pointer is still a quarter of the way across it.
    CNA_EDITOR_EXPECT(seen.hasPointer());
    CNA_EDITOR_EXPECT(std::abs(seen.mouseX / seen.surfaceWidth - 0.25f) < 0.01f);

    application.stopPlay();
    CNA_EDITOR_EXPECT(application.getPlayMode() == PlayMode::Stopped);
}

CNA_EDITOR_TEST(AnInputSnapshotSurvivesTheWireAndIsMappedIntoThePlayersWindow)
{
    PlayerInputSnapshot sent;
    sent.keys = {"W", "Space"};
    sent.mouseX = 100.0f;
    sent.mouseY = 50.0f;
    sent.surfaceWidth = 400.0f;
    sent.surfaceHeight = 200.0f;
    sent.leftButton = true;
    sent.wheel = -2.0f;

    const std::optional<EditorMessage> decoded =
        EditorMessage::decode(EditorMessage::makeInput(sent).encode());
    CNA_EDITOR_EXPECT(decoded.has_value());
    if (!decoded) { return; }

    CNA_EDITOR_EXPECT(decoded->type == EditorMessageType::Input);
    CNA_EDITOR_EXPECT(PlayerInputSnapshot::fromJson(decoded->payload) == sent);

    // The mapping is the whole point of sending the surface with the pointer: a quarter of the way
    // across the editor's panel is a quarter of the way across the game's window, whatever the two
    // are measured in.
    const PlayerInputSnapshot mapped = sent.mapToSurface(1280.0f, 720.0f);
    CNA_EDITOR_EXPECT(std::abs(mapped.mouseX - 320.0f) < 0.01f);
    CNA_EDITOR_EXPECT(std::abs(mapped.mouseY - 180.0f) < 0.01f);
    CNA_EDITOR_EXPECT(mapped.isKeyDown("W") && !mapped.isKeyDown("A"));

    // No pointer stays no pointer. Mapping it would put the cursor in a corner of the game window
    // and leave it there, which is worse than saying nothing.
    PlayerInputSnapshot keysOnly;
    keysOnly.keys = {"Escape"};
    CNA_EDITOR_EXPECT(!keysOnly.hasPointer());
    CNA_EDITOR_EXPECT(keysOnly.mapToSurface(1280.0f, 720.0f) == keysOnly);
}

CNA_EDITOR_TEST(ThePlayerMapsForwardedInputAndReportsWhatItHolds)
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
    host.handle(EditorMessage::makeInput(sent), outbox);
    CNA_EDITOR_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(host.getInput() == sent);

    outbox.clear();
    host.setSurfaceSize(1280, 720);
    host.handle(EditorMessage::makeInput(sent), outbox);

    CNA_EDITOR_EXPECT_EQ(outbox.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(outbox.front().type == EditorMessageType::ReportInput);

    const PlayerInputSnapshot held = host.getInput();
    CNA_EDITOR_EXPECT(std::abs(held.surfaceWidth - 1280.0f) < 0.01f);
    CNA_EDITOR_EXPECT(std::abs(held.mouseX - 470.5f * (1280.0f / 941.0f)) < 0.01f);
    CNA_EDITOR_EXPECT(held.isKeyDown("D"));
    CNA_EDITOR_EXPECT(held.rightButton);

    // The reply carries exactly what the player holds, so the editor can show the game's own view
    // rather than an echo of what it sent.
    CNA_EDITOR_EXPECT(PlayerInputSnapshot::fromJson(outbox.front().payload) == held);
}
