// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/SceneDocument.hpp
 * @brief An open `.cnascene` document: the entity graph, its invariants, and its serialisation.
 *
 * SceneDocument owns the entities and is the only place the parent/child invariant is enforced.
 * It deliberately exposes *mutating* operations as ordinary methods rather than hiding them --
 * the "everything goes through a command" rule (EditorCommand.hpp) is enforced one layer up, in
 * SceneCommands.hpp, because the commands themselves have to call these methods to do their work.
 * Panels and plugins must go through the commands; nothing outside cna-editor-scene should call
 * a mutating method here directly.
 */

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/FormatMigration.hpp"
#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/EditorEntity.hpp"
#include "CNA/Editor/Scene/SceneEnvironment.hpp"

namespace CNA::Editor
{
    /** @brief Outcome of a load attempt, with a message suitable for the console panel. */
    struct SceneLoadResult
    {
        bool succeeded = false;
        std::string errorMessage;

        /**
         * @brief Non-fatal problems: unknown component types, dangling parents, dropped cycles.
         *
         * A scene with warnings still loads. This is a deliberate stance -- an editor that refuses
         * to open a slightly broken file is an editor you cannot use to *fix* a broken file.
         */
        std::vector<std::string> warnings;
    };

    /**
     * @brief Returns the migration chain that upgrades a `.cnascene` to the current version.
     *
     * Empty today, because the format has only ever been at version 1. It is called on every load
     * regardless, so the first real migration is a small addition to a path that already runs
     * rather than a new path nobody has exercised.
     */
    [[nodiscard]] const FormatMigrator& getSceneFormatMigrator();

    /**
     * @brief The in-memory form of one scene file.
     *
     * Entity storage is a vector plus an id index. The vector gives stable iteration order for
     * serialisation (so saves stay diff-stable); the index gives O(1) lookup by id, which the
     * hierarchy panel and every command need constantly.
     */
    class SceneDocument
    {
    public:
        /** @brief The `formatVersion` this build writes, and the highest it can read. */
        static constexpr int kFormatVersion = 1;

        SceneDocument();

        [[nodiscard]] const Uuid& getSceneId() const { return sceneId_; }
        void setSceneId(Uuid sceneId) { sceneId_ = sceneId; }

        [[nodiscard]] const std::string& getName() const { return name_; }
        void setName(std::string name) { name_ = std::move(name); }

        /** @brief The scene's ambient light and fog (plan.md ED-407). */
        [[nodiscard]] const SceneEnvironment& getEnvironment() const { return environment_; }
        void setEnvironment(const SceneEnvironment& environment) { environment_ = environment; }

        /** @brief Returns every entity, in serialisation order. */
        [[nodiscard]] const std::vector<EditorEntity>& getEntities() const { return entities_; }

        /** @brief Returns the entity with @p id, or nullptr when there is none. */
        [[nodiscard]] const EditorEntity* findEntity(const Uuid& id) const;
        [[nodiscard]] EditorEntity* findEntity(const Uuid& id);

        /** @brief Returns the number of entities. */
        [[nodiscard]] std::size_t getEntityCount() const { return entities_.size(); }

        /**
         * @brief Adds @p entity, assigning it a fresh id when it has none.
         * @return The id of the stored entity, or the nil Uuid when an entity with that id existed.
         */
        Uuid addEntity(EditorEntity entity);

        /**
         * @brief Removes @p id and, recursively, every descendant.
         * @return The removed entities, parents first, so that a DeleteEntityCommand can restore
         *         them in the same order and rebuild the hierarchy correctly.
         */
        std::vector<EditorEntity> removeEntityRecursive(const Uuid& id);

        /**
         * @brief Reparents @p childId under @p newParentId.
         *
         * Passing the nil Uuid as @p newParentId makes @p childId a root entity.
         *
         * @return False when either id is unknown, or when the move would create a cycle (i.e.
         *         @p newParentId is @p childId itself or one of its descendants).
         */
        bool reparentEntity(const Uuid& childId, const Uuid& newParentId);

        /** @brief Returns the ids of @p parentId's direct children, ordered by sort order then name. */
        [[nodiscard]] std::vector<Uuid> getChildren(const Uuid& parentId) const;

        /** @brief Returns the ids of every entity with no parent, ordered by sort order then name. */
        [[nodiscard]] std::vector<Uuid> getRootEntities() const;

        /** @brief Returns true when @p ancestorId is @p descendantId or one of its ancestors. */
        [[nodiscard]] bool isAncestorOf(const Uuid& ancestorId, const Uuid& descendantId) const;

        /** @brief Serialises to the `.cnascene` JSON documented in docs/FORMATS.md. */
        [[nodiscard]] JsonValue toJson() const;

        /**
         * @brief Replaces the document's contents from @p json.
         *
         * @param json The parsed scene file.
         * @param registry Used to resolve each component's property types. When a component type
         *        is unknown, its properties are still read -- as strings and numbers inferred from
         *        the JSON shape -- and a warning is recorded. Nothing is dropped.
         * @param migrator The migration chain to upgrade an older file with. Null means the
         *        format's own chain, which is what every caller outside a test wants; the seam
         *        exists so that a test can prove the loader really runs migrations and really
         *        reads the upgraded document rather than the original.
         */
        SceneLoadResult loadFromJson(const JsonValue& json, const ComponentRegistry& registry,
                                     const FormatMigrator* migrator = nullptr);

        /** @brief Loads from a file on disk. */
        SceneLoadResult loadFromFile(const std::string& path, const ComponentRegistry& registry,
                                     const FormatMigrator* migrator = nullptr);

        /** @brief Writes to a file on disk, creating parent directories as needed. */
        [[nodiscard]] bool saveToFile(const std::string& path, std::string* errorMessage = nullptr) const;

        /** @brief Drops every entity and resets the scene id, keeping the name. */
        void clear();

    private:
        void rebuildIndex();

        Uuid sceneId_;
        std::string name_ = "Untitled";

        /**
         * @brief Written only when it differs from the defaults (ED-407).
         *
         * An *additive* field, which is the permission the owner gave and not a version bump: a
         * scene written before ED-407 has no `environment` object, reads as the defaults, and comes
         * back out without one. That last part is the half that is easy to lose -- a loader that
         * read a default and then wrote it would turn every existing scene file into a modified one
         * the first time it was opened, and a diff full of scenes nobody edited is how a format
         * change gets blamed on the wrong commit.
         */
        SceneEnvironment environment_;
        std::vector<EditorEntity> entities_;
        std::unordered_map<Uuid, std::size_t> indexById_;
    };
}
