// SPDX-License-Identifier: MS-PL
/**
 * @file CoreTests.cpp
 * @brief Tests for identity, JSON and the reflection metadata layer.
 */

#include "TestHarness.hpp"

#include <cmath>
#include <functional>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/ComponentDescriptor.hpp"
#include "CNA/Studio/Core/FormatMigration.hpp"
#include "CNA/Studio/Core/StudioMatrix.hpp"
#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/PropertyValue.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"

using namespace CNA::Studio;

CNA_STUDIO_TEST(UuidGeneratesDistinctValidValues)
{
    const Uuid first = Uuid::generate();
    const Uuid second = Uuid::generate();

    CNA_STUDIO_EXPECT(first.isValid());
    CNA_STUDIO_EXPECT(second.isValid());
    CNA_STUDIO_EXPECT(first != second);
    CNA_STUDIO_EXPECT_EQ(first.toString().size(), std::size_t{36});
}

CNA_STUDIO_TEST(UuidRoundTripsThroughItsTextualForm)
{
    const Uuid original = Uuid::generate();
    CNA_STUDIO_EXPECT(Uuid::parse(original.toString()) == original);
    CNA_STUDIO_EXPECT(Uuid::parse("{" + original.toString() + "}") == original);
}

CNA_STUDIO_TEST(UuidRejectsMalformedText)
{
    CNA_STUDIO_EXPECT(!Uuid::parse("").isValid());
    CNA_STUDIO_EXPECT(!Uuid::parse("not-a-uuid").isValid());
    // One hex digit short: must not silently succeed with a zero-padded value.
    CNA_STUDIO_EXPECT(!Uuid::parse("f392aaaa-bbbb-cccc-dddd-eeeeeeeeeee").isValid());
}

CNA_STUDIO_TEST(UuidDefaultIsNilAndInvalid)
{
    const Uuid nil;
    CNA_STUDIO_EXPECT(!nil.isValid());
    CNA_STUDIO_EXPECT_EQ(nil.toString(), std::string{"00000000-0000-0000-0000-000000000000"});
}

