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
#include "CNA/Studio/Assets/AssetDocumentCache.hpp"
#include "CNA/Studio/Assets/AssetRelink.hpp"
#include "CNA/Studio/Core/StudioCommand.hpp"
#include "CNA/Studio/Assets/MaterialDocument.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/PrefabDocument.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"

#include <algorithm>
#include <memory>
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
 * @brief A reference on a component with no descriptor survives a save and a reload
 *        (`plan.md` STUDIO-09017).
 *
 * It did not, and the case it cost is the one a dependency view is opened for. A component the
 * build has no descriptor for had its properties read back with their types *inferred from the JSON
 * shape*, and an asset reference is written as a bare UUID string — indistinguishable from a string
 * property holding one. So after a round trip the reference was a `String`, and neither this index
 * nor `findMissingReferences` could see it.
 *
 * The file now records the type alongside the value, written whenever the type is *known* — which
 * is while the descriptor is present, the one moment the information exists to record.
 */
CNA_STUDIO_TEST(AReferenceOnAnUnknownComponentSurvivesARoundTrip)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"unknownroundtrip"};

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid atlasId = track(assets, "Assets/atlas.png", AssetType::Texture2D);
    const Uuid targetId = Uuid::generate();

    SceneDocument scene;
    StudioEntity entity{Uuid::generate(), "Decal"};
    StudioComponent exotic{"ThirdParty.Decal"};
    exotic.setProperty("atlas", PropertyValue{PropertyValue::AssetReference{atlasId}});
    exotic.setProperty("target", PropertyValue{PropertyValue::EntityReference{targetId}});
    exotic.setProperty("label", PropertyValue{std::string{"front"}});
    entity.addComponent(std::move(exotic));
    scene.addEntity(std::move(entity));
    CNA_STUDIO_EXPECT(scene.saveToFile(project.absolute("Assets/Level.cnascene")));
    (void)track(assets, "Assets/Level.cnascene", AssetType::Scene);

    SceneDocument reloaded;
    CNA_STUDIO_EXPECT(
        reloaded.loadFromFile(project.absolute("Assets/Level.cnascene"), registry).succeeded);

    const StudioComponent& readBack = reloaded.getEntities().front().getComponents().front();

    // Both reference kinds come back as themselves, and they are told apart: an entity reference
    // read as an asset one would be reported as a broken asset on every scene that has one.
    const PropertyValue atlas = readBack.getProperty("atlas");
    CNA_STUDIO_EXPECT(atlas.getType() == PropertyType::AssetReference);
    CNA_STUDIO_EXPECT_EQ(atlas.get<PropertyValue::AssetReference>().id.toString(),
                         atlasId.toString());

    const PropertyValue target = readBack.getProperty("target");
    CNA_STUDIO_EXPECT(target.getType() == PropertyType::EntityReference);
    CNA_STUDIO_EXPECT_EQ(target.get<PropertyValue::EntityReference>().id.toString(),
                         targetId.toString());

    // A string property is still a string. The hint records only the two types that serialise as a
    // bare UUID, so nothing else changes shape.
    CNA_STUDIO_EXPECT(readBack.getProperty("label").getType() == PropertyType::String);

    // And the hint is not itself a property: a component that grew one would write it back out
    // twice and show it in an inspector as a field nobody declared.
    CNA_STUDIO_EXPECT_EQ(readBack.getProperties().size(), std::size_t{3});

    // Which is what the dependency index was blind to.
    AssetDependencyIndex index;
    (void)index.build(assets, registry);
    CNA_STUDIO_EXPECT_EQ(index.referencedBy(atlasId).size(), std::size_t{1});
}

