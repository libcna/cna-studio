// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SceneDocument.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Scene/EntityJson.hpp"

namespace CNA::Editor
{

    const FormatMigrator& getSceneFormatMigrator()
    {
        // Constructed once. Empty today: the scene format has only ever been at version 1, and a
        // step is added here the first time that changes.
        static const FormatMigrator migrator{"scene", SceneDocument::kFormatVersion};
        return migrator;
    }

    SceneDocument::SceneDocument() : sceneId_(Uuid::generate()) {}

    const EditorEntity* SceneDocument::findEntity(const Uuid& id) const
    {
        const auto found = indexById_.find(id);
        return found == indexById_.end() ? nullptr : &entities_[found->second];
    }

    EditorEntity* SceneDocument::findEntity(const Uuid& id)
    {
        const auto found = indexById_.find(id);
        return found == indexById_.end() ? nullptr : &entities_[found->second];
    }

    Uuid SceneDocument::addEntity(EditorEntity entity)
    {
        if (!entity.getId().isValid()) { entity.setId(Uuid::generate()); }
        if (indexById_.find(entity.getId()) != indexById_.end()) { return Uuid{}; }

        const Uuid id = entity.getId();
        indexById_.emplace(id, entities_.size());
        entities_.push_back(std::move(entity));
        return id;
    }