CNA_STUDIO_TEST(JsonParsesAndPreservesMemberOrder)
{
    const JsonParseResult parsed = Json::parse(R"({"z": 1, "a": 2, "m": [true, null, "x"]})");
    CNA_STUDIO_EXPECT(parsed.succeeded);

    const auto& members = parsed.value.getMembers();
    CNA_STUDIO_EXPECT_EQ(members.size(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(members[0].first, std::string{"z"});
    CNA_STUDIO_EXPECT_EQ(members[1].first, std::string{"a"});
    CNA_STUDIO_EXPECT_EQ(members[2].first, std::string{"m"});
    CNA_STUDIO_EXPECT_EQ(parsed.value["m"].getElements().size(), std::size_t{3});
}

CNA_STUDIO_TEST(JsonAcceptsCommentsAndTrailingCommas)
{
    const JsonParseResult parsed = Json::parse(R"({
        // a hand-written project file
        "name": "MyGame",
        "modules": ["cna-core", "cna-audio",],
    })");
    CNA_STUDIO_EXPECT(parsed.succeeded);
    CNA_STUDIO_EXPECT_EQ(parsed.value["name"].asString(), std::string{"MyGame"});
    CNA_STUDIO_EXPECT_EQ(parsed.value["modules"].getElements().size(), std::size_t{2});
}

CNA_STUDIO_TEST(JsonReportsFailureRatherThanThrowing)
{
    const JsonParseResult parsed = Json::parse(R"({"unterminated": )");
    CNA_STUDIO_EXPECT(!parsed.succeeded);
    CNA_STUDIO_EXPECT(!parsed.errorMessage.empty());
}

CNA_STUDIO_TEST(JsonAccessorsFallBackInsteadOfThrowing)
{
    const JsonParseResult parsed = Json::parse(R"({"count": 7})");
    CNA_STUDIO_EXPECT(parsed.succeeded);

    // Asking for the wrong alternative must degrade, never throw: a scene with one bad field has
    // to load with that field defaulted.
    CNA_STUDIO_EXPECT_EQ(parsed.value["count"].asString("fallback"), std::string{"fallback"});
    CNA_STUDIO_EXPECT_EQ(parsed.value["missing"].asInt(42), 42);
    CNA_STUDIO_EXPECT(parsed.value["missing"].isNull());
}

CNA_STUDIO_TEST(JsonRoundTripsThroughWriteAndParse)
{
    JsonValue original = JsonValue::makeObject();
    original.set("name", JsonValue{"Level\t01 \"quoted\""});
    original.set("count", JsonValue{3});
    original.set("ratio", JsonValue{0.5});
    original.set("flag", JsonValue{true});

    JsonValue nested = JsonValue::makeArray();
    nested.append(JsonValue{1});
    nested.append(JsonValue{2});
    original.set("values", std::move(nested));

    const JsonParseResult parsed = Json::parse(Json::write(original, true));
    CNA_STUDIO_EXPECT(parsed.succeeded);
    CNA_STUDIO_EXPECT_EQ(parsed.value["name"].asString(), std::string{"Level\t01 \"quoted\""});
    CNA_STUDIO_EXPECT_EQ(parsed.value["count"].asInt(), 3);
    CNA_STUDIO_EXPECT_EQ(parsed.value["ratio"].asNumber(), 0.5);
    CNA_STUDIO_EXPECT(parsed.value["flag"].asBoolean());
    CNA_STUDIO_EXPECT_EQ(parsed.value["values"].getElements().size(), std::size_t{2});
}

CNA_STUDIO_TEST(JsonWritesIntegersWithoutDecimalPoint)
{
    JsonValue json = JsonValue::makeObject();
    json.set("count", JsonValue{120});
    const std::string text = Json::write(json, false);
    CNA_STUDIO_EXPECT(text.find("120") != std::string::npos);
    CNA_STUDIO_EXPECT(text.find("120.0") == std::string::npos);
}

CNA_STUDIO_TEST(PropertyValueRoundTripsEveryType)
{
    const std::vector<PropertyValue> values{
        PropertyValue{true},
        PropertyValue{static_cast<std::int64_t>(-17)},
        PropertyValue{2.5f},
        PropertyValue{std::string{"hello"}},
        PropertyValue{PropertyValue::EnumValue{"FlipHorizontally"}},
        PropertyValue{StudioColor{1, 2, 3, 4}},
        PropertyValue{StudioVector2{1.0f, 2.0f}},
        PropertyValue{StudioVector3{1.0f, 2.0f, 3.0f}},
        PropertyValue{StudioVector4{1.0f, 2.0f, 3.0f, 4.0f}},
        PropertyValue{StudioQuaternion{0.0f, 0.0f, 0.7071f, 0.7071f}},
        PropertyValue{StudioRectangle{1, 2, 3, 4}},
        PropertyValue{PropertyValue::AssetReference{Uuid::generate()}},
        PropertyValue{PropertyValue::EntityReference{Uuid::generate()}},
    };

    for (const PropertyValue& value : values)
    {
        const PropertyValue restored = PropertyValue::fromJson(value.toJson(), value.getType());
        CNA_STUDIO_EXPECT_EQ(restored.toDisplayString(), value.toDisplayString());
        CNA_STUDIO_EXPECT(restored == value);
    }
}

CNA_STUDIO_TEST(PropertyValueTypeNamesRoundTrip)
{
    for (int index = 0; index <= static_cast<int>(PropertyType::EntityReference); ++index)
    {
        const auto type = static_cast<PropertyType>(index);
        CNA_STUDIO_EXPECT(parsePropertyType(toString(type)) == type);
    }
}

CNA_STUDIO_TEST(PropertyValueAbsentQuaternionDefaultsToIdentity)
{
    // An all-zero quaternion is not a rotation; a missing field must give identity instead.
    const PropertyValue restored = PropertyValue::fromJson(JsonValue{}, PropertyType::Quaternion);
    const StudioQuaternion rotation = restored.get<StudioQuaternion>();
    CNA_STUDIO_EXPECT_EQ(rotation.w, 1.0f);
    CNA_STUDIO_EXPECT_EQ(rotation.x, 0.0f);
}

CNA_STUDIO_TEST(PropertyValueGetReturnsFallbackOnTypeMismatch)
{
    const PropertyValue value{std::string{"text"}};
    CNA_STUDIO_EXPECT_EQ(value.get<float>(9.0f), 9.0f);
    CNA_STUDIO_EXPECT_EQ(value.get<std::string>(), std::string{"text"});
}

CNA_STUDIO_TEST(ComponentRegistryRegistersFindsAndReplaces)
{
    ComponentRegistry registry;

    ComponentDescriptor descriptor;
    descriptor.typeId = "Game.PlayerSpawn";
    descriptor.displayName = "Player Spawn";
    // Named rather than positional: a new PropertyDescriptor field would silently shift every
    // value along, and the compiler only catches it when the types happen to disagree.
    PropertyDescriptor health;
    health.name = "health";
    health.displayName = "Health";
    health.type = PropertyType::Integer;
    health.defaultValue = PropertyValue{100};
    descriptor.properties.push_back(std::move(health));

    CNA_STUDIO_EXPECT(registry.registerComponent(descriptor));
    CNA_STUDIO_EXPECT(registry.contains("Game.PlayerSpawn"));
    CNA_STUDIO_EXPECT_EQ(registry.getCount(), std::size_t{1});

    const ComponentDescriptor* found = registry.find("Game.PlayerSpawn");
    CNA_STUDIO_EXPECT(found != nullptr);
    CNA_STUDIO_EXPECT(found->findProperty("health") != nullptr);
    CNA_STUDIO_EXPECT(found->findProperty("mana") == nullptr);

    // Re-registering replaces rather than duplicating; this is what makes plugin reload possible.
    descriptor.displayName = "Player Start";
    CNA_STUDIO_EXPECT(registry.registerComponent(descriptor));
    CNA_STUDIO_EXPECT_EQ(registry.getCount(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(registry.find("Game.PlayerSpawn")->displayName, std::string{"Player Start"});

    CNA_STUDIO_EXPECT(registry.unregisterComponent("Game.PlayerSpawn"));
    CNA_STUDIO_EXPECT(!registry.contains("Game.PlayerSpawn"));
}

CNA_STUDIO_TEST(ComponentRegistryRejectsEmptyTypeId)
{
    ComponentRegistry registry;
    CNA_STUDIO_EXPECT(!registry.registerComponent(ComponentDescriptor{}));
    CNA_STUDIO_EXPECT_EQ(registry.getCount(), std::size_t{0});
}

// --------------------------------------------------------------------------------------------
// Format migration (plan.md ED-902)
//
// Every real format is at version 1, so these run against synthetic chains. That is the point:
// the mechanism has to be proven before the first real migration is written, not by it.
// --------------------------------------------------------------------------------------------

namespace
{
    /** @brief A document claiming @p version, carrying one renameable field. */
    JsonValue makeVersionedDocument(int version)
    {
        JsonValue document = JsonValue::makeObject();
        document.set("formatVersion", JsonValue{version});
        document.set("oldName", JsonValue{"value"});
        return document;
    }

    /** @brief A step renaming `oldName` to `newName`. */
    std::function<bool(JsonValue&, std::string&)> renameField(std::string from, std::string to)
    {
        return [from = std::move(from), to = std::move(to)](JsonValue& document, std::string& errorMessage) {
            if (!document.contains(from))
            {
                errorMessage = "'" + from + "' is missing";
                return false;
            }
            document.set(to, document[from]);
            document.remove(from);
            return true;
        };
    }
}

CNA_STUDIO_TEST(AMigrationChainUpgradesOneVersionAtATime)
{
    FormatMigrator migrator{"scene", 3};
    CNA_STUDIO_EXPECT(migrator.addMigration(1, "renamed oldName to middleName",
                                            renameField("oldName", "middleName")));
    CNA_STUDIO_EXPECT(migrator.addMigration(2, "renamed middleName to newName",
                                            renameField("middleName", "newName")));
    CNA_STUDIO_EXPECT_EQ(migrator.getMigrationCount(), std::size_t{2});

    JsonValue document = makeVersionedDocument(1);
    const FormatMigrationResult result = migrator.migrate(document);

    CNA_STUDIO_EXPECT(result.succeeded);
    CNA_STUDIO_EXPECT_EQ(result.fromVersion, 1);
    CNA_STUDIO_EXPECT_EQ(result.toVersion, 3);
    CNA_STUDIO_EXPECT_EQ(result.applied.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(result.changedAnything());

    // Both steps ran, in order, and the version was stamped by the migrator rather than by them.
    CNA_STUDIO_EXPECT_EQ(document["newName"].asString(), std::string{"value"});
    CNA_STUDIO_EXPECT(!document.contains("oldName"));
    CNA_STUDIO_EXPECT(!document.contains("middleName"));
    CNA_STUDIO_EXPECT_EQ(document["formatVersion"].asInt(), 3);

    // Running it again is a no-op, not a second rename. Migration has to be idempotent with
    // respect to the version it has already reached, or a re-save would corrupt the file.
    const FormatMigrationResult again = migrator.migrate(document);
    CNA_STUDIO_EXPECT(again.succeeded);
    CNA_STUDIO_EXPECT(!again.changedAnything());
    CNA_STUDIO_EXPECT_EQ(document["newName"].asString(), std::string{"value"});
}

CNA_STUDIO_TEST(AMigratorRefusesWhatItCannotUpgrade)
{
    FormatMigrator migrator{"scene", 3};

    // No step at all: better to refuse than to read a version-1 file with a version-3 reader,
    // which would silently substitute defaults for fields that moved.
    JsonValue old = makeVersionedDocument(1);
    const FormatMigrationResult noStep = migrator.migrate(old);
    CNA_STUDIO_EXPECT(!noStep.succeeded);
    CNA_STUDIO_EXPECT(noStep.errorMessage.find("no migration") != std::string::npos);

    // A gap in the chain stops at the gap rather than skipping it.
    CNA_STUDIO_EXPECT(migrator.addMigration(1, "step one", renameField("oldName", "middleName")));
    JsonValue gapped = makeVersionedDocument(1);
    const FormatMigrationResult gap = migrator.migrate(gapped);
    CNA_STUDIO_EXPECT(!gap.succeeded);
    CNA_STUDIO_EXPECT_EQ(gapped["formatVersion"].asInt(), 2);

    // A step that refuses reports its own reason.
    FormatMigrator refusing{"scene", 2};
    CNA_STUDIO_EXPECT(refusing.addMigration(1, "needs a field that is not there",
                                            renameField("absent", "present")));
    JsonValue missing = makeVersionedDocument(1);
    const FormatMigrationResult refused = refusing.migrate(missing);
    CNA_STUDIO_EXPECT(!refused.succeeded);
    CNA_STUDIO_EXPECT(refused.errorMessage.find("'absent' is missing") != std::string::npos);

    // A file from the future is refused by the same code that upgrades one from the past: both
    // answer "what version is this?", and splitting them is how a loader refuses what it could read.
    JsonValue future = makeVersionedDocument(9);
    const FormatMigrationResult ahead = migrator.migrate(future);
    CNA_STUDIO_EXPECT(!ahead.succeeded);
    CNA_STUDIO_EXPECT(ahead.errorMessage.find("newer than this build supports") != std::string::npos);

    // As is one with no version at all.
    JsonValue unversioned = JsonValue::makeObject();
    CNA_STUDIO_EXPECT(!migrator.migrate(unversioned).succeeded);
    JsonValue notAnObject{"not an object"};
    CNA_STUDIO_EXPECT(!migrator.migrate(notAnObject).succeeded);
}

CNA_STUDIO_TEST(AMigratorRefusesAnUnusableStep)
{
    FormatMigrator migrator{"scene", 3};

    CNA_STUDIO_EXPECT(migrator.addMigration(1, "first", renameField("a", "b")));

    // Two steps reading the same version would make the outcome depend on registration order.
    CNA_STUDIO_EXPECT(!migrator.addMigration(1, "duplicate", renameField("a", "c")));

    // A step that upgrades to or past the current version has nowhere to go.
    CNA_STUDIO_EXPECT(!migrator.addMigration(3, "at the top", renameField("a", "b")));
    CNA_STUDIO_EXPECT(!migrator.addMigration(0, "below the first version", renameField("a", "b")));
    CNA_STUDIO_EXPECT(!migrator.addMigration(2, "no function", {}));

    CNA_STUDIO_EXPECT_EQ(migrator.getMigrationCount(), std::size_t{1});
}

CNA_STUDIO_TEST(TheRealFormatsRunTheirChainsOnEveryLoad)
{
    // Empty today, and that is the intended state: the mechanism exists so that the first real
    // migration is a small tested addition rather than an emergency.
    CNA_STUDIO_EXPECT_EQ(getSceneFormatMigrator().getMigrationCount(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(getProjectFormatMigrator().getMigrationCount(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(getAssetFormatMigrator().getMigrationCount(), std::size_t{0});

    CNA_STUDIO_EXPECT_EQ(getSceneFormatMigrator().getCurrentVersion(), SceneDocument::kFormatVersion);
    CNA_STUDIO_EXPECT_EQ(getProjectFormatMigrator().getCurrentVersion(), Project::kFormatVersion);
    CNA_STUDIO_EXPECT_EQ(getAssetFormatMigrator().getCurrentVersion(), AssetDatabase::kFormatVersion);
}

CNA_STUDIO_TEST(TheSceneLoaderReadsTheUpgradedDocumentNotTheOriginal)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    // A synthetic version 1 whose entity list moved: proof that the loader runs the chain and then
    // reads what came out of it, rather than running it and reading the original anyway.
    FormatMigrator migrator{"scene", 2};
    CNA_STUDIO_EXPECT(migrator.addMigration(1, "moved 'objects' to 'entities'",
                                            renameField("objects", "entities")));

    JsonValue entity = JsonValue::makeObject();
    entity.set("id", JsonValue{Uuid::generate().toString()});
    entity.set("name", JsonValue{"Player"});
    entity.set("components", JsonValue::makeObject());

    JsonValue objects = JsonValue::makeArray();
    objects.append(std::move(entity));

    JsonValue document = JsonValue::makeObject();
    document.set("formatVersion", JsonValue{1});
    document.set("sceneId", JsonValue{Uuid::generate().toString()});
    document.set("name", JsonValue{"Level01"});
    document.set("objects", std::move(objects));

    SceneDocument scene;
    const SceneLoadResult result = scene.loadFromJson(document, registry, &migrator);

    CNA_STUDIO_EXPECT(result.succeeded);
    CNA_STUDIO_EXPECT_EQ(scene.getEntityCount(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(scene.getEntities().front().getName(), std::string{"Player"});

    // The upgrade is reported rather than performed silently, and the caller's document is left
    // exactly as it was -- a loader that edited its input would surprise anyone reusing it.
    CNA_STUDIO_EXPECT_EQ(result.warnings.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(result.warnings.front().find("moved 'objects' to 'entities'") != std::string::npos);
    CNA_STUDIO_EXPECT(document.contains("objects"));
    CNA_STUDIO_EXPECT_EQ(document["formatVersion"].asInt(), 1);
}

CNA_STUDIO_TEST(TheProjectLoaderRunsItsChainToo)
{
    FormatMigrator migrator{"project", 2};
    CNA_STUDIO_EXPECT(migrator.addMigration(1, "renamed 'title' to 'name'", renameField("title", "name")));

    JsonValue document = JsonValue::makeObject();
    document.set("formatVersion", JsonValue{1});
    document.set("title", JsonValue{"Upgraded"});
    document.set("kind", JsonValue{"CnaNative"});

    Project project;
    const ProjectLoadResult result = project.loadFromJson(document, &migrator);

    CNA_STUDIO_EXPECT(result.succeeded);
    CNA_STUDIO_EXPECT_EQ(project.getName(), std::string{"Upgraded"});
    CNA_STUDIO_EXPECT(!result.warnings.empty());
}

// --------------------------------------------------------------------------------------------
// List properties (plan.md ED-311)
// --------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AListRoundTripsThroughJsonWithItsDeclaredElementType)
{
    PropertyValue::ListValue tags;
    tags.items.emplace_back(std::string{"ground"});
    tags.items.emplace_back(std::string{"solid"});

    const PropertyValue value{tags};
    CNA_STUDIO_EXPECT(value.getType() == PropertyType::List);
    CNA_STUDIO_EXPECT_EQ(value.toDisplayString(), std::string{"2 items"});

    // A plain array with no per-element type tag: the descriptor is the one source of truth for
    // what the elements are.
    const JsonValue json = value.toJson();
    CNA_STUDIO_EXPECT(json.isArray());
    CNA_STUDIO_EXPECT_EQ(json.getElements().size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(json.getElements().front().asString(), std::string{"ground"});

    const PropertyValue read = PropertyValue::fromJson(json, PropertyType::List, PropertyType::String);
    CNA_STUDIO_EXPECT(read == value);

    // A list of vectors is a list of arrays, and nests exactly one level -- which is all it has to.
    PropertyValue::ListValue points;
    points.items.emplace_back(StudioVector2{1.0f, 2.0f});
    points.items.emplace_back(StudioVector2{3.0f, 4.0f});
    const PropertyValue vectors{points};
    CNA_STUDIO_EXPECT(PropertyValue::fromJson(vectors.toJson(), PropertyType::List,
                                              PropertyType::Vector2) == vectors);

    // One item reads as "1 item", because "1 items" is the kind of detail that makes a UI look
    // machine-written.
    PropertyValue::ListValue single;
    single.items.emplace_back(std::int64_t{7});
    CNA_STUDIO_EXPECT_EQ(PropertyValue{single}.toDisplayString(), std::string{"1 item"});
}

CNA_STUDIO_TEST(AListWithNoDeclaredElementTypeReadsBackEmpty)
{
    JsonValue json = JsonValue::makeArray();
    json.append(JsonValue{"ground"});
    json.append(JsonValue{"solid"});

    // Guessing would produce a list the inspector cannot edit and the next save would write out in
    // a shape nothing declared. Empty is the honest answer.
    const PropertyValue read = PropertyValue::fromJson(json, PropertyType::List);
    CNA_STUDIO_EXPECT(read.getType() == PropertyType::List);
    CNA_STUDIO_EXPECT(read.get<PropertyValue::ListValue>().items.empty());

    // Nor do lists nest: a list of lists is a table and deserves its own type.
    CNA_STUDIO_EXPECT(PropertyValue::fromJson(json, PropertyType::List, PropertyType::List)
                          .get<PropertyValue::ListValue>()
                          .items.empty());
}

CNA_STUDIO_TEST(TheListTypeNameIsAppendedRatherThanInserted)
{
    // toString(PropertyType) is on the editor-to-player wire, so every existing name has to stay
    // exactly where it was. This asserts the whole table, which is the only way to notice an
    // insertion in the middle.
    CNA_STUDIO_EXPECT_EQ(std::string{toString(PropertyType::List)}, std::string{"list"});
    CNA_STUDIO_EXPECT(parsePropertyType("list") == PropertyType::List);

    CNA_STUDIO_EXPECT_EQ(std::string{toString(PropertyType::Boolean)}, std::string{"bool"});
    CNA_STUDIO_EXPECT_EQ(std::string{toString(PropertyType::Vector3)}, std::string{"vector3"});
    CNA_STUDIO_EXPECT_EQ(std::string{toString(PropertyType::AssetReference)}, std::string{"asset"});
    CNA_STUDIO_EXPECT_EQ(std::string{toString(PropertyType::EntityReference)}, std::string{"entity"});
    CNA_STUDIO_EXPECT(parsePropertyType("vector3") == PropertyType::Vector3);
    CNA_STUDIO_EXPECT(parsePropertyType("nonsense") == PropertyType::None);
}

CNA_STUDIO_TEST(ASceneRoundTripsAListAndDoesNotLoseAnUnregisteredOne)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    ComponentDescriptor tagged;
    tagged.typeId = "Game.Tagged";
    tagged.displayName = "Tagged";
    PropertyDescriptor tags;
    tags.name = "tags";
    tags.type = PropertyType::List;
    tags.elementType = PropertyType::String;
    tags.defaultValue = PropertyValue{PropertyValue::ListValue{}};
    tagged.properties.push_back(std::move(tags));
    CNA_STUDIO_EXPECT(registry.registerComponent(tagged));

    PropertyValue::ListValue list;
    list.items.emplace_back(std::string{"ground"});
    list.items.emplace_back(std::string{"solid"});

    SceneDocument scene;
    StudioEntity entity{Uuid::generate(), "Tile"};
    StudioComponent component{"Game.Tagged"};
    component.setProperty("tags", PropertyValue{list});
    entity.addComponent(std::move(component));
    scene.addEntity(std::move(entity));

    SceneDocument reloaded;
    CNA_STUDIO_EXPECT(reloaded.loadFromJson(scene.toJson(), registry).succeeded);

    const StudioComponent* readBack = reloaded.getEntities().front().findComponent("Game.Tagged");
    CNA_STUDIO_EXPECT(readBack != nullptr);
    CNA_STUDIO_EXPECT(readBack->getProperty("tags") == PropertyValue{list});

    // The same scene opened by a build whose plugin is missing must save back byte for byte.
    // Before lists existed this array became the empty string and the field was silently lost.
    ComponentRegistry withoutPlugin;
    registerBuiltinComponents(withoutPlugin);

    SceneDocument blind;
    const SceneLoadResult blindLoad = blind.loadFromJson(scene.toJson(), withoutPlugin);
    CNA_STUDIO_EXPECT(blindLoad.succeeded);
    CNA_STUDIO_EXPECT(!blindLoad.warnings.empty());
    CNA_STUDIO_EXPECT_EQ(Json::write(blind.toJson()), Json::write(scene.toJson()));
}

namespace
{
    /** @brief Returns true when @p a and @p b agree to within @p tolerance. */
    bool matrixNearlyEqual(float a, float b, float tolerance = 0.0005f)
    {
        return std::abs(a - b) <= tolerance;
    }

    /** @brief Fails unless every field of @p actual matches @p expected. */
    void expectMatrixNearlyEqual(const StudioMatrix& actual, const StudioMatrix& expected)
    {
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m11, expected.m11));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m12, expected.m12));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m13, expected.m13));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m14, expected.m14));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m21, expected.m21));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m22, expected.m22));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m23, expected.m23));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m24, expected.m24));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m31, expected.m31));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m32, expected.m32));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m33, expected.m33));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m34, expected.m34));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m41, expected.m41));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m42, expected.m42));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m43, expected.m43));
        CNA_STUDIO_EXPECT(matrixNearlyEqual(actual.m44, expected.m44));
    }
}

