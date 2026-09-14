// SPDX-License-Identifier: MS-PL
/**
 * @file SceneTests.cpp
 * @brief Tests for the scene document, its invariants, its serialisation and its undo behaviour.
 */

#include "TestHarness.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/EditorCamera3D.hpp"
#include "CNA/Editor/Scene/SceneLighting.hpp"
#include "CNA/Editor/Scene/SceneModels.hpp"
#include "CNA/Editor/Scene/SceneSprites3D.hpp"
#include "CNA/Editor/Scene/SceneWireframe.hpp"
#include "CNA/Editor/Scene/TransformGizmos3D.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Scene/PrefabCommands.hpp"
#include "CNA/Editor/Scene/PrefabDocument.hpp"
#include "CNA/Editor/Scene/SceneValidation.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"
#include "CNA/Editor/Scene/Tilemap.hpp"
#include "CNA/Editor/Project/Project.hpp"

using namespace CNA::Editor;

namespace
{
    /** @brief Returns a registry holding the built-in component descriptors. */
    ComponentRegistry makeRegistry()
    {
        ComponentRegistry registry;
        registerBuiltinComponents(registry);
        return registry;
    }

    /** @brief Builds an entity with a Transform positioned at (@p x, @p y, 0). */
    EditorEntity makeEntity(const ComponentRegistry& registry, std::string name, float x, float y)
    {
        EditorEntity entity{Uuid::generate(), std::move(name)};
        EditorComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
        transform.setProperty("position", PropertyValue{EditorVector3{x, y, 0.0f}});
        entity.addComponent(std::move(transform));
        return entity;
    }

    /** @brief Returns a unique scratch directory for a test that touches the filesystem. */
    std::filesystem::path makeScratchDirectory(const std::string& name)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / ("cna-editor-tests-" + name + "-" + Uuid::generate().toString());
        std::filesystem::create_directories(directory);
        return directory;
    }
}

CNA_EDITOR_TEST(BuiltinComponentsAreRegistered)
{
    const ComponentRegistry registry = makeRegistry();
    CNA_EDITOR_EXPECT(registry.contains(BuiltinComponentIds::kTransform));
    CNA_EDITOR_EXPECT(registry.contains(BuiltinComponentIds::kSpriteRenderer));
    CNA_EDITOR_EXPECT(registry.contains(BuiltinComponentIds::kCamera));
    CNA_EDITOR_EXPECT(registry.contains(BuiltinComponentIds::kAudioSource));

    const ComponentDescriptor* transform = registry.find(BuiltinComponentIds::kTransform);
    CNA_EDITOR_EXPECT(transform->required);
    CNA_EDITOR_EXPECT(transform->unique);

    // Scale must default to 1, not 0: a zero-scaled entity is invisible, and "my sprite does not
    // appear" is the least debuggable possible first experience.
    const PropertyDescriptor* scale = transform->findProperty("scale");
    CNA_EDITOR_EXPECT(scale != nullptr);
    CNA_EDITOR_EXPECT_EQ(scale->defaultValue.get<EditorVector3>().x, 1.0f);
}

CNA_EDITOR_TEST(SceneAddsAndFindsEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = scene.addEntity(makeEntity(registry, "Player", 10.0f, 20.0f));
    CNA_EDITOR_EXPECT(id.isValid());
    CNA_EDITOR_EXPECT_EQ(scene.getEntityCount(), std::size_t{1});

    const EditorEntity* entity = scene.findEntity(id);
    CNA_EDITOR_EXPECT(entity != nullptr);
    CNA_EDITOR_EXPECT_EQ(entity->getName(), std::string{"Player"});
    CNA_EDITOR_EXPECT(scene.findEntity(Uuid::generate()) == nullptr);
}

CNA_EDITOR_TEST(SceneRejectsDuplicateEntityIds)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Player", 0.0f, 0.0f);
    const Uuid id = scene.addEntity(entity);
    CNA_EDITOR_EXPECT(id.isValid());
    CNA_EDITOR_EXPECT(!scene.addEntity(entity).isValid());
    CNA_EDITOR_EXPECT_EQ(scene.getEntityCount(), std::size_t{1});
}

CNA_EDITOR_TEST(SceneMaintainsHierarchy)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = scene.addEntity(makeEntity(registry, "Parent", 0.0f, 0.0f));
    const Uuid child = scene.addEntity(makeEntity(registry, "Child", 0.0f, 0.0f));

    CNA_EDITOR_EXPECT_EQ(scene.getRootEntities().size(), std::size_t{2});
    CNA_EDITOR_EXPECT(scene.reparentEntity(child, parent));
    CNA_EDITOR_EXPECT_EQ(scene.getRootEntities().size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(scene.getChildren(parent).size(), std::size_t{1});
    CNA_EDITOR_EXPECT(scene.isAncestorOf(parent, child));
    CNA_EDITOR_EXPECT(!scene.isAncestorOf(child, parent));
}

CNA_EDITOR_TEST(SceneRejectsParentCycles)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid a = scene.addEntity(makeEntity(registry, "A", 0.0f, 0.0f));
    const Uuid b = scene.addEntity(makeEntity(registry, "B", 0.0f, 0.0f));
    const Uuid c = scene.addEntity(makeEntity(registry, "C", 0.0f, 0.0f));

    CNA_EDITOR_EXPECT(scene.reparentEntity(b, a));
    CNA_EDITOR_EXPECT(scene.reparentEntity(c, b));

    // A graph with a cycle has no roots and makes every hierarchy walk infinite, so these must
    // be refused outright rather than detected later.
    CNA_EDITOR_EXPECT(!scene.reparentEntity(a, c));
    CNA_EDITOR_EXPECT(!scene.reparentEntity(a, a));
    CNA_EDITOR_EXPECT_EQ(scene.getRootEntities().size(), std::size_t{1});
}

CNA_EDITOR_TEST(SceneDeletesSubtreesRecursively)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid root = scene.addEntity(makeEntity(registry, "Root", 0.0f, 0.0f));
    const Uuid middle = scene.addEntity(makeEntity(registry, "Middle", 0.0f, 0.0f));
    const Uuid leaf = scene.addEntity(makeEntity(registry, "Leaf", 0.0f, 0.0f));
    scene.reparentEntity(middle, root);
    scene.reparentEntity(leaf, middle);

    const std::vector<EditorEntity> removed = scene.removeEntityRecursive(root);
    CNA_EDITOR_EXPECT_EQ(removed.size(), std::size_t{3});
    CNA_EDITOR_EXPECT_EQ(scene.getEntityCount(), std::size_t{0});

    // Parents first, so an undo can re-add without ever pointing at a missing parent.
    CNA_EDITOR_EXPECT(removed.front().getId() == root);
}

CNA_EDITOR_TEST(SceneRoundTripsThroughJson)
{
    const ComponentRegistry registry = makeRegistry();

    SceneDocument original;
    original.setName("Level01");
    const Uuid parent = original.addEntity(makeEntity(registry, "Parent", 100.0f, 220.0f));

    EditorEntity sprite = makeEntity(registry, "Player", 5.0f, 6.0f);
    EditorComponent renderer{BuiltinComponentIds::kSpriteRenderer};
    renderer.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    const Uuid textureId = Uuid::generate();
    renderer.setProperty("texture", PropertyValue{PropertyValue::AssetReference{textureId}});
    renderer.setProperty("tint", PropertyValue{EditorColor{255, 0, 0, 128}});
    renderer.setProperty("layerDepth", PropertyValue{0.25f});
    sprite.addComponent(std::move(renderer));
    sprite.setEditorState("expanded", PropertyValue{true});
    const Uuid spriteId = original.addEntity(std::move(sprite));
    original.reparentEntity(spriteId, parent);

    SceneDocument restored;
    const SceneLoadResult result = restored.loadFromJson(original.toJson(), registry);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{0});
    CNA_EDITOR_EXPECT_EQ(restored.getName(), std::string{"Level01"});
    CNA_EDITOR_EXPECT(restored.getSceneId() == original.getSceneId());
    CNA_EDITOR_EXPECT_EQ(restored.getEntityCount(), std::size_t{2});

    const EditorEntity* restoredSprite = restored.findEntity(spriteId);
    CNA_EDITOR_EXPECT(restoredSprite != nullptr);
    CNA_EDITOR_EXPECT(restoredSprite->getParentId() == parent);

    const EditorComponent* restoredRenderer = restoredSprite->findComponent(BuiltinComponentIds::kSpriteRenderer);
    CNA_EDITOR_EXPECT(restoredRenderer != nullptr);
    CNA_EDITOR_EXPECT(restoredRenderer->getProperty("texture").get<PropertyValue::AssetReference>().id == textureId);
    CNA_EDITOR_EXPECT(restoredRenderer->getProperty("tint").get<EditorColor>() == (EditorColor{255, 0, 0, 128}));
    CNA_EDITOR_EXPECT_EQ(restoredRenderer->getProperty("layerDepth").get<float>(), 0.25f);
    CNA_EDITOR_EXPECT(restoredSprite->getEditorState().count("expanded") > 0);
}

CNA_EDITOR_TEST(SceneRoundTripsThroughAFile)
{
    const ComponentRegistry registry = makeRegistry();
    const std::filesystem::path directory = makeScratchDirectory("scene");
    const std::string path = (directory / "Level01.cnascene").generic_string();

    SceneDocument original;
    original.setName("Level01");
    const Uuid id = original.addEntity(makeEntity(registry, "Player", 1.0f, 2.0f));

    std::string errorMessage;
    CNA_EDITOR_EXPECT(original.saveToFile(path, &errorMessage));
    CNA_EDITOR_EXPECT_EQ(errorMessage, std::string{});

    SceneDocument restored;
    const SceneLoadResult result = restored.loadFromFile(path, registry);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT(restored.findEntity(id) != nullptr);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(SceneRejectsAFutureFormatVersion)
{
    const ComponentRegistry registry = makeRegistry();

    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{SceneDocument::kFormatVersion + 1});
    json.set("name", JsonValue{"FromTheFuture"});

    SceneDocument scene;
    const SceneLoadResult result = scene.loadFromJson(json, registry);
    CNA_EDITOR_EXPECT(!result.succeeded);
    CNA_EDITOR_EXPECT(result.errorMessage.find("newer") != std::string::npos);
}

CNA_EDITOR_TEST(ScenePreservesUnknownComponentTypes)
{
    // A scene using a component from a plugin that failed to load must still round-trip. Dropping
    // the data would turn "the plugin is missing" into "the plugin's data is gone".
    ComponentRegistry registry = makeRegistry();

    JsonValue componentJson = JsonValue::makeObject();
    componentJson.set("spawnCount", JsonValue{7});
    componentJson.set("label", JsonValue{"wave-1"});

    JsonValue componentsJson = JsonValue::makeObject();
    componentsJson.set("Mc3.SpawnPoint", std::move(componentJson));

    JsonValue entityJson = JsonValue::makeObject();
    entityJson.set("id", JsonValue{Uuid::generate().toString()});
    entityJson.set("name", JsonValue{"Spawner"});
    entityJson.set("components", std::move(componentsJson));

    JsonValue entitiesJson = JsonValue::makeArray();
    entitiesJson.append(std::move(entityJson));

    JsonValue sceneJson = JsonValue::makeObject();
    sceneJson.set("formatVersion", JsonValue{SceneDocument::kFormatVersion});
    sceneJson.set("sceneId", JsonValue{Uuid::generate().toString()});
    sceneJson.set("name", JsonValue{"WithPlugin"});
    sceneJson.set("entities", std::move(entitiesJson));

    SceneDocument scene;
    const SceneLoadResult result = scene.loadFromJson(sceneJson, registry);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{1});

    const EditorEntity& entity = scene.getEntities().front();
    const EditorComponent* component = entity.findComponent("Mc3.SpawnPoint");
    CNA_EDITOR_EXPECT(component != nullptr);
    CNA_EDITOR_EXPECT_EQ(component->getProperty("label").get<std::string>(), std::string{"wave-1"});

    // And it must survive a save/load cycle unchanged.
    SceneDocument reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(scene.toJson(), registry).succeeded);
    CNA_EDITOR_EXPECT(reloaded.getEntities().front().findComponent("Mc3.SpawnPoint") != nullptr);
}

CNA_EDITOR_TEST(SceneRepairsDanglingParentReferences)
{
    ComponentRegistry registry = makeRegistry();

    JsonValue entityJson = JsonValue::makeObject();
    entityJson.set("id", JsonValue{Uuid::generate().toString()});
    entityJson.set("name", JsonValue{"Orphan"});
    entityJson.set("parent", JsonValue{Uuid::generate().toString()});
    entityJson.set("components", JsonValue::makeObject());

    JsonValue entitiesJson = JsonValue::makeArray();
    entitiesJson.append(std::move(entityJson));

    JsonValue sceneJson = JsonValue::makeObject();
    sceneJson.set("formatVersion", JsonValue{SceneDocument::kFormatVersion});
    sceneJson.set("sceneId", JsonValue{Uuid::generate().toString()});
    sceneJson.set("entities", std::move(entitiesJson));

    SceneDocument scene;
    const SceneLoadResult result = scene.loadFromJson(sceneJson, registry);

    // A scene broken by a bad merge is exactly the scene a user needs the editor to open.
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(scene.getRootEntities().size(), std::size_t{1});
}

// --------------------------------------------------------------------------------------------
// Scene validation (plan.md ED-310)
//
// Every rule describes a state the editor allows the user to reach, so each test asserts both
// halves: that the offending scene is reported, and that the nearest legitimate scene is not.
// A validator that cries wolf is turned off, and then it catches nothing at all.
// --------------------------------------------------------------------------------------------

namespace
{
    /** @brief Adds a component of @p typeId with its declared defaults filled in. */
    EditorComponent& addComponentWithDefaults(EditorEntity& entity,
                                              const ComponentRegistry& registry,
                                              const std::string& typeId)
    {
        EditorComponent component{typeId};
        if (const ComponentDescriptor* descriptor = registry.find(typeId))
        {
            component.applyDefaults(*descriptor);
        }
        return entity.addComponent(std::move(component));
    }

    /** @brief Returns how many issues carry @p ruleId. */
    std::size_t countRule(const std::vector<SceneIssue>& issues, const std::string& ruleId)
    {
        return static_cast<std::size_t>(
            std::count_if(issues.begin(), issues.end(),
                          [&](const SceneIssue& issue) { return issue.ruleId == ruleId; }));
    }
}

CNA_EDITOR_TEST(AnEmptySceneReportsNothing)
{
    const ComponentRegistry registry = makeRegistry();
    const SceneDocument scene;

    CNA_EDITOR_EXPECT(validateScene(scene, registry).empty());
}

CNA_EDITOR_TEST(TwoPrimaryCamerasAreAnError)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity first = makeEntity(registry, "Main Camera", 0.0f, 0.0f);
    addComponentWithDefaults(first, registry, BuiltinComponentIds::kCamera);
    scene.addEntity(std::move(first));

    EditorEntity second = makeEntity(registry, "Cutscene Camera", 0.0f, 0.0f);
    addComponentWithDefaults(second, registry, BuiltinComponentIds::kCamera);
    scene.addEntity(std::move(second));

    const std::vector<SceneIssue> issues = validateScene(scene, registry);

    // One issue per offending camera, so that either row selects a real entity.
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "duplicate-primary-camera"), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(countIssues(issues, SceneIssue::Severity::Error), std::size_t{2});

    // And the ordinary case -- one camera, marked primary -- says nothing at all.
    SceneDocument single;
    EditorEntity only = makeEntity(registry, "Main Camera", 0.0f, 0.0f);
    addComponentWithDefaults(only, registry, BuiltinComponentIds::kCamera);
    single.addEntity(std::move(only));

    CNA_EDITOR_EXPECT(validateScene(single, registry).empty());
}

CNA_EDITOR_TEST(SwitchingACameraOffResolvesThePrimaryConflict)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity first = makeEntity(registry, "Main Camera", 0.0f, 0.0f);
    addComponentWithDefaults(first, registry, BuiltinComponentIds::kCamera);
    scene.addEntity(std::move(first));

    EditorEntity second = makeEntity(registry, "Cutscene Camera", 0.0f, 0.0f);
    addComponentWithDefaults(second, registry, BuiltinComponentIds::kCamera);
    // Switching the entity off is how a person swaps cameras, so it has to count as a resolution.
    second.setEnabled(false);
    scene.addEntity(std::move(second));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "duplicate-primary-camera"),
                         std::size_t{0});
}

CNA_EDITOR_TEST(ACameraUnderADisabledParentDoesNotCompete)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity primary = makeEntity(registry, "Main Camera", 0.0f, 0.0f);
    addComponentWithDefaults(primary, registry, BuiltinComponentIds::kCamera);
    scene.addEntity(std::move(primary));

    EditorEntity group = makeEntity(registry, "Cutscene", 0.0f, 0.0f);
    group.setEnabled(false);
    const Uuid groupId = scene.addEntity(std::move(group));

    EditorEntity child = makeEntity(registry, "Cutscene Camera", 0.0f, 0.0f);
    addComponentWithDefaults(child, registry, BuiltinComponentIds::kCamera);
    const Uuid childId = scene.addEntity(std::move(child));
    CNA_EDITOR_EXPECT(scene.reparentEntity(childId, groupId));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "duplicate-primary-camera"),
                         std::size_t{0});
}

CNA_EDITOR_TEST(ASceneWhoseCamerasAreAllSecondaryIsReported)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Camera", 0.0f, 0.0f);
    EditorComponent& camera = addComponentWithDefaults(entity, registry, BuiltinComponentIds::kCamera);
    camera.setProperty("isPrimary", PropertyValue{false});
    scene.addEntity(std::move(entity));

    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "no-primary-camera"), std::size_t{1});

    // Scene-wide: there is no offending entity, and blaming one would send the user to the
    // wrong row.
    for (const SceneIssue& issue : issues)
    {
        if (issue.ruleId == "no-primary-camera") { CNA_EDITOR_EXPECT(!issue.entityId.isValid()); }
    }

    // A scene with no cameras at all is not reported: it is a fragment, not a broken level.
    SceneDocument empty;
    empty.addEntity(makeEntity(registry, "Group", 0.0f, 0.0f));
    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(empty, registry), "no-primary-camera"), std::size_t{0});
}

