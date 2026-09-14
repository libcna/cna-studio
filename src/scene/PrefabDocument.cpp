// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/PrefabDocument.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "CNA/Editor/Scene/EntityJson.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    const FormatMigrator& getPrefabFormatMigrator()
    {
        static const FormatMigrator migrator{"prefab", PrefabDocument::kFormatVersion};
        return migrator;
    }

    Uuid PrefabDocument::getRootId() const
    {
        return entities_.empty() ? Uuid{} : entities_.front().getId();
    }

    const EditorEntity* PrefabDocument::findEntity(const Uuid& id) const
    {
        const auto found = std::find_if(entities_.begin(), entities_.end(),
                                        [&id](const EditorEntity& entity) { return entity.getId() == id; });
        return found == entities_.end() ? nullptr : &*found;
    }

    void PrefabDocument::clear()
    {
        entities_.clear();
        prefabId_ = Uuid{};
        name_ = "Prefab";
    }

    bool PrefabDocument::captureFromScene(const SceneDocument& scene, const Uuid& rootId, std::string name)
    {
        const EditorEntity* root = scene.findEntity(rootId);
        if (root == nullptr) { return false; }

        clear();
        prefabId_ = Uuid::generate();
        name_ = std::move(name);

        // Breadth-first from the root, so the stored order is parents before children and an
        // instantiation can walk it once without ever needing a parent it has not created yet.
        std::vector<Uuid> pending{rootId};
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            const EditorEntity* entity = scene.findEntity(pending[index]);
            if (entity == nullptr) { continue; }

            EditorEntity copy = *entity;

            // The root of a prefab has no parent *within the prefab*. Keeping the scene parent
            // would make the file describe a hierarchy that only exists in the scene it came from.
            if (copy.getId() == rootId) { copy.setParentId(Uuid{}); }

            entities_.push_back(std::move(copy));

            for (const Uuid& childId : scene.getChildren(pending[index]))
            {
                pending.push_back(childId);
            }
        }

        return true;
    }

    JsonValue PrefabDocument::toJson() const
    {
        JsonValue root = JsonValue::makeObject();
        root.set("formatVersion", JsonValue{kFormatVersion});
        root.set("prefabId", JsonValue{prefabId_.toString()});
        root.set("name", JsonValue{name_});

        JsonValue entitiesJson = JsonValue::makeArray();
        for (const EditorEntity& entity : entities_)
        {
            entitiesJson.append(entityToJson(entity));
        }
        root.set("entities", std::move(entitiesJson));
        return root;
    }

    PrefabLoadResult PrefabDocument::loadFromJson(const JsonValue& json,
                                                  const ComponentRegistry& registry,
                                                  const FormatMigrator* migrator)
    {
        PrefabLoadResult result;

        if (!json.isObject())
        {
            result.errorMessage = "prefab root is not a JSON object";
            return result;
        }

        const FormatMigrator& chain = migrator != nullptr ? *migrator : getPrefabFormatMigrator();

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
                result.warnings.push_back("upgraded from an older prefab format: " + step);
            }

            source = &upgraded;
        }

        const JsonValue& document = *source;

        clear();

        const Uuid prefabId = Uuid::parse(document["prefabId"].asString());
        prefabId_ = prefabId.isValid() ? prefabId : Uuid::generate();
        if (!prefabId.isValid())
        {
            result.warnings.push_back("prefab had no valid 'prefabId'; a new one was generated");
        }
        name_ = document["name"].asString("Prefab");

        for (const JsonValue& entityJson : document["entities"].getElements())
        {
            EditorEntity entity = entityFromJson(entityJson, registry, result.warnings);

            if (findEntity(entity.getId()) != nullptr)
            {
                result.warnings.push_back("duplicate entity id in prefab; the later entity was dropped");
                continue;
            }
            entities_.push_back(std::move(entity));
        }

        if (entities_.empty())
        {
            // A prefab with no entities instantiates to nothing, and the user would have no way to
            // tell that from an instantiation that silently failed.
            result.errorMessage = "prefab has no entities";
            return result;
        }

        // The first entity is the root by definition of the format. A file whose first entity
        // claims a parent is repaired rather than refused, the same way a scene's dangling parents
        // are: a file broken by a bad merge is exactly the one somebody needs the editor to open.
        if (entities_.front().getParentId().isValid())
        {
            entities_.front().setParentId(Uuid{});
            result.warnings.push_back("the prefab's first entity had a parent; it was cleared");
        }

        for (std::size_t index = 1; index < entities_.size(); ++index)
        {
            const Uuid parentId = entities_[index].getParentId();
            if (parentId.isValid() && findEntity(parentId) != nullptr) { continue; }

            entities_[index].setParentId(entities_.front().getId());
            result.warnings.push_back("entity '" + entities_[index].getName()
                                      + "' had no parent inside the prefab; it was attached to the root");
        }

        result.succeeded = true;
        return result;
    }

    PrefabLoadResult PrefabDocument::loadFromFile(const std::string& path,
                                                  const ComponentRegistry& registry,
                                                  const FormatMigrator* migrator)
    {
        PrefabLoadResult result;

        std::ifstream stream{path, std::ios::binary};
        if (!stream)
        {
            result.errorMessage = "cannot open '" + path + "'";
            return result;
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        const JsonParseResult parsed = Json::parse(buffer.str());
        if (!parsed.succeeded)
        {
            result.errorMessage = "'" + path + "': " + parsed.errorMessage + " at offset "
                                + std::to_string(parsed.errorOffset);
            return result;
        }

        return loadFromJson(parsed.value, registry, migrator);
    }

    bool PrefabDocument::saveToFile(const std::string& path, std::string* errorMessage) const
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
}
