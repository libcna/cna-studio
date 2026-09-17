// SPDX-License-Identifier: MS-PL
/**
 * @file AssetDependencyTests.cpp
 * @brief The asset reference graph, in both directions (`plan.md` STUDIO-09012).
 *
 * The thing worth checking is the thing the index exists to answer and that nothing on disk
 * records: **what breaks if I delete this?** A scene holds a Uuid, not a path, so the only way to
 * know that `Level.cnascene` uses `player.png` is to read the scene — and an index that read it
 * wrongly would look exactly like an asset nothing references.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetDependencies.hpp"
#include "CNA/Studio/Assets/MaterialDocument.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/PrefabDocument.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace CNA::Studio;

namespace
{
    /** @brief A temporary project root, removed on the way out. */
    class ScopedProject
    {
    public:
        explicit ScopedProject(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-deps-" + name + "-" + std::to_string(counter()++));
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

        [[nodiscard]] std::string absolute(const std::string& relative) const
        {
            return (path_ / relative).generic_string();
        }

        void write(const std::string& relative, const std::string& contents) const
        {
            const std::filesystem::path file = path_ / relative;
            std::error_code code;
            std::filesystem::create_directories(file.parent_path(), code);
            std::ofstream stream{file, std::ios::binary};
            stream << contents;
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    /** @brief Registers @p path with @p type and returns its id. */
    Uuid track(AssetDatabase& assets, const std::string& path, AssetType type)
    {
        AssetRecord record;
        record.id = Uuid::generate();
        record.sourcePath = path;
        record.type = type;
        const Uuid id = record.id;
        CNA_STUDIO_EXPECT(assets.add(std::move(record)));
        return id;
    }

    /** @brief One entity with a sprite renderer pointing at @p textureId. */
    StudioEntity spriteEntity(const ComponentRegistry& registry, const std::string& name,
                              const Uuid& textureId)
    {
        StudioEntity entity{Uuid::generate(), name};
        StudioComponent sprite{BuiltinComponentIds::kSpriteRenderer};
        sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
        sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{textureId}});
        entity.addComponent(std::move(sprite));
        return entity;
    }

    bool holds(const std::vector<Uuid>& ids, const Uuid& id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
}

CNA_STUDIO_TEST(TheIndexAnswersBothDirectionsFromOnePass)
{
    // Both directions out of the same walk, because they are the same data read two ways. An index
    // that computed "referenced by" from one pass and "references" from another would be two things
    // that could disagree, and the disagreement would show up as a delete that looked safe.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"bothways"};
    project.write("Assets/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid textureId = track(assets, "Assets/player.png", AssetType::Texture2D);

    SceneDocument scene;
    scene.addEntity(spriteEntity(registry, "Hero", textureId));
    scene.addEntity(spriteEntity(registry, "Villain", textureId));
    CNA_STUDIO_EXPECT(scene.saveToFile(project.absolute("Assets/Level.cnascene")));
    const Uuid sceneId = track(assets, "Assets/Level.cnascene", AssetType::Scene);

    AssetDependencyIndex index;
    const AssetDependencyScan scan = index.build(assets, registry);
    CNA_STUDIO_EXPECT_EQ(scan.filesRead, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(scan.referencesFound, std::size_t{2});
    CNA_STUDIO_EXPECT(scan.warnings.empty());

    // Referenced by: every *place*, because two entities in one scene are two things to fix.
    const std::vector<AssetUsage> users = index.referencedBy(textureId);
    CNA_STUDIO_EXPECT_EQ(users.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(users.front().holderId.toString(), sceneId.toString());
    CNA_STUDIO_EXPECT_EQ(users.front().holderPath, std::string{"Assets/Level.cnascene"});
    CNA_STUDIO_EXPECT_EQ(users.front().propertyName, std::string{"texture"});
    CNA_STUDIO_EXPECT(users.front().holderType == AssetType::Scene);
    CNA_STUDIO_EXPECT(!users.front().entityName.empty());

    // And the row says which file *and* where in it: a row showing only the path would make two
    // references in one scene indistinguishable.
    CNA_STUDIO_EXPECT(users.front().describe().find("Level.cnascene") != std::string::npos);
    CNA_STUDIO_EXPECT(users.front().describe().find("texture") != std::string::npos);

    // References to: the distinct assets the scene points at, once each.
    const std::vector<Uuid> referenced = index.referencesTo(sceneId);
    CNA_STUDIO_EXPECT_EQ(referenced.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(holds(referenced, textureId));

    // A texture references nothing, and asking is not an error.
    CNA_STUDIO_EXPECT(index.referencesTo(textureId).empty());
    CNA_STUDIO_EXPECT(index.referencedBy(sceneId).empty());
}

CNA_STUDIO_TEST(AMaterialsTextureFieldsAreReferencesLikeAnyOther)
{
    // A material names its textures by id for the reason MaterialDocument's header gives, so they
    // are edges in the same graph. A dependency view that knew about scenes and not about materials
    // would call a texture unused and let the user delete it.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"material"};
    project.write("Assets/diffuse.png", "pixels");
    project.write("Assets/normal.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid diffuseId = track(assets, "Assets/diffuse.png", AssetType::Texture2D);
    const Uuid normalId = track(assets, "Assets/normal.png", AssetType::Texture2D);

    MaterialDocument material;
    material.diffuseTexture = diffuseId;
    material.normalTexture = normalId;
    project.write("Assets/Stone.cnamaterial", Json::write(material.toJson(), true));
    const Uuid materialId = track(assets, "Assets/Stone.cnamaterial", AssetType::Material);

    AssetDependencyIndex index;
    const AssetDependencyScan scan = index.build(assets, registry);
    CNA_STUDIO_EXPECT_EQ(scan.referencesFound, std::size_t{2});

    const std::vector<AssetUsage> users = index.referencedBy(diffuseId);
    CNA_STUDIO_EXPECT_EQ(users.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(users.front().holderId.toString(), materialId.toString());
    CNA_STUDIO_EXPECT_EQ(users.front().propertyName, std::string{"diffuseTexture"});

    const std::vector<Uuid> referenced = index.referencesTo(materialId);
    CNA_STUDIO_EXPECT_EQ(referenced.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(holds(referenced, diffuseId));
    CNA_STUDIO_EXPECT(holds(referenced, normalId));
}

CNA_STUDIO_TEST(APrefabsReferencesCountAndSoDoAnImportersOwn)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"prefab"};
    project.write("Assets/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid textureId = track(assets, "Assets/player.png", AssetType::Texture2D);

    SceneDocument source;
    const Uuid rootId = source.addEntity(spriteEntity(registry, "Hero", textureId));

    PrefabDocument prefab;
    CNA_STUDIO_EXPECT(prefab.captureFromScene(source, rootId, "Hero"));
    CNA_STUDIO_EXPECT(prefab.saveToFile(project.absolute("Assets/Hero.cnaprefab")));
    const Uuid prefabId = track(assets, "Assets/Hero.cnaprefab", AssetType::Prefab);

    // An importer-declared dependency: a model naming the texture it came with. It is an edge even
    // though nothing reads the model file here, because the record already says so.
    project.write("Assets/crate.gltf", "not really a gltf");
    const Uuid modelId = track(assets, "Assets/crate.gltf", AssetType::Model);
    assets.findMutable(modelId)->dependencies.push_back(textureId);

    AssetDependencyIndex index;
    (void)index.build(assets, registry);

    const std::vector<AssetUsage> users = index.referencedBy(textureId);
    CNA_STUDIO_EXPECT_EQ(users.size(), std::size_t{2});

    bool fromPrefab = false;
    bool fromModel = false;
    for (const AssetUsage& usage : users)
    {
        if (usage.holderId == prefabId) { fromPrefab = true; }
        if (usage.holderId == modelId)
        {
            fromModel = true;

            // No entity and no property: the file itself declares this one, and inventing a
            // property name for it would be a row that points at nothing to open.
            CNA_STUDIO_EXPECT(usage.entityName.empty());
            CNA_STUDIO_EXPECT(usage.propertyName.empty());
        }
    }
    CNA_STUDIO_EXPECT(fromPrefab);
    CNA_STUDIO_EXPECT(fromModel);
}

CNA_STUDIO_TEST(ANilReferenceIsAnEmptySlotRatherThanAnEdge)
{
    // A sprite that has not been given a texture yet points at nothing. A graph that recorded it
    // would answer "what uses nothing?" with the whole project, and every empty slot in it.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"nilref"};

    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    SceneDocument scene;
    scene.addEntity(spriteEntity(registry, "Unset", Uuid{}));
    CNA_STUDIO_EXPECT(scene.saveToFile(project.absolute("Assets/Level.cnascene")));
    (void)track(assets, "Assets/Level.cnascene", AssetType::Scene);

    AssetDependencyIndex index;
    const AssetDependencyScan scan = index.build(assets, registry);
    CNA_STUDIO_EXPECT_EQ(scan.filesRead, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(scan.referencesFound, std::size_t{0});
    CNA_STUDIO_EXPECT(index.isEmpty());
    CNA_STUDIO_EXPECT(index.referencedBy(Uuid{}).empty());
}

CNA_STUDIO_TEST(AReferenceOnAComponentNoPluginProvidesIsStillFound)
{
    // The walk is over each entity's *stored* properties rather than over its descriptor's, so a
    // component whose plugin failed to load has its references counted like any other. That file is
    // the one most likely to be broken, and a descriptor-driven walk would skip exactly it.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"unknowncomponent"};

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid atlasId = track(assets, "Assets/atlas.png", AssetType::Texture2D);
    const Uuid sceneId = track(assets, "Assets/Level.cnascene", AssetType::Scene);

    SceneDocument scene;
    StudioEntity entity{Uuid::generate(), "Decal"};
    StudioComponent exotic{"ThirdParty.Decal"};
    exotic.setProperty("atlas", PropertyValue{PropertyValue::AssetReference{atlasId}});
    entity.addComponent(std::move(exotic));
    scene.addEntity(std::move(entity));

    AssetDependencyIndex index;
    (void)index.build(assets, registry);
    index.observeScene(scene, sceneId, "Assets/Level.cnascene");

    const std::vector<AssetUsage> users = index.referencedBy(atlasId);
    CNA_STUDIO_EXPECT_EQ(users.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(users.front().componentTypeId, std::string{"ThirdParty.Decal"});
    CNA_STUDIO_EXPECT_EQ(users.front().propertyName, std::string{"atlas"});
}

/**
 * @brief The limitation `STUDIO-09017` exists to remove, asserted so it cannot change unnoticed.
 *
 * A component with no descriptor has its properties read back with their types *inferred from the
 * JSON shape* (`EntityJson.cpp`), and an asset reference is written as a bare UUID string — which
 * is indistinguishable from a string property that happens to hold one. So after a save and reload
 * the reference is a `String`, and neither this index nor `findMissingReferences` can see it.
 *
 * This is not new and not caused by the index: the same blind spot has always been there, and the
 * existing missing-reference test misses it only because it never round-trips. It is recorded here
 * rather than hidden because the case it costs — a plugin that failed to load — is precisely the
 * one a dependency view is opened for.
 */
CNA_STUDIO_TEST(AReferenceOnAnUnknownComponentIsLostByARoundTripUntilStudio09017)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"unknownroundtrip"};

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid atlasId = track(assets, "Assets/atlas.png", AssetType::Texture2D);

    SceneDocument scene;
    StudioEntity entity{Uuid::generate(), "Decal"};
    StudioComponent exotic{"ThirdParty.Decal"};
    exotic.setProperty("atlas", PropertyValue{PropertyValue::AssetReference{atlasId}});
    entity.addComponent(std::move(exotic));
    scene.addEntity(std::move(entity));
    CNA_STUDIO_EXPECT(scene.saveToFile(project.absolute("Assets/Level.cnascene")));
    (void)track(assets, "Assets/Level.cnascene", AssetType::Scene);

    SceneDocument reloaded;
    CNA_STUDIO_EXPECT(
        reloaded.loadFromFile(project.absolute("Assets/Level.cnascene"), registry).succeeded);

    // The value survives, which is what the inference promises. Its *type* does not.
    const StudioComponent& readBack = reloaded.getEntities().front().getComponents().front();
    const PropertyValue atlas = readBack.getProperty("atlas");
    CNA_STUDIO_EXPECT(atlas.getType() == PropertyType::String);
    CNA_STUDIO_EXPECT_EQ(atlas.get<std::string>(), atlasId.toString());

    AssetDependencyIndex index;
    (void)index.build(assets, registry);
    CNA_STUDIO_EXPECT(index.referencedBy(atlasId).empty());
}

CNA_STUDIO_TEST(AnUnreadableFileIsAWarningRatherThanAFailedIndex)
{
    // A project with one broken prefab still has a useful answer for every other asset in it. An
    // index that refused to build would take the dependency view away precisely when something is
    // wrong, which is when it is wanted.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"unreadable"};
    project.write("Assets/player.png", "pixels");
    project.write("Assets/Broken.cnascene", "{ this is not json");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid textureId = track(assets, "Assets/player.png", AssetType::Texture2D);
    (void)track(assets, "Assets/Broken.cnascene", AssetType::Scene);

    SceneDocument good;
    good.addEntity(spriteEntity(registry, "Hero", textureId));
    CNA_STUDIO_EXPECT(good.saveToFile(project.absolute("Assets/Good.cnascene")));
    (void)track(assets, "Assets/Good.cnascene", AssetType::Scene);

    AssetDependencyIndex index;
    const AssetDependencyScan scan = index.build(assets, registry);

    CNA_STUDIO_EXPECT_EQ(scan.warnings.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(scan.warnings.front().find("Broken.cnascene") != std::string::npos);
    CNA_STUDIO_EXPECT_EQ(scan.filesRead, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(index.referencedBy(textureId).size(), std::size_t{1});
}

CNA_STUDIO_TEST(TheOpenSceneReplacesWhatTheIndexReadFromDisk)
{
    // A dependency view built only from files is right until the user edits something, which is
    // exactly when they ask. Replacing rather than adding is the part that has to be right: an
    // observe that merged would leave the reference the user just removed in the answer for ever.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"openscene"};
    project.write("Assets/old.png", "pixels");
    project.write("Assets/new.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid oldId = track(assets, "Assets/old.png", AssetType::Texture2D);
    const Uuid newId = track(assets, "Assets/new.png", AssetType::Texture2D);

    SceneDocument onDisk;
    onDisk.addEntity(spriteEntity(registry, "Hero", oldId));
    CNA_STUDIO_EXPECT(onDisk.saveToFile(project.absolute("Assets/Level.cnascene")));
    const Uuid sceneId = track(assets, "Assets/Level.cnascene", AssetType::Scene);

    AssetDependencyIndex index;
    (void)index.build(assets, registry);
    CNA_STUDIO_EXPECT_EQ(index.referencedBy(oldId).size(), std::size_t{1});

    // The user retargets the sprite and has not saved.
    SceneDocument edited;
    edited.addEntity(spriteEntity(registry, "Hero", newId));
    index.observeScene(edited, sceneId, "Assets/Level.cnascene");

    CNA_STUDIO_EXPECT(index.referencedBy(oldId).empty());
    CNA_STUDIO_EXPECT_EQ(index.referencedBy(newId).size(), std::size_t{1});
    CNA_STUDIO_EXPECT(holds(index.referencesTo(sceneId), newId));
    CNA_STUDIO_EXPECT(!holds(index.referencesTo(sceneId), oldId));

    // An unsaved new scene is not yet an asset anything can reference, so observing it under a nil
    // id does nothing rather than filing every reference under one key that means "nowhere".
    SceneDocument untitled;
    untitled.addEntity(spriteEntity(registry, "Hero", oldId));
    index.observeScene(untitled, Uuid{}, {});
    CNA_STUDIO_EXPECT(index.referencedBy(oldId).empty());
}
