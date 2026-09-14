// SPDX-License-Identifier: MS-PL
/**
 * @file ProjectAndAssetTests.cpp
 * @brief Tests for the project format, the backend table, the asset database and the wire protocol.
 */

#include "TestHarness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sstream>

#include "CNA/Editor/Assets/AssetCommands.hpp"
#include "CNA/Editor/EditorContext.hpp"
#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Assets/MaterialDocument.hpp"
#include "CNA/Editor/Assets/AssetImporters.hpp"
#include "CNA/Editor/Assets/AssetTree.hpp"
#include "CNA/Editor/Assets/AssetWatcher.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/MissingReferences.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"
#include "CNA/Editor/Plugins/Plugin.hpp"
#include "CNA/Editor/Project/BuildRunner.hpp"
#include "CNA/Editor/Project/Project.hpp"
#include "CNA/Editor/ProjectCommands.hpp"
#include "CNA/Editor/Project/RecoveryStore.hpp"
#include "CNA/Editor/RuntimeBridge/EditorProtocol.hpp"

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

    void writeFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream << contents;
    }
}

CNA_EDITOR_TEST(ProjectRoundTripsThroughAFile)
{
    const std::filesystem::path directory = makeScratchDirectory("project");
    const std::string path = (directory / "MyGame.cnaproject").generic_string();

    Project original = Project::createDefault("MyGame", directory.generic_string());
    original.setDefaultGraphicsBackend("vulkan");
    original.setModules({"cna-core", "cna-graphics-2d", "cna-audio"});
    original.setTargetPlatforms({"linux-x64", "windows-x64"});

    std::string errorMessage;
    CNA_EDITOR_EXPECT(original.saveToFile(path, &errorMessage));
    CNA_EDITOR_EXPECT_EQ(errorMessage, std::string{});

    Project restored;
    const ProjectLoadResult result = restored.loadFromFile(path);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{0});
    CNA_EDITOR_EXPECT_EQ(restored.getName(), std::string{"MyGame"});
    CNA_EDITOR_EXPECT(restored.getKind() == ProjectKind::CnaNative);
    CNA_EDITOR_EXPECT_EQ(restored.getDefaultGraphicsBackend(), std::string{"vulkan"});
    CNA_EDITOR_EXPECT_EQ(restored.getModules().size(), std::size_t{3});
    CNA_EDITOR_EXPECT_EQ(restored.getStartupScene(), std::string{"Scenes/MainMenu.cnascene"});
    CNA_EDITOR_EXPECT_EQ(restored.getRootPath(), directory.generic_string());

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(ProjectWarnsAboutAnUnknownBackend)
{
    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{Project::kFormatVersion});
    json.set("name", JsonValue{"MyGame"});
    json.set("defaultGraphicsBackend", JsonValue{"glide"});

    Project project;
    const ProjectLoadResult result = project.loadFromJson(json);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{1});
}

CNA_EDITOR_TEST(XnaCompatibleProjectWarnsAboutAStartupScene)
{
    // An XNA-style game owns its own object model. Declaring a scene the editor will never load
    // is a real mismatch worth surfacing, not something to silently honour.
    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{Project::kFormatVersion});
    json.set("name", JsonValue{"PortedGame"});
    json.set("kind", JsonValue{"XnaCompatible"});
    json.set("startupScene", JsonValue{"Scenes/Main.cnascene"});

    Project project;
    const ProjectLoadResult result = project.loadFromJson(json);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT(project.getKind() == ProjectKind::XnaCompatible);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{1});
}

CNA_EDITOR_TEST(BackendTableCoversEveryCnaBackend)
{
    // Mirrors cmake/BackendSelection.cmake in the CNA revision this editor targets. When CNA gains
    // a backend, this count changes and this test is the reminder to update the table.
    CNA_EDITOR_EXPECT_EQ(getKnownBackends().size(), std::size_t{14});

    CNA_EDITOR_EXPECT(findBackend("easygl") != nullptr);
    CNA_EDITOR_EXPECT(findBackend("EASYGL") != nullptr);
    CNA_EDITOR_EXPECT(findBackend("glide") == nullptr);

    CNA_EDITOR_EXPECT(findBackend("easygl")->support == BackendEditorSupport::EditorSupported);
    // A CPU rasterizer is a fine reference renderer and a hopeless interactive UI host.
    CNA_EDITOR_EXPECT(findBackend("software")->support == BackendEditorSupport::PreviewOnly);
    // Historical and Emscripten-only backends are exactly why the player is a separate process.
    CNA_EDITOR_EXPECT(findBackend("dx3")->support == BackendEditorSupport::RuntimeOnly);
    CNA_EDITOR_EXPECT(findBackend("canvas")->support == BackendEditorSupport::RuntimeOnly);
}

