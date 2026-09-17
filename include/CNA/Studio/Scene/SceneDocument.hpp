// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Scene/SceneDocument.hpp
 * @brief An open `.cnascene` document: the entity graph, its invariants, and its serialisation.
 *
 * SceneDocument owns the entities and is the only place the parent/child invariant is enforced.
 * It deliberately exposes *mutating* operations as ordinary methods rather than hiding them --
 * the "everything goes through a command" rule (StudioCommand.hpp) is enforced one layer up, in
 * SceneCommands.hpp, because the commands themselves have to call these methods to do their work.
 * Panels and plugins must go through the commands; nothing outside cna-studio-scene should call
 * a mutating method here directly.
 */

#include <optional>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "CNA/Studio/Core/ComponentDescriptor.hpp"
#include "CNA/Studio/Core/FormatMigration.hpp"
#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/StudioEntity.hpp"
#include "CNA/Studio/Scene/SceneEnvironment.hpp"

namespace CNA::Studio
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
        [[nodiscard]] const std::vector<StudioEntity>& getEntities() const { return entities_; }

        /**
         * @brief Returns the entity with @p id to read, or nullptr when there is none.
         *
         * Const-qualified only, deliberately (`plan.md` STUDIO-30026). It used to have a non-const
         * overload beside it, and that is a trap in a document that caches anything derived from
         * its entities: overload resolution picks the *mutable* one whenever the document itself is
         * non-const, which is almost everywhere — so a caller writing
         * `const StudioEntity* e = context.getScene().findEntity(id)`, plainly a reader, got the
         * write handle and invalidated the hierarchy index. Twenty-odd call sites did exactly that,
         * every frame, and `STUDIO-30011`'s cache never survived one.
         *
         * A const member function is callable on a non-const object, so a reader now gets the read
         * whatever it holds, and only @ref findEntityForEdit — which a caller has to ask for by
         * name — gives up the index.
         */
        [[nodiscard]] const StudioEntity* findEntity(const Uuid& id) const;

        /**
         * @brief Returns the entity with @p id to change, or nullptr when there is none.
         *
         * Named for what it is, because handing out a changeable entity invalidates the hierarchy
         * index: this is the only handle through which a parent, a name or a sort order can change
         * behind the document's back, and all three decide that index (the first its shape, the
         * other two the order within a parent). Conservative — it costs a rebuild that may not have
         * been needed, and it cannot be wrong.
         *
         * Use @ref findEntity to read. A `findEntityForEdit` on a draw path is a bug, and
         * `SceneDocument::getHierarchyRebuildCount()` is how a test says so.
         */
        [[nodiscard]] StudioEntity* findEntityForEdit(const Uuid& id);

        /** @brief Returns the number of entities. */
        [[nodiscard]] std::size_t getEntityCount() const { return entities_.size(); }

        /**
         * @brief Adds @p entity, assigning it a fresh id when it has none.
         * @return The id of the stored entity, or the nil Uuid when an entity with that id existed.
         */
        Uuid addEntity(StudioEntity entity);

        /**
         * @brief Removes @p id and, recursively, every descendant.
         * @return The removed entities, parents first, so that a DeleteEntityCommand can restore
         *         them in the same order and rebuild the hierarchy correctly.
         */
        std::vector<StudioEntity> removeEntityRecursive(const Uuid& id);

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

        /**
         * @brief Every parent's children at once, in the same order @ref getChildren gives.
         *
         * `plan.md` STUDIO-30013. **Use this for anything that walks the whole hierarchy.**
         * `getChildren` scans every entity in the document, so calling it once per node makes a
         * tree walk O(n²) — which is not a theoretical concern: the World Outliner's flatten did
         * exactly that, and `--ui-benchmark` measured 9 ms a frame at 250 entities, 25 ms at 500,
         * 84 ms at 1 000 and 309 ms at 2 000. Four times the cost for twice the entities is the
         * signature, and at 2 000 entities — not a large scene — the whole editor ran at three
         * frames a second.
         *
         * This is one pass and one sort per group: O(n log k) for the whole document, against
         * O(n²) for n calls to `getChildren`.
         *
         * ### Cached, with the invalidation made explicit (`plan.md` STUDIO-30011)
         *
         * It used to be rebuilt on every call, deliberately: `findEntity` hands out a mutable
         * `StudioEntity*` and `StudioEntity::setParentId` is public, so the document could not know
         * when a parent changed, and a silently stale hierarchy presents as entities *vanishing
         * from the outliner* rather than as a failure. That is the right thing to be afraid of —
         * but "rebuild it every time" is a strategy for not knowing, and at twenty thousand
         * entities it cost about 20 ms a frame with the Outliner open (`STUDIO-30020`).
         *
         * So the document now knows. Every mutator invalidates, and **asking for a changeable
         * entity is itself an invalidation**: the non-const @ref findEntity hands out the only
         * handle through which a parent, a name or a sort order can change behind the document's
         * back, so handing one out marks the index stale. Conservative on purpose — it costs a
         * rebuild that may not have been needed, and it cannot be wrong. A reader that only reads
         * (the Outliner, every hierarchy walk) never triggers one, which is the case that mattered.
         *
         * @ref getHierarchyRebuildCount exists so that "drawing a frame rebuilds nothing" is a
         * test rather than a hope.
         *
         * The returned reference is valid until the next mutation or the next non-const
         * @ref findEntity. Callers hold it for one walk, which is what they did with the copy.
         *
         * The nil Uuid's entry holds the root entities, so a walk needs no special case for them.
         *
         * @return Parent id to its children, ordered. Parents with no children are absent.
         */
        [[nodiscard]] const std::unordered_map<Uuid, std::vector<Uuid>>& getChildrenByParent() const;

        /**
         * @brief How many times the hierarchy index has been built (`plan.md` STUDIO-30011).
         *
         * Counted rather than timed, for the same reason `AssetDatabase` counts its filesystem
         * probes: a wall-clock assertion on a shared machine fails for reasons that have nothing
         * to do with the code, and "a frame of drawing rebuilds nothing" is exactly the sort of
         * claim that decays silently the day somebody adds a non-const lookup to a draw path.
         */
        [[nodiscard]] std::uint64_t getHierarchyRebuildCount() const { return hierarchyRebuilds_; }

        /**
         * @brief Marks the hierarchy index stale.
         *
         * Called by every mutator and by the non-const @ref findEntity. Public as well, because a
         * caller that reaches around the document — a migration writing entities in place, a test
         * arranging a scene — needs a way to say so, and an escape hatch that exists is better
         * than one that gets invented locally.
         */
        void invalidateHierarchy() const { hierarchyStale_ = true; }

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
        std::vector<StudioEntity> entities_;
        std::unordered_map<Uuid, std::size_t> indexById_;

        /** @brief The cached hierarchy. Mutable because building it is not a change to the scene. */
        mutable std::unordered_map<Uuid, std::vector<Uuid>> childrenByParent_;

        /** @brief Whether @ref childrenByParent_ still describes the document. */
        mutable bool hierarchyStale_ = true;

        /** @brief How many times it has been built. See getHierarchyRebuildCount(). */
        mutable std::uint64_t hierarchyRebuilds_ = 0;
    };
}