CNA_STUDIO_TEST(MatrixMultiplicationAppliesTheLeftTransformFirst)
{
    const StudioMatrix translate = createTranslation(StudioVector3{10.0f, 0.0f, 0.0f});
    const StudioMatrix scaleBy = createScale(StudioVector3{2.0f, 2.0f, 2.0f});

    // Row vectors: `multiply(a, b)` is "a, then b". Translating and *then* scaling scales the
    // translation as well; the other order does not. If these two ever agree, the convention has
    // been transposed somewhere and every camera built on it is subtly wrong.
    const StudioVector3 translateThenScale =
        transformPosition(multiply(translate, scaleBy), StudioVector3{});
    const StudioVector3 scaleThenTranslate =
        transformPosition(multiply(scaleBy, translate), StudioVector3{});

    CNA_STUDIO_EXPECT(matrixNearlyEqual(translateThenScale.x, 20.0f));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(scaleThenTranslate.x, 10.0f));

    // Identity on either side leaves a matrix alone.
    expectMatrixNearlyEqual(multiply(translate, StudioMatrix{}), translate);
    expectMatrixNearlyEqual(multiply(StudioMatrix{}, translate), translate);
}

CNA_STUDIO_TEST(MatrixInverseUndoesTheMatrix)
{
    const StudioMatrix composed =
        multiply(multiply(createScale(StudioVector3{2.0f, 3.0f, 4.0f}),
                          createFromQuaternion(quaternionFromEulerDegrees(StudioVector3{20.0f, 35.0f, 10.0f}))),
                 createTranslation(StudioVector3{5.0f, -2.0f, 7.0f}));

    const std::optional<StudioMatrix> inverse = invert(composed);
    CNA_STUDIO_EXPECT(inverse.has_value());
    expectMatrixNearlyEqual(multiply(composed, *inverse), StudioMatrix{});

    // A projection is not affine, and screen-to-world inverts exactly that one.
    const StudioMatrix projection = createPerspectiveFieldOfView(0.9f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const std::optional<StudioMatrix> projectionInverse = invert(projection);
    CNA_STUDIO_EXPECT(projectionInverse.has_value());
    expectMatrixNearlyEqual(multiply(projection, *projectionInverse), StudioMatrix{});

    // A singular matrix reports that it is singular rather than returning something plausible.
    CNA_STUDIO_EXPECT(!invert(createScale(StudioVector3{1.0f, 0.0f, 1.0f})).has_value());
}

CNA_STUDIO_TEST(MatrixRotationAgreesWithQuaternionRotation)
{
    // Two implementations of "rotate this vector" have to agree, or the gizmos and the camera
    // would each be right about a different world.
    const StudioQuaternion rotation = quaternionFromEulerDegrees(StudioVector3{15.0f, -40.0f, 25.0f});
    const StudioVector3 sample{1.0f, 2.0f, -3.0f};

    const StudioVector3 byQuaternion = rotate(rotation, sample);
    const StudioVector3 byMatrix = transformDirection(createFromQuaternion(rotation), sample);

    CNA_STUDIO_EXPECT(matrixNearlyEqual(byQuaternion.x, byMatrix.x));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(byQuaternion.y, byMatrix.y));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(byQuaternion.z, byMatrix.z));
}

