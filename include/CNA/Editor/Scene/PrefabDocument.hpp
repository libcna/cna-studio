// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/PrefabDocument.hpp
 * @brief A `.cnaprefab`: one reusable entity subtree, and the link an instance keeps to it.
 *
 * plan.md ED-300. A prefab stores its entities in exactly the shape a scene stores its own
 * (`EntityJson.hpp`), and it has to: an instantiated prefab and a hand-authored entity must be
 * indistinguishable once they are in a scene, or the same entity would round-trip differently
 * depending on where it came from.
 *
 * **Overrides are computed, not stored.** The scene file holds the instance's actual values, the
 * way it holds every other entity's, and "what has this instance changed?" is answered by comparing
 * it against the prefab. Storing an override list would mean two descriptions of the same fact,
 * free to disagree -- and the disagreement shows up as a property that reverts to a value the user
 * never chose, which is the single worst thing a prefab system can do. It also means no new
 * serialised structure at all: a scene written before prefabs existed is still a valid scene.
 *
 * What an instance *does* store is the link: the id of the prefab asset on the instance root, and
 * on every entity of the instance the id of the prefab entity it came from. Both live in
 * `editorState`, because they are editor bookkeeping rather than something the game runs (D-07).
 */

#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/FormatMigration.hpp"
#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/EditorEntity.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief Outcome of loading a prefab, with the same forgiving stance as a scene. */
    struct PrefabLoadResult
    {
        bool succeeded = false;
        std::string errorMessage;
        std::vector<std::string> warnings;
    };

    /** @brief Editor-state keys that make an entity part of a prefab instance. */
    namespace PrefabKeys
    {
        /** @brief On the instance root: the id of the prefab *asset*. */
        inline constexpr const char* kPrefabAsset = "prefabAsset";

        /** @brief On every entity of an instance: the id of the prefab entity it came from. */
        inline constexpr const char* kPrefabEntity = "prefabEntity";
    }

    /**
     * @brief One `.cnaprefab` file in memory.
     *
     * Entities are stored root-first, so a reader can rebuild the hierarchy in one pass and an
     * instantiation can walk them in an order where every parent already exists.
     */
    class PrefabDocument
    {
    public:
        /** @brief The `formatVersion` this build writes, and the highest it can read. */
        static constexpr int kFormatVersion = 1;

        /** @brief File extension, matching the `.cnascene` convention. */
        static constexpr const char* kExtension = ".cnaprefab";

        [[nodiscard]] const Uuid& getPrefabId() const { return prefabId_; }
        void setPrefabId(Uuid prefabId) { prefabId_ = prefabId; }

        [[nodiscard]] const std::string& getName() const { return name_; }
        void setName(std::string name) { name_ = std::move(name); }

        /** @brief Returns every entity, root first. */
        [[nodiscard]] const std::vector<EditorEntity>& getEntities() const { return entities_; }

        /** @brief Returns the root entity's id, or the nil Uuid when the prefab is empty. */
        [[nodiscard]] Uuid getRootId() const;

        /** @brief Returns the entity with @p id, or nullptr. */
        [[nodiscard]] const EditorEntity* findEntity(const Uuid& id) const;

        [[nodiscard]] bool isEmpty() const { return entities_.empty(); }

        /**
         * @brief Replaces the contents with @p rootId's subtree, taken from @p scene.
         *
         * The entities keep the ids they had in the scene. That is what lets the entity they were
         * captured from become an instance of the prefab it produced without a second mapping: its
         * `prefabEntity` link points at itself.
         *
         * @return False when @p rootId is not in @p scene.
         */
        bool captureFromScene(const SceneDocument& scene, const Uuid& rootId, std::string name);

        /**
         * @brief Replaces the entities wholesale, root first.
         *
         * Used when a prefab is rebuilt from a live instance, where the ids have to be mapped back
         * to the prefab's own before they are stored. Nothing validates the order here; the loader
         * repairs a file whose order is wrong, and this is the same contract in memory.
         */
        void setEntities(std::vector<EditorEntity> entities) { entities_ = std::move(entities); }

        /** @brief Serialises to the `.cnaprefab` JSON documented in docs/FORMATS.md. */
        [[nodiscard]] JsonValue toJson() const;

        /** @brief Replaces the contents from @p json. */
        PrefabLoadResult loadFromJson(const JsonValue& json,
                                      const ComponentRegistry& registry,
                                      const FormatMigrator* migrator = nullptr);

        /** @brief Loads from a file on disk. */
        PrefabLoadResult loadFromFile(const std::string& path,
                                      const ComponentRegistry& registry,
                                      const FormatMigrator* migrator = nullptr);

        /** @brief Writes to a file on disk, creating parent directories as needed. */
        [[nodiscard]] bool saveToFile(const std::string& path, std::string* errorMessage = nullptr) const;

        void clear();

    private:
        Uuid prefabId_;
        std::string name_ = "Prefab";
        std::vector<EditorEntity> entities_;
    };

    /**
     * @brief Returns the migration chain that upgrades a `.cnaprefab`.
     *
     * Empty, like every other chain here: the format has only ever been at version 1.
     */
    [[nodiscard]] const FormatMigrator& getPrefabFormatMigrator();
}