CNA_EDITOR_TEST(InvertedCameraPlanesAreAnError)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Camera", 0.0f, 0.0f);
    EditorComponent& camera = addComponentWithDefaults(entity, registry, BuiltinComponentIds::kCamera);
    camera.setProperty("nearPlane", PropertyValue{1000.0f});
    camera.setProperty("farPlane", PropertyValue{0.1f});
    scene.addEntity(std::move(entity));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "camera-planes-inverted"),
                         std::size_t{1});
}

CNA_EDITOR_TEST(AZeroScaleIsReportedOnAnyAxis)
{
    const ComponentRegistry registry = makeRegistry();

    for (int axis = 0; axis < 3; ++axis)
    {
        SceneDocument scene;
        EditorEntity entity = makeEntity(registry, "Player", 0.0f, 0.0f);
        addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteRenderer);

        EditorVector3 scale{1.0f, 1.0f, 1.0f};
        (axis == 0 ? scale.x : axis == 1 ? scale.y : scale.z) = 0.0f;
        entity.findComponent(BuiltinComponentIds::kTransform)->setProperty("scale", PropertyValue{scale});

        scene.addEntity(std::move(entity));

        CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "zero-scale"), std::size_t{1});
    }

    // A negative scale is a mirror, not a mistake, and must not be reported.
    SceneDocument mirrored;
    EditorEntity entity = makeEntity(registry, "Player", 0.0f, 0.0f);
    addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteRenderer);
    entity.findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("scale", PropertyValue{EditorVector3{-1.0f, 1.0f, 1.0f}});
    mirrored.addEntity(std::move(entity));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(mirrored, registry), "zero-scale"), std::size_t{0});
}

CNA_EDITOR_TEST(AnEntityThatDoesNothingIsReportedButAGroupIsNot)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    // A transform and nothing else, with no children: it occupies a row in the hierarchy and
    // does not exist as far as the game is concerned.
    scene.addEntity(makeEntity(registry, "Leftover", 0.0f, 0.0f));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "empty-entity"), std::size_t{1});

    // The same entity with a child is a group, which is the ordinary way to move a set of things
    // together, and must stay silent.
    SceneDocument grouped;
    const Uuid parent = grouped.addEntity(makeEntity(registry, "Enemies", 0.0f, 0.0f));

    EditorEntity child = makeEntity(registry, "Enemy", 0.0f, 0.0f);
    addComponentWithDefaults(child, registry, BuiltinComponentIds::kSpriteRenderer);
    const Uuid childId = grouped.addEntity(std::move(child));
    CNA_EDITOR_EXPECT(grouped.reparentEntity(childId, parent));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(grouped, registry), "empty-entity"), std::size_t{0});
}

CNA_EDITOR_TEST(ASpriteWithNoTextureIsReported)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Player", 0.0f, 0.0f);
    addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteRenderer);
    scene.addEntity(std::move(entity));

    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "sprite-without-texture"), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(countIssues(issues, SceneIssue::Severity::Error), std::size_t{0});

    // Once it points at something, the rule stops firing -- whether that id resolves is the
    // missing-reference report's question, not this one's.
    SceneDocument textured;
    EditorEntity sprite = makeEntity(registry, "Player", 0.0f, 0.0f);
    EditorComponent& renderer =
        addComponentWithDefaults(sprite, registry, BuiltinComponentIds::kSpriteRenderer);
    renderer.setProperty("texture", PropertyValue{PropertyValue::AssetReference{Uuid::generate()}});
    textured.addEntity(std::move(sprite));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(textured, registry), "sprite-without-texture"),
                         std::size_t{0});
}

CNA_EDITOR_TEST(ATilemapWithNoTileSizeIsReported)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Level", 0.0f, 0.0f);
    EditorComponent& tilemap = addComponentWithDefaults(entity, registry, BuiltinComponentIds::kTilemap);
    tilemap.setProperty(TilemapKeys::kTileWidth, PropertyValue{std::int64_t{0}});
    scene.addEntity(std::move(entity));

    // A tile size of zero draws nothing and swallows every brush stroke, neither of which says
    // why. That is the whole reason this rule exists.
    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "tilemap-without-tile-size"), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(countIssues(issues, SceneIssue::Severity::Error), std::size_t{0});

    // A tilemap with a real tile size is not reported, whether or not it has any tiles in it yet.
    SceneDocument sized;
    EditorEntity ok = makeEntity(registry, "Level", 0.0f, 0.0f);
    addComponentWithDefaults(ok, registry, BuiltinComponentIds::kTilemap);
    sized.addEntity(std::move(ok));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(sized, registry), "tilemap-without-tile-size"),
                         std::size_t{0});
}

CNA_EDITOR_TEST(AnAnimationWithNoSheetOrNoFramesIsReported)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Hero", 0.0f, 0.0f);
    addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteRenderer);
    addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteAnimation);
    scene.addEntity(std::move(entity));

    // Both halves fire on a freshly added component, and each is worth its own line: a sheet with
    // no frames plays nothing, and frames with no sheet leave the sprite drawing a placeholder --
    // which looks exactly like a broken asset reference and is a different problem.
    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "animation-without-sheet"), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "animation-without-frames"), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(countIssues(issues, SceneIssue::Severity::Error), std::size_t{0});

    SceneDocument ready;
    EditorEntity animated = makeEntity(registry, "Hero", 0.0f, 0.0f);
    addComponentWithDefaults(animated, registry, BuiltinComponentIds::kSpriteRenderer);
    EditorComponent& animation =
        addComponentWithDefaults(animated, registry, BuiltinComponentIds::kSpriteAnimation);
    animation.setProperty(SpriteAnimationKeys::kSheet,
                          PropertyValue{PropertyValue::AssetReference{Uuid::generate()}});

    PropertyValue::ListValue frames;
    frames.items.push_back(PropertyValue{std::int64_t{0}});
    animation.setProperty(SpriteAnimationKeys::kFrames, PropertyValue{frames});
    ready.addEntity(std::move(animated));

    const std::vector<SceneIssue> readyIssues = validateScene(ready, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(readyIssues, "animation-without-sheet"), std::size_t{0});
    CNA_EDITOR_EXPECT_EQ(countRule(readyIssues, "animation-without-frames"), std::size_t{0});
}

CNA_EDITOR_TEST(AnUnregisteredComponentTypeIsReportedRatherThanIgnored)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Spawner", 0.0f, 0.0f);
    entity.addComponent(EditorComponent{"Mc3.SpawnPoint"});
    scene.addEntity(std::move(entity));

    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "unknown-component-type"), std::size_t{1});

    // And it counts as doing something: the editor cannot see what it does, which is not the
    // same as knowing that it does nothing.
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "empty-entity"), std::size_t{0});
}

CNA_EDITOR_TEST(AnEntityWithoutATransformIsAnError)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    // Reachable from a hand-edited file, or from a build older than the Transform requirement.
    scene.addEntity(EditorEntity{Uuid::generate(), "Ghost"});

    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "missing-required-component"), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(countIssues(issues, SceneIssue::Severity::Error), std::size_t{1});
}

CNA_EDITOR_TEST(ASecondUniqueComponentIsAnError)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Player", 0.0f, 0.0f);
    addComponentWithDefaults(entity, registry, BuiltinComponentIds::kTransform);
    scene.addEntity(std::move(entity));

    const std::vector<SceneIssue> issues = validateScene(scene, registry);

    // Reported once for the pair, not once per instance: the user has one problem to fix.
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "duplicate-component"), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(issues.front().componentTypeId, std::string{BuiltinComponentIds::kTransform});
}

CNA_EDITOR_TEST(IssuesComeBackInDocumentOrder)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    scene.addEntity(makeEntity(registry, "First", 0.0f, 0.0f));
    scene.addEntity(makeEntity(registry, "Second", 0.0f, 0.0f));
    scene.addEntity(makeEntity(registry, "Third", 0.0f, 0.0f));

    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(issues.size(), std::size_t{3});
    CNA_EDITOR_EXPECT_EQ(issues[0].entityName, std::string{"First"});
    CNA_EDITOR_EXPECT_EQ(issues[1].entityName, std::string{"Second"});
    CNA_EDITOR_EXPECT_EQ(issues[2].entityName, std::string{"Third"});

    // Pure: the same document must produce the same report, or it cannot be diffed or asserted on.
    CNA_EDITOR_EXPECT(validateScene(scene, registry).size() == issues.size());
}

CNA_EDITOR_TEST(AnEntityLeftOnARenamedLayerIsReportedNotRewritten)
{
    ComponentRegistry registry = makeRegistry();
    applyProjectLayers(registry, {"Background", "Default"});

    SceneDocument scene;
    EditorEntity entity = makeEntity(registry, "Backdrop", 0.0f, 0.0f);
    addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteRenderer);
    EditorComponent& layer = addComponentWithDefaults(entity, registry, BuiltinComponentIds::kLayer);
    layer.setProperty("layer", PropertyValue{PropertyValue::EnumValue{"Background"}});
    const Uuid entityId = scene.addEntity(std::move(entity));

    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "unknown-enum-value"), std::size_t{0});

    // Rename the layer out from under it, which is exactly what editing the project's layer list
    // does. The stored value is kept: which of the remaining layers the user meant is their
    // decision, not the editor's.
    applyProjectLayers(registry, {"Backdrop", "Default"});

    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "unknown-enum-value"), std::size_t{1});

    const EditorComponent* stored = scene.findEntity(entityId)->findComponent(BuiltinComponentIds::kLayer);
    CNA_EDITOR_EXPECT_EQ(stored->getProperty("layer").get<PropertyValue::EnumValue>().name,
                         std::string{"Background"});

    // And it is a warning, not an error: the scene still runs and nothing was lost.
    CNA_EDITOR_EXPECT_EQ(countIssues(issues, SceneIssue::Severity::Error), std::size_t{0});
}

CNA_EDITOR_TEST(TagsAreAListOnTheirOwnComponent)
{
    const ComponentRegistry registry = makeRegistry();

    // Its own component rather than a field on EditorEntity: a tag is a game concept and the
    // entity type is deliberately not one (D-04).
    const ComponentDescriptor* descriptor = registry.find(BuiltinComponentIds::kTags);
    CNA_EDITOR_EXPECT(descriptor != nullptr);

    const PropertyDescriptor* tags = descriptor->findProperty("tags");
    CNA_EDITOR_EXPECT(tags != nullptr);
    CNA_EDITOR_EXPECT(tags->type == PropertyType::List);
    CNA_EDITOR_EXPECT(tags->elementType == PropertyType::String);

    SceneDocument scene;
    EditorEntity entity = makeEntity(registry, "Enemy", 0.0f, 0.0f);
    EditorComponent& component = addComponentWithDefaults(entity, registry, BuiltinComponentIds::kTags);

    PropertyValue::ListValue list;
    list.items.emplace_back(std::string{"enemy"});
    list.items.emplace_back(std::string{"spawns-loot"});
    component.setProperty("tags", PropertyValue{list});
    scene.addEntity(std::move(entity));

    SceneDocument reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(scene.toJson(), registry).succeeded);
    CNA_EDITOR_EXPECT(reloaded.getEntities().front().findComponent(BuiltinComponentIds::kTags)
                          ->getProperty("tags") == PropertyValue{list});

    // An entity carrying only a transform and a tag list still does nothing, and says so.
    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(reloaded, registry), "empty-entity"), std::size_t{0});
}

// --------------------------------------------------------------------------------------------
// Prefabs (plan.md ED-300)
// --------------------------------------------------------------------------------------------

namespace
{
    /** @brief Builds a two-entity scene: "Enemy" with a sprite, and a child "Weapon". */
    struct PrefabScene
    {
        ComponentRegistry registry = makeRegistry();
        SceneDocument scene;
        Uuid rootId;
        Uuid childId;

        PrefabScene()
        {
            EditorEntity root = makeEntity(registry, "Enemy", 10.0f, 20.0f);
            addComponentWithDefaults(root, registry, BuiltinComponentIds::kSpriteRenderer);
            rootId = scene.addEntity(std::move(root));

            EditorEntity child = makeEntity(registry, "Weapon", 5.0f, 0.0f);
            addComponentWithDefaults(child, registry, BuiltinComponentIds::kSpriteRenderer);
            childId = scene.addEntity(std::move(child));
            scene.reparentEntity(childId, rootId);
        }

        [[nodiscard]] PrefabDocument capture(const std::string& name = "Enemy") const
        {
            PrefabDocument prefab;
            prefab.captureFromScene(scene, rootId, name);
            return prefab;
        }
    };
}

CNA_EDITOR_TEST(APrefabCapturesASubtreeAndRoundTripsThroughAFile)
{
    const PrefabScene fixture;
    const PrefabDocument prefab = fixture.capture();

    CNA_EDITOR_EXPECT_EQ(prefab.getEntities().size(), std::size_t{2});
    CNA_EDITOR_EXPECT(prefab.getRootId() == fixture.rootId);
    CNA_EDITOR_EXPECT(prefab.getPrefabId().isValid());

    // The root has no parent inside the prefab: keeping the scene's would make the file describe a
    // hierarchy that only exists where it was captured from.
    CNA_EDITOR_EXPECT(!prefab.getEntities().front().getParentId().isValid());
    CNA_EDITOR_EXPECT(prefab.getEntities().back().getParentId() == fixture.rootId);

    PrefabDocument reloaded;
    const PrefabLoadResult result = reloaded.loadFromJson(prefab.toJson(), fixture.registry);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT(result.warnings.empty());
    CNA_EDITOR_EXPECT_EQ(Json::write(reloaded.toJson()), Json::write(prefab.toJson()));

    // Capturing something that is not in the scene fails rather than producing an empty prefab.
    PrefabDocument missing;
    CNA_EDITOR_EXPECT(!missing.captureFromScene(fixture.scene, Uuid::generate(), "Nothing"));
}

CNA_EDITOR_TEST(APrefabRefusesAnEmptyFileAndRepairsABrokenOne)
{
    const ComponentRegistry registry = makeRegistry();

    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{PrefabDocument::kFormatVersion});
    json.set("prefabId", JsonValue{Uuid::generate().toString()});
    json.set("entities", JsonValue::makeArray());

    // A prefab with no entities instantiates to nothing, and the user would have no way to tell
    // that from an instantiation that silently failed.
    PrefabDocument empty;
    CNA_EDITOR_EXPECT(!empty.loadFromJson(json, registry).succeeded);

    // A child whose parent is not in the file is attached to the root rather than dropped: a file
    // broken by a bad merge is exactly the one somebody needs the editor to open.
    const Uuid rootId = Uuid::generate();

    JsonValue root = JsonValue::makeObject();
    root.set("id", JsonValue{rootId.toString()});
    root.set("name", JsonValue{"Root"});
    root.set("components", JsonValue::makeObject());

    JsonValue orphan = JsonValue::makeObject();
    orphan.set("id", JsonValue{Uuid::generate().toString()});
    orphan.set("name", JsonValue{"Orphan"});
    orphan.set("parent", JsonValue{Uuid::generate().toString()});
    orphan.set("components", JsonValue::makeObject());

    JsonValue entities = JsonValue::makeArray();
    entities.append(std::move(root));
    entities.append(std::move(orphan));
    json.set("entities", std::move(entities));

    PrefabDocument repaired;
    const PrefabLoadResult result = repaired.loadFromJson(json, registry);
    CNA_EDITOR_EXPECT(result.succeeded);
    CNA_EDITOR_EXPECT_EQ(result.warnings.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(repaired.getEntities().back().getParentId() == rootId);

    // A file from a newer build is refused by the same gate every other format uses.
    json.set("formatVersion", JsonValue{PrefabDocument::kFormatVersion + 1});
    PrefabDocument future;
    CNA_EDITOR_EXPECT(!future.loadFromJson(json, registry).succeeded);
    CNA_EDITOR_EXPECT_EQ(getPrefabFormatMigrator().getMigrationCount(), std::size_t{0});
}