CNA_STUDIO_TEST(LookAtPutsTheTargetDownTheNegativeZAxis)
{
    const StudioMatrix view = createLookAt(StudioVector3{0.0f, 0.0f, 10.0f}, StudioVector3{},
                                           StudioVector3{0.0f, 1.0f, 0.0f});

    // Right-handed: the camera looks down its own -Z, so a target ten units away sits at z = -10.
    // Getting the sign wrong yields a view that renders the scene mirrored and otherwise correct.
    const StudioVector3 target = transformPosition(view, StudioVector3{});
    CNA_STUDIO_EXPECT(matrixNearlyEqual(target.x, 0.0f));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(target.y, 0.0f));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(target.z, -10.0f));

    // World +X is to the camera's right when it looks down -Z from +Z.
    const StudioVector3 right = transformPosition(view, StudioVector3{3.0f, 0.0f, 0.0f});
    CNA_STUDIO_EXPECT(matrixNearlyEqual(right.x, 3.0f));

    // Looking straight down is the degenerate case for the up vector, and must still produce a
    // usable matrix rather than a field of NaNs an orbit would carry into every projected point.
    const StudioMatrix fromAbove = createLookAt(StudioVector3{0.0f, 10.0f, 0.0f}, StudioVector3{},
                                                StudioVector3{0.0f, 1.0f, 0.0f});
    const StudioVector3 below = transformPosition(fromAbove, StudioVector3{});
    CNA_STUDIO_EXPECT(matrixNearlyEqual(below.z, -10.0f));
}