    std::vector<EditorEntity> SceneDocument::removeEntityRecursive(const Uuid& id)
    {
        std::vector<EditorEntity> removed;
        if (findEntity(id) == nullptr) { return removed; }

        // Breadth-first so the result is ordered parents-before-children, which is what lets
        // DeleteEntityCommand::undo() re-add them without ever pointing at a missing parent.
        std::vector<Uuid> pending{id};
        std::unordered_set<Uuid> doomed;
        while (!pending.empty())
        {
            const Uuid current = pending.front();
            pending.erase(pending.begin());
            if (!doomed.insert(current).second) { continue; }
            for (const Uuid& child : getChildren(current)) { pending.push_back(child); }
        }

        for (auto iterator = entities_.begin(); iterator != entities_.end();)
        {
            if (doomed.count(iterator->getId()) > 0)
            {
                removed.push_back(*iterator);
                iterator = entities_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }

        // Preserve parents-first order regardless of storage order: an entity whose parent is
        // also being removed must come after it.
        std::stable_sort(removed.begin(), removed.end(),
                         [&](const EditorEntity& lhs, const EditorEntity& rhs) {
                             const bool lhsIsRootOfDeletion = lhs.getId() == id;
                             const bool rhsIsRootOfDeletion = rhs.getId() == id;
                             if (lhsIsRootOfDeletion != rhsIsRootOfDeletion) { return lhsIsRootOfDeletion; }
                             return false;
                         });

        rebuildIndex();
        return removed;
    }

    bool SceneDocument::reparentEntity(const Uuid& childId, const Uuid& newParentId)
    {
        EditorEntity* child = findEntity(childId);
        if (child == nullptr) { return false; }

        if (newParentId.isValid())
        {
            if (findEntity(newParentId) == nullptr) { return false; }
            // Rejecting the cycle here rather than detecting it later is the whole point: an
            // entity graph with a cycle has no roots, and every hierarchy walk becomes infinite.
            if (isAncestorOf(childId, newParentId)) { return false; }
        }

        child->setParentId(newParentId);
        return true;
    }

    std::vector<Uuid> SceneDocument::getChildren(const Uuid& parentId) const
    {
        std::vector<const EditorEntity*> children;
        for (const EditorEntity& entity : entities_)
        {
            if (entity.getParentId() == parentId) { children.push_back(&entity); }
        }

        std::stable_sort(children.begin(), children.end(),
                         [](const EditorEntity* lhs, const EditorEntity* rhs) {
                             if (lhs->getSortOrder() != rhs->getSortOrder())
                             {
                                 return lhs->getSortOrder() < rhs->getSortOrder();
                             }
                             return lhs->getName() < rhs->getName();
                         });

        std::vector<Uuid> ids;
        ids.reserve(children.size());
        for (const EditorEntity* entity : children) { ids.push_back(entity->getId()); }
        return ids;
    }

    std::vector<Uuid> SceneDocument::getRootEntities() const
    {
        return getChildren(Uuid{});
    }

    bool SceneDocument::isAncestorOf(const Uuid& ancestorId, const Uuid& descendantId) const
    {
        Uuid current = descendantId;
        // Bounded by the entity count so that an already-corrupt graph (a cycle introduced by a
        // hand-edited file) cannot hang the editor here.
        for (std::size_t step = 0; step <= entities_.size(); ++step)
        {
            if (!current.isValid()) { return false; }
            if (current == ancestorId) { return true; }
            const EditorEntity* entity = findEntity(current);
            if (entity == nullptr) { return false; }
            current = entity->getParentId();
        }
        return false;
    }

    namespace
    {
        /** @brief Writes an `EditorColor` as the four-number array every other colour uses. */
        JsonValue colorToJson(const EditorColor& color)
        {
            JsonValue array = JsonValue::makeArray();
            array.append(JsonValue{static_cast<double>(color.r)});
            array.append(JsonValue{static_cast<double>(color.g)});
            array.append(JsonValue{static_cast<double>(color.b)});
            array.append(JsonValue{static_cast<double>(color.a)});
            return array;
        }

        /** @brief Reads a four-number colour array, keeping @p fallback for anything missing. */
        EditorColor colorFromJson(const JsonValue& json, const EditorColor& fallback)
        {
            if (!json.isArray() || json.getElements().size() < 4) { return fallback; }

            const auto channel = [](const JsonValue& value, std::uint8_t otherwise)
            {
                const double clamped = std::min(255.0, std::max(0.0, value.asNumber(static_cast<double>(otherwise))));
                return static_cast<std::uint8_t>(clamped);
            };

            const std::vector<JsonValue>& elements = json.getElements();
            return EditorColor{channel(elements[0], fallback.r), channel(elements[1], fallback.g),
                               channel(elements[2], fallback.b), channel(elements[3], fallback.a)};
        }
    }

    JsonValue SceneDocument::toJson() const
    {
        JsonValue root = JsonValue::makeObject();
        root.set("formatVersion", JsonValue{kFormatVersion});
        root.set("sceneId", JsonValue{sceneId_.toString()});
        root.set("name", JsonValue{name_});

        JsonValue entitiesJson = JsonValue::makeArray();
        for (const EditorEntity& entity : entities_)
        {
            entitiesJson.append(entityToJson(entity));
        }

        root.set("entities", std::move(entitiesJson));

        // Written only when it is not the default (ED-407). A scene that never touched its
        // environment must come back out byte for byte as it went in, or opening every existing
        // scene once would rewrite every existing scene once.
        if (!environment_.isDefault())
        {
            JsonValue environmentJson = JsonValue::makeObject();
            environmentJson.set("ambientColor", colorToJson(environment_.ambientColor));
            environmentJson.set("fogEnabled", JsonValue{environment_.fogEnabled});
            environmentJson.set("fogColor", colorToJson(environment_.fogColor));
            environmentJson.set("fogStart", JsonValue{static_cast<double>(environment_.fogStart)});
            environmentJson.set("fogEnd", JsonValue{static_cast<double>(environment_.fogEnd)});
            root.set("environment", std::move(environmentJson));
        }

        return root;
    }

    SceneLoadResult SceneDocument::loadFromJson(const JsonValue& json, const ComponentRegistry& registry,
                                                const FormatMigrator* migrator)
    {
        SceneLoadResult result;

        if (!json.isObject())
        {
            result.errorMessage = "scene root is not a JSON object";
            return result;
        }

        // The version gate and the upgrade path are one thing: refusing a file from the future and
        // upgrading one from the past are both answers to "what version is this?", and splitting
        // them is how a loader comes to refuse a file it could have read.
        const FormatMigrator& chain = migrator != nullptr ? *migrator : getSceneFormatMigrator();

        // Copied only when something has to change it. A file already at the current version is
        // the overwhelmingly common case and costs nothing.
        JsonValue upgraded;
        const JsonValue* source = &json;

        if (json["formatVersion"].asInt(0) != chain.getCurrentVersion())
        {
            upgraded = json;

            const FormatMigrationResult migration = chain.migrate(upgraded);
            if (!migration.succeeded)
            {
                result.errorMessage = migration.errorMessage;
                return result;
            }
            for (const std::string& step : migration.applied)
            {
                result.warnings.push_back("upgraded from an older scene format: " + step);
            }

            source = &upgraded;
        }

        const JsonValue& document = *source;

        clear();

        const Uuid sceneId = Uuid::parse(document["sceneId"].asString());
        sceneId_ = sceneId.isValid() ? sceneId : Uuid::generate();
        if (!sceneId.isValid()) { result.warnings.push_back("scene had no valid 'sceneId'; a new one was generated"); }
        name_ = document["name"].asString("Untitled");

        // Absent is the ordinary case, not an error: every scene written before ED-407 has no
        // `environment`, and reads as the defaults (ED-407 in SceneEnvironment.hpp says why that
        // has to stay true in both directions).
        environment_ = SceneEnvironment{};
        if (const JsonValue& environmentJson = document["environment"]; environmentJson.isObject())
        {
            const SceneEnvironment defaults;
            environment_.ambientColor =
                colorFromJson(environmentJson["ambientColor"], defaults.ambientColor);
            environment_.fogEnabled = environmentJson["fogEnabled"].asBoolean(defaults.fogEnabled);
            environment_.fogColor = colorFromJson(environmentJson["fogColor"], defaults.fogColor);
            environment_.fogStart = static_cast<float>(
                environmentJson["fogStart"].asNumber(static_cast<double>(defaults.fogStart)));
            environment_.fogEnd = static_cast<float>(
                environmentJson["fogEnd"].asNumber(static_cast<double>(defaults.fogEnd)));
        }

        for (const JsonValue& entityJson : document["entities"].getElements())
        {
            EditorEntity entity = entityFromJson(entityJson, registry, result.warnings);

            if (!addEntity(std::move(entity)).isValid())
            {
                result.warnings.push_back("duplicate entity id in scene; the later entity was dropped");
            }
        }

        // Dangling and cyclic parent links are repaired rather than rejected, so that a scene
        // whose parent entity was deleted by a bad merge still opens and can be fixed by hand.
        for (EditorEntity& entity : entities_)
        {
            const Uuid parentId = entity.getParentId();
            if (!parentId.isValid()) { continue; }
            if (findEntity(parentId) == nullptr)
            {
                result.warnings.push_back("entity '" + entity.getName() + "' referenced a missing parent; "
                                          "it was made a root entity");
                entity.setParentId(Uuid{});
            }
        }
        for (EditorEntity& entity : entities_)
        {
            if (entity.getParentId().isValid() && isAncestorOf(entity.getId(), entity.getParentId()))
            {
                result.warnings.push_back("entity '" + entity.getName() + "' was part of a parent cycle; "
                                          "it was made a root entity");
                entity.setParentId(Uuid{});
            }
        }

        result.succeeded = true;
        return result;
    }

    SceneLoadResult SceneDocument::loadFromFile(const std::string& path, const ComponentRegistry& registry,
                                                const FormatMigrator* migrator)
    {
        SceneLoadResult result;

        std::ifstream stream{path, std::ios::binary};
        if (!stream)
        {
            result.errorMessage = "cannot open '" + path + "'";
            return result;
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        const std::string text = buffer.str();

        const JsonParseResult parsed = Json::parse(text);
        if (!parsed.succeeded)
        {
            result.errorMessage = "'" + path + "': " + parsed.errorMessage + " at offset "
                                + std::to_string(parsed.errorOffset);
            return result;
        }

        return loadFromJson(parsed.value, registry, migrator);
    }

    bool SceneDocument::saveToFile(const std::string& path, std::string* errorMessage) const
    {
        std::error_code errorCode;
        const std::filesystem::path filePath{path};
        if (filePath.has_parent_path())
        {
            std::filesystem::create_directories(filePath.parent_path(), errorCode);
            if (errorCode)
            {
                if (errorMessage != nullptr) { *errorMessage = "cannot create directory: " + errorCode.message(); }
                return false;
            }
        }

        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        if (!stream)
        {
            if (errorMessage != nullptr) { *errorMessage = "cannot open '" + path + "' for writing"; }
            return false;
        }

        stream << Json::write(toJson(), true);
        if (!stream)
        {
            if (errorMessage != nullptr) { *errorMessage = "write to '" + path + "' failed"; }
            return false;
        }
        return true;
    }

    void SceneDocument::clear()
    {
        entities_.clear();
        indexById_.clear();
        sceneId_ = Uuid::generate();
    }

    void SceneDocument::rebuildIndex()
    {
        indexById_.clear();
        for (std::size_t index = 0; index < entities_.size(); ++index)
        {
            indexById_[entities_[index].getId()] = index;
        }
    }
}