CNA_EDITOR_TEST(InstantiatingAPrefabGivesFreshIdsAndKeepsTheLink)
{
    PrefabScene fixture;
    const PrefabDocument prefab = fixture.capture();
    const Uuid assetId = Uuid::generate();

    SceneDocument target;
    auto command = std::make_unique<InstantiatePrefabCommand>(target, prefab, assetId, Uuid{});
    CNA_EDITOR_EXPECT(command->isValid());

    const Uuid instanceRoot = command->getRootId();
    command->execute();

    CNA_EDITOR_EXPECT_EQ(target.getEntityCount(), std::size_t{2});
    CNA_EDITOR_EXPECT(getPrefabAssetOf(target, instanceRoot) == assetId);

    // Fresh ids: two instances of one prefab are two different entities, and reusing the prefab's
    // would make the second instantiation collide with the first.
    CNA_EDITOR_EXPECT(instanceRoot != fixture.rootId);

    auto second = std::make_unique<InstantiatePrefabCommand>(target, prefab, assetId, Uuid{});
    second->execute();
    CNA_EDITOR_EXPECT_EQ(target.getEntityCount(), std::size_t{4});
    CNA_EDITOR_EXPECT(second->getRootId() != instanceRoot);

    // Every entity carries its link, so selecting a child still answers questions about the
    // instance it belongs to.
    const std::vector<Uuid> children = target.getChildren(instanceRoot);
    CNA_EDITOR_EXPECT_EQ(children.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(findInstanceRoot(target, children.front()) == instanceRoot);

    // A freshly made instance has changed nothing.
    CNA_EDITOR_EXPECT(findPrefabOverrides(target, instanceRoot, prefab, fixture.registry).empty());

    second->undo();
    command->undo();
    CNA_EDITOR_EXPECT_EQ(target.getEntityCount(), std::size_t{0});

    // An empty prefab, or an unknown parent, is refused rather than half-applied.
    const PrefabDocument nothing;
    CNA_EDITOR_EXPECT(!InstantiatePrefabCommand(target, nothing, assetId, Uuid{}).isValid());
    CNA_EDITOR_EXPECT(!InstantiatePrefabCommand(target, prefab, assetId, Uuid::generate()).isValid());
}

CNA_EDITOR_TEST(OverridesAreFoundByComparingRatherThanByRecording)
{
    PrefabScene fixture;
    const PrefabDocument prefab = fixture.capture();

    SceneDocument target;
    InstantiatePrefabCommand instantiate{target, prefab, Uuid::generate(), Uuid{}};
    instantiate.execute();
    const Uuid instanceRoot = instantiate.getRootId();

    // Change a property, rename an entity, add one and delete one -- four different ways an
    // instance can diverge, all of them ordinary things a user does.
    target.findEntity(instanceRoot)
        ->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("position", PropertyValue{EditorVector3{999.0f, 0.0f, 0.0f}});
    target.findEntity(instanceRoot)->setName("Boss");

    EditorEntity extra = makeEntity(fixture.registry, "Shield", 0.0f, 0.0f);
    const Uuid extraId = target.addEntity(std::move(extra));
    target.reparentEntity(extraId, instanceRoot);

    const std::vector<PrefabOverride> overrides = findPrefabOverrides(target, instanceRoot, prefab, fixture.registry);

    const auto countKind = [&](PrefabOverride::Kind kind) {
        return static_cast<std::size_t>(
            std::count_if(overrides.begin(), overrides.end(),
                          [kind](const PrefabOverride& entry) { return entry.kind == kind; }));
    };

    CNA_EDITOR_EXPECT_EQ(countKind(PrefabOverride::Kind::AddedEntity), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(countKind(PrefabOverride::Kind::RemovedEntity), std::size_t{0});
    CNA_EDITOR_EXPECT(countKind(PrefabOverride::Kind::Property) >= std::size_t{2});

    bool sawRename = false;
    bool sawPosition = false;
    for (const PrefabOverride& entry : overrides)
    {
        if (entry.propertyName == "name") { sawRename = true; }
        if (entry.propertyName == "position") { sawPosition = true; }
    }
    CNA_EDITOR_EXPECT(sawRename);
    CNA_EDITOR_EXPECT(sawPosition);

    // Deleting one of the prefab's *own* entities is a removal, not a silent match. Found by its
    // link rather than by position: the children are ordered by name, and "Shield" sorts before
    // "Weapon", so an index here would delete the entity the test just added.
    Uuid weaponId;
    for (const Uuid& childId : target.getChildren(instanceRoot))
    {
        if (target.findEntity(childId)->getName() == "Weapon") { weaponId = childId; }
    }
    CNA_EDITOR_EXPECT(weaponId.isValid());
    target.removeEntityRecursive(weaponId);
    const std::vector<PrefabOverride> afterDelete = findPrefabOverrides(target, instanceRoot, prefab, fixture.registry);
    CNA_EDITOR_EXPECT(std::any_of(afterDelete.begin(), afterDelete.end(),
                                  [](const PrefabOverride& entry)
                                  { return entry.kind == PrefabOverride::Kind::RemovedEntity; }));
}

CNA_EDITOR_TEST(RevertingAnInstancePutsItBackExactlyAndUndoesInOnePress)
{
    PrefabScene fixture;
    const PrefabDocument prefab = fixture.capture();

    SceneDocument target;
    InstantiatePrefabCommand instantiate{target, prefab, Uuid::generate(), Uuid{}};
    instantiate.execute();
    const Uuid instanceRoot = instantiate.getRootId();

    target.findEntity(instanceRoot)->setName("Boss");
    target.findEntity(instanceRoot)
        ->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("position", PropertyValue{EditorVector3{999.0f, 0.0f, 0.0f}});

    EditorEntity extra = makeEntity(fixture.registry, "Shield", 0.0f, 0.0f);
    const Uuid extraId = target.addEntity(std::move(extra));
    target.reparentEntity(extraId, instanceRoot);

    auto revert = std::make_unique<RevertPrefabInstanceCommand>(target, instanceRoot, prefab);
    CNA_EDITOR_EXPECT(revert->isValid());
    revert->execute();

    CNA_EDITOR_EXPECT(findPrefabOverrides(target, instanceRoot, prefab, fixture.registry).empty());
    CNA_EDITOR_EXPECT_EQ(target.getEntityCount(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(target.findEntity(instanceRoot)->getName(), std::string{"Enemy"});

    // The added entity is gone. That is deliberate: a revert that left some of the user's changes
    // in place would not be a revert, and the user who wanted a partial one has undo.
    CNA_EDITOR_EXPECT(target.findEntity(extraId) == nullptr);

    // The root keeps its id, because the selection and every reference into the instance point at
    // it -- reverting must not break them.
    CNA_EDITOR_EXPECT(target.findEntity(instanceRoot) != nullptr);
    CNA_EDITOR_EXPECT(getPrefabAssetOf(target, instanceRoot).isValid());

    revert->undo();
    CNA_EDITOR_EXPECT_EQ(target.getEntityCount(), std::size_t{3});
    CNA_EDITOR_EXPECT_EQ(target.findEntity(instanceRoot)->getName(), std::string{"Boss"});
    CNA_EDITOR_EXPECT(target.findEntity(extraId) != nullptr);

    // Reverting an instance that has not diverged is refused: an undo entry that undoes to the
    // state it is already in reads to the user as a broken Ctrl+Z.
    revert->execute();
    CNA_EDITOR_EXPECT(!RevertPrefabInstanceCommand(target, instanceRoot, prefab).isValid());

    // As is reverting something that is not an instance at all.
    const Uuid plain = target.addEntity(makeEntity(fixture.registry, "Plain", 0.0f, 0.0f));
    CNA_EDITOR_EXPECT(!RevertPrefabInstanceCommand(target, plain, prefab).isValid());
}

// --------------------------------------------------------------------------------------------
// Tilemaps (plan.md ED-301)
// --------------------------------------------------------------------------------------------

namespace
{
    /** @brief Builds a scene with one 4x3 tilemap entity at the origin, 32px tiles. */
    struct TilemapScene
    {
        ComponentRegistry registry = makeRegistry();
        SceneDocument scene;
        Uuid entityId;

        TilemapScene()
        {
            EditorEntity entity = makeEntity(registry, "Ground", 0.0f, 0.0f);
            EditorComponent& tilemap = addComponentWithDefaults(entity, registry, BuiltinComponentIds::kTilemap);
            tilemap.setProperty(TilemapKeys::kColumns, PropertyValue{std::int64_t{4}});
            tilemap.setProperty(TilemapKeys::kRows, PropertyValue{std::int64_t{3}});
            entityId = scene.addEntity(std::move(entity));
        }

        [[nodiscard]] TilemapGrid grid()
        {
            return readTilemapGrid(*scene.findEntity(entityId)->findComponent(BuiltinComponentIds::kTilemap),
                                   registry.find(BuiltinComponentIds::kTilemap));
        }
    };
}

CNA_EDITOR_TEST(ATilemapGridReadsPadsAndResizesByCoordinate)
{
    TilemapScene fixture;

    // A tilemap that has never been painted reads as a full grid of empty cells, not as nothing:
    // the paint tool needs somewhere to put the first tile.
    TilemapGrid grid = fixture.grid();
    CNA_EDITOR_EXPECT_EQ(grid.columns, 4);
    CNA_EDITOR_EXPECT_EQ(grid.rows, 3);
    CNA_EDITOR_EXPECT_EQ(grid.tiles.size(), std::size_t{12});
    CNA_EDITOR_EXPECT_EQ(grid.at(0, 0), kEmptyTile);

    // Out of range answers empty rather than failing: the cursor leaves the map constantly.
    CNA_EDITOR_EXPECT_EQ(grid.at(-1, 0), kEmptyTile);
    CNA_EDITOR_EXPECT_EQ(grid.at(4, 0), kEmptyTile);
    grid.set(9, 9, 5);
    CNA_EDITOR_EXPECT_EQ(grid.tiles.size(), std::size_t{12});

    grid.set(0, 0, 7);
    grid.set(3, 2, 8);
    CNA_EDITOR_EXPECT_EQ(grid.at(0, 0), std::int64_t{7});
    CNA_EDITOR_EXPECT_EQ(grid.at(3, 2), std::int64_t{8});

    // Resizing keeps tiles where they were on screen. Copying a flat list without remapping shifts
    // every row sideways, which turns "one column wider" into "scramble the level".
    const TilemapGrid wider = resizeTilemapGrid(grid, 6, 3);
    CNA_EDITOR_EXPECT_EQ(wider.at(0, 0), std::int64_t{7});
    CNA_EDITOR_EXPECT_EQ(wider.at(3, 2), std::int64_t{8});
    CNA_EDITOR_EXPECT_EQ(wider.at(5, 2), kEmptyTile);

    // And shrinking drops what no longer fits rather than wrapping it somewhere unexpected.
    const TilemapGrid smaller = resizeTilemapGrid(grid, 2, 2);
    CNA_EDITOR_EXPECT_EQ(smaller.at(0, 0), std::int64_t{7});
    CNA_EDITOR_EXPECT_EQ(smaller.tiles.size(), std::size_t{4});

    // A stored list of the wrong length is padded, not rejected: a hand-edited scene one row short
    // should open and be fixable.
    EditorComponent* component =
        fixture.scene.findEntity(fixture.entityId)->findComponent(BuiltinComponentIds::kTilemap);
    PropertyValue::ListValue truncated;
    truncated.items.emplace_back(std::int64_t{3});
    component->setProperty(TilemapKeys::kTiles, PropertyValue{truncated});

    const TilemapGrid padded = fixture.grid();
    CNA_EDITOR_EXPECT_EQ(padded.tiles.size(), std::size_t{12});
    CNA_EDITOR_EXPECT_EQ(padded.at(0, 0), std::int64_t{3});
    CNA_EDITOR_EXPECT_EQ(padded.at(1, 0), kEmptyTile);
}

CNA_EDITOR_TEST(WorldPointsMapToTilesIncludingOutsideTheMap)
{
    WorldTransform transform;
    transform.position = EditorVector3{100.0f, 200.0f, 0.0f};

    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 32, 32, EditorVector2{100.0f, 200.0f}).x, 0);
    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 32, 32, EditorVector2{131.0f, 200.0f}).x, 0);
    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 32, 32, EditorVector2{132.0f, 200.0f}).x, 1);

    // Grows downward, matching how SpriteBatch addresses the screen and how tile editors number
    // their rows.
    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 32, 32, EditorVector2{100.0f, 233.0f}).y, 1);

    // Floored rather than truncated: truncation folds -0.5 and +0.5 onto the same cell, so a click
    // just left of the origin would land on the first column instead of outside the map.
    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 32, 32, EditorVector2{99.0f, 200.0f}).x, -1);

    // Scale is honoured, because zooming a map by scaling its entity is ordinary.
    transform.scale = EditorVector3{2.0f, 2.0f, 1.0f};
    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 32, 32, EditorVector2{163.0f, 200.0f}).x, 0);
    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 32, 32, EditorVector2{165.0f, 200.0f}).x, 1);

    // A zero tile size would divide by zero, and a hand-edited scene can hold one.
    CNA_EDITOR_EXPECT_EQ(worldToTile(transform, 0, 32, EditorVector2{100.0f, 200.0f}).x, -1);
}

CNA_EDITOR_TEST(APaintStrokeIsOneUndoEntryAndTwoStrokesAreTwo)
{
    TilemapScene fixture;
    CommandHistory history;

    const auto paint = [&](int x, int y, std::int64_t tile, std::uint64_t stroke, bool first) {
        auto command = std::make_unique<PaintTilesCommand>(fixture.scene, fixture.registry,
                                                            fixture.entityId, stroke);
        if (!command->paint(x, y, tile)) { return; }
        history.execute(std::move(command),
                        first ? MergePolicy::NewEntry : MergePolicy::MergeWithPrevious);
    };

    // One drag across three cells.
    paint(0, 0, 5, 1, true);
    paint(1, 0, 5, 1, false);
    paint(2, 0, 5, 1, false);

    CNA_EDITOR_EXPECT_EQ(history.getCount(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(1, 0), std::int64_t{5});

    // A second drag, on a different stroke, must not fold into the first -- one Ctrl+Z would then
    // lose both, and the merge key of entity + property alone cannot tell them apart.
    paint(0, 1, 6, 2, true);
    paint(1, 1, 6, 2, false);
    CNA_EDITOR_EXPECT_EQ(history.getCount(), std::size_t{2});

    history.undo();
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(0, 1), kEmptyTile);
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(0, 0), std::int64_t{5});

    history.undo();
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(0, 0), kEmptyTile);
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(2, 0), kEmptyTile);

    history.redo();
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(2, 0), std::int64_t{5});
}

CNA_EDITOR_TEST(PaintingIgnoresNoOpCellsAndRemembersTheValueTheStrokeStartedFrom)
{
    TilemapScene fixture;

    PaintTilesCommand first{fixture.scene, fixture.registry, fixture.entityId, 1};
    CNA_EDITOR_EXPECT(first.paint(0, 0, 3));
    first.execute();
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(0, 0), std::int64_t{3});

    // Painting a cell the value it already holds records nothing: an undo entry that changes
    // nothing is one the user cannot see the effect of.
    PaintTilesCommand same{fixture.scene, fixture.registry, fixture.entityId, 2};
    CNA_EDITOR_EXPECT(!same.paint(0, 0, 3));
    CNA_EDITOR_EXPECT(!same.isValid());

    // Nor does a cell outside the grid.
    CNA_EDITOR_EXPECT(!same.paint(99, 99, 4));

    // Crossing the same cell twice within one stroke keeps the value it had *before* the stroke,
    // so one Ctrl+Z goes back to before the drag rather than to the middle of it.
    PaintTilesCommand wobble{fixture.scene, fixture.registry, fixture.entityId, 3};
    CNA_EDITOR_EXPECT(wobble.paint(0, 0, 7));
    CNA_EDITOR_EXPECT(wobble.paint(0, 0, 8));
    CNA_EDITOR_EXPECT_EQ(wobble.getCells().size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(wobble.getCells().front().oldTile, std::int64_t{3});

    wobble.execute();
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(0, 0), std::int64_t{8});
    wobble.undo();
    CNA_EDITOR_EXPECT_EQ(fixture.grid().at(0, 0), std::int64_t{3});
}

CNA_EDITOR_TEST(ATilemapRoundTripsThroughASceneFile)
{
    TilemapScene fixture;

    PaintTilesCommand stroke{fixture.scene, fixture.registry, fixture.entityId, 1};
    stroke.paint(0, 0, 1);
    stroke.paint(3, 2, 2);
    stroke.execute();

    SceneDocument reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(fixture.scene.toJson(), fixture.registry).succeeded);

    const EditorComponent* tilemap =
        reloaded.getEntities().front().findComponent(BuiltinComponentIds::kTilemap);
    CNA_EDITOR_EXPECT(tilemap != nullptr);

    const TilemapGrid grid =
        readTilemapGrid(*tilemap, fixture.registry.find(BuiltinComponentIds::kTilemap));
    CNA_EDITOR_EXPECT_EQ(grid.at(0, 0), std::int64_t{1});
    CNA_EDITOR_EXPECT_EQ(grid.at(3, 2), std::int64_t{2});
    CNA_EDITOR_EXPECT_EQ(grid.at(1, 1), kEmptyTile);

    // No new serialised structure: the grid is an ordinary List property, so a scene holding a
    // tilemap is readable by anything that could already read a scene.
    CNA_EDITOR_EXPECT(fixture.scene.toJson()["entities"]
                          .getElements()
                          .front()["components"]["CNA.Tilemap"]["tiles"]
                          .isArray());
}

// --------------------------------------------------------------------------------------------
// Sprite animation (plan.md ED-303)
// --------------------------------------------------------------------------------------------

namespace
{
    /** @brief A four-frame clip at 10 fps, frames 0..3 of an 8-wide sheet of 32px cells. */
    SpriteAnimationClip makeClip(bool loop = true)
    {
        SpriteAnimationClip clip;
        clip.frames = {0, 1, 2, 3};
        clip.frameWidth = 32;
        clip.frameHeight = 32;
        clip.sheetColumns = 8;
        clip.framesPerSecond = 10.0f;
        clip.loop = loop;
        return clip;
    }
}

CNA_EDITOR_TEST(AClipTurnsAFrameIndexIntoASourceRectangle)
{
    SpriteAnimationClip clip = makeClip();
    clip.frames = {0, 7, 8, 15};

    CNA_EDITOR_EXPECT_EQ(clip.getFrameRectangle(0).x, 0);
    CNA_EDITOR_EXPECT_EQ(clip.getFrameRectangle(0).y, 0);

    // The last cell of the first row, then the first of the second: the wrap is what makes an
    // index cheaper to author than a rectangle.
    CNA_EDITOR_EXPECT_EQ(clip.getFrameRectangle(1).x, 224);
    CNA_EDITOR_EXPECT_EQ(clip.getFrameRectangle(1).y, 0);
    CNA_EDITOR_EXPECT_EQ(clip.getFrameRectangle(2).x, 0);
    CNA_EDITOR_EXPECT_EQ(clip.getFrameRectangle(2).y, 32);
    CNA_EDITOR_EXPECT_EQ(clip.getFrameRectangle(3).y, 32);

    // Out of range draws nothing rather than clamping: a caller that has lost track of where it is
    // should show that, not silently show frame zero.
    CNA_EDITOR_EXPECT(clip.getFrameRectangle(99).isEmpty());

    CNA_EDITOR_EXPECT_EQ(clip.getFrameCount(), std::size_t{4});
    CNA_EDITOR_EXPECT_EQ(clip.getDuration(), 0.4f);
}

CNA_EDITOR_TEST(PlaybackAdvancesOnTheClockItIsGivenAndCatchesUp)
{
    const SpriteAnimationClip clip = makeClip();
    AnimationPlayback playback;

    // Not playing: the clock passes and nothing moves.
    CNA_EDITOR_EXPECT(!playback.advance(clip, 1.0f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{0});

    playback.playing = true;
    CNA_EDITOR_EXPECT(!playback.advance(clip, 0.05f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{0});

    CNA_EDITOR_EXPECT(playback.advance(clip, 0.05f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{1});

    // A long stall covers the frames it spans rather than crawling back one at a time: at 10 fps a
    // quarter-second gap is two and a half frames.
    CNA_EDITOR_EXPECT(playback.advance(clip, 0.25f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{3});

    // Looping wraps and keeps playing.
    CNA_EDITOR_EXPECT(playback.advance(clip, 0.1f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{0});
    CNA_EDITOR_EXPECT(playback.playing);
}

CNA_EDITOR_TEST(ANonLoopingClipHoldsItsLastFrameAndStops)
{
    const SpriteAnimationClip clip = makeClip(false);
    AnimationPlayback playback;
    playback.playing = true;

    playback.advance(clip, 1.0f);

    // Held, not wrapped, and stopped -- a non-looping clip that quietly restarted would be
    // indistinguishable from a looping one.
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{3});
    CNA_EDITOR_EXPECT(!playback.playing);
}

CNA_EDITOR_TEST(SteppingWrapsBothWaysAndStopsPlayback)
{
    const SpriteAnimationClip clip = makeClip();
    AnimationPlayback playback;
    playback.playing = true;

    playback.step(clip, 1);
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{1});

    // A step is a deliberate look at one frame, so it stops playback rather than fighting it.
    CNA_EDITOR_EXPECT(!playback.playing);

    playback.step(clip, -1);
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{0});
    playback.step(clip, -1);
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{3});

    // A frame list shortened while the preview was past its new end is clamped, not left dangling.
    SpriteAnimationClip shorter = clip;
    shorter.frames = {0, 1};
    playback.clampTo(shorter);
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{1});

    SpriteAnimationClip empty = clip;
    empty.frames.clear();
    playback.clampTo(empty);
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{0});
}