CNA_STUDIO_TEST(ProjectionsMapTheDepthRangeToZeroAndOne)
{
    const StudioMatrix perspective = createPerspectiveFieldOfView(1.0f, 1.0f, 1.0f, 100.0f);

    // XNA's convention, not OpenGL's: the near plane is 0 and the far plane is 1.
    float w = 0.0f;
    const StudioVector3 atNear = transformWithPerspective(perspective, StudioVector3{0.0f, 0.0f, -1.0f}, w);
    CNA_STUDIO_EXPECT(matrixNearlyEqual(atNear.z, 0.0f));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(w, 1.0f));

    const StudioVector3 atFar = transformWithPerspective(perspective, StudioVector3{0.0f, 0.0f, -100.0f}, w);
    CNA_STUDIO_EXPECT(matrixNearlyEqual(atFar.z, 1.0f));

    // Behind the eye, w goes negative -- which is the only signal a caller has that the divided
    // coordinate it just computed is meaningless rather than merely off-screen.
    const StudioVector3 behind =
        transformWithPerspective(perspective, StudioVector3{0.0f, 0.0f, 5.0f}, w);
    static_cast<void>(behind);
    CNA_STUDIO_EXPECT(w < 0.0f);

    const StudioMatrix orthographic = createOrthographic(20.0f, 10.0f, 1.0f, 100.0f);
    const StudioVector3 edge = transformPosition(orthographic, StudioVector3{10.0f, 5.0f, -1.0f});
    CNA_STUDIO_EXPECT(matrixNearlyEqual(edge.x, 1.0f));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(edge.y, 1.0f));
    CNA_STUDIO_EXPECT(matrixNearlyEqual(edge.z, 0.0f));
}

