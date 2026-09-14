// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/PrefabCommands.hpp
 * @brief Putting a prefab into a scene, and putting an instance back the way the prefab has it.
 *
 * plan.md ED-300, scene half. Everything here works on a `SceneDocument` and a `PrefabDocument`
 * and touches no files; creating a prefab from a selection and applying an instance back to its
 * file need the asset database and live a layer up.
 *
 * The override model is worth stating once: **an override is a difference, not a record**. The
 * scene holds the instance's actual values the way it holds every other entity's, and
 * `findPrefabOverrides` answers "what has this instance changed?" by comparing the two. A stored
 * override list would be a second description of the same fact, free to disagree with the first --
 * and the way that disagreement shows up is a property reverting to a value the user never chose,
 * which is the worst thing a prefab system can do.
 */

#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/EditorCommand.hpp"
#include "CNA/Editor/Core/PropertyValue.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/EditorEntity.hpp"

namespace CNA::Editor
{
    class SceneDocument;
    class PrefabDocument;

    /** @brief One way an instance differs from the prefab it came from. */
    struct PrefabOverride
    {
        /** @brief What kind of difference this is. */
        enum class Kind
        {
            /** @brief A property holds a different value. */
            Property,
            /** @brief The instance has an entity the prefab does not. */
            AddedEntity,
            /** @brief The prefab has an entity the instance does not. */
            RemovedEntity
        };

        Kind kind = Kind::Property;

        /** @brief The instance entity, or the nil Uuid for a RemovedEntity. */
        Uuid entityId;

        /** @brief The entity's name, for display without a second lookup. */
        std::string entityName;

        /** @brief Set for Property overrides. */
        std::string componentTypeId;
        std::string propertyName;

        /** @brief The value the instance holds, and the one the prefab declares. */
        PropertyValue instanceValue;
        PropertyValue prefabValue;
    };

    /** @brief Returns the display name of @p kind. */
    const char* toString(PrefabOverride::Kind kind);

    /**
     * @brief Returns the id of the prefab asset @p entityId is an instance root of, or nil.
     *
     * Only the *root* of an instance carries the link, so this is also the test for "is this entity
     * the top of an instance" rather than merely part of one.
     */
    [[nodiscard]] Uuid getPrefabAssetOf(const SceneDocument& scene, const Uuid& entityId);

    /**
     * @brief Returns the instance root @p entityId belongs to, or the nil Uuid.
     *
     * Walks up until it finds an entity carrying a prefab asset link, so selecting a child of an
     * instance still answers questions about the instance.
     */
    [[nodiscard]] Uuid findInstanceRoot(const SceneDocument& scene, const Uuid& entityId);

    /**
     * @brief Returns every difference between @p instanceRootId's subtree and @p prefab.
     *
     * In document order, properties before structure for each entity, so the report reads the same
     * way the inspector does. An empty result means the instance is exactly the prefab.
     *
     * @param registry Supplies each component's declared defaults. Needed because "unset" and "set
     *        to the default" are deliberately indistinguishable in the document model
     *        (`EditorComponent::getProperty`), so comparing stored values alone would report an
     *        override every time one side happened to have written a default out and the other
     *        had not -- which is exactly what a round trip through a file does.
     */
    [[nodiscard]] std::vector<PrefabOverride> findPrefabOverrides(const SceneDocument& scene,
                                                                  const Uuid& instanceRootId,
                                                                  const PrefabDocument& prefab,
                                                                  const ComponentRegistry& registry);

    /**
     * @brief Adds a copy of @p prefab to the scene, under @p parentId.
     *
     * Every entity gets a fresh id -- two instances of one prefab are two different entities -- and
     * keeps a link to the prefab entity it came from, which is what lets an override be found
     * later without depending on names or on sibling order.
     */
    class InstantiatePrefabCommand final : public EditorCommand
    {
    public:
        /**
         * @param document The target scene; must outlive this command.
         * @param prefab The prefab to copy. Read at construction, so the command is independent of
         *        it afterwards.
         * @param prefabAssetId The prefab file's id in the asset database, which is what the
         *        instance references (D-08).
         * @param parentId Where to attach the instance root; nil makes it a root entity.
         */
        InstantiatePrefabCommand(SceneDocument& document,
                                 const PrefabDocument& prefab,
                                 Uuid prefabAssetId,
                                 Uuid parentId);

        /** @brief Returns false when the prefab is empty or @p parentId is unknown. */
        [[nodiscard]] bool isValid() const { return valid_; }

        /** @brief Returns the id the instance root will have, valid from construction onwards. */
        [[nodiscard]] const Uuid& getRootId() const { return rootId_; }

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

    private:
        SceneDocument* document_;
        std::string prefabName_;
        Uuid rootId_;

        /** @brief The entities to add, parents first, with their links already set. */
        std::vector<EditorEntity> entities_;
        bool valid_ = false;
    };

    /**
     * @brief Puts an instance back exactly as its prefab has it.
     *
     * Restores overridden properties, re-adds entities the user deleted, and **removes entities the
     * user added**. That last part is deliberate and is why this is one command with one undo entry:
     * "revert" that left some of the user's changes in place would not be a revert, and the user
     * who wanted a partial one has undo.
     */
    class RevertPrefabInstanceCommand final : public EditorCommand
    {
    public:
        RevertPrefabInstanceCommand(SceneDocument& document,
                                    const Uuid& instanceRootId,
                                    const PrefabDocument& prefab);

        /** @brief Returns false when the root is unknown, unlinked, or already identical. */
        [[nodiscard]] bool isValid() const { return valid_; }

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

    private:
        void replaceSubtree(const std::vector<EditorEntity>& entities);

        SceneDocument* document_;
        Uuid rootId_;
        std::string prefabName_;

        /** @brief The subtree as it was, parents first, so undo restores it exactly. */
        std::vector<EditorEntity> before_;

        /** @brief The subtree as the prefab has it, with the instance's own ids preserved. */
        std::vector<EditorEntity> after_;
        bool valid_ = false;
    };
}