CNA_EDITOR_TEST(AnAnimationClipRoundTripsThroughAComponent)
{
    const ComponentRegistry registry = makeRegistry();

    EditorEntity entity = makeEntity(registry, "Hero", 0.0f, 0.0f);
    EditorComponent& component =
        addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteAnimation);

    PropertyValue::ListValue frames;
    frames.items.emplace_back(std::int64_t{4});
    frames.items.emplace_back(std::int64_t{5});
    component.setProperty(SpriteAnimationKeys::kFrames, PropertyValue{frames});
    component.setProperty(SpriteAnimationKeys::kFramesPerSecond, PropertyValue{24.0f});

    SceneDocument scene;
    scene.addEntity(std::move(entity));

    SceneDocument reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(scene.toJson(), registry).succeeded);

    const EditorComponent* readBack =
        reloaded.getEntities().front().findComponent(BuiltinComponentIds::kSpriteAnimation);
    CNA_EDITOR_EXPECT(readBack != nullptr);

    const SpriteAnimationClip clip =
        readSpriteAnimationClip(*readBack, registry.find(BuiltinComponentIds::kSpriteAnimation));
    CNA_EDITOR_EXPECT_EQ(clip.frames.size(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(clip.frames.front(), std::int64_t{4});
    CNA_EDITOR_EXPECT_EQ(clip.framesPerSecond, 24.0f);
    CNA_EDITOR_EXPECT(clip.loop);
}

CNA_EDITOR_TEST(PerFrameDurationsAreOptionalAndIgnoredWhenTheyDoNotFit)
{
    SpriteAnimationClip clip = makeClip();

    // No durations: the uniform rate applies, which is what every scene written before this
    // existed relies on.
    CNA_EDITOR_EXPECT(!clip.hasFrameDurations());
    CNA_EDITOR_EXPECT_EQ(clip.getFrameDuration(0), 0.1f);
    CNA_EDITOR_EXPECT_EQ(clip.getDuration(), 0.4f);

    // A list of the wrong length is ignored rather than partly applied: half a clip at one rate
    // and half at another is a state nobody asked for.
    clip.frameDurations = {0.5f, 0.5f};
    CNA_EDITOR_EXPECT(!clip.hasFrameDurations());
    CNA_EDITOR_EXPECT_EQ(clip.getFrameDuration(0), 0.1f);

    clip.frameDurations = {0.5f, 0.1f, 0.1f, 0.1f};
    CNA_EDITOR_EXPECT(clip.hasFrameDurations());
    CNA_EDITOR_EXPECT_EQ(clip.getFrameDuration(0), 0.5f);
    CNA_EDITOR_EXPECT_EQ(clip.getFrameDuration(1), 0.1f);
    // Summed rather than multiplied, so the comparison has to tolerate the last bit.
    CNA_EDITOR_EXPECT(std::fabs(clip.getDuration() - 0.8f) < 1e-5f);

    // A zero entry falls back to the uniform rate rather than becoming a frame of no length, which
    // would spin the playback loop forever looking for the next one.
    clip.frameDurations[2] = 0.0f;
    CNA_EDITOR_EXPECT_EQ(clip.getFrameDuration(2), 0.1f);
}

CNA_EDITOR_TEST(PlaybackHoldsEachFrameForItsOwnDuration)
{
    SpriteAnimationClip clip = makeClip();
    clip.frameDurations = {0.5f, 0.1f, 0.1f, 0.1f};

    AnimationPlayback playback;
    playback.playing = true;

    // The long first frame is held through what would have been four uniform frames.
    CNA_EDITOR_EXPECT(!playback.advance(clip, 0.4f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{0});

    CNA_EDITOR_EXPECT(playback.advance(clip, 0.1f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{1});

    // Then the short ones go by three times as fast.
    CNA_EDITOR_EXPECT(playback.advance(clip, 0.2f));
    CNA_EDITOR_EXPECT_EQ(playback.position, std::size_t{3});

    // A rate of zero with no durations cannot play, and says so by not moving rather than by
    // dividing by it.
    SpriteAnimationClip stopped = makeClip();
    stopped.framesPerSecond = 0.0f;
    AnimationPlayback stuck;
    stuck.playing = true;
    CNA_EDITOR_EXPECT(!stuck.advance(stopped, 10.0f));
    CNA_EDITOR_EXPECT_EQ(stuck.position, std::size_t{0});
}

CNA_EDITOR_TEST(AClipWithoutDurationsSerialisesExactlyAsItDidBefore)
{
    const ComponentRegistry registry = makeRegistry();

    EditorEntity entity = makeEntity(registry, "Hero", 0.0f, 0.0f);
    EditorComponent& component =
        addComponentWithDefaults(entity, registry, BuiltinComponentIds::kSpriteAnimation);

    PropertyValue::ListValue frames;
    frames.items.emplace_back(std::int64_t{0});
    frames.items.emplace_back(std::int64_t{1});
    component.setProperty(SpriteAnimationKeys::kFrames, PropertyValue{frames});

    SceneDocument scene;
    scene.addEntity(std::move(entity));

    SceneDocument reloaded;
    CNA_EDITOR_EXPECT(reloaded.loadFromJson(scene.toJson(), registry).succeeded);

    const SpriteAnimationClip clip = readSpriteAnimationClip(
        *reloaded.getEntities().front().findComponent(BuiltinComponentIds::kSpriteAnimation),
        registry.find(BuiltinComponentIds::kSpriteAnimation));

    // The default is an empty list, which round-trips as an empty list and changes nothing about
    // how the clip plays.
    CNA_EDITOR_EXPECT(!clip.hasFrameDurations());
    CNA_EDITOR_EXPECT_EQ(clip.getFrameDuration(0), clip.getFrameDuration(1));
}

CNA_EDITOR_TEST(TwoEnabledAudioListenersAreAnError)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity first = makeEntity(registry, "Player", 0.0f, 0.0f);
    addComponentWithDefaults(first, registry, BuiltinComponentIds::kAudioListener);
    scene.addEntity(std::move(first));

    // One listener is the ordinary case and says nothing.
    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "duplicate-audio-listener"),
                         std::size_t{0});

    EditorEntity second = makeEntity(registry, "Camera Rig", 0.0f, 0.0f);
    addComponentWithDefaults(second, registry, BuiltinComponentIds::kAudioListener);
    scene.addEntity(std::move(second));

    // XNA mixes 3D audio relative to one listener. Two is not louder or wider -- it is a choice
    // the runtime makes for the user, exactly as invisible as two primary cameras.
    const std::vector<SceneIssue> issues = validateScene(scene, registry);
    CNA_EDITOR_EXPECT_EQ(countRule(issues, "duplicate-audio-listener"), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(countIssues(issues, SceneIssue::Severity::Error), std::size_t{2});

    // Switching one off resolves it, the same way it resolves a second primary camera.
    scene.findEntity(scene.getEntities().back().getId())->setEnabled(false);
    CNA_EDITOR_EXPECT_EQ(countRule(validateScene(scene, registry), "duplicate-audio-listener"),
                         std::size_t{0});
}

namespace
{
    /** @brief Returns true when @p a and @p b agree to within @p tolerance. */
    bool cameraNearlyEqual(float a, float b, float tolerance = 0.01f) { return std::abs(a - b) <= tolerance; }

    /** @brief Fails unless @p actual matches @p expected on every axis. */
    void expectVectorEquals(const EditorVector3& actual, const EditorVector3& expected)
    {
        CNA_EDITOR_EXPECT(cameraNearlyEqual(actual.x, expected.x));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(actual.y, expected.y));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(actual.z, expected.z));
    }

    /** @brief A camera over a 1600x900 viewport, looking at the origin from ten units away. */
    EditorCamera3D makeCamera()
    {
        EditorCamera3D camera;
        camera.setViewportSize(EditorVector2{1600.0f, 900.0f});
        return camera;
    }
}

CNA_EDITOR_TEST(TheEyeIsDerivedFromThePivotDistanceAndAngles)
{
    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{5.0f, 1.0f, -2.0f});
    camera.setDistance(20.0f);
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);

    // Yaw zero, pitch zero: looking down -Z, so the eye is twenty units along +Z from the pivot.
    const EditorVector3 eye = camera.getEye();
    CNA_EDITOR_EXPECT(cameraNearlyEqual(eye.x, 5.0f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(eye.y, 1.0f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(eye.z, 18.0f));

    // Positive pitch looks down on screen, which is what dragging downwards on an orbit has to do
    // -- and in this camera's Y-down world, downward on screen is towards +Y, so the eye sits at
    // the *smaller* Y. Both of these read backwards against XNA's 3D convention and are correct
    // against the one the document and the 2D viewport actually use.
    camera.setPitch(0.5f);
    CNA_EDITOR_EXPECT(camera.getForward().y > 0.0f);
    CNA_EDITOR_EXPECT(camera.getEye().y < camera.getPivot().y);

    // The basis stays orthonormal whatever the angles, or every projection built on it shears.
    const EditorVector3 right = camera.getRight();
    const EditorVector3 up = camera.getUp();
    const EditorVector3 forward = camera.getForward();
    CNA_EDITOR_EXPECT(cameraNearlyEqual(length(right), 1.0f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(length(up), 1.0f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(dot(right, up), 0.0f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(dot(right, forward), 0.0f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(dot(up, forward), 0.0f));
}

CNA_EDITOR_TEST(PitchIsClampedJustShortOfVertical)
{
    EditorCamera3D camera = makeCamera();

    // Straight down is where the up vector and the view direction become parallel and the view
    // matrix stops being defined. Clamping short of it is cheaper than handling it everywhere.
    camera.orbit(0.0f, 100.0f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getPitch(), EditorCamera3D::kMaxPitchRadians));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(length(camera.getRight()), 1.0f));

    camera.orbit(0.0f, -100.0f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getPitch(), -EditorCamera3D::kMaxPitchRadians));

    // Yaw wraps rather than clamping: it has no ends, and letting it grow costs precision.
    camera.setYaw(0.0f);
    camera.orbit(100.0f, 0.0f);
    CNA_EDITOR_EXPECT(std::abs(camera.getYaw()) <= 3.1416f);
}

CNA_EDITOR_TEST(ThePivotProjectsToTheCentreOfTheViewport)
{
    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{3.0f, -4.0f, 12.0f});
    camera.orbit(0.7f, -0.2f);

    const std::optional<EditorVector2> centre = camera.worldToScreen(camera.getPivot());
    CNA_EDITOR_EXPECT(centre.has_value());
    CNA_EDITOR_EXPECT(cameraNearlyEqual(centre->x, 800.0f, 0.5f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(centre->y, 450.0f, 0.5f));

    // Behind the eye there is no answer. Returning one would send a line across the screen the
    // moment a vertex passed the camera, which is the classic wireframe artefact.
    const EditorVector3 behind = add(camera.getEye(), scale(camera.getForward(), -5.0f));
    CNA_EDITOR_EXPECT(!camera.worldToScreen(behind).has_value());

    // Up on screen is up in the world: a point above the pivot lands above the centre, where
    // screen Y is smaller. The Y flip between clip space and the panel is easy to lose.
    const std::optional<EditorVector2> above =
        camera.worldToScreen(add(camera.getPivot(), scale(camera.getUp(), 1.0f)));
    CNA_EDITOR_EXPECT(above.has_value());
    CNA_EDITOR_EXPECT(above->y < centre->y);
}

CNA_EDITOR_TEST(ARayThroughAPixelComesBackToThatPixel)
{
    for (const CameraProjection projection : {CameraProjection::Perspective, CameraProjection::Orthographic})
    {
        EditorCamera3D camera = makeCamera();
        camera.setProjection(projection);
        camera.setPivot(EditorVector3{2.0f, 3.0f, -1.0f});
        camera.orbit(0.5f, 0.3f);

        const EditorVector2 pixel{1180.0f, 260.0f};
        const WorldRay ray = camera.screenToRay(pixel);

        CNA_EDITOR_EXPECT(cameraNearlyEqual(length(ray.direction), 1.0f));

        // Round trip: a point along the ray must project back to the pixel it came from. This is
        // the property picking depends on, and it is the one an inverted projection gets wrong.
        const std::optional<EditorVector2> back = camera.worldToScreen(ray.at(25.0f));
        CNA_EDITOR_EXPECT(back.has_value());
        CNA_EDITOR_EXPECT(cameraNearlyEqual(back->x, pixel.x, 0.5f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(back->y, pixel.y, 0.5f));
    }

    // An orthographic ray starts wherever the pixel is, not at the eye -- which is why the ray is
    // unprojected at both depth limits rather than fired from the camera position.
    EditorCamera3D orthographic = makeCamera();
    orthographic.setProjection(CameraProjection::Orthographic);
    const WorldRay corner = orthographic.screenToRay(EditorVector2{0.0f, 0.0f});
    const WorldRay middle = orthographic.screenToRay(EditorVector2{800.0f, 450.0f});
    CNA_EDITOR_EXPECT(length(subtract(corner.origin, middle.origin)) > 1.0f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(dot(corner.direction, middle.direction), 1.0f));
}

CNA_EDITOR_TEST(OrbitTurnsAboutThePivotAndFlyingCarriesItAlong)
{
    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{1.0f, 2.0f, 3.0f});
    camera.setDistance(15.0f);

    const EditorVector3 pivotBefore = camera.getPivot();
    const EditorVector3 eyeBefore = camera.getEye();

    camera.orbit(0.9f, 0.1f);
    expectVectorEquals(camera.getPivot(), pivotBefore);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getDistance(), 15.0f));
    CNA_EDITOR_EXPECT(length(subtract(camera.getEye(), eyeBefore)) > 1.0f);

    // Looking is the mirror image: the eye stays put and the pivot swings round in front of it,
    // so that a following orbit turns about what the user is now looking at.
    const EditorVector3 eyeBeforeLook = camera.getEye();
    camera.look(0.4f, -0.2f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getEye().x, eyeBeforeLook.x));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getEye().y, eyeBeforeLook.y));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getEye().z, eyeBeforeLook.z));
    CNA_EDITOR_EXPECT(length(subtract(camera.getPivot(), pivotBefore)) > 0.5f);

    // Flying forward closes the gap to whatever is ahead without changing the orbit radius.
    const EditorVector3 forward = camera.getForward();
    const EditorVector3 eyeBeforeMove = camera.getEye();
    camera.moveLocal(EditorVector3{0.0f, 0.0f, 4.0f});
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getDistance(), 15.0f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(length(subtract(camera.getEye(), eyeBeforeMove)), 4.0f));
    CNA_EDITOR_EXPECT(dot(subtract(camera.getEye(), eyeBeforeMove), forward) > 0.0f);
}

CNA_EDITOR_TEST(PanningKeepsThePivotUnderTheCursor)
{
    for (const CameraProjection projection : {CameraProjection::Perspective, CameraProjection::Orthographic})
    {
        EditorCamera3D camera = makeCamera();
        camera.setProjection(projection);
        camera.orbit(0.6f, 0.35f);

        const EditorVector3 pivotBefore = camera.getPivot();
        const float yawBefore = camera.getYaw();
        const float pitchBefore = camera.getPitch();
        camera.panByScreenDelta(EditorVector2{120.0f, -45.0f});

        // A drag moves the world with the cursor: the point that was at the centre is now exactly
        // as far from it as the cursor travelled. Anything else is drift, which is the whole
        // reason the delta is taken in pixels rather than converted by the caller.
        const std::optional<EditorVector2> moved = camera.worldToScreen(pivotBefore);
        CNA_EDITOR_EXPECT(moved.has_value());
        CNA_EDITOR_EXPECT(cameraNearlyEqual(moved->x, 800.0f + 120.0f, 0.5f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(moved->y, 450.0f - 45.0f, 0.5f));

        // Panning slides the camera; it never turns it.
        CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getYaw(), yawBefore));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getPitch(), pitchBefore));
    }
}

CNA_EDITOR_TEST(DollyingScalesTheDistanceAndStopsAtTheLimits)
{
    EditorCamera3D camera = makeCamera();
    camera.setDistance(10.0f);

    camera.dolly(0.5f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getDistance(), 5.0f));

    // Multiplying rather than subtracting is what makes one wheel notch feel the same close up
    // and far away -- a fixed step crawls across a level and lands inside a model.
    camera.dolly(2.0f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getDistance(), 10.0f));

    for (int step = 0; step < 200; ++step) { camera.dolly(0.5f); }
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getDistance(), EditorCamera3D::kMinDistance, 0.001f));

    // A factor of zero or less would put the eye on the pivot or behind it; it is refused rather
    // than clamped, because there is no sensible interpretation of a negative zoom.
    camera.setDistance(10.0f);
    camera.dolly(0.0f);
    camera.dolly(-1.0f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(camera.getDistance(), 10.0f));
}