/**
 * @brief ED-311's `Structure`, built at last because ED-410 asked: it round-trips against a schema.
 *
 * Two properties, and the second is the one that matters. A structure encodes as a plain JSON
 * object, so writing needs no schema -- a value knows its own field names. *Reading* does, and the
 * read is driven by the schema rather than by the file: a field the JSON omits comes back as its
 * declared default, so a structure can gain a field without every document already written
 * becoming a document with a hole in it, and a field the schema does not declare is dropped rather
 * than carried through into a shape nothing can read back.
 */
CNA_STUDIO_TEST(AStructureRoundTripsAgainstItsDeclaredSchemaRatherThanItsFile)
{
    PropertyDescriptor part;
    part.name = "part";
    part.type = PropertyType::String;
    part.defaultValue = PropertyValue{std::string{"Body"}};

    PropertyDescriptor material;
    material.name = "material";
    material.type = PropertyType::AssetReference;
    material.defaultValue = PropertyValue{PropertyValue::AssetReference{}};

    PropertyDescriptor entry;
    entry.name = "entry";
    entry.type = PropertyType::Structure;
    entry.structureFields = {part, material};

    const Uuid materialId = Uuid::generate();

    PropertyValue::StructureValue value;
    value.set("part", PropertyValue{std::string{"Lid"}});
    value.set("material", PropertyValue{PropertyValue::AssetReference{materialId}});

    const JsonValue encoded = PropertyValue{value}.toJson();
    const PropertyValue decoded = propertyValueFromJson(encoded, entry);

    CNA_STUDIO_EXPECT(decoded.getType() == PropertyType::Structure);
    const auto& fields = decoded.get<PropertyValue::StructureValue>();
    CNA_STUDIO_EXPECT_EQ(fields.find("part")->get<std::string>(), std::string{"Lid"});
    CNA_STUDIO_EXPECT(fields.find("material")->get<PropertyValue::AssetReference>().id == materialId);

    // A field the file never had reads as the declared default, not as nothing.
    JsonValue partial = JsonValue::makeObject();
    partial.set("material", JsonValue{materialId.toString()});
    const PropertyValue defaulted = propertyValueFromJson(partial, entry);
    CNA_STUDIO_EXPECT_EQ(defaulted.get<PropertyValue::StructureValue>().find("part")->get<std::string>(),
                         std::string{"Body"});

    // A field the *schema* never had is dropped rather than carried through.
    JsonValue extra = encoded;
    extra.set("unexpected", JsonValue{std::string{"kept?"}});
    const PropertyValue trimmed = propertyValueFromJson(extra, entry);

    // Held in a named value rather than re-read through `get<>()` per assertion: `get<>()` hands
    // back a copy, and `EXPECT_EQ` binds a reference to what it is given -- so indexing into a
    // temporary's vector would leave that reference dangling. It crashed exactly that way once.
    const PropertyValue::StructureValue trimmedFields = trimmed.get<PropertyValue::StructureValue>();

    CNA_STUDIO_EXPECT(trimmedFields.find("unexpected") == nullptr);
    CNA_STUDIO_EXPECT_EQ(trimmedFields.fields.size(), std::size_t{2});

    // The field order is the declared order, both in the value and in the JSON it writes -- which
    // is what lets a document round-trip in the shape it arrived in rather than in hash order.
    CNA_STUDIO_EXPECT_EQ(trimmedFields.fields[0].first, std::string{"part"});
}