CNA_STUDIO_TEST(ASceneWrittenBeforeTheHintExistedStillLoads)
{
    // The change is additive, so no migration and no version bump: a file without the hint reads
    // exactly as it did, and an older Studio reading a file *with* one ignores a key it does not
    // know. Both directions have to keep working or this would be a format break wearing the
    // clothes of a bug fix.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"unknownnohint"};

    SceneDocument scene;
    StudioEntity entity{Uuid::generate(), "Decal"};
    StudioComponent exotic{"ThirdParty.Decal"};
    exotic.setProperty("atlas", PropertyValue{std::string{"not-a-uuid-at-all"}});
    exotic.setProperty("count", PropertyValue{3.0f});
    entity.addComponent(std::move(exotic));
    scene.addEntity(std::move(entity));

    // Saved with no reference-typed property, so no hint is written at all -- which is the shape
    // every scene written before this had.
    CNA_STUDIO_EXPECT(scene.saveToFile(project.absolute("Assets/Level.cnascene")));

    std::ifstream stream{project.absolute("Assets/Level.cnascene"), std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    CNA_STUDIO_EXPECT(text.find("$refs") == std::string::npos);

    SceneDocument reloaded;
    CNA_STUDIO_EXPECT(
        reloaded.loadFromFile(project.absolute("Assets/Level.cnascene"), registry).succeeded);

    const StudioComponent& readBack = reloaded.getEntities().front().getComponents().front();
    CNA_STUDIO_EXPECT(readBack.getProperty("atlas").getType() == PropertyType::String);
    CNA_STUDIO_EXPECT(readBack.getProperty("count").getType() == PropertyType::Float);
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

// ------------------------------------------------------------------------------------------------
// Finding a missing asset's file again (STUDIO-09013)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(RelinkPrefersTheUntrackedFileBecauseThatRepairTouchesNoScene)
{
    // The two repairs are different operations and offering the wrong one is expensive: pointing
    // the record back at its file edits nothing, and relinking references edits every scene that
    // used the asset. So an untracked file outranks a tracked one, always.
    const ScopedProject project{"relinkrank"};
    project.write("Assets/Art/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    // Tracked at a path where nothing is: the file moved and nothing has re-imported it.
    const Uuid missingId = track(assets, "Assets/player.png", AssetType::Texture2D);
    CNA_STUDIO_EXPECT(assets.isMissing(missingId));

    const std::vector<RelinkCandidate> candidates = studioRelinkCandidates(assets, missingId);
    CNA_STUDIO_EXPECT(!candidates.empty());
    if (candidates.empty()) { return; }

    CNA_STUDIO_EXPECT(candidates.front().kind == RelinkCandidate::Kind::UntrackedFile);
    CNA_STUDIO_EXPECT_EQ(candidates.front().path, std::string{"Assets/Art/player.png"});
    CNA_STUDIO_EXPECT_EQ(candidates.front().reason, std::string{"same name"});

    // A sidecar is metadata, not an asset: offering one would point the record at the file that
    // describes it.
    for (const RelinkCandidate& candidate : candidates)
    {
        CNA_STUDIO_EXPECT(candidate.path.find(".cnaasset") == std::string::npos);
    }
}

CNA_STUDIO_TEST(ARelinkKeepsTheIdSoNoSceneIsTouchedAndUndoPutsItBack)
{
    // The acceptance condition this shares with a move: the id does not change, so every reference
    // that was broken is correct again and every reference that was correct stays so.
    const ScopedProject project{"relinkapply"};
    project.write("Assets/Art/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid id = track(assets, "Assets/player.png", AssetType::Texture2D);

    CommandHistory history;
    auto relink = std::make_unique<RelinkAssetFileCommand>(assets, id, "Assets/Art/player.png");
    CNA_STUDIO_EXPECT(relink->isValid());
    history.execute(std::move(relink));

    CNA_STUDIO_EXPECT_EQ(assets.find(id)->sourcePath, std::string{"Assets/Art/player.png"});
    CNA_STUDIO_EXPECT(!assets.isMissing(id));

    // A sidecar at the new location, or the next scan gives the file a fresh id and breaks every
    // reference again -- which is the failure the relink exists to end.
    CNA_STUDIO_EXPECT(
        std::filesystem::exists(project.absolute("Assets/Art/player.png.cnaasset")));

    // Undo restores the state the user had, and that state was an asset whose file was missing.
    CNA_STUDIO_EXPECT(history.undo());
    CNA_STUDIO_EXPECT_EQ(assets.find(id)->sourcePath, std::string{"Assets/player.png"});
    CNA_STUDIO_EXPECT(assets.isMissing(id));
}

CNA_STUDIO_TEST(ARelinkIsRefusedWhereTheOtherRepairIsTheRightOne)
{
    const ScopedProject project{"relinkrefuse"};
    project.write("Assets/here.png", "pixels");
    project.write("Assets/Art/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    // Its file is still where the record says. That is a *move*, and MoveAssetCommand is the
    // operation that moves a file -- repointing here would leave the original behind for the next
    // scan to give a fresh id, turning one file into two assets silently.
    const Uuid present = track(assets, "Assets/here.png", AssetType::Texture2D);
    const RelinkAssetFileCommand move{assets, present, "Assets/Art/player.png"};
    CNA_STUDIO_EXPECT(!move.isValid());
    CNA_STUDIO_EXPECT(move.getError().find("still on disk") != std::string::npos);

    // The destination is already tracked, so the repair is to relink the references to *that*
    // asset rather than to give one file two records.
    const Uuid tracked = track(assets, "Assets/Art/player.png", AssetType::Texture2D);
    (void)tracked;
    const Uuid missing = track(assets, "Assets/gone.png", AssetType::Texture2D);
    const RelinkAssetFileCommand taken{assets, missing, "Assets/Art/player.png"};
    CNA_STUDIO_EXPECT(!taken.isValid());
    CNA_STUDIO_EXPECT(taken.getError().find("already tracked") != std::string::npos);

    // And a destination with nothing on it is not a repair at all.
    const RelinkAssetFileCommand nowhere{assets, missing, "Assets/nothing-here.png"};
    CNA_STUDIO_EXPECT(!nowhere.isValid());
}

CNA_STUDIO_TEST(ATrackedLookalikeIsOfferedWhenAScanAlreadyGaveTheFileANewId)
{
    // Two records exist and the scenes point at the one whose file is gone. This repair rewrites
    // references, so it is offered -- below every untracked file, and labelled as what it is.
    const ScopedProject project{"relinktracked"};
    project.write("Assets/Art/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid missingId = track(assets, "Assets/player.png", AssetType::Texture2D);
    const Uuid reimported = track(assets, "Assets/Art/player.png", AssetType::Texture2D);

    const std::vector<RelinkCandidate> candidates = studioRelinkCandidates(assets, missingId);
    CNA_STUDIO_EXPECT_EQ(candidates.size(), std::size_t{1});
    if (candidates.empty()) { return; }

    CNA_STUDIO_EXPECT(candidates.front().kind == RelinkCandidate::Kind::TrackedAsset);
    CNA_STUDIO_EXPECT_EQ(candidates.front().assetId.toString(), reimported.toString());
    CNA_STUDIO_EXPECT_EQ(candidates.front().reason, std::string{"same name and kind"});
}

CNA_STUDIO_TEST(NothingIsSuggestedForAnIdTheDatabaseDoesNotKnow)
{
    // The other kind of broken reference: a scene pointing at an asset that was never imported
    // here. There is no name to search on, so a suggestion would be a guess -- and its repair is
    // dropping the right asset onto the row, which needs no name.
    const ScopedProject project{"relinkunknown"};
    project.write("Assets/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    (void)track(assets, "Assets/player.png", AssetType::Texture2D);

    CNA_STUDIO_EXPECT(studioRelinkCandidates(assets, Uuid::generate()).empty());
}

CNA_STUDIO_TEST(ADifferentExtensionIsOfferedButNeverAboveAnExactName)
{
    // The extension decides the asset's type on the next scan, so a `.jpg` in place of a `.png` is
    // a worse answer than the `.png` two folders over -- and a better one than nothing.
    const ScopedProject project{"relinkextension"};
    project.write("Assets/Art/player.jpg", "pixels");
    project.write("Assets/Backup/player.png", "pixels");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    const Uuid missingId = track(assets, "Assets/player.png", AssetType::Texture2D);

    const std::vector<RelinkCandidate> candidates = studioRelinkCandidates(assets, missingId);
    CNA_STUDIO_EXPECT_EQ(candidates.size(), std::size_t{2});
    if (candidates.size() < 2) { return; }

    CNA_STUDIO_EXPECT_EQ(candidates[0].path, std::string{"Assets/Backup/player.png"});
    CNA_STUDIO_EXPECT_EQ(candidates[1].path, std::string{"Assets/Art/player.jpg"});
    CNA_STUDIO_EXPECT_EQ(candidates[1].reason, std::string{"same name, different extension"});
}

// ------------------------------------------------------------------------------------------------
// Documents read when they change rather than when they are drawn (STUDIO-30016)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheDocumentCacheOpensAFileOnceAndThenNotAgain)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"doccache"};

    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    MaterialDocument material;
    material.roughness = 0.25f;
    project.write("Assets/Stone.cnamaterial", Json::write(material.toJson(), true));
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const Uuid id = assets.findByPath("Assets/Stone.cnamaterial")->id;

    StudioAssetDocumentCache cache;
    const MaterialDocument* first = cache.material(assets, id);
    CNA_STUDIO_EXPECT(first != nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getFileReadCount(), std::uint64_t{1});

    // Ten more asks, no more reads. That is the whole task: this was one file open per frame for
    // as long as the material was selected.
    for (int i = 0; i < 10; ++i) { (void)cache.material(assets, id); }
    CNA_STUDIO_EXPECT_EQ(cache.getFileReadCount(), std::uint64_t{1});

    // An explicit invalidation is what Studio's own writes use, because a write changes the file
    // without changing the record.
    cache.invalidate(id);
    CNA_STUDIO_EXPECT(cache.material(assets, id) != nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getFileReadCount(), std::uint64_t{2});
}

CNA_STUDIO_TEST(ADocumentIsRereadWhenTheRecordsStampMoves)
{
    // Which is what the watcher updates, so an edit made outside Studio reaches the panel the same
    // way a deleted file reaches the browser -- within the watcher's interval, and with no syscall
    // on the frames in between.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"doccachestamp"};

    AssetDatabase assets;
    assets.setProjectRoot(project.root());

    MaterialDocument material;
    material.roughness = 0.25f;
    project.write("Assets/Stone.cnamaterial", Json::write(material.toJson(), true));
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const Uuid id = assets.findByPath("Assets/Stone.cnamaterial")->id;

    StudioAssetDocumentCache cache;
    const MaterialDocument* first = cache.material(assets, id);
    CNA_STUDIO_EXPECT(first != nullptr);
    if (first != nullptr) { CNA_STUDIO_EXPECT_EQ(first->roughness, 0.25f); }

    // Rewritten outside the editor, and the record's stamp moved with it -- which is what an
    // `AssetWatcher` poll does, spelled here without waiting half a second for one.
    material.roughness = 0.75f;
    project.write("Assets/Stone.cnamaterial", Json::write(material.toJson(), true));
    assets.findMutable(id)->sourceModifiedTime += 1;

    const MaterialDocument* second = cache.material(assets, id);
    CNA_STUDIO_EXPECT(second != nullptr);
    if (second != nullptr) { CNA_STUDIO_EXPECT_EQ(second->roughness, 0.75f); }
    CNA_STUDIO_EXPECT_EQ(cache.getFileReadCount(), std::uint64_t{2});
}

CNA_STUDIO_TEST(ABrokenDocumentIsCachedAsBrokenRatherThanReopenedEveryFrame)
{
    // The case most likely to be sitting on somebody's screen: a file that will not parse is a file
    // they are looking at *because* it will not parse.
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    const ScopedProject project{"doccachebroken"};
    project.write("Assets/Broken.cnamaterial", "{ not json");
    project.write("Assets/Broken.cnaprefab", "{ not json either");

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const Uuid materialId = assets.findByPath("Assets/Broken.cnamaterial")->id;
    const Uuid prefabId = assets.findByPath("Assets/Broken.cnaprefab")->id;

    StudioAssetDocumentCache cache;
    for (int i = 0; i < 5; ++i)
    {
        CNA_STUDIO_EXPECT(cache.material(assets, materialId) == nullptr);
        CNA_STUDIO_EXPECT(cache.prefab(assets, prefabId, registry) == nullptr);
    }
    CNA_STUDIO_EXPECT_EQ(cache.getFileReadCount(), std::uint64_t{2});

    // An id that is not a document of that kind is not a read at all, and not an entry either: a
    // texture asked for as a material must not cost an open.
    const Uuid texture = track(assets, "Assets/player.png", AssetType::Texture2D);
    CNA_STUDIO_EXPECT(cache.material(assets, texture) == nullptr);
    CNA_STUDIO_EXPECT(cache.prefab(assets, texture, registry) == nullptr);
    CNA_STUDIO_EXPECT_EQ(cache.getFileReadCount(), std::uint64_t{2});

    cache.invalidate();
    CNA_STUDIO_EXPECT_EQ(cache.getEntryCount(), std::size_t{0});
}