CNA_EDITOR_TEST(FramingFitsTheWholeBoxOnScreen)
{
    for (const CameraProjection projection : {CameraProjection::Perspective, CameraProjection::Orthographic})
    {
        EditorCamera3D camera = makeCamera();
        camera.setProjection(projection);
        camera.orbit(0.8f, 0.4f);

        const WorldBounds3D bounds{EditorVector3{-30.0f, 10.0f, -5.0f}, EditorVector3{50.0f, 40.0f, 25.0f}};
        camera.frame(bounds);

        expectVectorEquals(camera.getPivot(), bounds.getCenter());

        // Every corner has to land inside the panel, not merely the centre: framing that fits the
        // middle of a box and clips its ends is the failure this is here to catch.
        for (int corner = 0; corner < 8; ++corner)
        {
            const EditorVector3 point{(corner & 1) != 0 ? bounds.max.x : bounds.min.x,
                                      (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
                                      (corner & 4) != 0 ? bounds.max.z : bounds.min.z};
            const std::optional<EditorVector2> screen = camera.worldToScreen(point);
            CNA_EDITOR_EXPECT(screen.has_value());
            if (!screen) { continue; }
            CNA_EDITOR_EXPECT(screen->x >= 0.0f && screen->x <= 1600.0f);
            CNA_EDITOR_EXPECT(screen->y >= 0.0f && screen->y <= 900.0f);
        }
    }

    // An empty box is not a request to look at nothing: it is a caller with no selection, and the
    // camera it already has is a better answer than an arbitrary one.
    EditorCamera3D unchanged = makeCamera();
    unchanged.setPivot(EditorVector3{7.0f, 7.0f, 7.0f});
    unchanged.frame(WorldBounds3D::makeEmpty());
    expectVectorEquals(unchanged.getPivot(), EditorVector3{7.0f, 7.0f, 7.0f});
}

CNA_EDITOR_TEST(SwitchingProjectionKeepsTheSubjectTheSameSize)
{
    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{0.0f, 0.0f, 0.0f});
    camera.setDistance(30.0f);
    camera.orbit(0.0f, 0.0f);

    // The orthographic height is the perspective extent *at the pivot*, so a toggle is a change of
    // projection rather than a jump cut -- which is the point of having the toggle at all.
    const EditorVector3 sample = add(camera.getPivot(), scale(camera.getUp(), 4.0f));

    const std::optional<EditorVector2> inPerspective = camera.worldToScreen(sample);
    camera.setProjection(CameraProjection::Orthographic);
    const std::optional<EditorVector2> inOrthographic = camera.worldToScreen(sample);

    CNA_EDITOR_EXPECT(inPerspective.has_value() && inOrthographic.has_value());
    CNA_EDITOR_EXPECT(cameraNearlyEqual(inPerspective->y, inOrthographic->y, 1.0f));

    // An orthographic camera can see what is beside it, so its near plane sits behind the eye:
    // clipping at the eye would hide everything the user just dollied towards.
    const EditorVector3 besideTheEye = add(camera.getEye(), scale(camera.getRight(), 3.0f));
    CNA_EDITOR_EXPECT(camera.worldToScreen(besideTheEye).has_value());
}

CNA_EDITOR_TEST(SceneBoundsCoverEntitiesThatDrawNothing)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid spriteId = scene.addEntity(makeEntity(registry, "Sprite", 100.0f, 0.0f));
    EditorComponent renderer{BuiltinComponentIds::kSpriteRenderer};
    renderer.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    scene.findEntity(spriteId)->addComponent(std::move(renderer));

    // A bare Transform -- a camera, a light, an empty parent. The 2D viewport leaves these out of
    // framing because it draws them as fixed-size icons, but in a 3D view they are often the only
    // thing a scene contains, and "nothing to look at" would be the wrong answer.
    const Uuid emptyId = scene.addEntity(makeEntity(registry, "Spawn Point", -400.0f, 0.0f));

    const SpriteSizeProvider noSizes = [](const Uuid&) { return EditorVector2{32.0f, 32.0f}; };

    const std::optional<WorldBounds3D> empty = computeEntityBounds3D(scene, emptyId, noSizes);
    CNA_EDITOR_EXPECT(empty.has_value());
    CNA_EDITOR_EXPECT(empty->contains(EditorVector3{-400.0f, 0.0f, 0.0f}));

    const std::optional<WorldBounds3D> whole = computeSceneBounds3D(scene, noSizes);
    CNA_EDITOR_EXPECT(whole.has_value());
    CNA_EDITOR_EXPECT(whole->min.x <= -400.0f);
    CNA_EDITOR_EXPECT(whole->max.x >= 100.0f);

    // An unknown entity has no bounds, rather than bounds at the origin that would drag every
    // union towards it.
    CNA_EDITOR_EXPECT(!computeEntityBounds3D(scene, Uuid::generate(), noSizes).has_value());
}

CNA_EDITOR_TEST(ASegmentCrossingTheNearPlaneIsShortenedRatherThanDropped)
{
    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(10.0f);

    // The eye is at (0, 0, 10) looking down -Z. A line running from well behind the camera to well
    // in front of it is the grid line under the user's feet: dropping it whole leaves a wedge of
    // missing floor exactly where they are looking.
    const std::optional<std::pair<EditorVector2, EditorVector2>> crossing =
        projectSegment(camera, EditorVector3{0.0f, -5.0f, 40.0f}, EditorVector3{0.0f, -5.0f, -40.0f});
    CNA_EDITOR_EXPECT(crossing.has_value());

    // Entirely behind the eye there is nothing to draw, and the projected coordinates of such a
    // segment are mirrored through the origin -- plausible numbers describing the wrong line.
    CNA_EDITOR_EXPECT(!projectSegment(camera, EditorVector3{0.0f, 0.0f, 20.0f},
                                      EditorVector3{0.0f, 0.0f, 15.0f})
                           .has_value());

    // Entirely in front, nothing is clipped and both ends survive as they are.
    const std::optional<std::pair<EditorVector2, EditorVector2>> ahead =
        projectSegment(camera, EditorVector3{-5.0f, 0.0f, -10.0f}, EditorVector3{5.0f, 0.0f, -10.0f});
    CNA_EDITOR_EXPECT(ahead.has_value());
    if (ahead) { CNA_EDITOR_EXPECT(ahead->first.x < ahead->second.x); }
}

CNA_EDITOR_TEST(TheSceneGridIsCentredOnThePivotAndMarksTheAxes)
{
    EditorCamera3D camera = makeCamera();
    camera.setPitch(0.6f);

    WireframeOptions options;
    options.gridSpacing = 10.0f;
    options.gridHalfExtent = 5;

    const std::vector<WireSegment> atOrigin = buildSceneGrid(camera, options);

    // Eleven lines each way, minus whatever the near plane took. The axes must be among them, or
    // the view has no landmark at all: an orbiting camera with no origin marker is disorienting
    // in a way no amount of grid is.
    CNA_EDITOR_EXPECT(!atOrigin.empty());
    const auto hasColor = [&atOrigin](const EditorColor& color) {
        return std::any_of(atOrigin.begin(), atOrigin.end(),
                           [&color](const WireSegment& segment) { return segment.color == color; });
    };
    CNA_EDITOR_EXPECT(hasColor(WireColors::kAxisX));
    CNA_EDITOR_EXPECT(hasColor(WireColors::kAxisY));

    // Flying away carries the grid: snapped to the spacing, so the lines do not shimmer, but
    // centred on the pivot, so a level laid out far from the origin still has a floor.
    camera.setPivot(EditorVector3{1000.0f, 1000.0f, 0.0f});
    const std::vector<WireSegment> farAway = buildSceneGrid(camera, options);
    CNA_EDITOR_EXPECT(!farAway.empty());

    // No spacing given means one is chosen from the camera's distance, exactly as the 2D grid
    // chooses one from its zoom -- and it must be a round number a user can read coordinates off.
    WireframeOptions automatic;
    automatic.gridHalfExtent = 4;
    camera.setPivot(EditorVector3{});
    CNA_EDITOR_EXPECT(!buildSceneGrid(camera, automatic).empty());
}

CNA_EDITOR_TEST(TheWireframeBoxesEveryEntityAndMarksTheSelection)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid firstId = scene.addEntity(makeEntity(registry, "First", 0.0f, 0.0f));
    const Uuid secondId = scene.addEntity(makeEntity(registry, "Second", 60.0f, 0.0f));
    const Uuid disabledId = scene.addEntity(makeEntity(registry, "Disabled", -60.0f, 0.0f));
    scene.findEntity(disabledId)->setEnabled(false);

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setDistance(400.0f);
    camera.setPitch(0.5f);

    const SpriteSizeProvider sizes = [](const Uuid&) { return EditorVector2{32.0f, 32.0f}; };

    WireframeOptions options;
    options.drawGrid = false;

    const WireframeResult result = buildSceneWireframe(scene, camera, {secondId}, sizes, options);

    // Two enabled entities, twelve edges each. A disabled entity is not drawn, for the same reason
    // it gets no icon in the 2D viewport: it is not part of the running game.
    CNA_EDITOR_EXPECT_EQ(result.entitiesDrawn, std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(result.segments.size(), std::size_t{24});
    CNA_EDITOR_EXPECT(!result.truncated);

    const std::size_t selected =
        static_cast<std::size_t>(std::count_if(result.segments.begin(), result.segments.end(),
                                               [](const WireSegment& segment) {
                                                   return segment.color == WireColors::kSelected;
                                               }));
    CNA_EDITOR_EXPECT_EQ(selected, std::size_t{12});
    static_cast<void>(firstId);

    // The ceiling is announced rather than reached quietly, or a half-drawn scene reads as a scene
    // with half its entities missing.
    WireframeOptions tiny = options;
    tiny.maxSegments = 5;
    CNA_EDITOR_EXPECT(buildSceneWireframe(scene, camera, {}, sizes, tiny).truncated);
}

CNA_EDITOR_TEST(PickingInThreeDimensionsTakesTheNearestBox)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    // Two entities one behind the other along the view direction. The near one has to win --
    // "nearest" rather than the 2D viewport's "topmost", because depth is a real quantity here
    // and layer order is not.
    const Uuid nearId = scene.addEntity(makeEntity(registry, "Near", 0.0f, 0.0f));
    scene.findEntity(nearId)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("position", PropertyValue{EditorVector3{0.0f, 0.0f, 40.0f}});

    const Uuid farId = scene.addEntity(makeEntity(registry, "Far", 0.0f, 0.0f));
    scene.findEntity(farId)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("position", PropertyValue{EditorVector3{0.0f, 0.0f, -40.0f}});

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(200.0f);

    const SpriteSizeProvider sizes = [](const Uuid&) { return EditorVector2{32.0f, 32.0f}; };
    const EditorVector2 centre{800.0f, 450.0f};

    CNA_EDITOR_EXPECT(pickEntityAt3D(scene, camera, centre, sizes) == nearId);

    // Turned around, the other one is nearest. Nothing about the entities changed.
    camera.setYaw(3.14159265f);
    CNA_EDITOR_EXPECT(pickEntityAt3D(scene, camera, centre, sizes) == farId);

    // A ray into empty space hits nothing, and says so with the nil Uuid rather than the last
    // entity it happened to test.
    CNA_EDITOR_EXPECT(!pickEntityAt3D(scene, camera, EditorVector2{5.0f, 5.0f}, sizes).isValid());
}

CNA_EDITOR_TEST(TheSlabTestHandlesFlatBoxesAndRaysStartingInside)
{
    const WorldBounds3D box{EditorVector3{-1.0f, -1.0f, -1.0f}, EditorVector3{1.0f, 1.0f, 1.0f}};

    const WorldRay towards{EditorVector3{0.0f, 0.0f, 10.0f}, EditorVector3{0.0f, 0.0f, -1.0f}};
    const std::optional<float> hit = intersectRayWithBounds(towards, box);
    CNA_EDITOR_EXPECT(hit.has_value());
    CNA_EDITOR_EXPECT(cameraNearlyEqual(*hit, 9.0f));

    // Behind the ray is a miss, not a hit at a negative distance -- which would let a click select
    // whatever happened to be behind the camera.
    const WorldRay away{EditorVector3{0.0f, 0.0f, 10.0f}, EditorVector3{0.0f, 0.0f, 1.0f}};
    CNA_EDITOR_EXPECT(!intersectRayWithBounds(away, box).has_value());

    // Starting inside is a hit at zero distance, not a miss.
    const WorldRay inside{EditorVector3{}, EditorVector3{1.0f, 0.0f, 0.0f}};
    const std::optional<float> fromInside = intersectRayWithBounds(inside, box);
    CNA_EDITOR_EXPECT(fromInside.has_value());
    CNA_EDITOR_EXPECT(cameraNearlyEqual(*fromInside, 0.0f));

    // A sprite is a box with no thickness, so the axis-parallel case is the common one rather
    // than the exotic one: get it wrong and no sprite can be clicked from the side.
    const WorldBounds3D flat{EditorVector3{-1.0f, -1.0f, 0.0f}, EditorVector3{1.0f, 1.0f, 0.0f}};
    CNA_EDITOR_EXPECT(intersectRayWithBounds(towards, flat).has_value());

    const WorldRay parallel{EditorVector3{0.0f, 0.0f, 5.0f}, EditorVector3{1.0f, 0.0f, 0.0f}};
    CNA_EDITOR_EXPECT(!intersectRayWithBounds(parallel, flat).has_value());
}

CNA_EDITOR_TEST(TheThreeDimensionalGizmoIsSizedInPixelsAndGrabbedInPixels)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid entityId = scene.addEntity(makeEntity(registry, "Crate", 0.0f, 0.0f));

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.6f);
    camera.setPitch(0.4f);
    camera.setDistance(100.0f);

    const std::optional<TranslateGizmo3DLayout> near =
        computeTranslateGizmo3DLayout(scene, camera, entityId);
    CNA_EDITOR_EXPECT(near.has_value());
    if (!near) { return; }

    // Twice as far away, the arms are twice as long in world units -- which is what keeps them the
    // same length on screen, the property that makes them grabbable at any distance.
    camera.setDistance(200.0f);
    const std::optional<TranslateGizmo3DLayout> far =
        computeTranslateGizmo3DLayout(scene, camera, entityId);
    CNA_EDITOR_EXPECT(far.has_value());
    if (!far) { return; }

    CNA_EDITOR_EXPECT(cameraNearlyEqual(far->armLength / near->armLength, 2.0f, 0.05f));

    const float nearScreenLength =
        length(subtract(EditorVector3{near->screenTips[0].x, near->screenTips[0].y, 0.0f},
                        EditorVector3{near->screenOrigin.x, near->screenOrigin.y, 0.0f}));
    const float farScreenLength =
        length(subtract(EditorVector3{far->screenTips[0].x, far->screenTips[0].y, 0.0f},
                        EditorVector3{far->screenOrigin.x, far->screenOrigin.y, 0.0f}));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(nearScreenLength, farScreenLength, 2.0f));

    // Grabbing is decided on the screen: on an arm is a grab, well away from all three is not.
    const EditorVector2 alongX{(far->screenOrigin.x + far->screenTips[0].x) * 0.5f,
                               (far->screenOrigin.y + far->screenTips[0].y) * 0.5f};
    CNA_EDITOR_EXPECT(hitTestTranslateGizmo3D(*far, alongX) == GizmoAxis3D::X);
    CNA_EDITOR_EXPECT(hitTestTranslateGizmo3D(*far, EditorVector2{5.0f, 5.0f}) == GizmoAxis3D::None);

    // An entity behind the eye has no manipulator at all, rather than one at a plausible-looking
    // screen position the user would be able to grab.
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(100.0f);
    camera.setPivot(EditorVector3{0.0f, 0.0f, -200.0f});  // Eye at z = -100, looking further away.
    CNA_EDITOR_EXPECT(!computeTranslateGizmo3DLayout(scene, camera, entityId).has_value());
}