/** @brief A list of structures decodes element by element against the same schema. */
CNA_STUDIO_TEST(AListOfStructuresDecodesEveryElementAgainstTheSchema)
{
    PropertyDescriptor part;
    part.name = "part";
    part.type = PropertyType::String;
    part.defaultValue = PropertyValue{std::string{}};

    PropertyDescriptor list;
    list.name = "materials";
    list.type = PropertyType::List;
    list.elementType = PropertyType::Structure;
    list.structureFields = {part};

    JsonValue array = JsonValue::makeArray();
    for (const char* name : {"Lid", "Body"})
    {
        JsonValue element = JsonValue::makeObject();
        element.set("part", JsonValue{std::string{name}});
        array.append(element);
    }

    const PropertyValue decoded = propertyValueFromJson(array, list);
    CNA_STUDIO_EXPECT(decoded.getType() == PropertyType::List);

    const PropertyValue::ListValue list_ = decoded.get<PropertyValue::ListValue>();
    CNA_STUDIO_EXPECT_EQ(list_.items.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(
        list_.items[0].get<PropertyValue::StructureValue>().find("part")->get<std::string>(),
        std::string{"Lid"});

    // The type name is on the editor-to-player wire, so it has to be the appended one rather than
    // anything inserted among the names that were already there.
    CNA_STUDIO_EXPECT_EQ(std::string{toString(PropertyType::Structure)}, std::string{"structure"});
    CNA_STUDIO_EXPECT(parsePropertyType("structure") == PropertyType::Structure);
}