CNA_EDITOR_TEST(AssetDatabaseAssignsStableIdsAndWritesSidecars)
{
    const std::filesystem::path directory = makeScratchDirectory("assets");
    writeFile(directory / "Assets" / "Textures" / "player.png", "not-a-real-png");
    writeFile(directory / "Assets" / "Sounds" / "click.wav", "not-a-real-wav");

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());

    const AssetScanResult first = database.scan("Assets");
    CNA_EDITOR_EXPECT(first.succeeded);
    CNA_EDITOR_EXPECT_EQ(first.discoveredCount, std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(first.newCount, std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(database.getCount(), std::size_t{2});

    const AssetRecord* texture = database.findByPath("Assets/Textures/player.png");
    CNA_EDITOR_EXPECT(texture != nullptr);
    CNA_EDITOR_EXPECT(texture->type == AssetType::Texture2D);
    CNA_EDITOR_EXPECT_EQ(texture->importerId, std::string{"CNA.TextureImporter"});
    const Uuid textureId = texture->id;

    // The sidecar must be on disk, so the id survives a restart.
    CNA_EDITOR_EXPECT(std::filesystem::exists(
        (directory / "Assets" / "Textures" / "player.png.cnaasset").generic_string()));

    // A second scan from a fresh database must recover the same ids, not mint new ones.
    AssetDatabase reopened;
    reopened.setProjectRoot(directory.generic_string());
    const AssetScanResult second = reopened.scan("Assets");
    CNA_EDITOR_EXPECT(second.succeeded);
    CNA_EDITOR_EXPECT_EQ(second.newCount, std::size_t{0});
    CNA_EDITOR_EXPECT(reopened.find(textureId) != nullptr);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(AssetDatabaseKeepsIdentityAcrossAMove)
{
    // This is the entire reason assets are referenced by id and not by path: moving a file must
    // not touch a single scene.
    const std::filesystem::path directory = makeScratchDirectory("assetmove");
    writeFile(directory / "Assets" / "player.png", "pixels");

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_EDITOR_EXPECT(database.scan("Assets").succeeded);

    const Uuid originalId = database.findByPath("Assets/player.png")->id;

    std::filesystem::create_directories(directory / "Assets" / "Characters");
    std::filesystem::rename(directory / "Assets" / "player.png",
                            directory / "Assets" / "Characters" / "player.png");
    std::filesystem::rename(directory / "Assets" / "player.png.cnaasset",
                            directory / "Assets" / "Characters" / "player.png.cnaasset");

    AssetDatabase rescanned;
    rescanned.setProjectRoot(directory.generic_string());
    const AssetScanResult result = rescanned.scan("Assets");
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.newCount, std::size_t{0});

    const AssetRecord* moved = rescanned.find(originalId);
    CNA_EDITOR_EXPECT(moved != nullptr);
    CNA_EDITOR_EXPECT_EQ(moved->sourcePath, std::string{"Assets/Characters/player.png"});
    CNA_EDITOR_EXPECT(rescanned.findByPath("Assets/player.png") == nullptr);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(AssetDatabaseReportsMissingSourcesRatherThanDroppingThem)
{
    const std::filesystem::path directory = makeScratchDirectory("assetmissing");
    writeFile(directory / "Assets" / "gone.png", "pixels");

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_EDITOR_EXPECT(database.scan("Assets").succeeded);

    const Uuid id = database.findByPath("Assets/gone.png")->id;
    std::filesystem::remove(directory / "Assets" / "gone.png");

    // The record survives: a file that is gone today may be one git checkout away from returning,
    // and deleting the record would break every reference to it permanently.
    CNA_EDITOR_EXPECT(database.find(id) != nullptr);
    CNA_EDITOR_EXPECT(database.isMissing(id));
    CNA_EDITOR_EXPECT_EQ(database.getMissingAssets().size(), std::size_t{1});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(AssetDatabaseTolerationOfAnAbsentAssetDirectory)
{
    const std::filesystem::path directory = makeScratchDirectory("assetempty");

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());

    // A brand-new project has no Assets directory yet; that is normal, not an error.
    const AssetScanResult result = database.scan("Assets");
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(database.getCount(), std::size_t{0});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(AssetSidecarStampSurvivesAJsonRoundTrip)
{
    // Regression: the stamp was originally the filesystem clock's native tick count -- around
    // 4.6e18 nanoseconds -- which is past the range a double represents exactly. JSON numbers are
    // doubles, so it came back changed and every asset looked modified on every scan.
    const std::filesystem::path directory = makeScratchDirectory("assetstamp");
    writeFile(directory / "Assets" / "stamped.png", "pixels");

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_EDITOR_EXPECT(database.scan("Assets").succeeded);

    const std::int64_t stamp = database.findByPath("Assets/stamped.png")->sourceModifiedTime;
    CNA_EDITOR_EXPECT(stamp != 0);

    AssetDatabase reopened;
    reopened.setProjectRoot(directory.generic_string());
    CNA_EDITOR_EXPECT(reopened.scan("Assets").succeeded);
    CNA_EDITOR_EXPECT_EQ(reopened.findByPath("Assets/stamped.png")->sourceModifiedTime, stamp);

    // And the sidecar itself must hold a plain integer, not scientific notation.
    std::ifstream sidecar{(directory / "Assets" / "stamped.png.cnaasset").generic_string()};
    std::string text{std::istreambuf_iterator<char>{sidecar}, std::istreambuf_iterator<char>{}};
    const std::size_t fieldOffset = text.find("\"modifiedTime\"");
    CNA_EDITOR_EXPECT(fieldOffset != std::string::npos);
    const std::size_t valueOffset = text.find(':', fieldOffset) + 1;
    const std::string writtenValue = text.substr(valueOffset, text.find('\n', valueOffset) - valueOffset);
    CNA_EDITOR_EXPECT(writtenValue.find('e') == std::string::npos);
    CNA_EDITOR_EXPECT(writtenValue.find('E') == std::string::npos);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(AssetTypeGuessingCoversTheCommonExtensions)
{
    CNA_EDITOR_EXPECT(AssetDatabase::guessTypeFromExtension("a/b/c.PNG") == AssetType::Texture2D);
    CNA_EDITOR_EXPECT(AssetDatabase::guessTypeFromExtension("x.wav") == AssetType::SoundEffect);
    CNA_EDITOR_EXPECT(AssetDatabase::guessTypeFromExtension("x.ogg") == AssetType::Song);
    CNA_EDITOR_EXPECT(AssetDatabase::guessTypeFromExtension("x.gltf") == AssetType::Model);
    CNA_EDITOR_EXPECT(AssetDatabase::guessTypeFromExtension("x.cnj") == AssetType::Model);
    CNA_EDITOR_EXPECT(AssetDatabase::guessTypeFromExtension("x.cnascene") == AssetType::Scene);
    CNA_EDITOR_EXPECT(AssetDatabase::guessTypeFromExtension("x.zzz") == AssetType::Unknown);
}

CNA_EDITOR_TEST(ProtocolMessagesRoundTripThroughTheWire)
{
    const EditorMessage original = EditorMessage::makeLoadScene("Scenes/Level01.cnascene");
    const std::string encoded = original.encode();

    // The framing is the newline, so the body must contain exactly one and it must be last.
    CNA_EDITOR_EXPECT_EQ(encoded.back(), '\n');
    CNA_EDITOR_EXPECT_EQ(encoded.find('\n'), encoded.size() - 1);

    const std::optional<EditorMessage> decoded = EditorMessage::decode(encoded.substr(0, encoded.size() - 1));
    CNA_EDITOR_EXPECT(decoded.has_value());
    CNA_EDITOR_EXPECT(decoded->type == EditorMessageType::LoadScene);
    CNA_EDITOR_EXPECT_EQ(decoded->payload["scenePath"].asString(), std::string{"Scenes/Level01.cnascene"});
}

CNA_EDITOR_TEST(ProtocolSetPropertyCarriesItsOwnType)
{
    const Uuid entityId = Uuid::generate();
    const EditorMessage message = EditorMessage::makeSetProperty(
        entityId, "CNA.Transform", "position", PropertyValue{EditorVector3{1.0f, 2.0f, 3.0f}});

    // Unlike a scene file, the wire must be self-describing: the player's component registry may
    // not match the editor's after a plugin reload.
    CNA_EDITOR_EXPECT_EQ(message.payload["valueType"].asString(), std::string{"vector3"});
    CNA_EDITOR_EXPECT_EQ(message.payload["entityId"].asString(), entityId.toString());

    const PropertyValue restored =
        PropertyValue::fromJson(message.payload["value"],
                                parsePropertyType(message.payload["valueType"].asString()));
    CNA_EDITOR_EXPECT_EQ(restored.get<EditorVector3>().y, 2.0f);
}

CNA_EDITOR_TEST(MessageDecoderReassemblesSplitMessages)
{
    // A stream socket delivers arbitrary chunks. A reader that assumes one recv() equals one
    // message works until a message straddles a packet boundary, and then fails unreproducibly.
    const std::string wire = EditorMessage::makeHello("/tmp/MyGame").encode()
                           + EditorMessage::makeReportLog("info", "ready").encode();

    MessageStreamDecoder decoder;
    std::vector<EditorMessage> messages;
    for (std::size_t offset = 0; offset < wire.size(); offset += 7)
    {
        const std::vector<EditorMessage> batch = decoder.feed(std::string_view{wire}.substr(offset, 7));
        messages.insert(messages.end(), batch.begin(), batch.end());
    }

    CNA_EDITOR_EXPECT_EQ(messages.size(), std::size_t{2});
    CNA_EDITOR_EXPECT(messages[0].type == EditorMessageType::Hello);
    CNA_EDITOR_EXPECT_EQ(messages[0].payload["protocolVersion"].asInt(), kEditorProtocolVersion);
    CNA_EDITOR_EXPECT(messages[1].type == EditorMessageType::ReportLog);
    CNA_EDITOR_EXPECT_EQ(decoder.getDroppedCount(), std::uint64_t{0});
}

CNA_EDITOR_TEST(MessageDecoderSkipsGarbageWithoutLosingTheStream)
{
    MessageStreamDecoder decoder;
    const std::string wire = "this is not json\n"
                           + EditorMessage::makeHello("/tmp/MyGame").encode()
                           + "{\"type\":\"somethingFromTheFuture\"}\n"
                           + EditorMessage::makeReportLog("warn", "still here").encode();

    const std::vector<EditorMessage> messages = decoder.feed(wire);

    // A peer from a newer revision sending something unrecognised must not kill a play session.
    CNA_EDITOR_EXPECT_EQ(messages.size(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(decoder.getDroppedCount(), std::uint64_t{2});
}

CNA_EDITOR_TEST(PluginHostRejectsIncompatibleManifestsWithoutLoadingThem)
{
    const std::filesystem::path directory = makeScratchDirectory("plugins");

    writeFile(directory / "good" / "plugin.json",
              R"({"id":"org.openeggbert.mc3","name":"MC3 Tools","version":"0.1.0",)"
              R"("editorApiVersion":1,"library":"libmc3.so"})");
    writeFile(directory / "good" / "libmc3.so", "not-a-real-library");

    writeFile(directory / "stale" / "plugin.json",
              R"({"id":"org.example.stale","editorApiVersion":0,"library":"libstale.so"})");

    writeFile(directory / "broken" / "plugin.json", "{ this is not json");

    PluginHost host;
    const std::vector<LoadedPlugin> plugins = host.discover(directory.generic_string());
    CNA_EDITOR_EXPECT_EQ(plugins.size(), std::size_t{3});

    std::size_t apiMismatchCount = 0;
    std::size_t malformedCount = 0;
    std::size_t acceptedCount = 0;
    for (const LoadedPlugin& plugin : plugins)
    {
        if (plugin.error.find("editor API version") != std::string::npos) { ++apiMismatchCount; }
        if (plugin.error.find("malformed") != std::string::npos) { ++malformedCount; }
        if (plugin.loaded) { ++acceptedCount; }

        // Rejected or accepted, *nothing has been opened*: `loaded` means the manifest passed
        // every check that can be made without running the plugin's code. An ABI mismatch that
        // reaches dlopen is a crash rather than an error message (D-11), so the gate comes first.
        CNA_EDITOR_EXPECT(!plugin.active);
    }
    CNA_EDITOR_EXPECT_EQ(apiMismatchCount, std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(malformedCount, std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(acceptedCount, std::size_t{1});

    // And the accepted one still does not *open*: its "library" is a text file. That is the whole
    // reason `loaded` and `active` are two flags -- a manifest a user can fix and a build a user
    // can fix are different problems, and telling them apart is the point.
    EditorContext context;
    CNA_EDITOR_EXPECT_EQ(host.loadAll(context), std::size_t{0});
    for (const LoadedPlugin& plugin : host.getPlugins())
    {
        CNA_EDITOR_EXPECT(!plugin.active);
    }

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(MissingReferencesFindsBrokenAssetSlots)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    // A real project on disk, so "tracked and present" and "tracked but the file is gone" are
    // genuinely different states rather than both resolving to absent.
    const std::filesystem::path directory = makeScratchDirectory("missingrefs");
    writeFile(directory / "Textures" / "Present.png", "not really a png");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());

    const Uuid presentId = Uuid::generate();
    AssetRecord present;
    present.id = presentId;
    present.sourcePath = "Textures/Present.png";
    present.type = AssetType::Texture2D;
    assets.add(std::move(present));

    const Uuid strandedId = Uuid::generate();
    AssetRecord stranded;
    stranded.id = strandedId;
    stranded.sourcePath = "Textures/Deleted.png";
    stranded.type = AssetType::Texture2D;
    assets.add(std::move(stranded));

    SceneDocument scene;
    const Uuid goneId = Uuid::generate();

    const auto addSprite = [&](const std::string& name, const Uuid& textureId) {
        EditorEntity entity{Uuid::generate(), name};
        EditorComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
        entity.addComponent(std::move(transform));

        EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
        sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
        sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{textureId}});
        entity.addComponent(std::move(sprite));
        return scene.addEntity(std::move(entity));
    };

    addSprite("Good", presentId);
    addSprite("BrokenA", goneId);
    addSprite("BrokenB", goneId);
    addSprite("Stranded", strandedId);
    addSprite("Empty", Uuid{});

    const std::vector<MissingReference> missing = findMissingReferences(scene, assets);

    // The resolvable one is fine, and an empty slot is an ordinary state rather than a fault --
    // a sprite that has not been given a texture yet is not broken.
    CNA_EDITOR_EXPECT_EQ(missing.size(), std::size_t{3});

    std::size_t notInDatabase = 0;
    std::size_t fileMissing = 0;
    for (const MissingReference& reference : missing)
    {
        CNA_EDITOR_EXPECT_EQ(reference.propertyName, std::string{"texture"});
        if (reference.reason == MissingReference::Reason::NotInDatabase)
        {
            ++notInDatabase;
            CNA_EDITOR_EXPECT_EQ(reference.assetId.toString(), goneId.toString());
        }
        else
        {
            ++fileMissing;
            CNA_EDITOR_EXPECT_EQ(reference.assetId.toString(), strandedId.toString());
        }
    }

    // The two reasons are genuinely different and both matter: an id deleted from the database is
    // invisible to AssetDatabase::getMissingAssets(), and a tracked asset whose file vanished is
    // invisible to a check that only looks the id up.
    CNA_EDITOR_EXPECT_EQ(notInDatabase, std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(fileMissing, std::size_t{1});

    // Grouped by id, because a broken reference is almost always one asset that many entities
    // point at, and the fix is the same for all of them.
    CNA_EDITOR_EXPECT_EQ(collectMissingAssetIds(missing).size(), std::size_t{2});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(MissingReferencesChecksComponentsWithNoDescriptor)
{
    // A component whose plugin failed to load keeps its data and must still have its references
    // checked -- that scene is the one most likely to be broken.
    AssetDatabase assets;
    SceneDocument scene;

    EditorEntity entity{Uuid::generate(), "Exotic"};
    EditorComponent unknown{"ThirdParty.Decal"};
    unknown.setProperty("atlas", PropertyValue{PropertyValue::AssetReference{Uuid::generate()}});
    entity.addComponent(std::move(unknown));
    scene.addEntity(std::move(entity));

    const std::vector<MissingReference> missing = findMissingReferences(scene, assets);
    CNA_EDITOR_EXPECT_EQ(missing.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(missing.front().componentTypeId, std::string{"ThirdParty.Decal"});
}

CNA_EDITOR_TEST(RelinkingRewritesEveryReferenceAsOneUndoEntry)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    SceneDocument scene;
    CommandHistory history;

    const Uuid goneId = Uuid::generate();
    const Uuid replacementId = Uuid::generate();

    std::vector<Uuid> sprites;
    for (int index = 0; index < 3; ++index)
    {
        EditorEntity entity{Uuid::generate(), "Sprite" + std::to_string(index)};
        EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
        sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
        sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{goneId}});
        entity.addComponent(std::move(sprite));
        sprites.push_back(scene.addEntity(std::move(entity)));
    }

    auto command = std::make_unique<RelinkAssetCommand>(scene, goneId, replacementId);
    CNA_EDITOR_EXPECT(command->isValid());
    CNA_EDITOR_EXPECT_EQ(command->getReferenceCount(), std::size_t{3});
    history.execute(std::move(command));

    const auto textureOf = [&](const Uuid& entityId) {
        return scene.findEntity(entityId)->findComponent(BuiltinComponentIds::kSpriteRenderer)
            ->getProperty("texture").get<PropertyValue::AssetReference>().id;
    };

    for (const Uuid& id : sprites) { CNA_EDITOR_EXPECT_EQ(textureOf(id).toString(), replacementId.toString()); }

    // One entry, not three. Undoing a relink of forty sprites must not be forty presses.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), std::size_t{1});
    CNA_EDITOR_EXPECT(history.undo());
    for (const Uuid& id : sprites) { CNA_EDITOR_EXPECT_EQ(textureOf(id).toString(), goneId.toString()); }

    // Redo rewrites the same set the undo restored, because the targets were found once at
    // construction. Re-scanning in execute() would find nothing the second time.
    CNA_EDITOR_EXPECT(history.redo());
    for (const Uuid& id : sprites) { CNA_EDITOR_EXPECT_EQ(textureOf(id).toString(), replacementId.toString()); }
}

CNA_EDITOR_TEST(RelinkingToNilClearsTheReferences)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    SceneDocument scene;
    const Uuid goneId = Uuid::generate();

    EditorEntity entity{Uuid::generate(), "Sprite"};
    EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{goneId}});
    entity.addComponent(std::move(sprite));
    const Uuid entityId = scene.addEntity(std::move(entity));

    RelinkAssetCommand clear{scene, goneId, Uuid{}};
    CNA_EDITOR_EXPECT(clear.isValid());
    clear.execute();

    // Clearing is the right answer when the asset is simply gone and nothing should replace it.
    CNA_EDITOR_EXPECT(!scene.findEntity(entityId)->findComponent(BuiltinComponentIds::kSpriteRenderer)
                           ->getProperty("texture").get<PropertyValue::AssetReference>().id.isValid());
}

CNA_EDITOR_TEST(RelinkingRefusesWhenNothingRefersToTheOldId)
{
    SceneDocument scene;
    RelinkAssetCommand nothing{scene, Uuid::generate(), Uuid::generate()};

    // A command that would do nothing must not reach the undo stack.
    CNA_EDITOR_EXPECT(!nothing.isValid());

    RelinkAssetCommand sameId{scene, Uuid{}, Uuid{}};
    CNA_EDITOR_EXPECT(!sameId.isValid());
}

CNA_EDITOR_TEST(TheTextureImporterDeclaresItsSettings)
{
    ComponentRegistry importers;
    registerBuiltinImporters(importers);

    const ComponentDescriptor* texture = importers.find(ImporterIds::kTexture);
    CNA_EDITOR_EXPECT(texture != nullptr);

    const auto property = [&](const std::string& name) -> const PropertyDescriptor* {
        for (const PropertyDescriptor& candidate : texture->properties)
        {
            if (candidate.name == name) { return &candidate; }
        }
        return nullptr;
    };

    // Premultiplied by default, because XNA's SpriteBatch blends that way -- an importer that
    // defaulted the other way would halo the edge of every sprite in a default project.
    CNA_EDITOR_EXPECT(property("premultiplyAlpha") != nullptr);
    CNA_EDITOR_EXPECT(property("premultiplyAlpha")->defaultValue.get<bool>());

    // Mipmaps off, because most 2D art is drawn at its native size and they cost a third more
    // memory for nothing.
    CNA_EDITOR_EXPECT(!property("generateMipmaps")->defaultValue.get<bool>());

    // The pixel size is a fact about the file, not a choice about it.
    CNA_EDITOR_EXPECT(property("pixelSize")->readOnly);

    CNA_EDITOR_EXPECT_EQ(property("wrapMode")->enumOptions.size(), std::size_t{3});
}

CNA_EDITOR_TEST(ImporterSettingsSurviveTheSidecarAndUndoBackToAbsent)
{
    const std::filesystem::path directory = makeScratchDirectory("importsettings");
    writeFile(directory / "Textures" / "Hero.png", "not really a png");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Textures");

    CNA_EDITOR_EXPECT_EQ(assets.getCount(), std::size_t{1});
    const Uuid assetId = assets.getAll().front()->id;
    CNA_EDITOR_EXPECT_EQ(assets.find(assetId)->importerId, std::string{ImporterIds::kTexture});

    CommandHistory history;
    auto command = std::make_unique<SetImporterSettingCommand>(assets, assetId, "generateMipmaps",
                                                               PropertyValue{true});
    CNA_EDITOR_EXPECT(command->isValid());
    history.execute(std::move(command));

    CNA_EDITOR_EXPECT(assets.find(assetId)->importerSettings["generateMipmaps"].asBoolean(false));

    // Written through, not merely held in memory: a setting that lived only in the session would
    // be lost on the next scan, and the user could not tell that from it having no effect.
    AssetDatabase reopened;
    reopened.setProjectRoot(directory.generic_string());
    reopened.scan("Textures");
    CNA_EDITOR_EXPECT(reopened.find(assetId) != nullptr);
    CNA_EDITOR_EXPECT(reopened.find(assetId)->importerSettings["generateMipmaps"].asBoolean(false));

    // Undo takes the field back out rather than writing a default in its place: a sidecar that
    // accumulated every field anyone glanced at would make its every diff noise.
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT(assets.find(assetId)->importerSettings["generateMipmaps"].isNull());

    AssetDatabase afterUndo;
    afterUndo.setProjectRoot(directory.generic_string());
    afterUndo.scan("Textures");
    CNA_EDITOR_EXPECT(afterUndo.find(assetId)->importerSettings["generateMipmaps"].isNull());

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(ImporterSettingEditsMergeIntoOneUndoEntry)
{
    const std::filesystem::path directory = makeScratchDirectory("importmerge");
    writeFile(directory / "Audio" / "Jump.wav", "not really a wav");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Audio");

    const Uuid assetId = assets.getAll().front()->id;
    CommandHistory history;

    for (const float volume : {0.9f, 0.8f, 0.7f, 0.6f})
    {
        auto command = std::make_unique<SetImporterSettingCommand>(assets, assetId, "importVolume",
                                                                   PropertyValue{volume});
        history.execute(std::move(command), MergePolicy::MergeWithPrevious);
    }

    // One entry for the drag, undoing to where it started -- the same policy the scene's own
    // property fields use.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), std::size_t{1});
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT(assets.find(assetId)->importerSettings["importVolume"].isNull());

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(JsonRemoveTakesAFieldBackOut)
{
    JsonValue object = JsonValue::makeObject();
    object.set("kept", JsonValue{1});
    object.set("dropped", JsonValue{2});

    CNA_EDITOR_EXPECT(object.remove("dropped"));
    CNA_EDITOR_EXPECT(object["dropped"].isNull());
    CNA_EDITOR_EXPECT_EQ(object["kept"].asInt(0), 1);

    // Removing what is not there is not an error, and removing from a non-object is not a crash.
    CNA_EDITOR_EXPECT(!object.remove("dropped"));
    JsonValue scalar{5};
    CNA_EDITOR_EXPECT(!scalar.remove("anything"));
}

CNA_EDITOR_TEST(ImageSizeIsReadFromThePngHeader)
{
    const std::filesystem::path directory = makeScratchDirectory("imagesize");

    // A 3x2 PNG header: the 8-byte signature, then the IHDR length and tag, then the dimensions
    // big-endian. Only the header matters -- nothing here decodes the image.
    std::string png;
    const unsigned char signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                       0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
                                       0x00, 0x00, 0x00, 0x03,
                                       0x00, 0x00, 0x00, 0x02,
                                       0x08, 0x06, 0x00, 0x00, 0x00};
    png.assign(reinterpret_cast<const char*>(signature), sizeof(signature));
    writeFile(directory / "Tiny.png", png);

    const std::optional<ImageSize> size = readImageSize((directory / "Tiny.png").generic_string());
    CNA_EDITOR_EXPECT(size.has_value());
    CNA_EDITOR_EXPECT_EQ(size->width, 3);
    CNA_EDITOR_EXPECT_EQ(size->height, 2);

    // JPEG: no fixed offset to read from. The size lives in a start-of-frame segment that sits
    // after a chain of others -- here an APP0 and a quantisation table -- each of which has to be
    // stepped over by its own length.
    static const unsigned char kJpeg[] = {
        0xFF, 0xD8,
        0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01, 0x01, 0x00,
        0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
        0xFF, 0xDB, 0x00, 0x04, 0x00, 0x00,
        // SOF0: precision 8, height 0x0040, width 0x0060 -- height first, which is the reverse of
        // PNG's order and the classic way to get this wrong.
        0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x40, 0x00, 0x60, 0x03,
        0x01, 0x11, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01};
    writeFile(directory / "Photo.jpg",
              std::string{reinterpret_cast<const char*>(kJpeg), sizeof(kJpeg)});

    const std::optional<ImageSize> jpeg = readImageSize((directory / "Photo.jpg").generic_string());
    CNA_EDITOR_EXPECT(jpeg.has_value());
    CNA_EDITOR_EXPECT_EQ(jpeg->width, 96);
    CNA_EDITOR_EXPECT_EQ(jpeg->height, 64);

    // A format the editor cannot measure is unknown, not zero -- the caller has to be able to tell
    // "I could not read this" from "this image is empty". A truncated JPEG is the same answer: the
    // walk runs off the end of the file rather than reading whatever happens to be there.
    writeFile(directory / "Broken.jpg", "\xFF\xD8\xFF\xE0 not really a jpeg but long enough to read");
    CNA_EDITOR_EXPECT(!readImageSize((directory / "Broken.jpg").generic_string()).has_value());
    CNA_EDITOR_EXPECT(!readImageSize((directory / "Absent.png").generic_string()).has_value());

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(ImporterFactsAreWrittenOnceAndNotRewrittenOnEveryScan)
{
    const std::filesystem::path directory = makeScratchDirectory("importerfacts");

    std::string png;
    const unsigned char signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                       0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
                                       0x00, 0x00, 0x00, 0x20,
                                       0x00, 0x00, 0x00, 0x10,
                                       0x08, 0x06, 0x00, 0x00, 0x00};
    png.assign(reinterpret_cast<const char*>(signature), sizeof(signature));
    writeFile(directory / "Textures" / "Hero.png", png);

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Textures");

    CNA_EDITOR_EXPECT_EQ(applyImporterFacts(assets), std::size_t{1});

    const Uuid assetId = assets.getAll().front()->id;
    const EditorVector2 size =
        PropertyValue::fromJson(assets.find(assetId)->importerSettings["pixelSize"], PropertyType::Vector2)
            .get<EditorVector2>();
    CNA_EDITOR_EXPECT_EQ(size.x, 32.0f);
    CNA_EDITOR_EXPECT_EQ(size.y, 16.0f);

    // Nothing changed, so nothing is rewritten. A scan that touched every sidecar on every open
    // would show up as a repository full of spurious diffs.
    CNA_EDITOR_EXPECT_EQ(applyImporterFacts(assets), std::size_t{0});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(TheWatcherNoticesAnExternalEditExactlyOnce)
{
    const std::filesystem::path directory = makeScratchDirectory("watchedit");
    writeFile(directory / "Textures" / "Hero.png", "first contents");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Textures");
    const Uuid assetId = assets.getAll().front()->id;

    AssetWatcher watcher;
    watcher.setInterval(1.0);

    // Nothing has changed and no interval has elapsed, so nothing is polled and nothing reported.
    CNA_EDITOR_EXPECT(!watcher.poll(assets, 0.1).polled);
    CNA_EDITOR_EXPECT(!watcher.poll(assets, 2.0).hasChanges());

    writeFile(directory / "Textures" / "Hero.png", "second contents, a different length entirely");

    const AssetWatchResult first = watcher.poll(assets, 2.0);
    CNA_EDITOR_EXPECT_EQ(first.changed.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(first.changed.front().toString(), assetId.toString());

    // Reported once. The stamp is updated as it is reported, so a console does not fill with the
    // same line twice a second until something else happens to fix it.
    CNA_EDITOR_EXPECT(!watcher.poll(assets, 2.0).hasChanges());

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(TheWatcherReportsDisappearanceAndReturnSeparately)
{
    const std::filesystem::path directory = makeScratchDirectory("watchgone");
    writeFile(directory / "Textures" / "Hero.png", "contents");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Textures");
    const Uuid assetId = assets.getAll().front()->id;

    AssetWatcher watcher;
    watcher.setInterval(0.0);

    std::filesystem::remove(directory / "Textures" / "Hero.png");

    const AssetWatchResult gone = watcher.poll(assets, 0.0);
    CNA_EDITOR_EXPECT_EQ(gone.removed.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(gone.removed.front().toString(), assetId.toString());
    CNA_EDITOR_EXPECT(gone.changed.empty());

    // Once, not on every poll for the rest of the session.
    CNA_EDITOR_EXPECT(!watcher.poll(assets, 0.0).hasChanges());

    writeFile(directory / "Textures" / "Hero.png", "contents are back");

    // A file returning is worth telling apart from one being edited: the first fixes a broken
    // reference, the second means reloading something already on screen.
    const AssetWatchResult back = watcher.poll(assets, 0.0);
    CNA_EDITOR_EXPECT_EQ(back.restored.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(back.restored.front().toString(), assetId.toString());
    CNA_EDITOR_EXPECT(back.changed.empty());
    CNA_EDITOR_EXPECT(!watcher.poll(assets, 0.0).hasChanges());

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(TheWatcherHonoursItsInterval)
{
    const std::filesystem::path directory = makeScratchDirectory("watchinterval");
    writeFile(directory / "Textures" / "Hero.png", "contents");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Textures");

    AssetWatcher watcher;
    watcher.setInterval(10.0);

    writeFile(directory / "Textures" / "Hero.png", "much longer contents than before");

    // A change that has happened is not reported until a poll is due: the whole point of the
    // interval is that a frame does not cost one stat call per asset.
    for (int frame = 0; frame < 5; ++frame)
    {
        CNA_EDITOR_EXPECT(!watcher.poll(assets, 1.0).polled);
    }

    // Unless something asks for one now, which is what a manual "Refresh" would do.
    watcher.requestImmediatePoll();
    CNA_EDITOR_EXPECT_EQ(watcher.poll(assets, 0.0).changed.size(), std::size_t{1});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(MovingAnAssetKeepsItsIdAndTakesItsSidecarAlong)
{
    const std::filesystem::path directory = makeScratchDirectory("assetmove");
    writeFile(directory / "Assets" / "Textures" / "Hero.png", "contents");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Assets");

    const Uuid assetId = assets.getAll().front()->id;
    CNA_EDITOR_EXPECT(std::filesystem::exists(directory / "Assets" / "Textures" / "Hero.png.cnaasset"));

    std::string error;
    CNA_EDITOR_EXPECT(assets.moveAsset(assetId, "Assets/Sprites/Player.png", &error));

    // The id is the identity, so nothing that references it needs to know anything moved (D-08).
    CNA_EDITOR_EXPECT(assets.find(assetId) != nullptr);
    CNA_EDITOR_EXPECT_EQ(assets.find(assetId)->sourcePath, std::string{"Assets/Sprites/Player.png"});
    CNA_EDITOR_EXPECT(assets.findByPath("Assets/Textures/Hero.png") == nullptr);
    CNA_EDITOR_EXPECT(assets.findByPath("Assets/Sprites/Player.png") != nullptr);

    // The sidecar travels with the file. An orphaned source file would be given a fresh id by the
    // next scan, silently breaking every reference to it.
    CNA_EDITOR_EXPECT(std::filesystem::exists(directory / "Assets" / "Sprites" / "Player.png"));
    CNA_EDITOR_EXPECT(std::filesystem::exists(directory / "Assets" / "Sprites" / "Player.png.cnaasset"));
    CNA_EDITOR_EXPECT(!std::filesystem::exists(directory / "Assets" / "Textures" / "Hero.png"));
    CNA_EDITOR_EXPECT(!std::filesystem::exists(directory / "Assets" / "Textures" / "Hero.png.cnaasset"));

    // Rescanning finds the same asset with the same id, which is the proof the move was complete.
    AssetDatabase reopened;
    reopened.setProjectRoot(directory.generic_string());
    const AssetScanResult rescan = reopened.scan("Assets");
    CNA_EDITOR_EXPECT_EQ(reopened.getCount(), std::size_t{1});
    CNA_EDITOR_EXPECT(reopened.find(assetId) != nullptr);
    CNA_EDITOR_EXPECT_EQ(rescan.newCount, std::size_t{0});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(MovingAnAssetDoesNotTouchAnySceneFile)
{
    const std::filesystem::path directory = makeScratchDirectory("assetmovescene");
    writeFile(directory / "Assets" / "Textures" / "Hero.png", "contents");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Assets");
    const Uuid assetId = assets.getAll().front()->id;

    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    SceneDocument scene;
    EditorEntity entity{Uuid::generate(), "Player"};
    EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{assetId}});
    entity.addComponent(std::move(sprite));
    scene.addEntity(std::move(entity));

    const std::filesystem::path scenePath = directory / "Scenes" / "Level01.cnascene";
    std::string errorMessage;
    CNA_EDITOR_EXPECT(scene.saveToFile(scenePath.generic_string(), &errorMessage));

    std::ifstream before{scenePath, std::ios::binary};
    const std::string beforeContents{std::istreambuf_iterator<char>{before},
                                     std::istreambuf_iterator<char>{}};

    CNA_EDITOR_EXPECT(assets.moveAsset(assetId, "Assets/Sprites/Player.png"));

    std::ifstream after{scenePath, std::ios::binary};
    const std::string afterContents{std::istreambuf_iterator<char>{after},
                                    std::istreambuf_iterator<char>{}};

    // Byte for byte. An editor that rewrote every scene on a rename would turn tidying an asset
    // folder into a review of the whole project.
    CNA_EDITOR_EXPECT_EQ(afterContents, beforeContents);

    // And the reference still resolves, because it was never a path in the first place.
    CNA_EDITOR_EXPECT(findMissingReferences(scene, assets).empty());

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(MovingAnAssetRefusesRatherThanOverwriting)
{
    const std::filesystem::path directory = makeScratchDirectory("assetmoverefuse");
    writeFile(directory / "Assets" / "A.png", "first");
    writeFile(directory / "Assets" / "B.png", "second");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Assets");
    CNA_EDITOR_EXPECT_EQ(assets.getCount(), std::size_t{2});

    const Uuid firstId = assets.findByPath("Assets/A.png")->id;

    std::string error;
    CNA_EDITOR_EXPECT(!assets.moveAsset(firstId, "Assets/B.png", &error));
    CNA_EDITOR_EXPECT(!error.empty());

    // Both files are still there, with their contents intact. Silently overwriting one asset with
    // another is the one outcome a move must never have.
    CNA_EDITOR_EXPECT(std::filesystem::exists(directory / "Assets" / "A.png"));
    CNA_EDITOR_EXPECT(std::filesystem::exists(directory / "Assets" / "B.png"));
    CNA_EDITOR_EXPECT_EQ(assets.find(firstId)->sourcePath, std::string{"Assets/A.png"});

    // Nor may a destination climb out of the project: the sidecar's relative path would stop
    // meaning anything.
    CNA_EDITOR_EXPECT(!assets.moveAsset(firstId, "../Escaped.png", &error));
    CNA_EDITOR_EXPECT(!assets.moveAsset(firstId, "", &error));

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(MovingAnAssetIsUndoable)
{
    const std::filesystem::path directory = makeScratchDirectory("assetmoveundo");
    writeFile(directory / "Assets" / "Textures" / "Hero.png", "contents");

    AssetDatabase assets;
    assets.setProjectRoot(directory.generic_string());
    assets.scan("Assets");
    const Uuid assetId = assets.getAll().front()->id;

    CommandHistory history;
    auto command = std::make_unique<MoveAssetCommand>(assets, assetId, "Assets/Sprites/Player.png");
    CNA_EDITOR_EXPECT(command->isValid());
    history.execute(std::move(command));

    CNA_EDITOR_EXPECT_EQ(assets.find(assetId)->sourcePath, std::string{"Assets/Sprites/Player.png"});

    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(assets.find(assetId)->sourcePath, std::string{"Assets/Textures/Hero.png"});
    CNA_EDITOR_EXPECT(std::filesystem::exists(directory / "Assets" / "Textures" / "Hero.png"));

    CNA_EDITOR_EXPECT(history.redo());
    CNA_EDITOR_EXPECT_EQ(assets.find(assetId)->sourcePath, std::string{"Assets/Sprites/Player.png"});

    std::filesystem::remove_all(directory);
}

namespace
{
    /** @brief Registers @p paths as tracked assets, without touching the filesystem. */
    void trackPaths(AssetDatabase& assets, const std::vector<std::string>& paths)
    {
        for (const std::string& path : paths)
        {
            AssetRecord record;
            record.id = Uuid::generate();
            record.sourcePath = path;
            record.type = AssetDatabase::guessTypeFromExtension(path);
            assets.add(std::move(record));
        }
    }
}

CNA_EDITOR_TEST(TheAssetTreeIsDerivedFromThePathsAndOrdered)
{
    AssetDatabase assets;
    trackPaths(assets, {"Assets/Textures/hero.png", "Assets/Textures/enemy.png",
                        "Assets/Audio/jump.wav", "Assets/Textures/ui/button.png",
                        "readme.txt"});

    const AssetFolder root = buildAssetTree(assets);

    CNA_EDITOR_EXPECT_EQ(root.getTotalAssetCount(), std::size_t{5});

    // A file with no directory part sits at the root rather than inventing a folder for itself.
    CNA_EDITOR_EXPECT_EQ(root.assets.size(), std::size_t{1});

    CNA_EDITOR_EXPECT_EQ(root.folders.size(), std::size_t{1});
    const AssetFolder& assetsFolder = root.folders.front();
    CNA_EDITOR_EXPECT_EQ(assetsFolder.name, std::string{"Assets"});
    CNA_EDITOR_EXPECT_EQ(assetsFolder.path, std::string{"Assets"});

    // Ordered by name, so the tree reads the same every frame.
    CNA_EDITOR_EXPECT_EQ(assetsFolder.folders.size(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(assetsFolder.folders[0].name, std::string{"Audio"});
    CNA_EDITOR_EXPECT_EQ(assetsFolder.folders[1].name, std::string{"Textures"});

    const AssetFolder& textures = assetsFolder.folders[1];
    CNA_EDITOR_EXPECT_EQ(textures.assets.size(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(textures.getTotalAssetCount(), std::size_t{3});
    CNA_EDITOR_EXPECT_EQ(textures.folders.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(textures.folders.front().path, std::string{"Assets/Textures/ui"});
}

CNA_EDITOR_TEST(TheAssetFilterIsCaseInsensitiveAndLeavesNoEmptyFolders)
{
    AssetDatabase assets;
    trackPaths(assets, {"Assets/Textures/hero.png", "Assets/Textures/enemy.png",
                        "Assets/Audio/jump.wav"});

    // Part of a file name finds it wherever it lives.
    const AssetFolder byName = buildAssetTree(assets, "HERO");
    CNA_EDITOR_EXPECT_EQ(byName.getTotalAssetCount(), std::size_t{1});

    // A filtered tree contains no empty folders: one that told the user nothing about where the
    // match is would be worse than no tree at all.
    CNA_EDITOR_EXPECT_EQ(byName.folders.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(byName.folders.front().folders.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(byName.folders.front().folders.front().name, std::string{"Textures"});

    // A folder name keeps everything under it, because the test is against the whole path.
    CNA_EDITOR_EXPECT_EQ(buildAssetTree(assets, "audio").getTotalAssetCount(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(buildAssetTree(assets, "textures").getTotalAssetCount(), std::size_t{2});

    // Nothing matching yields an empty tree rather than the whole thing.
    CNA_EDITOR_EXPECT(buildAssetTree(assets, "nothing here").isEmpty());
    CNA_EDITOR_EXPECT_EQ(buildAssetTree(assets, "").getTotalAssetCount(), std::size_t{3});
}

CNA_EDITOR_TEST(AssetPathHelpersSplitAndJoinConsistently)
{
    CNA_EDITOR_EXPECT_EQ(assetFileName("Assets/Textures/hero.png"), std::string{"hero.png"});
    CNA_EDITOR_EXPECT_EQ(assetDirectory("Assets/Textures/hero.png"), std::string{"Assets/Textures"});

    // A bare file name has no directory part, and joining must not invent one -- a leading slash
    // would look absolute and resolve somewhere else entirely.
    CNA_EDITOR_EXPECT_EQ(assetFileName("readme.txt"), std::string{"readme.txt"});
    CNA_EDITOR_EXPECT_EQ(assetDirectory("readme.txt"), std::string{});
    CNA_EDITOR_EXPECT_EQ(joinAssetPath("", "readme.txt"), std::string{"readme.txt"});

    CNA_EDITOR_EXPECT_EQ(joinAssetPath(assetDirectory("Assets/Textures/hero.png"), "villain.png"),
                         std::string{"Assets/Textures/villain.png"});
}

// --------------------------------------------------------------------------------------------
// Crash-recovery snapshots (plan.md ED-903)
// --------------------------------------------------------------------------------------------

namespace
{
    /** @brief Builds a snapshot carrying a one-entity scene. */
    RecoverySnapshot makeSnapshot(const std::string& projectPath,
                                  const std::string& sceneName,
                                  std::int64_t savedAt)
    {
        RecoverySnapshot snapshot;
        snapshot.projectPath = projectPath;
        snapshot.scenePath = projectPath + ".scene";
        snapshot.sceneName = sceneName;
        snapshot.sceneId = Uuid::generate();
        snapshot.savedAtSeconds = savedAt;

        snapshot.scene = JsonValue::makeObject();
        snapshot.scene.set("formatVersion", JsonValue{1});
        snapshot.scene.set("name", JsonValue{sceneName});
        snapshot.scene.set("entities", JsonValue::makeArray());
        return snapshot;
    }
}

CNA_EDITOR_TEST(ARecoverySnapshotRoundTripsAndIsFoundByItsProject)
{
    const std::filesystem::path directory = makeScratchDirectory("recovery");
    const RecoveryStore store{directory.generic_string()};

    const RecoverySnapshot written = makeSnapshot("/games/Alpha.cnaproject", "Level01", 1700000000);
    std::string errorMessage;
    CNA_EDITOR_EXPECT(store.write(written, &errorMessage));
    CNA_EDITOR_EXPECT(errorMessage.empty());

    const std::optional<RecoverySnapshot> found = store.findForProject("/games/Alpha.cnaproject");
    CNA_EDITOR_EXPECT(found.has_value());
    CNA_EDITOR_EXPECT_EQ(found->sceneName, std::string{"Level01"});
    CNA_EDITOR_EXPECT(found->sceneId == written.sceneId);
    CNA_EDITOR_EXPECT_EQ(found->savedAtSeconds, std::int64_t{1700000000});
    CNA_EDITOR_EXPECT_EQ(found->scene["name"].asString(), std::string{"Level01"});

    // Another project's snapshot must not be offered: recovering the wrong game's work would be
    // a worse outcome than recovering nothing.
    CNA_EDITOR_EXPECT(!store.findForProject("/games/Beta.cnaproject").has_value());

    CNA_EDITOR_EXPECT(store.discard(written.sceneId));
    CNA_EDITOR_EXPECT(!store.findForProject("/games/Alpha.cnaproject").has_value());
    CNA_EDITOR_EXPECT(!store.discard(written.sceneId));

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(TheNewestSnapshotWinsAndACorruptOneIsSkipped)
{
    const std::filesystem::path directory = makeScratchDirectory("recoverynewest");
    const RecoveryStore store{directory.generic_string()};

    CNA_EDITOR_EXPECT(store.write(makeSnapshot("/games/Alpha.cnaproject", "Older", 1000)));
    CNA_EDITOR_EXPECT(store.write(makeSnapshot("/games/Alpha.cnaproject", "Newer", 2000)));

    const std::optional<RecoverySnapshot> found = store.findForProject("/games/Alpha.cnaproject");
    CNA_EDITOR_EXPECT(found.has_value());
    CNA_EDITOR_EXPECT_EQ(found->sceneName, std::string{"Newer"});

    // A truncated file is skipped rather than hiding the readable ones. Recovery is a best-effort
    // path by construction, and one bad file must not cost the others.
    writeFile(directory / (Uuid::generate().toString() + ".cnarecovery"), "{\"formatVersion\":1,");
    CNA_EDITOR_EXPECT_EQ(store.list().size(), std::size_t{2});

    // So is one written by a build that knows more than this one does.
    JsonValue future = JsonValue::makeObject();
    future.set("formatVersion", JsonValue{RecoveryStore::kFormatVersion + 1});
    future.set("projectPath", JsonValue{"/games/Alpha.cnaproject"});
    future.set("sceneId", JsonValue{Uuid::generate().toString()});
    future.set("scene", JsonValue::makeObject());
    writeFile(directory / (Uuid::generate().toString() + ".cnarecovery"), Json::write(future));
    CNA_EDITOR_EXPECT_EQ(store.list().size(), std::size_t{2});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(AFailedSnapshotIsReportedRatherThanThrown)
{
    // No directory at all: an editor that died because it could not autosave would have caused
    // exactly the loss it was installed to prevent.
    const RecoveryStore store{""};

    std::string errorMessage;
    CNA_EDITOR_EXPECT(!store.write(makeSnapshot("/games/Alpha.cnaproject", "Level01", 1), &errorMessage));
    CNA_EDITOR_EXPECT(!errorMessage.empty());
    CNA_EDITOR_EXPECT(store.list().empty());
    CNA_EDITOR_EXPECT(!store.findForProject("/games/Alpha.cnaproject").has_value());
}

CNA_EDITOR_TEST(TheDefaultRecoveryDirectoryIsUnderTheUsersState)
{
    const std::string directory = getDefaultRecoveryDirectory();

    // Never empty on any platform the editor builds for, and never inside a project: an autosave
    // of unsaved work is not part of the game being edited.
    CNA_EDITOR_EXPECT(!directory.empty());
    CNA_EDITOR_EXPECT(directory.find("cna-editor") != std::string::npos);
    CNA_EDITOR_EXPECT(directory.find("recovery") != std::string::npos);
}

CNA_EDITOR_TEST(ASidecarThisBuildCannotReadKeepsItsIdAndIsLeftAlone)
{
    const std::filesystem::path directory = makeScratchDirectory("sidecarversion");
    writeFile(directory / "Assets" / "hero.png", "hero");

    // A sidecar from a build that knows more than this one. The id is the one thing a scene
    // references (D-08), so it must survive; the rest may not be safe to read.
    const Uuid pinned = Uuid::generate();
    JsonValue sidecar = JsonValue::makeObject();
    sidecar.set("formatVersion", JsonValue{AssetDatabase::kFormatVersion + 1});
    sidecar.set("id", JsonValue{pinned.toString()});
    sidecar.set("type", JsonValue{"Texture2D"});
    sidecar.set("importer", JsonValue{"Future.Importer"});
    const std::string original = Json::write(sidecar);
    writeFile(directory / "Assets" / "hero.png.cnaasset", original);

    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    const AssetScanResult result = database.scan("Assets");

    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.newCount, std::size_t{0});

    const AssetRecord* record = database.find(pinned);
    CNA_EDITOR_EXPECT(record != nullptr);
    CNA_EDITOR_EXPECT_EQ(record->sourcePath, std::string{"Assets/hero.png"});

    // The unreadable importer was not adopted, and the user was told why.
    CNA_EDITOR_EXPECT(record->importerId != std::string{"Future.Importer"});

    bool warned = false;
    for (const std::string& warning : result.warnings)
    {
        if (warning.find("newer than this build supports") != std::string::npos
            && warning.find("its id was kept") != std::string::npos)
        {
            warned = true;
        }
    }
    CNA_EDITOR_EXPECT(warned);

    // And the file on disk is untouched, so a build that understands it still can.
    std::ifstream stream{directory / "Assets" / "hero.png.cnaasset", std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    CNA_EDITOR_EXPECT_EQ(buffer.str(), original);

    std::filesystem::remove_all(directory);
}

// --------------------------------------------------------------------------------------------
// Layers (plan.md ED-305)
// --------------------------------------------------------------------------------------------

CNA_EDITOR_TEST(AProjectCarriesItsLayersAndNeverHasNone)
{
    Project project = Project::createDefault("Layered", "/tmp/layered");
    CNA_EDITOR_EXPECT_EQ(project.getLayers().size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(project.getLayers().front(), std::string{Project::kDefaultLayer});

    project.setLayers({"Background", "Default", "Foreground"});
    CNA_EDITOR_EXPECT_EQ(project.getLayers().size(), std::size_t{3});

    // The order is the meaning -- index 0 draws first -- so it has to survive a round trip exactly.
    Project reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(project.toJson()).succeeded);
    CNA_EDITOR_EXPECT_EQ(reloaded.getLayers().front(), std::string{"Background"});
    CNA_EDITOR_EXPECT_EQ(reloaded.getLayers().back(), std::string{"Foreground"});

    // An empty list is refused rather than accepted and repaired, so a caller that computed one
    // finds out here instead of later.
    project.setLayers({});
    CNA_EDITOR_EXPECT_EQ(project.getLayers().size(), std::size_t{3});
}

CNA_EDITOR_TEST(AProjectFileWrittenBeforeLayersExistedStillOpens)
{
    // The additive-field promise, tested rather than asserted in a comment: no formatVersion was
    // bumped for layers, so a file from before them has to load and get the default.
    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{Project::kFormatVersion});
    json.set("name", JsonValue{"Older"});
    json.set("kind", JsonValue{"CnaNative"});

    Project project;
    CNA_EDITOR_EXPECT(project.loadFromJson(json).succeeded);
    CNA_EDITOR_EXPECT_EQ(project.getLayers().size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(project.getLayers().front(), std::string{Project::kDefaultLayer});

    // As does one hand-edited down to nothing, or to blanks.
    JsonValue blanks = JsonValue::makeArray();
    blanks.append(JsonValue{""});
    json.set("layers", std::move(blanks));
    CNA_EDITOR_EXPECT(project.loadFromJson(json).succeeded);
    CNA_EDITOR_EXPECT_EQ(project.getLayers().front(), std::string{Project::kDefaultLayer});
}

CNA_EDITOR_TEST(TheDefaultLayerNameMatchesTheProjects)
{
    // Two constants, deliberately: cna-editor-scene links cna-editor-core and nothing else, so
    // reaching into the project module for one string would trade a duplicated literal for a
    // dependency the build graph is meant to forbid. This is what keeps them honest.
    CNA_EDITOR_EXPECT_EQ(std::string{kDefaultLayerName}, std::string{Project::kDefaultLayer});
}

CNA_EDITOR_TEST(TheLayerComponentOffersWhateverTheProjectDeclares)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ComponentDescriptor* descriptor = registry.find(BuiltinComponentIds::kLayer);
    CNA_EDITOR_EXPECT(descriptor != nullptr);
    CNA_EDITOR_EXPECT_EQ(descriptor->findProperty("layer")->enumOptions.size(), std::size_t{1});

    applyProjectLayers(registry, {"Background", "Default", "Foreground"});

    descriptor = registry.find(BuiltinComponentIds::kLayer);
    CNA_EDITOR_EXPECT(descriptor != nullptr);
    const PropertyDescriptor* layer = descriptor->findProperty("layer");
    CNA_EDITOR_EXPECT(layer != nullptr);
    CNA_EDITOR_EXPECT_EQ(layer->enumOptions.size(), std::size_t{3});
    CNA_EDITOR_EXPECT_EQ(layer->enumOptions.front(), std::string{"Background"});

    // An empty list is ignored: a component whose enum has no options is one nothing can be set
    // to, and leaving the previous list in place is the more useful failure.
    applyProjectLayers(registry, {});
    CNA_EDITOR_EXPECT_EQ(registry.find(BuiltinComponentIds::kLayer)->findProperty("layer")->enumOptions.size(),
                         std::size_t{3});
}

// --------------------------------------------------------------------------------------------
// Sprite fonts (plan.md ED-302)
// --------------------------------------------------------------------------------------------

CNA_EDITOR_TEST(ASpriteFontDescribesItselfAndTheEditorReportsIt)
{
    const std::filesystem::path directory = makeScratchDirectory("spritefont");
    writeFile(directory / "Assets" / "Menu.spritefont",
              R"(<?xml version="1.0" encoding="utf-8"?>)"
              R"(<XnaContent xmlns:Graphics="Microsoft.Xna.Framework.Content.Pipeline.Graphics">)"
              R"(<Asset Type="Graphics:FontDescription">)"
              R"(<FontName>Segoe UI</FontName>)"
              R"(<Size>14</Size>)"
              R"(<Spacing>2</Spacing>)"
              R"(<UseKerning>false</UseKerning>)"
              R"(<CharacterRegions><CharacterRegion>)"
              R"(<Start>&#32;</Start><End>&#126;</End>)"
              R"(</CharacterRegion></CharacterRegions>)"
              R"(</Asset></XnaContent>)");

    const std::optional<SpriteFontDescription> description =
        readSpriteFontDescription((directory / "Assets" / "Menu.spritefont").generic_string());
    CNA_EDITOR_EXPECT(description.has_value());
    if (!description) { return; }

    CNA_EDITOR_EXPECT_EQ(description->fontName, std::string{"Segoe UI"});
    CNA_EDITOR_EXPECT_EQ(description->pointSize, 14.0f);
    CNA_EDITOR_EXPECT_EQ(description->spacing, 2.0f);
    CNA_EDITOR_EXPECT(!description->useKerning);

    // The entity form is the common one, because the usual region starts at a space -- and a space
    // written literally between two tags is exactly what whitespace trimming would eat.
    CNA_EDITOR_EXPECT_EQ(description->firstCharacter, 32);
    CNA_EDITOR_EXPECT_EQ(description->lastCharacter, 126);

    // Scanning writes the facts into the sidecar, where the inspector shows them read-only.
    AssetDatabase database;
    database.setProjectRoot(directory.generic_string());
    CNA_EDITOR_EXPECT(database.scan("Assets").succeeded);
    CNA_EDITOR_EXPECT_EQ(applyImporterFacts(database), std::size_t{1});

    const AssetRecord* record = database.findByPath("Assets/Menu.spritefont");
    CNA_EDITOR_EXPECT(record != nullptr);
    if (record == nullptr) { return; }

    CNA_EDITOR_EXPECT(record->type == AssetType::SpriteFont);
    CNA_EDITOR_EXPECT_EQ(record->importerSettings["fontName"].asString(), std::string{"Segoe UI"});
    CNA_EDITOR_EXPECT_EQ(record->importerSettings["characterRange"].asString(), std::string{"32-126"});

    // Nothing changed the second time, so opening a project twice produces no diff -- the rule that
    // keeps --headless safe to run against a repository you want left alone.
    CNA_EDITOR_EXPECT_EQ(applyImporterFacts(database), std::size_t{0});

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(AnXmlFileThatIsNotASpriteFontIsNotReadAsOne)
{
    const std::filesystem::path directory = makeScratchDirectory("notaspritefont");

    // Without a structural check, any XML with a <Size> element would be read as a sprite font and
    // the inspector would report confident nonsense about it.
    writeFile(directory / "config.xml", "<Settings><Size>14</Size></Settings>");
    CNA_EDITOR_EXPECT(!readSpriteFontDescription((directory / "config.xml").generic_string()).has_value());
    CNA_EDITOR_EXPECT(!readSpriteFontDescription((directory / "absent.spritefont").generic_string()).has_value());

    // A real one missing half its fields is read for what it does say rather than refused: an
    // asset the editor cannot fully describe is still one it must not hide.
    writeFile(directory / "Bare.spritefont",
              R"(<Asset Type="Graphics:SpriteFontDescription"><FontName>Arial</FontName></Asset>)");
    const std::optional<SpriteFontDescription> bare =
        readSpriteFontDescription((directory / "Bare.spritefont").generic_string());
    CNA_EDITOR_EXPECT(bare.has_value());
    if (bare)
    {
        CNA_EDITOR_EXPECT_EQ(bare->fontName, std::string{"Arial"});
        CNA_EDITOR_EXPECT_EQ(bare->pointSize, 0.0f);
        CNA_EDITOR_EXPECT(bare->useKerning);
    }

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(TheSpriteFontImporterOffersFactsAndNoSettings)
{
    ComponentRegistry registry;
    registerBuiltinImporters(registry);

    const ComponentDescriptor* descriptor = registry.find(ImporterIds::kSpriteFont);
    CNA_EDITOR_EXPECT(descriptor != nullptr);
    if (descriptor == nullptr) { return; }

    // Every field read-only, and that is the design. A .spritefont is the content pipeline's own
    // input; an editable copy of its fields in the sidecar would be a second answer to a question
    // the build asks the file.
    CNA_EDITOR_EXPECT(!descriptor->properties.empty());
    for (const PropertyDescriptor& property : descriptor->properties)
    {
        CNA_EDITOR_EXPECT(property.readOnly);
    }
    CNA_EDITOR_EXPECT(descriptor->findProperty("fontName") != nullptr);
    CNA_EDITOR_EXPECT(descriptor->findProperty("characterRange") != nullptr);
}

// --------------------------------------------------------------------------------------------
// Building (plan.md ED-308)
// --------------------------------------------------------------------------------------------

CNA_EDITOR_TEST(ABuildPlanIsTwoCommandsWithTheOptionsTheEditorActuallyKnows)
{
    const std::filesystem::path directory = makeScratchDirectory("buildplan");
    writeFile(directory / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");

    BuildRequest request;
    request.projectRoot = directory.generic_string();
    request.targetPlatform = "linux-x64";
    request.graphicsBackend = "EASYGL";
    request.configuration = "RelWithDebInfo";
    request.cmakePath = findCMake();

    // Skipped rather than failed when there is no cmake: the plan is about *which* options are
    // passed, and asserting that on a machine without a toolchain would be testing the machine.
    if (request.cmakePath.empty())
    {
        CNA_EDITOR_EXPECT(!describeBuildProblem(request).empty());
        std::filesystem::remove_all(directory);
        return;
    }

    request.buildDirectory = getDefaultBuildDirectory(request);
    CNA_EDITOR_EXPECT(describeBuildProblem(request).empty());

    // Beside the project and keyed by platform: a build people iterate on has to be incremental,
    // and two platforms must not overwrite each other.
    CNA_EDITOR_EXPECT(request.buildDirectory.find("/build/linux-x64") != std::string::npos);

    const std::vector<BuildStep> steps = planBuild(request);
    CNA_EDITOR_EXPECT_EQ(steps.size(), std::size_t{2});
    if (steps.size() != 2) { return; }

    const std::string configure = steps.front().toCommandLine();
    CNA_EDITOR_EXPECT(configure.find("-S " + request.projectRoot) != std::string::npos);
    CNA_EDITOR_EXPECT(configure.find("-DCMAKE_BUILD_TYPE=RelWithDebInfo") != std::string::npos);

    // The one option the editor genuinely knows about. Passing more would be guessing at somebody
    // else's CMakeLists.
    CNA_EDITOR_EXPECT(configure.find("-DCNA_GRAPHICS_BACKEND=EASYGL") != std::string::npos);

    // --config as well as CMAKE_BUILD_TYPE: single-config generators read the first and
    // multi-config ones read the second, and a build that produced a Debug binary on one
    // developer's machine and a Release one on another's is the bug this avoids.
    const std::string build = steps.back().toCommandLine();
    CNA_EDITOR_EXPECT(build.find("--build") != std::string::npos);
    CNA_EDITOR_EXPECT(build.find("--config RelWithDebInfo") != std::string::npos);
    CNA_EDITOR_EXPECT(build.find("--parallel") != std::string::npos);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(ABuildSaysWhyItCannotRunBeforeItIsOffered)
{
    BuildRequest empty;
    CNA_EDITOR_EXPECT_EQ(describeBuildProblem(empty), std::string{"no project is open"});
    CNA_EDITOR_EXPECT(planBuild(empty).empty());

    BuildRequest missing;
    missing.projectRoot = "/definitely/not/a/directory";
    CNA_EDITOR_EXPECT(describeBuildProblem(missing).find("does not exist") != std::string::npos);

    // A project directory with no CMakeLists is the case CMake reports worst: a wall of text about
    // a missing file that says nothing about what the user should do.
    const std::filesystem::path directory = makeScratchDirectory("buildproblem");
    BuildRequest noCMakeLists;
    noCMakeLists.projectRoot = directory.generic_string();
    CNA_EDITOR_EXPECT(describeBuildProblem(noCMakeLists).find("no CMakeLists.txt") != std::string::npos);

    // And a cmake that is not there is named plainly rather than left to fail on exec.
    writeFile(directory / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
    BuildRequest badCMake;
    badCMake.projectRoot = directory.generic_string();
    badCMake.cmakePath = (directory / "not-cmake").generic_string();
    CNA_EDITOR_EXPECT(describeBuildProblem(badCMake).find("is not an executable file")
                      != std::string::npos);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(ABuildRunsItsStepsAndReportsTheOutcome)
{
    const std::filesystem::path directory = makeScratchDirectory("buildrun");

    // A CMakeLists that configures and builds nothing. Enough to prove the editor drives cmake,
    // reaps it, advances to the second step and reports success -- without needing a compiler.
    writeFile(directory / "CMakeLists.txt",
              "cmake_minimum_required(VERSION 3.20)\n"
              "project(BuildRunnerProbe NONE)\n");

    BuildRequest request;
    request.projectRoot = directory.generic_string();
    request.targetPlatform = "probe";
    request.configuration = "Release";
    request.cmakePath = findCMake();
    request.buildDirectory = getDefaultBuildDirectory(request);

    if (request.cmakePath.empty())
    {
        std::filesystem::remove_all(directory);
        return;
    }

    BuildProcess process;
    std::string errorMessage;
    CNA_EDITOR_EXPECT(process.start(request, &errorMessage));
    CNA_EDITOR_EXPECT(errorMessage.empty());
    CNA_EDITOR_EXPECT(process.getState() == BuildState::Running);

    // A build must never block the editor, so poll() is called until it finishes rather than
    // waited on. Bounded so a hung cmake fails the test instead of hanging it.
    for (int attempt = 0; attempt < 2000 && process.getState() == BuildState::Running; ++attempt)
    {
        process.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CNA_EDITOR_EXPECT(process.getState() == BuildState::Succeeded);
    CNA_EDITOR_EXPECT_EQ(process.getStepNumber(), std::size_t{2});
    CNA_EDITOR_EXPECT(std::filesystem::exists(process.getLogPath()));

    // The log opens with the commands that were run, so a build that failed an hour ago is still
    // explainable from the file alone.
    const std::vector<std::string> tail = process.readLogTail(200);
    CNA_EDITOR_EXPECT(!tail.empty());

    bool sawHeader = false;
    for (const std::string& line : tail)
    {
        if (line.find("cna-editor build: probe, Release") != std::string::npos) { sawHeader = true; }
    }
    CNA_EDITOR_EXPECT(sawHeader);

    // Starting a second build over a finished one is allowed; over a running one is not.
    CNA_EDITOR_EXPECT(process.start(request, &errorMessage));
    CNA_EDITOR_EXPECT(!process.start(request, &errorMessage));
    CNA_EDITOR_EXPECT_EQ(errorMessage, std::string{"a build is already running"});

    process.cancel();
    CNA_EDITOR_EXPECT(process.getState() == BuildState::Failed);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(TheGridSnapIsAnAdditiveFieldThatOlderProjectsSimplyLack)
{
    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{Project::kFormatVersion});
    json.set("name", JsonValue{"Older"});
    json.set("kind", JsonValue{"CnaNative"});

    // The same additive-field promise layers were held to, and no formatVersion bump here either.
    // Zero is not "no snapping" -- Ctrl turns snapping on -- it is "use the grid the viewport is
    // drawing", which is exactly what the editor did before the setting existed.
    Project project;
    CNA_EDITOR_EXPECT(project.loadFromJson(json).succeeded);
    CNA_EDITOR_EXPECT_EQ(project.getGridSnap(), 0.0f);

    // A project that never sets one must not start writing the field, or the first save of every
    // existing project would be a diff that says nothing.
    CNA_EDITOR_EXPECT(!project.toJson().contains("gridSnap"));

    project.setGridSnap(16.0f);
    CNA_EDITOR_EXPECT_EQ(project.getGridSnap(), 16.0f);

    Project reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(project.toJson()).succeeded);
    CNA_EDITOR_EXPECT_EQ(reloaded.getGridSnap(), 16.0f);

    // Negative is refused rather than clamped: it is not a smaller step, it is a value with no
    // meaning, and rounding to it would land an entity where nothing else agrees it is.
    reloaded.setGridSnap(-4.0f);
    CNA_EDITOR_EXPECT_EQ(reloaded.getGridSnap(), 16.0f);

    // Zero puts it back, and takes the field back out of the file with it.
    reloaded.setGridSnap(0.0f);
    CNA_EDITOR_EXPECT(!reloaded.toJson().contains("gridSnap"));
}

CNA_EDITOR_TEST(SettingTheGridSnapUndoesAndReachesTheFile)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("cna-snap-" + Uuid::generate().toString());
    std::filesystem::create_directories(root);

    Project project = Project::createDefault("Snappy", root.generic_string());
    CNA_EDITOR_EXPECT(project.saveToFile((root / "Snappy.cnaproject").generic_string()));

    SetProjectGridSnapCommand command{project, 16.0f};
    CNA_EDITOR_EXPECT(command.isValid());
    command.execute();
    CNA_EDITOR_EXPECT_EQ(project.getGridSnap(), 16.0f);
    CNA_EDITOR_EXPECT(command.wasSavedToDisk());

    // Written through rather than held in memory: the recovery snapshot holds the scene, not the
    // project, so a project change that never reached disk is one a crash loses entirely.
    Project fromDisk;
    CNA_EDITOR_EXPECT(fromDisk.loadFromFile((root / "Snappy.cnaproject").generic_string()).succeeded);
    CNA_EDITOR_EXPECT_EQ(fromDisk.getGridSnap(), 16.0f);

    command.undo();
    CNA_EDITOR_EXPECT_EQ(project.getGridSnap(), 0.0f);

    // Setting what is already set is not an edit. An undo entry that restores the state it is
    // already in reads to a user as a broken Ctrl+Z.
    SetProjectGridSnapCommand unchanged{project, 0.0f};
    CNA_EDITOR_EXPECT(!unchanged.isValid());

    SetProjectGridSnapCommand negative{project, -1.0f};
    CNA_EDITOR_EXPECT(!negative.isValid());

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

/**
 * @brief ED-403: a `.cnamaterial` round-trips, and converts to what the model pass already draws.
 *
 * The conversion is the half worth checking. A material asset stores metallic-roughness and the
 * renderer may be drawing through `BasicEffect` (gap G-05), so `toMeshMaterial` derives the
 * Blinn-Phong pair rather than storing a second copy that could disagree with the first. A metal
 * reflects its own colour and a dielectric reflects white; that is the one line of the PBR model
 * that survives the trip meaning what it meant, and it is what this asserts.
 */
CNA_EDITOR_TEST(AMaterialAssetRoundTripsAndDerivesItsBlinnPhongHalf)
{
    MaterialDocument material;
    material.name = "Brushed Steel";
    material.diffuseColor = EditorVector3{0.9f, 0.9f, 0.95f};
    material.emissiveColor = EditorVector3{0.0f, 0.05f, 0.1f};
    material.metallic = 0.95f;
    material.roughness = 0.2f;
    material.alpha = 0.8f;
    material.diffuseTexture = Uuid::generate();

    MaterialDocument reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(material.toJson()));

    CNA_EDITOR_EXPECT_EQ(reloaded.name, std::string{"Brushed Steel"});
    CNA_EDITOR_EXPECT(reloaded.diffuseTexture == material.diffuseTexture);
    CNA_EDITOR_EXPECT(std::fabs(reloaded.metallic - 0.95f) < 0.001f);
    CNA_EDITOR_EXPECT(std::fabs(reloaded.alpha - 0.8f) < 0.001f);

    // A texture that was never set is absent from the file rather than written as a nil id, and
    // reads back as nil either way -- the two mean the same thing and only one of them is noise.
    CNA_EDITOR_EXPECT(material.toJson()["normalTexture"].asString("absent") == "absent");
    CNA_EDITOR_EXPECT(!reloaded.normalTexture.isValid());

    const MeshMaterial mesh = reloaded.toMeshMaterial();
    CNA_EDITOR_EXPECT(mesh.specularColor.x > 0.8f);
    CNA_EDITOR_EXPECT(mesh.specularPower > 16.0f);

    // The paths stay empty: this document speaks in ids, and resolving one to a file belongs to
    // whoever holds the asset database.
    CNA_EDITOR_EXPECT(mesh.diffuseTexturePath.empty());
}

/** @brief A material from a newer editor is refused rather than silently rewritten with less in it. */
CNA_EDITOR_TEST(AMaterialFromAFutureFormatVersionIsRefused)
{
    MaterialDocument material;
    JsonValue future = material.toJson();
    future.set("formatVersion", JsonValue{MaterialDocument::kFormatVersion + 1});

    MaterialDocument reloaded;
    CNA_EDITOR_EXPECT(!reloaded.loadFromJson(future));
}