CNA_EDITOR_TEST(AThreeDimensionalDragFollowsTheCursorAlongTheGrabbedAxis)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid entityId = scene.addEntity(makeEntity(registry, "Crate", 0.0f, 0.0f));

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(100.0f);

    const std::optional<TranslateGizmo3DLayout> layout =
        computeTranslateGizmo3DLayout(scene, camera, entityId);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    // Looking down -Z with no pitch: world +X is screen right, so grabbing the X arm and dragging
    // right must move the entity along +X. A sign error here is the classic gizmo bug -- it works
    // and moves everything the wrong way.
    const EditorVector2 grab{(layout->screenOrigin.x + layout->screenTips[0].x) * 0.5f,
                             layout->screenOrigin.y};

    TranslateGizmo3DDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, camera, *layout, entityId, grab));
    CNA_EDITOR_EXPECT(drag.getAxis() == GizmoAxis3D::X);

    // Not moved yet: a drag that has not moved must push nothing, or one Ctrl+Z is spent undoing
    // a change the user cannot see.
    CNA_EDITOR_EXPECT(!drag.update(scene, camera, grab, GizmoSnap{}).has_value());

    const std::optional<EditorVector3> moved =
        drag.update(scene, camera, EditorVector2{grab.x + 60.0f, grab.y}, GizmoSnap{});
    CNA_EDITOR_EXPECT(moved.has_value());
    if (!moved) { return; }

    CNA_EDITOR_EXPECT(moved->x > 1.0f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(moved->y, 0.0f, 0.001f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(moved->z, 0.0f, 0.001f));

    // Snapping rounds the result rather than the movement, so the entity lands on the grid rather
    // than a grid-sized distance from where it happened to start.
    GizmoSnap snap;
    snap.translate = 10.0f;
    const std::optional<EditorVector3> snapped =
        drag.update(scene, camera, EditorVector2{grab.x + 60.0f, grab.y}, snap);
    CNA_EDITOR_EXPECT(snapped.has_value());
    if (snapped) { CNA_EDITOR_EXPECT(cameraNearlyEqual(std::fmod(snapped->x, 10.0f), 0.0f, 0.001f)); }

    // An arm pointing straight at the camera has no answer -- a pixel of movement would otherwise
    // fling the entity across the level -- so the drag refuses rather than inventing one.
    const WorldRay downZ{EditorVector3{0.0f, 0.0f, 100.0f}, EditorVector3{0.0f, 0.0f, -1.0f}};
    CNA_EDITOR_EXPECT(
        !closestPointOnAxis(downZ, EditorVector3{}, EditorVector3{0.0f, 0.0f, 1.0f}).has_value());

    // And the ordinary case has the answer the geometry says: a ray straight down at x = 30 meets
    // the X axis thirty units along it.
    const WorldRay downwards{EditorVector3{30.0f, 50.0f, 0.0f}, EditorVector3{0.0f, -1.0f, 0.0f}};
    const std::optional<float> where =
        closestPointOnAxis(downwards, EditorVector3{}, EditorVector3{1.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(where.has_value());
    if (where) { CNA_EDITOR_EXPECT(cameraNearlyEqual(*where, 30.0f)); }
}

CNA_EDITOR_TEST(AThreeDimensionalDragOfAChildStoresTheParentRelativePosition)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parentId = scene.addEntity(makeEntity(registry, "Rig", 0.0f, 0.0f));
    scene.findEntity(parentId)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{quaternionFromEulerDegrees(EditorVector3{0.0f, 90.0f, 0.0f})});

    const Uuid childId = scene.addEntity(makeEntity(registry, "Mount", 0.0f, 0.0f));
    CNA_EDITOR_EXPECT(scene.reparentEntity(childId, parentId));

    // A world delta along X, under a parent turned 90 degrees about Y, is a local delta along the
    // child's own Z. A gizmo that skipped this works perfectly on roots and drifts on every child.
    const EditorVector3 local = worldDeltaToLocal3D(scene, childId, EditorVector3{10.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(cameraNearlyEqual(std::abs(local.z), 10.0f, 0.01f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(local.x, 0.0f, 0.01f));

    // A root entity is its own frame, so the delta passes through untouched.
    CNA_EDITOR_EXPECT(worldDeltaToLocal3D(scene, parentId, EditorVector3{10.0f, 0.0f, 0.0f})
                      == (EditorVector3{10.0f, 0.0f, 0.0f}));
}

CNA_EDITOR_TEST(TheThreeDimensionalViewAgreesWithTheTwoDimensionalOneAboutWhichWayIsDown)
{
    // The property the whole Y-down decision exists for: an entity below another one in the 2D
    // viewport is below it in the 3D one too. Switching views moves the camera, not the scene.
    EditorCamera2D flat;
    flat.setViewportSize(EditorVector2{1600.0f, 900.0f});

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(500.0f);

    const EditorVector3 lower{0.0f, 200.0f, 0.0f};  // y grows downward, as SpriteBatch has it.
    const EditorVector3 higher{0.0f, -200.0f, 0.0f};

    CNA_EDITOR_EXPECT(flat.worldToScreen(EditorVector2{lower.x, lower.y}).y
                      > flat.worldToScreen(EditorVector2{higher.x, higher.y}).y);

    const std::optional<EditorVector2> lowerOnScreen = camera.worldToScreen(lower);
    const std::optional<EditorVector2> higherOnScreen = camera.worldToScreen(higher);
    CNA_EDITOR_EXPECT(lowerOnScreen.has_value() && higherOnScreen.has_value());
    if (!lowerOnScreen || !higherOnScreen) { return; }

    CNA_EDITOR_EXPECT(lowerOnScreen->y > higherOnScreen->y);

    // And so does X, which is the half a mirrored "up" vector would have broken: a 180-degree roll
    // fixes the vertical and puts world +X on the left, which is why the mirror is in the
    // projection instead.
    const std::optional<EditorVector2> right = camera.worldToScreen(EditorVector3{200.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(right.has_value());
    if (right) { CNA_EDITOR_EXPECT(right->x > 800.0f); }

    CNA_EDITOR_EXPECT(flat.worldToScreen(EditorVector2{200.0f, 0.0f}).x > 800.0f);

    // A ray still comes back to the pixel it came from: the mirror is in the view-projection both
    // directions go through, so picking cannot disagree with what is drawn.
    const EditorVector2 pixel{1100.0f, 300.0f};
    const std::optional<EditorVector2> back = camera.worldToScreen(camera.screenToRay(pixel).at(400.0f));
    CNA_EDITOR_EXPECT(back.has_value());
    if (back)
    {
        CNA_EDITOR_EXPECT(cameraNearlyEqual(back->x, pixel.x, 0.5f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(back->y, pixel.y, 0.5f));
    }
}

CNA_EDITOR_TEST(EntitiesThatDrawNothingGetABadgeRatherThanACube)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = scene.addEntity(makeEntity(registry, "Main Camera", 0.0f, 0.0f));
    EditorComponent cameraComponent{BuiltinComponentIds::kCamera};
    cameraComponent.applyDefaults(*registry.find(BuiltinComponentIds::kCamera));
    scene.findEntity(cameraId)->addComponent(std::move(cameraComponent));

    const Uuid emptyId = scene.addEntity(makeEntity(registry, "Spawn Point", 200.0f, 0.0f));

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{100.0f, 0.0f, 0.0f});
    camera.setDistance(600.0f);

    const SpriteSizeProvider sizes = [](const Uuid&) { return EditorVector2{32.0f, 32.0f}; };

    WireframeOptions options;
    options.drawGrid = false;

    const WireframeResult result = buildSceneWireframe(scene, camera, {}, sizes, options);
    CNA_EDITOR_EXPECT_EQ(result.entitiesDrawn, std::size_t{2});

    // The camera is a badge and the bare Transform is still a box: twelve edges for one, the
    // camera's silhouette for the other, so the two cannot be confused for each other. Ten
    // entities that draw nothing were ten identical cubes before this.
    const std::size_t cameraSegments =
        buildIconBadge(EditorIconKind::Camera, EditorVector2{}, WireColors::kEntity).size();
    CNA_EDITOR_EXPECT(cameraSegments > 0);
    CNA_EDITOR_EXPECT_EQ(result.segments.size(), cameraSegments + std::size_t{12});

    // Every icon kind draws something, and None draws nothing at all -- a badge for "this entity
    // has no icon" would be a badge on every entity in the scene.
    for (const EditorIconKind kind : {EditorIconKind::Camera, EditorIconKind::Light,
                                      EditorIconKind::AudioSource, EditorIconKind::Model})
    {
        CNA_EDITOR_EXPECT(!buildIconBadge(kind, EditorVector2{400.0f, 300.0f}, WireColors::kEntity).empty());
    }
    CNA_EDITOR_EXPECT(buildIconBadge(EditorIconKind::None, EditorVector2{}, WireColors::kEntity).empty());

    // The badge is a fixed size in pixels: it must not change when the camera moves away, or a
    // camera at the far end of a level becomes invisible and one nearby swallows the screen.
    const std::vector<WireSegment> near =
        buildIconBadge(EditorIconKind::Light, EditorVector2{100.0f, 100.0f}, WireColors::kEntity);
    const std::vector<WireSegment> far =
        buildIconBadge(EditorIconKind::Light, EditorVector2{900.0f, 700.0f}, WireColors::kEntity);
    CNA_EDITOR_EXPECT_EQ(near.size(), far.size());
    CNA_EDITOR_EXPECT(cameraNearlyEqual(near.front().to.x - near.front().from.x,
                                        far.front().to.x - far.front().from.x));

    static_cast<void>(emptyId);
}

CNA_EDITOR_TEST(TheThreeDimensionalRotateRingsAreGrabbedWhereTheyAreDrawn)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid entityId = scene.addEntity(makeEntity(registry, "Crate", 0.0f, 0.0f));

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(300.0f);

    const std::optional<RotateGizmo3DLayout> layout =
        computeRotateGizmo3DLayout(scene, camera, entityId);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    // The Z ring faces the camera, so it is drawn in full and is the one a click lands on.
    const std::vector<EditorVector2>& zRing = layout->rings[2];
    CNA_EDITOR_EXPECT(zRing.size() > 8);

    CNA_EDITOR_EXPECT(hitTestRotateGizmo3D(*layout, zRing.front()) != GizmoAxis3D::None);
    CNA_EDITOR_EXPECT(hitTestRotateGizmo3D(*layout, EditorVector2{800.0f, 450.0f}) == GizmoAxis3D::None);

    // What is drawn is what can be grabbed: the segments are the same polyline the hit-test walks,
    // so a ring the user can see is a ring the user can take hold of.
    const std::vector<WireSegment> segments = buildRotateGizmo3DSegments(*layout);
    CNA_EDITOR_EXPECT(!segments.empty());
    CNA_EDITOR_EXPECT(hitTestRotateGizmo3D(*layout, segments.front().from) != GizmoAxis3D::None);

    // Dragging around the ring turns the entity, and the turn is snapped rather than the angle --
    // snapping the absolute angle would straighten whatever was grabbed the moment it was grabbed.
    RotateGizmo3DDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, camera, *layout, entityId, zRing.front()));

    const EditorVector2 quarter = zRing[zRing.size() / 4];
    const std::optional<EditorQuaternion> turned = drag.update(scene, camera, quarter, GizmoSnap{});
    CNA_EDITOR_EXPECT(turned.has_value());
    if (!turned) { return; }

    CNA_EDITOR_EXPECT(cameraNearlyEqual(std::abs(zRotationOf(*turned)), 1.5707963f, 0.05f));

    GizmoSnap snap;
    snap.rotate = kDefaultRotationSnap;
    const std::optional<EditorQuaternion> snapped = drag.update(scene, camera, quarter, snap);
    CNA_EDITOR_EXPECT(snapped.has_value());
    if (snapped)
    {
        const float degrees = zRotationOf(*snapped) * 57.29578f;
        CNA_EDITOR_EXPECT(cameraNearlyEqual(std::fmod(std::abs(degrees), 15.0f), 0.0f, 0.05f));
    }

    // Not moved is no edit at all: an undo entry restoring a rotation the entity already had
    // costs the user a Ctrl+Z to reach a change they can see.
    CNA_EDITOR_EXPECT(!drag.update(scene, camera, zRing.front(), GizmoSnap{}).has_value());
}

CNA_EDITOR_TEST(TheGridIsDrawnOnWhicheverPlaneTheOptionsName)
{
    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(500.0f);

    WireframeOptions scenePlane;
    scenePlane.gridSpacing = 100.0f;
    scenePlane.gridHalfExtent = 8;

    WireframeOptions ground = scenePlane;
    ground.gridPlane = GridPlane::Ground;

    const std::vector<WireSegment> sceneLines = buildSceneGrid(camera, scenePlane);
    const std::vector<WireSegment> groundLines = buildSceneGrid(camera, ground);

    CNA_EDITOR_EXPECT(!sceneLines.empty());
    CNA_EDITOR_EXPECT(!groundLines.empty());

    const auto verticalSpan = [](const std::vector<WireSegment>& segments) {
        float lowest = segments.front().from.y;
        float highest = lowest;
        for (const WireSegment& segment : segments)
        {
            for (const float y : {segment.from.y, segment.to.y})
            {
                lowest = std::min(lowest, y);
                highest = std::max(highest, y);
            }
        }
        return highest - lowest;
    };

    // A camera with no pitch is *in* the ground plane, so it looks along it edge-on and sees one
    // line where the scene's own plane fills the view. That is the whole reason the choice exists
    // rather than a floor being drawn unconditionally.
    CNA_EDITOR_EXPECT(verticalSpan(sceneLines) > 200.0f);
    CNA_EDITOR_EXPECT(verticalSpan(groundLines) < 2.0f);

    const auto contains = [](const std::vector<WireSegment>& segments, const EditorColor& color) {
        for (const WireSegment& segment : segments)
        {
            if (segment.color == color) { return true; }
        }
        return false;
    };

    // Each plane names the axis running down the middle of it, so the origin is findable in both.
    CNA_EDITOR_EXPECT(contains(sceneLines, WireColors::kAxisY));
    CNA_EDITOR_EXPECT(!contains(sceneLines, WireColors::kAxisZ));
    CNA_EDITOR_EXPECT(contains(groundLines, WireColors::kAxisZ));
    CNA_EDITOR_EXPECT(contains(groundLines, WireColors::kAxisX));

    // Tipped over the floor, the two swap roles: the ground grid opens out and the scene's own
    // plane is the one seen edge-on.
    camera.setPitch(1.4f);
    CNA_EDITOR_EXPECT(verticalSpan(buildSceneGrid(camera, ground)) > 200.0f);
}

CNA_EDITOR_TEST(AThreeDimensionalSelectionTurnsAndGrowsAboutItsSharedPivot)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid leftId = scene.addEntity(makeEntity(registry, "Left", -100.0f, 0.0f));
    const Uuid rightId = scene.addEntity(makeEntity(registry, "Right", 100.0f, 0.0f));

    const std::optional<EditorVector3> pivot = computeSelectionPivot3D(scene, {leftId, rightId});
    CNA_EDITOR_EXPECT(pivot.has_value());
    if (!pivot) { return; }

    CNA_EDITOR_EXPECT(cameraNearlyEqual(pivot->x, 0.0f, 0.001f));

    MultiTransform3D drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, {leftId, rightId}, *pivot));
    CNA_EDITOR_EXPECT_EQ(drag.getEntityCount(), std::size_t{2});

    // A quarter turn about world Z carries both members *around* the pivot as well as turning
    // them, which is the difference between turning an arrangement and spinning each of its parts
    // where it stands.
    const std::vector<EntityTransformEdit> turned =
        drag.rotate(scene, EditorVector3{0.0f, 0.0f, 1.0f}, 1.5707963f);
    CNA_EDITOR_EXPECT_EQ(turned.size(), std::size_t{2});

    for (const EntityTransformEdit& edit : turned)
    {
        CNA_EDITOR_EXPECT(edit.position.has_value());
        CNA_EDITOR_EXPECT(edit.rotation.has_value());
        if (!edit.position || !edit.rotation) { continue; }

        const float expectedY = edit.entityId == leftId ? -100.0f : 100.0f;
        CNA_EDITOR_EXPECT(cameraNearlyEqual(edit.position->x, 0.0f, 0.01f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(edit.position->y, expectedY, 0.01f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(zRotationOf(*edit.rotation), 1.5707963f, 0.01f));
    }

    // Scaling grows the distances from the pivot with the entities themselves. Without that half,
    // a group scaled up stays where it was and overlaps itself.
    const std::array<EditorVector3, 3> axes{EditorVector3{1.0f, 0.0f, 0.0f},
                                            EditorVector3{0.0f, 1.0f, 0.0f},
                                            EditorVector3{0.0f, 0.0f, 1.0f}};
    const std::vector<EntityTransformEdit> grown =
        drag.scale(scene, axes, EditorVector3{2.0f, 1.0f, 1.0f});
    CNA_EDITOR_EXPECT_EQ(grown.size(), std::size_t{2});

    for (const EntityTransformEdit& edit : grown)
    {
        CNA_EDITOR_EXPECT(edit.position.has_value());
        CNA_EDITOR_EXPECT(edit.scale.has_value());
        if (!edit.position || !edit.scale) { continue; }

        const float expectedX = edit.entityId == leftId ? -200.0f : 200.0f;
        CNA_EDITOR_EXPECT(cameraNearlyEqual(edit.position->x, expectedX, 0.01f));

        // Only the axis the gesture named: the other two are left exactly as they were.
        CNA_EDITOR_EXPECT(cameraNearlyEqual(edit.scale->x, 2.0f, 0.01f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(edit.scale->y, 1.0f, 0.001f));
    }

    // A child of a selected entity is carried by its parent, so taking it on its own account as
    // well would transform it twice. Only the selection's roots are captured.
    CNA_EDITOR_EXPECT(scene.reparentEntity(rightId, leftId));
    MultiTransform3D nested;
    CNA_EDITOR_EXPECT(nested.begin(scene, {leftId, rightId}, *pivot));
    CNA_EDITOR_EXPECT_EQ(nested.getEntityCount(), std::size_t{1});
}

CNA_EDITOR_TEST(TheThreeDimensionalScaleArmsShortenRatherThanVanishWhenSeenEdgeOn)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid entityId = scene.addEntity(makeEntity(registry, "Crate", 0.0f, 0.0f));

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(300.0f);

    const std::optional<ScaleGizmo3DLayout> straightOn = computeScaleGizmo3DLayout(scene, camera, entityId);
    CNA_EDITOR_EXPECT(straightOn.has_value());
    if (!straightOn) { return; }

    // Looking straight down the Z axis at an entity in the middle of the screen: X and Y lie across
    // the view, and Z runs exactly through the eye, where it projects onto its own origin and has
    // no direction to be dragged along at all. That is the one case an arm is dropped.
    CNA_EDITOR_EXPECT(straightOn->armVisible[0]);
    CNA_EDITOR_EXPECT(straightOn->armVisible[1]);
    CNA_EDITOR_EXPECT(!straightOn->armVisible[2]);

    // The pixels it vacated belong to the centre handle, so the middle of the gizmo still does
    // something -- and it is the thing a press in the middle of a gizmo means.
    CNA_EDITOR_EXPECT(hitTestScaleGizmo3D(*straightOn, straightOn->screenOrigin) == GizmoAxis3D::All);
    CNA_EDITOR_EXPECT(hitTestScaleGizmo3D(*straightOn, straightOn->screenHandles[0]) == GizmoAxis3D::X);
    CNA_EDITOR_EXPECT(hitTestScaleGizmo3D(*straightOn, EditorVector2{5.0f, 5.0f}) == GizmoAxis3D::None);

    // A degree of orbit and the arm is back: shortened to its floor and faded almost out, but on
    // screen and grabbable. This is where the rotate gizmo hides a ring instead -- a turn needs a
    // plane to measure an angle in, and a scale needs only a ratio, which the screen always has.
    camera.setYaw(0.02f);
    const std::optional<ScaleGizmo3DLayout> tilted = computeScaleGizmo3DLayout(scene, camera, entityId);
    CNA_EDITOR_EXPECT(tilted.has_value());
    if (!tilted) { return; }

    CNA_EDITOR_EXPECT(tilted->armVisible[2]);

    const float pixels = std::hypot(tilted->screenHandles[2].x - tilted->screenOrigin.x,
                                    tilted->screenHandles[2].y - tilted->screenOrigin.y);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(pixels, kScaleGizmo3DMinimumArmPixels, 0.5f));
    CNA_EDITOR_EXPECT(pixels > tilted->centerExtent + tilted->handleExtent);
    CNA_EDITOR_EXPECT(tilted->armFade[2] < 0.2f);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(tilted->armFade[0], 1.0f, 0.001f));

    // What is drawn is what can be grabbed, the same invariant the rings hold: the handle sits on
    // the X arm's line at this angle, and the square still wins, because a square is what the eye
    // was aiming at.
    CNA_EDITOR_EXPECT(hitTestScaleGizmo3D(*tilted, tilted->screenHandles[2]) == GizmoAxis3D::Z);
    CNA_EDITOR_EXPECT(!buildScaleGizmo3DSegments(*tilted, GizmoAxis3D::Z).empty());
}

CNA_EDITOR_TEST(AThreeDimensionalScaleDragIsARatioOfScreenDistances)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid entityId = scene.addEntity(makeEntity(registry, "Crate", 0.0f, 0.0f));

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.6f);
    camera.setPitch(0.4f);
    camera.setDistance(300.0f);

    const std::optional<ScaleGizmo3DLayout> layout = computeScaleGizmo3DLayout(scene, camera, entityId);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    const EditorVector2 handle = layout->screenHandles[0];
    const EditorVector2 arm{handle.x - layout->screenOrigin.x, handle.y - layout->screenOrigin.y};

    ScaleGizmo3DDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, entityId, handle));
    CNA_EDITOR_EXPECT(drag.getAxis() == GizmoAxis3D::X);

    // Not moved is no edit at all.
    CNA_EDITOR_EXPECT(!drag.update(*layout, handle, GizmoSnap{}).has_value());

    // Twice as far out along the arm is twice the size -- a ratio, which is the only measure of a
    // unitless quantity that means the same thing at every camera distance.
    const EditorVector2 twiceOut{layout->screenOrigin.x + arm.x * 2.0f,
                                 layout->screenOrigin.y + arm.y * 2.0f};
    const std::optional<EditorVector3> doubled = drag.update(*layout, twiceOut, GizmoSnap{});
    CNA_EDITOR_EXPECT(doubled.has_value());
    if (!doubled) { return; }

    CNA_EDITOR_EXPECT(cameraNearlyEqual(doubled->x, 2.0f, 0.01f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(doubled->y, 1.0f, 0.001f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(doubled->z, 1.0f, 0.001f));

    // Dragging back through the origin flips the entity, which is a legitimate edit XNA's own
    // negative scale supports -- but it never lands on zero, where the entity would be invisible
    // *and* unclickable, with nothing left to grab to get it back.
    const EditorVector2 behind{layout->screenOrigin.x - arm.x, layout->screenOrigin.y - arm.y};
    const std::optional<EditorVector3> flipped = drag.update(*layout, behind, GizmoSnap{});
    CNA_EDITOR_EXPECT(flipped.has_value());
    if (flipped) { CNA_EDITOR_EXPECT(flipped->x < 0.0f); }

    const std::optional<EditorVector3> atOrigin =
        drag.update(*layout, layout->screenOrigin, GizmoSnap{});
    CNA_EDITOR_EXPECT(atOrigin.has_value());
    if (atOrigin) { CNA_EDITOR_EXPECT(atOrigin->x != 0.0f); }

    // Snapped to tenths, and the *factor* is what is rounded: the shared quantity, so a selection
    // cannot end up at sizes that are round one at a time and not in proportion to each other.
    GizmoSnap snap;
    snap.scale = kDefaultScaleSnap;
    const EditorVector2 awkward{layout->screenOrigin.x + arm.x * 1.73f,
                                layout->screenOrigin.y + arm.y * 1.73f};
    const std::optional<EditorVector3> snapped = drag.update(*layout, awkward, snap);
    CNA_EDITOR_EXPECT(snapped.has_value());
    if (snapped) { CNA_EDITOR_EXPECT(cameraNearlyEqual(snapped->x, 1.7f, 0.001f)); }

    // The centre handle scales all three at once, which is the commonest scale there is.
    ScaleGizmo3DDrag uniform;
    const EditorVector2 nearCentre{layout->screenOrigin.x + 8.0f, layout->screenOrigin.y};
    CNA_EDITOR_EXPECT(uniform.begin(scene, *layout, entityId, nearCentre));
    CNA_EDITOR_EXPECT(uniform.getAxis() == GizmoAxis3D::All);

    const std::optional<EditorVector3> bigger =
        uniform.update(*layout, EditorVector2{layout->screenOrigin.x + 16.0f, layout->screenOrigin.y},
                       GizmoSnap{});
    CNA_EDITOR_EXPECT(bigger.has_value());
    if (bigger)
    {
        CNA_EDITOR_EXPECT(cameraNearlyEqual(bigger->x, 2.0f, 0.01f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(bigger->y, 2.0f, 0.01f));
        CNA_EDITOR_EXPECT(cameraNearlyEqual(bigger->z, 2.0f, 0.01f));
    }

    // A press exactly on the origin is not a drag: every factor is a division by how far out the
    // grab was, so a grab at the centre would scale by infinity. The press falls through instead.
    ScaleGizmo3DDrag atCentre;
    CNA_EDITOR_EXPECT(!atCentre.begin(scene, *layout, entityId, layout->screenOrigin));
}

CNA_EDITOR_TEST(AThreeDimensionalTurnIsAppliedInTheWorldRatherThanTheEntitysOwnFrame)
{
    ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid entityId = scene.addEntity(makeEntity(registry, "Crate", 0.0f, 0.0f));

    // Already lying on its side. An unrotated entity cannot tell the two compositions apart --
    // they differ by exactly the rotation the entity already has -- which is why the gizmo passed
    // its first test while turning things about the wrong axes.
    scene.findEntity(entityId)
        ->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation",
                      PropertyValue{quaternionFromEulerDegrees(EditorVector3{90.0f, 0.0f, 0.0f})});

    EditorCamera3D camera = makeCamera();
    camera.setPivot(EditorVector3{});
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setDistance(300.0f);

    const std::optional<RotateGizmo3DLayout> layout =
        computeRotateGizmo3DLayout(scene, camera, entityId, GizmoSpace::World);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    const std::vector<EditorVector2>& zRing = layout->rings[2];
    CNA_EDITOR_EXPECT(zRing.size() > 8);

    RotateGizmo3DDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, camera, *layout, entityId, zRing.front()));

    const std::optional<EditorQuaternion> turned =
        drag.update(scene, camera, zRing[zRing.size() / 4], GizmoSnap{});
    CNA_EDITOR_EXPECT(turned.has_value());
    if (!turned) { return; }

    // A turn about world Z leaves anything already pointing along world Z exactly where it is.
    // The entity's own Y axis is one such thing after a 90-degree tip about X, so this holds
    // whatever the drag's angle turned out to be -- and fails outright if the turn went in about
    // the entity's local axes instead, where the same drag would swing it a quarter turn away.
    const EditorVector3 localY = rotate(*turned, EditorVector3{0.0f, 1.0f, 0.0f});
    CNA_EDITOR_EXPECT(cameraNearlyEqual(localY.z, 1.0f, 0.02f));

    // And its own X axis, which started in the XY plane, stays in it.
    const EditorVector3 localX = rotate(*turned, EditorVector3{1.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(cameraNearlyEqual(localX.z, 0.0f, 0.02f));
}

namespace
{
    /** @brief Adds a `CNA.Light` to @p entity with the given kind, intensity and range. */
    void addLight(const ComponentRegistry& registry, EditorEntity& entity, const std::string& kind,
                  float intensity, float range, EditorColor color = EditorColor{255, 255, 255, 255})
    {
        EditorComponent light{BuiltinComponentIds::kLight};
        light.applyDefaults(*registry.find(BuiltinComponentIds::kLight));
        light.setProperty("kind", PropertyValue{PropertyValue::EnumValue{kind}});
        light.setProperty("intensity", PropertyValue{intensity});
        light.setProperty("range", PropertyValue{range});
        light.setProperty("color", PropertyValue{color});
        entity.addComponent(std::move(light));
    }
}

/**
 * @brief ED-402/ED-404: a scene with no light asks for the default rather than for darkness.
 *
 * The commonest scene there is -- one somebody has just dropped a model into -- has no light in
 * it, and a model rendered black in that scene is indistinguishable from a renderer that does not
 * work. So "no lights" is a distinct answer the renderer acts on, not zero lights applied.
 */
CNA_EDITOR_TEST(ASceneWithNoLightsAsksForTheDefaultLightingRatherThanForNone)
{
    const EffectLighting empty = computeEffectLighting({}, EditorVector3{0.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(empty.useDefaultLighting);
    CNA_EDITOR_EXPECT_EQ(empty.lightCount, std::size_t{0});

    // And so does a scene whose only lamp is out of range of what is being drawn: "unlit" and "too
    // far from the light" look identical to a user, and both are better answered by something
    // visible than by black.
    SceneLight distant;
    distant.kind = SceneLightKind::Point;
    distant.position = EditorVector3{1000.0f, 0.0f, 0.0f};
    distant.range = 5.0f;

    const EffectLighting outOfRange =
        computeEffectLighting({distant}, EditorVector3{0.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(outOfRange.useDefaultLighting);
}

/** @brief A light entity's rotation is what aims it, so turning it in the viewport turns the light. */
CNA_EDITOR_TEST(ADirectionalLightShinesAlongItsEntitysOwnForwardAxis)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Sun", 0.0f, 0.0f);
    addLight(registry, entity, "Directional", 1.0f, 10.0f);
    const Uuid id = scene.addEntity(std::move(entity));

    const std::vector<SceneLight> unrotated = collectSceneLights(scene);
    CNA_EDITOR_EXPECT_EQ(unrotated.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(unrotated[0].entityId == id);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(unrotated[0].direction.z, 1.0f, 0.001f));

    // Turned a half turn about Y, it must shine back the other way. Asserting the property rather
    // than a number: whatever convention the transform uses, a light spun 180 degrees cannot go on
    // pointing where it did.
    EditorEntity* stored = scene.findEntity(id);
    CNA_EDITOR_EXPECT(stored != nullptr);
    EditorComponent* transform = stored->findComponent(BuiltinComponentIds::kTransform);
    CNA_EDITOR_EXPECT(transform != nullptr);
    transform->setProperty("rotation",
                           PropertyValue{quaternionFromEulerDegrees(EditorVector3{0.0f, 180.0f, 0.0f})});

    const std::vector<SceneLight> turned = collectSceneLights(scene);
    CNA_EDITOR_EXPECT_EQ(turned.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(cameraNearlyEqual(turned[0].direction.z, -1.0f, 0.001f));
}

/**
 * @brief A disabled entity's light does not light anything.
 *
 * The same rule the rest of the viewport follows for what it draws. A user who disables an entity
 * has said "pretend this is not here", and a lamp that goes on shining is the editor disagreeing.
 */
CNA_EDITOR_TEST(ALightOnADisabledEntityIsNotCollected)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Lamp", 0.0f, 0.0f);
    addLight(registry, entity, "Point", 1.0f, 10.0f);
    const Uuid id = scene.addEntity(std::move(entity));

    CNA_EDITOR_EXPECT_EQ(collectSceneLights(scene).size(), std::size_t{1});

    scene.findEntity(id)->setEnabled(false);
    CNA_EDITOR_EXPECT(collectSceneLights(scene).empty());
}

/**
 * @brief `IEffectLights` holds three lights, so a fourth has to lose -- and the dimmest is the one.
 *
 * Brightest wins rather than nearest or first-in-document. Document order is not something a user
 * arranges deliberately, and dropping the sun because a dim lamp was added earlier is a picture
 * nobody could account for from what is on screen.
 */
CNA_EDITOR_TEST(WhereMoreThanThreeLightsApplyTheThreeBrightestWin)
{
    std::vector<SceneLight> lights;
    for (int i = 0; i < 4; ++i)
    {
        SceneLight light;
        light.kind = SceneLightKind::Directional;
        // The first is the dimmest, so a correct answer cannot also be "the first three".
        light.intensity = 0.1f + static_cast<float>(i);
        lights.push_back(light);
    }

    const EffectLighting lighting = computeEffectLighting(lights, EditorVector3{0.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(!lighting.useDefaultLighting);
    CNA_EDITOR_EXPECT_EQ(lighting.lightCount, std::size_t{3});

    // Brightest first, and the one left out is the dimmest.
    CNA_EDITOR_EXPECT(lighting.lights[0].diffuseColor.x > lighting.lights[1].diffuseColor.x);
    CNA_EDITOR_EXPECT(lighting.lights[1].diffuseColor.x > lighting.lights[2].diffuseColor.x);
    CNA_EDITOR_EXPECT(lighting.lights[2].diffuseColor.x > 1.0f);
}

/**
 * @brief A point light is resolved against the thing being lit, which is what makes it a point light.
 *
 * `IEffectLights` has no point light, so one is approximated as a directional light aimed at
 * whatever is being drawn. The property that makes the approximation worth having is exactly this:
 * two objects on opposite sides of a lamp must be lit from opposite directions. An implementation
 * that used the light's own forward axis would pass every other test here and fail this one.
 */
CNA_EDITOR_TEST(APointLightAimsAtWhateverIsBeingLitRatherThanAlongItsOwnAxis)
{
    SceneLight lamp;
    lamp.kind = SceneLightKind::Point;
    lamp.position = EditorVector3{0.0f, 0.0f, 0.0f};
    lamp.range = 100.0f;
    lamp.intensity = 1.0f;

    const EffectLighting left = computeEffectLighting({lamp}, EditorVector3{-10.0f, 0.0f, 0.0f});
    const EffectLighting right = computeEffectLighting({lamp}, EditorVector3{10.0f, 0.0f, 0.0f});

    CNA_EDITOR_EXPECT(!left.useDefaultLighting);
    CNA_EDITOR_EXPECT(!right.useDefaultLighting);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(left.lights[0].direction.x, -1.0f, 0.001f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(right.lights[0].direction.x, 1.0f, 0.001f));

    // And it dims with distance, reaching exactly nothing at its range rather than tinting the
    // whole level faintly forever -- which is what makes the range control do something visible.
    const EffectLighting near = computeEffectLighting({lamp}, EditorVector3{10.0f, 0.0f, 0.0f});
    const EffectLighting far = computeEffectLighting({lamp}, EditorVector3{90.0f, 0.0f, 0.0f});
    CNA_EDITOR_EXPECT(near.lights[0].diffuseColor.x > far.lights[0].diffuseColor.x);
    CNA_EDITOR_EXPECT(computeEffectLighting({lamp}, EditorVector3{101.0f, 0.0f, 0.0f}).useDefaultLighting);
}

namespace
{
    /** @brief A one-triangle mesh, enough for a batch to have something real to carry. */
    MeshData makeTinyMesh()
    {
        MeshPart part;
        part.vertices = {MeshVertex{EditorVector3{0.0f, 0.0f, 0.0f}, EditorVector3{0.0f, 0.0f, -1.0f}, {}},
                         MeshVertex{EditorVector3{1.0f, 0.0f, 0.0f}, EditorVector3{0.0f, 0.0f, -1.0f}, {}},
                         MeshVertex{EditorVector3{0.0f, 1.0f, 0.0f}, EditorVector3{0.0f, 0.0f, -1.0f}, {}}};
        part.indices = {0, 1, 2};

        MeshData mesh;
        mesh.parts.push_back(std::move(part));
        recomputeMeshBounds(mesh);
        return mesh;
    }

    /** @brief Adds a `CNA.ModelRenderer` naming @p modelId to @p entity. */
    void addModelRenderer(const ComponentRegistry& registry, EditorEntity& entity, const Uuid& modelId)
    {
        EditorComponent renderer{BuiltinComponentIds::kModelRenderer};
        renderer.applyDefaults(*registry.find(BuiltinComponentIds::kModelRenderer));
        renderer.setProperty("model", PropertyValue{PropertyValue::AssetReference{modelId}});
        entity.addComponent(std::move(renderer));
    }
}

/**
 * @brief ED-402: the batch carries the camera's own view-projection, mirror and all.
 *
 * The single most important property of this seam, and the cheapest to get wrong. The 3D camera's
 * view-projection already contains the Y mirror that converts XNA's Y-up 3D frame to this editor's
 * Y-down world; a model pass that built its own matrix, or applied the mirror a second time, would
 * draw models upside down relative to the grid and the gizmos -- or, worse, right way up but
 * somewhere a click could not reach them.
 */
CNA_EDITOR_TEST(TheModelBatchDrawsThroughTheSameViewProjectionAsEverythingElse)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const MeshData mesh = makeTinyMesh();
    const Uuid modelId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Crate", 3.0f, 4.0f);
    addModelRenderer(registry, entity, modelId);
    scene.addEntity(std::move(entity));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.orbit(35.0f, 25.0f);

    const MeshProvider provider = [&](const Uuid& id) { return id == modelId ? &mesh : nullptr; };
    const SceneModelBatch batch = buildSceneModelBatch(scene, camera, provider);

    CNA_EDITOR_EXPECT_EQ(batch.draws.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(batch.viewProjection == camera.getViewProjectionMatrix());
    CNA_EDITOR_EXPECT_EQ(batch.triangleCount, std::size_t{1});
    CNA_EDITOR_EXPECT(batch.draws[0].mesh == &mesh);
}

/**
 * @brief The world matrix in a draw maps model space exactly where the scene transform says.
 *
 * The property, not the sixteen numbers: a local point pushed through the matrix must land where
 * composing the entity's own scale, rotation and translation puts it. Written this way because the
 * failure it guards against is a *different composition order* -- rotate-then-scale instead of
 * scale-then-rotate -- which is invisible on an unrotated or unscaled entity and puts a rotated,
 * non-uniformly scaled one somewhere its own gizmo does not agree with.
 */
CNA_EDITOR_TEST(AModelDrawsWhereTheSceneTransformSaysItIs)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const MeshData mesh = makeTinyMesh();
    const Uuid modelId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Crate", 3.0f, 4.0f);
    addModelRenderer(registry, entity, modelId);
    const Uuid id = scene.addEntity(std::move(entity));

    EditorComponent* transform = scene.findEntity(id)->findComponent(BuiltinComponentIds::kTransform);
    transform->setProperty("scale", PropertyValue{EditorVector3{2.0f, 3.0f, 1.0f}});
    transform->setProperty("rotation",
                           PropertyValue{quaternionFromEulerDegrees(EditorVector3{0.0f, 0.0f, 90.0f})});

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const MeshProvider provider = [&](const Uuid& queried) { return queried == modelId ? &mesh : nullptr; };
    const SceneModelBatch batch = buildSceneModelBatch(scene, camera, provider);
    CNA_EDITOR_EXPECT_EQ(batch.draws.size(), std::size_t{1});

    // (1, 0, 0) scaled by 2 on X, then turned a quarter turn about Z, then moved to (3, 4, 0).
    const std::optional<WorldTransform> world = computeWorldTransform(scene, id);
    CNA_EDITOR_EXPECT(world.has_value());

    const EditorVector3 local{1.0f, 0.0f, 0.0f};
    const EditorVector3 expected =
        add(rotate(world->rotation,
                   EditorVector3{local.x * world->scale.x, local.y * world->scale.y,
                                 local.z * world->scale.z}),
            world->position);
    const EditorVector3 actual = transformPosition(batch.draws[0].world, local);

    CNA_EDITOR_EXPECT(cameraNearlyEqual(actual.x, expected.x, 0.001f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(actual.y, expected.y, 0.001f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(actual.z, expected.z, 0.001f));
}

/**
 * @brief A model still importing is counted, not silently dropped.
 *
 * "Still importing" and "this entity has no model" look identical on screen and only one of them
 * is worth waiting for, so the batch keeps a count the viewport can report rather than leaving a
 * user to wonder where their crate went.
 */
CNA_EDITOR_TEST(AnEntityWhoseMeshHasNotArrivedIsCountedRatherThanDropped)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Crate", 0.0f, 0.0f);
    addModelRenderer(registry, entity, Uuid::generate());
    const Uuid id = scene.addEntity(std::move(entity));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const MeshProvider none = [](const Uuid&) -> const MeshData* { return nullptr; };
    const SceneModelBatch pending = buildSceneModelBatch(scene, camera, none);
    CNA_EDITOR_EXPECT(pending.draws.empty());
    CNA_EDITOR_EXPECT_EQ(pending.pendingMeshes, std::size_t{1});

    // And a disabled entity is not pending either -- it is simply not there, like everywhere else.
    scene.findEntity(id)->setEnabled(false);
    const SceneModelBatch disabled = buildSceneModelBatch(scene, camera, none);
    CNA_EDITOR_EXPECT_EQ(disabled.pendingMeshes, std::size_t{0});
}

/**
 * @brief The batch's split camera multiplies back to the product it is drawn through.
 *
 * `SceneModelBatch` carries the camera twice -- as one view-projection and as the view and
 * projection separately -- because CNA's `PbrEffect` inverts the view to recover the eye position
 * and an identity view is a camera claiming to sit at the origin. Two descriptions of one thing
 * that could drift is exactly the bug this seam was arranged to avoid, so the invariant is pinned
 * rather than trusted: the mirror lives in the projection, and the product is the matrix
 * everything else in the editor already positions against.
 */
CNA_EDITOR_TEST(TheModelBatchsSplitCameraMultipliesBackToItsProduct)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const MeshData mesh = makeTinyMesh();
    const Uuid modelId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Crate", 1.0f, 2.0f);
    addModelRenderer(registry, entity, modelId);
    scene.addEntity(std::move(entity));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.orbit(40.0f, -20.0f);

    const MeshProvider provider = [&](const Uuid& id) { return id == modelId ? &mesh : nullptr; };
    const SceneModelBatch batch = buildSceneModelBatch(scene, camera, provider);

    const EditorMatrix product = multiply(batch.view, batch.projection);

    // Compared through a point rather than field by field: what has to agree is where a world
    // point lands, and a matrix comparison would fail on floating-point noise that moves nothing.
    const EditorVector3 probe{3.0f, -2.0f, 5.0f};
    const EditorVector3 throughProduct = transformPosition(product, probe);
    const EditorVector3 throughCombined = transformPosition(batch.viewProjection, probe);

    CNA_EDITOR_EXPECT(cameraNearlyEqual(throughProduct.x, throughCombined.x, 0.0005f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(throughProduct.y, throughCombined.y, 0.0005f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(throughProduct.z, throughCombined.z, 0.0005f));

    // And the mirror really is in the projection half, not left to the caller: a camera at rest
    // must send world +Y to screen -Y, which is the whole reason the 2D and 3D views agree about
    // which way is down.
    CNA_EDITOR_EXPECT(batch.projection.m22 < 0.0f);
}

namespace
{
    /** @brief Adds a `CNA.SpriteRenderer` naming @p textureId, with an optional origin. */
    void addSpriteRenderer(const ComponentRegistry& registry, EditorEntity& entity,
                           const Uuid& textureId, EditorVector2 origin = {})
    {
        EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
        sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
        sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{textureId}});
        sprite.setProperty("origin", PropertyValue{origin});
        entity.addComponent(std::move(sprite));
    }
}

/**
 * @brief A sprite's quad is the rectangle the picker already thinks the sprite is.
 *
 * The property worth pinning, because the alternative -- billboarding -- passes every other test
 * and fails this one. `computeEntityBounds3D`, the gizmos and the ray picker all treat a sprite as
 * a flat box in the scene's XY plane; a quad drawn anywhere else is a sprite drawn where it cannot
 * be clicked.
 */
CNA_EDITOR_TEST(ASpriteQuadIsTheRectangleThePickerAlreadyThinksItIs)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid textureId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Orb", 5.0f, 7.0f);
    addSpriteRenderer(registry, entity, textureId);
    const Uuid id = scene.addEntity(std::move(entity));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.orbit(30.0f, 20.0f);

    const SpriteSizeProvider sizes = [&](const Uuid& queried)
    { return queried == textureId ? EditorVector2{32.0f, 16.0f} : EditorVector2{}; };

    const SceneSpriteBatch3D batch = buildSceneSpriteQuads(scene, camera, sizes, {}, {}, &registry);
    CNA_EDITOR_EXPECT_EQ(batch.quads.size(), std::size_t{1});

    const std::optional<WorldBounds3D> bounds = computeEntityBounds3D(scene, id, sizes);
    CNA_EDITOR_EXPECT(bounds.has_value());

    // Every corner lies inside what the picker calls this entity, and the quad spans the whole of
    // it -- so the two describe one rectangle rather than two that happen to overlap.
    float minX = batch.quads[0].corners[0].x, maxX = minX;
    float minY = batch.quads[0].corners[0].y, maxY = minY;
    for (const EditorVector3& corner : batch.quads[0].corners)
    {
        minX = std::min(minX, corner.x);
        maxX = std::max(maxX, corner.x);
        minY = std::min(minY, corner.y);
        maxY = std::max(maxY, corner.y);

        // And it is flat in the scene plane: a sprite that turned to face the camera would not be.
        CNA_EDITOR_EXPECT(cameraNearlyEqual(corner.z, 0.0f, 0.001f));
    }

    CNA_EDITOR_EXPECT(cameraNearlyEqual(minX, bounds->min.x, 0.01f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(maxX, bounds->max.x, 0.01f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(minY, bounds->min.y, 0.01f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(maxY, bounds->max.y, 0.01f));
}

/**
 * @brief Pitch and roll on the entity do not tilt the sprite, because `SpriteBatch` cannot.
 *
 * The 3D view is allowed to show more than the 2D one and not more than the *game*. A quad honouring
 * the full world rotation would look better and would be the editor drawing something the player
 * will render flat.
 */
CNA_EDITOR_TEST(ASpriteIgnoresEverythingButItsZRotation)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid textureId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Orb", 0.0f, 0.0f);
    addSpriteRenderer(registry, entity, textureId);
    const Uuid id = scene.addEntity(std::move(entity));

    scene.findEntity(id)
        ->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation",
                      PropertyValue{quaternionFromEulerDegrees(EditorVector3{60.0f, 45.0f, 0.0f})});

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const SpriteSizeProvider sizes = [&](const Uuid&) { return EditorVector2{16.0f, 16.0f}; };
    const SceneSpriteBatch3D batch = buildSceneSpriteQuads(scene, camera, sizes, {}, {}, &registry);

    CNA_EDITOR_EXPECT_EQ(batch.quads.size(), std::size_t{1});
    for (const EditorVector3& corner : batch.quads[0].corners)
    {
        CNA_EDITOR_EXPECT(cameraNearlyEqual(corner.z, 0.0f, 0.001f));
    }
}

/**
 * @brief Sprites come back furthest first, because transparency does not commute.
 *
 * Two overlapping transparent quads blended in the wrong order give a different picture, and the
 * one that looks right is furthest first. This is the ordering the viewport relies on and does not
 * re-derive.
 */
CNA_EDITOR_TEST(SpriteQuadsArriveFurthestFromTheCameraFirst)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid textureId = Uuid::generate();

    // Added near-first, so a correct answer cannot also be "document order".
    EditorEntity near = makeEntity(registry, "Near", 0.0f, 0.0f);
    addSpriteRenderer(registry, near, textureId);
    scene.addEntity(std::move(near));

    EditorEntity far = makeEntity(registry, "Far", 400.0f, 0.0f);
    addSpriteRenderer(registry, far, textureId);
    scene.addEntity(std::move(far));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const SpriteSizeProvider sizes = [&](const Uuid&) { return EditorVector2{8.0f, 8.0f}; };
    const SceneSpriteBatch3D batch = buildSceneSpriteQuads(scene, camera, sizes, {}, {}, &registry);

    CNA_EDITOR_EXPECT_EQ(batch.quads.size(), std::size_t{2});
    CNA_EDITOR_EXPECT(batch.quads[0].cameraDistance >= batch.quads[1].cameraDistance);
}

/** @brief A sprite whose texture cannot be sized is counted, never drawn at a guessed size. */
CNA_EDITOR_TEST(ASpriteWithNoKnownSizeIsCountedRatherThanGuessedAt)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity = makeEntity(registry, "Orb", 0.0f, 0.0f);
    addSpriteRenderer(registry, entity, Uuid::generate());
    scene.addEntity(std::move(entity));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const SpriteSizeProvider unknown = [](const Uuid&) { return EditorVector2{}; };
    const SceneSpriteBatch3D batch =
        buildSceneSpriteQuads(scene, camera, unknown, {}, {}, &registry);

    CNA_EDITOR_EXPECT(batch.quads.empty());
    CNA_EDITOR_EXPECT_EQ(batch.skipped, std::size_t{1});
}

/**
 * @brief ED-407: a scene's environment round-trips, and a scene that has none stays that way.
 *
 * The second half is the one worth a test. `environment` is an *additive* field, which means a
 * scene written before ED-407 must read as the defaults and be written back out **without** the
 * object -- a loader that read a default and then serialised it would turn every existing scene
 * into a modified file the first time it was opened, and a diff full of scenes nobody touched is
 * how a format change gets blamed on the wrong commit.
 */
CNA_EDITOR_TEST(ASceneWithoutAnEnvironmentDoesNotGrowOneByBeingOpened)
{
    const ComponentRegistry registry = makeRegistry();

    SceneDocument fresh;
    fresh.setName("Level01");
    fresh.addEntity(makeEntity(registry, "Player", 1.0f, 2.0f));

    // Nothing set: the serialised form must not mention the environment at all.
    const JsonValue bare = fresh.toJson();
    CNA_EDITOR_EXPECT(!bare["environment"].isObject());

    SceneDocument reloaded;
    reloaded.loadFromJson(bare, registry, nullptr);
    CNA_EDITOR_EXPECT(reloaded.getEnvironment().isDefault());
    CNA_EDITOR_EXPECT(!reloaded.toJson()["environment"].isObject());

    // And once it is set, every field survives the trip.
    SceneEnvironment foggy;
    foggy.ambientColor = EditorColor{40, 44, 60, 255};
    foggy.fogEnabled = true;
    foggy.fogColor = EditorColor{200, 190, 180, 255};
    foggy.fogStart = 250.0f;
    foggy.fogEnd = 900.0f;
    fresh.setEnvironment(foggy);

    SceneDocument roundTripped;
    roundTripped.loadFromJson(fresh.toJson(), registry, nullptr);

    const SceneEnvironment& back = roundTripped.getEnvironment();
    CNA_EDITOR_EXPECT(!back.isDefault());
    CNA_EDITOR_EXPECT(back.fogEnabled);
    CNA_EDITOR_EXPECT(back.ambientColor == foggy.ambientColor);
    CNA_EDITOR_EXPECT(back.fogColor == foggy.fogColor);
    CNA_EDITOR_EXPECT(cameraNearlyEqual(back.fogStart, 250.0f, 0.001f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(back.fogEnd, 900.0f, 0.001f));

    // The formatVersion is untouched: this is an additive field, not a new version of the format.
    CNA_EDITOR_EXPECT_EQ(fresh.toJson()["formatVersion"].asNumber(0.0),
                         bare["formatVersion"].asNumber(-1.0));
}

/** @brief The scene's ambient reaches the model batch, because a cave is dark for everything in it. */
CNA_EDITOR_TEST(TheScenesAmbientReachesEveryModelDrawnInIt)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const MeshData mesh = makeTinyMesh();
    const Uuid modelId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Crate", 0.0f, 0.0f);
    addModelRenderer(registry, entity, modelId);
    scene.addEntity(std::move(entity));

    SceneEnvironment dim;
    dim.ambientColor = EditorColor{255, 0, 0, 255};
    scene.setEnvironment(dim);

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const MeshProvider provider = [&](const Uuid& id) { return id == modelId ? &mesh : nullptr; };
    const SceneModelBatch batch = buildSceneModelBatch(scene, camera, provider);

    CNA_EDITOR_EXPECT_EQ(batch.draws.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(cameraNearlyEqual(batch.draws[0].lighting.ambientColor.x, 1.0f, 0.001f));
    CNA_EDITOR_EXPECT(cameraNearlyEqual(batch.draws[0].lighting.ambientColor.y, 0.0f, 0.001f));

    // And the fog settings ride on the batch rather than on each draw: fog is a property of the
    // level, so a per-draw copy would be the same numbers repeated with nothing able to differ.
    CNA_EDITOR_EXPECT(batch.environment.ambientColor == dim.ambientColor);
}

/**
 * @brief ED-410: a per-part material lands on the part it names and leaves the others alone.
 *
 * The behaviour that makes the list worth having, and the one an index-keyed list gets wrong
 * silently. It is checked here rather than by screenshot because which material reaches which part
 * is a lookup, not a colour -- and a lookup is exactly the kind of thing a picture is bad at
 * disproving.
 */
CNA_EDITOR_TEST(APerPartMaterialAppliesToItsOwnPartAndNoOther)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    // Two parts, so "only this one" has something to be distinguished from.
    MeshData mesh = makeTinyMesh();
    mesh.parts[0].name = "Body";
    MeshPart lid = mesh.parts[0];
    lid.name = "Lid";
    mesh.parts.push_back(std::move(lid));

    const Uuid modelId = Uuid::generate();
    const Uuid lidMaterialId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Crate", 0.0f, 0.0f);
    addModelRenderer(registry, entity, modelId);

    PropertyValue::StructureValue lidEntry;
    lidEntry.set("part", PropertyValue{std::string{"Lid"}});
    lidEntry.set("material", PropertyValue{PropertyValue::AssetReference{lidMaterialId}});

    PropertyValue::ListValue materials;
    materials.items.push_back(PropertyValue{lidEntry});

    EditorComponent* renderer = entity.findComponent(BuiltinComponentIds::kModelRenderer);
    renderer->setProperty("materials", PropertyValue{materials});
    scene.addEntity(std::move(entity));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const MeshProvider meshes = [&](const Uuid& id) { return id == modelId ? &mesh : nullptr; };
    const MaterialProvider materialsProvider =
        [&](const Uuid& id) -> std::optional<MeshMaterial>
    {
        if (id != lidMaterialId) { return std::nullopt; }
        MeshMaterial material;
        material.name = "Lid Paint";
        return material;
    };

    const SceneModelBatch batch =
        buildSceneModelBatch(scene, camera, meshes, {}, materialsProvider);

    CNA_EDITOR_EXPECT_EQ(batch.draws.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(batch.draws[0].partMaterials.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(batch.draws[0].partMaterials[0].first, std::string{"Lid"});
    CNA_EDITOR_EXPECT_EQ(batch.draws[0].partMaterials[0].second.name, std::string{"Lid Paint"});

    // The whole-model override is untouched: the two compose as "this model, except these parts".
    CNA_EDITOR_EXPECT(!batch.draws[0].materialOverride.has_value());
}

/**
 * @brief A per-part entry naming a part the model does not have is kept, not quietly dropped.
 *
 * The reason the list is keyed by name at all. An entry that matches nothing is *detectable* --
 * validation can say so, and a user can fix it -- whereas an index-keyed entry that shifted after a
 * reimport still matches a part, just the wrong one, and there is nothing anywhere to notice.
 * Dropping it here would throw away the very evidence that makes the choice worth making.
 */
CNA_EDITOR_TEST(APerPartMaterialNamingAMissingPartSurvivesSoItCanBeReported)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const MeshData mesh = makeTinyMesh();
    const Uuid modelId = Uuid::generate();
    const Uuid materialId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Crate", 0.0f, 0.0f);
    addModelRenderer(registry, entity, modelId);

    PropertyValue::StructureValue entry;
    entry.set("part", PropertyValue{std::string{"NoSuchPart"}});
    entry.set("material", PropertyValue{PropertyValue::AssetReference{materialId}});

    PropertyValue::ListValue materials;
    materials.items.push_back(PropertyValue{entry});
    entity.findComponent(BuiltinComponentIds::kModelRenderer)
        ->setProperty("materials", PropertyValue{materials});
    scene.addEntity(std::move(entity));

    EditorCamera3D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const MeshProvider meshes = [&](const Uuid& id) { return id == modelId ? &mesh : nullptr; };
    const MaterialProvider materialsProvider = [&](const Uuid&) { return MeshMaterial{}; };

    const SceneModelBatch batch =
        buildSceneModelBatch(scene, camera, meshes, {}, materialsProvider);

    CNA_EDITOR_EXPECT_EQ(batch.draws.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(batch.draws[0].partMaterials.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(batch.draws[0].partMaterials[0].first, std::string{"NoSuchPart"});
}

/**
 * @brief The rule that makes name-keying worth its cost: a part that is not there is reported.
 *
 * Without this the argument for keying by name over index is only half made. An index-keyed
 * override that shifted after a reimport still points at *a* part and there is nothing to notice;
 * a name that matches nothing is detectable, and this is what detects it.
 */
CNA_EDITOR_TEST(AMaterialAssignedToAPartTheModelDoesNotHaveIsReported)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    MeshData mesh = makeTinyMesh();
    mesh.parts[0].name = "Body";
    const Uuid modelId = Uuid::generate();

    EditorEntity entity = makeEntity(registry, "Crate", 0.0f, 0.0f);
    addModelRenderer(registry, entity, modelId);

    PropertyValue::StructureValue entry;
    entry.set("part", PropertyValue{std::string{"Lid"}});
    entry.set("material", PropertyValue{PropertyValue::AssetReference{Uuid::generate()}});

    PropertyValue::ListValue materials;
    materials.items.push_back(PropertyValue{entry});
    entity.findComponent(BuiltinComponentIds::kModelRenderer)
        ->setProperty("materials", PropertyValue{materials});
    scene.addEntity(std::move(entity));

    const MeshProvider meshes = [&](const Uuid& id) { return id == modelId ? &mesh : nullptr; };

    const std::vector<SceneIssue> issues = validateModelPartMaterials(scene, meshes);
    CNA_EDITOR_EXPECT_EQ(issues.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(issues[0].ruleId, std::string{"model-part-not-found"});
    CNA_EDITOR_EXPECT(issues[0].message.find("Lid") != std::string::npos);

    // A part that *is* there says nothing at all.
    mesh.parts[0].name = "Lid";
    CNA_EDITOR_EXPECT(validateModelPartMaterials(scene, meshes).empty());

    // And a model whose mesh has not been imported says nothing either: "not imported yet" is not
    // "wrong", and a rule that fired mid-scan would report every model in the project.
    const MeshProvider none = [](const Uuid&) -> const MeshData* { return nullptr; };
    CNA_EDITOR_EXPECT(validateModelPartMaterials(scene, none).empty());
}
