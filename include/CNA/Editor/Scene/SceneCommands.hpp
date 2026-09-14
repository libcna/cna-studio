// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/SceneCommands.hpp
 * @brief The concrete undoable operations on a SceneDocument.
 *
 * Everything a panel, a gizmo or a plugin does to a scene is one of these, pushed through a
 * CommandHistory. Nothing else is allowed to touch SceneDocument's mutating methods -- see
 * EditorCommand.hpp for the rule and ANALYSIS.md decision D-06 for why it is absolute.
 *
 * Each command holds a raw SceneDocument pointer rather than owning the document. That is safe
 * because a CommandHistory is owned by, and cleared with, the document it belongs to; a command
 * cannot outlive its target.
 */

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/Core/EditorCommand.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    /** @brief Adds one entity to a scene. Undo removes it (and, by then, any of its children). */
    class CreateEntityCommand final : public EditorCommand
    {
    public:
        /**
         * @param document The target scene; must outlive this command.
         * @param entity The entity to add. Its id is assigned now, not at execute() time, so that
         *        the caller can select it immediately and so that redo restores the same id.
         */
        CreateEntityCommand(SceneDocument& document, EditorEntity entity);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns the id the created entity has, valid from construction onwards. */
        [[nodiscard]] const Uuid& getEntityId() const { return entity_.getId(); }

    private:
        SceneDocument* document_;
        EditorEntity entity_;
    };

    /**
     * @brief Removes an entity and its whole subtree.
     *
     * Undo restores every removed entity, parents first, so the hierarchy comes back intact --
     * including the parent links of children that pointed at the deleted entity.
     */
    class DeleteEntityCommand final : public EditorCommand
    {
    public:
        DeleteEntityCommand(SceneDocument& document, Uuid entityId);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

    private:
        SceneDocument* document_;
        Uuid entityId_;
        std::string entityName_;
        std::vector<EditorEntity> removed_;
    };

    /**
     * @brief Copies an entity and its whole subtree, as a sibling of the original.
     *
     * The clones get fresh ids -- an entity is identified by its Uuid everywhere, so reusing one
     * would make the scene ambiguous the instant it was saved -- and their parent links are
     * remapped onto the copies, so the duplicated subtree stands on its own rather than pointing
     * back into the original.
     *
     * The clone ids are decided at construction, not at execute() time, so the caller can select
     * the copy immediately and so redo restores the same ids the first execute() created.
     */
    class DuplicateEntityCommand final : public EditorCommand
    {
    public:
        DuplicateEntityCommand(SceneDocument& document, Uuid sourceId);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns false when @p sourceId is not in the document. */
        [[nodiscard]] bool isValid() const { return valid_; }

        /** @brief Returns the id of the copied root, valid from construction onwards. */
        [[nodiscard]] Uuid getEntityId() const;

    private:
        SceneDocument* document_;
        Uuid sourceId_;
        std::string sourceName_;

        /** @brief The copies, parents first, so execute() can add them in order. */
        std::vector<EditorEntity> clones_;
        bool valid_ = false;
    };

    /** @brief Renames an entity. */
    class RenameEntityCommand final : public EditorCommand
    {
    public:
        RenameEntityCommand(SceneDocument& document, Uuid entityId, std::string newName);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override;
        bool mergeWith(const EditorCommand& newer) override;

    private:
        SceneDocument* document_;
        Uuid entityId_;
        std::string newName_;
        std::string oldName_;
    };

    /**
     * @brief Moves an entity under a new parent.
     *
     * Passing the nil Uuid as the new parent makes the entity a root. A move that would create a
     * cycle is rejected by SceneDocument and leaves the document unchanged, so pushing such a
     * command is harmless -- but callers should check first, because a no-op undo entry is
     * confusing to the user.
     */
    class ReparentEntityCommand final : public EditorCommand
    {
    public:
        ReparentEntityCommand(SceneDocument& document, Uuid entityId, Uuid newParentId);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

    private:
        SceneDocument* document_;
        Uuid entityId_;
        Uuid newParentId_;
        Uuid oldParentId_;
    };

    /**
     * @brief Sets one property on one component of one entity.
     *
     * This single command covers every field of every component type, including plugin-supplied
     * ones -- that is the payoff of the PropertyValue/ComponentDescriptor design. It is also the
     * command that merges: its merge key is entity + component + property, so a gizmo drag or a
     * slider scrub collapses into one undo step that returns to where the interaction began.
     */
    class SetPropertyCommand final : public EditorCommand
    {
    public:
        SetPropertyCommand(SceneDocument& document,
                           Uuid entityId,
                           std::string componentTypeId,
                           std::string propertyName,
                           PropertyValue newValue);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override;
        bool mergeWith(const EditorCommand& newer) override;

        // What this command targets. Read by the runtime bridge, which mirrors a property edit
        // into a running player and has to know which property moved -- not what it moved to,
        // because after an undo the live value is the old one and the document is the truth.
        [[nodiscard]] const Uuid& getEntityId() const { return entityId_; }
        [[nodiscard]] const std::string& getComponentTypeId() const { return componentTypeId_; }
        [[nodiscard]] const std::string& getPropertyName() const { return propertyName_; }

    private:
        SceneDocument* document_;
        Uuid entityId_;
        std::string componentTypeId_;
        std::string propertyName_;
        PropertyValue newValue_;
        PropertyValue oldValue_;
        bool hadOldValue_ = false;
    };

    /** @brief One entity's new transform values. An unset field is left alone. */
    struct EntityTransformEdit
    {
        Uuid entityId;
        std::optional<EditorVector3> position;
        std::optional<EditorQuaternion> rotation;
        std::optional<EditorVector3> scale;
    };

    /**
     * @brief Moves, turns or resizes several entities as one undo entry.
     *
     * Dragging a gizmo with five entities selected is *one* action to the user, so it has to be one
     * entry: five `SetPropertyCommand`s would be five presses of Ctrl+Z to undo one drag, and --
     * worse -- they would undo one entity at a time, leaving the scene in arrangements that never
     * existed.
     *
     * The merge key is supplied by the caller rather than derived, because what distinguishes two
     * drags of the same five entities is only that the user let go in between. The viewport passes
     * a drag id; `CommandHistory::endInteraction()` closes the chain when the pointer is released.
     */
    /**
     * @brief Sets the scene's ambient light and fog (plan.md ED-407).
     *
     * A command like every other document change (D-06), not a setter, even though the environment
     * belongs to no entity. An editor where the layer list undoes and the fog does not is worse
     * than one where nothing undoes, because the user has to remember which is which.
     *
     * Merges with itself, so dragging a fog distance is one undo entry rather than one per pixel of
     * cursor movement -- the same bargain the transform commands strike, and the merge key names
     * the *field* so that changing the colour after the distance stays two entries.
     */
    class SetSceneEnvironmentCommand final : public EditorCommand
    {
    public:
        SetSceneEnvironmentCommand(SceneDocument& document, SceneEnvironment environment,
                                   std::string fieldName);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override;
        bool mergeWith(const EditorCommand& newer) override;

    private:
        SceneDocument* document_;
        SceneEnvironment newEnvironment_;
        SceneEnvironment oldEnvironment_;
        std::string fieldName_;
    };

    class TransformEntitiesCommand final : public EditorCommand
    {
    public:
        TransformEntitiesCommand(SceneDocument& document,
                                 std::vector<EntityTransformEdit> edits,
                                 std::string mergeKey);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override { return mergeKey_; }
        bool mergeWith(const EditorCommand& newer) override;

        /** @brief Returns true when there is anything to apply. */
        [[nodiscard]] bool isValid() const { return !edits_.empty(); }

        /** @brief Returns what this command will write, for a caller that has to report on it. */
        [[nodiscard]] const std::vector<EntityTransformEdit>& getEdits() const { return edits_; }

    private:
        /** @brief Captures the values @p execute() is about to overwrite, once. */
        void captureOldValues();

        SceneDocument* document_;
        std::vector<EntityTransformEdit> edits_;
        std::vector<EntityTransformEdit> oldValues_;
        std::string mergeKey_;
        bool captured_ = false;
    };

    /**
     * @brief Points every reference to one asset id at another, across the whole scene.
     *
     * Relinking a broken reference is one action to the user however many entities carry it, so it
     * is one undo entry. Pushing a SetPropertyCommand per reference would make undoing a relink of
     * forty sprites forty presses of Ctrl+Z, which is not undo -- it is punishment.
     *
     * Passing the nil Uuid as the replacement clears the references instead, which is the right
     * answer when the asset is simply gone and nothing should take its place.
     */
    class RelinkAssetCommand final : public EditorCommand
    {
    public:
        RelinkAssetCommand(SceneDocument& document, Uuid oldAssetId, Uuid newAssetId);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns false when nothing in the scene refers to the old id. */
        [[nodiscard]] bool isValid() const { return !targets_.empty(); }

        /** @brief Returns how many references the command rewrites. */
        [[nodiscard]] std::size_t getReferenceCount() const { return targets_.size(); }

    private:
        /** @brief One property the command rewrites. */
        struct Target
        {
            Uuid entityId;
            std::string componentTypeId;
            std::string propertyName;
        };

        SceneDocument* document_;
        Uuid oldAssetId_;
        Uuid newAssetId_;
        std::vector<Target> targets_;
    };

    /** @brief Adds a component to an entity, populated with the descriptor's declared defaults. */
    class AddComponentCommand final : public EditorCommand
    {
    public:
        AddComponentCommand(SceneDocument& document,
                            const ComponentRegistry& registry,
                            Uuid entityId,
                            std::string componentTypeId);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /**
         * @brief Returns false when the add cannot legally happen.
         *
         * True refusals are: an unknown type id, and a second instance of a type whose descriptor
         * declares @c unique. Callers should check this before pushing, so the undo stack never
         * gains an entry that does nothing.
         */
        [[nodiscard]] bool isValid() const { return valid_; }

    private:
        SceneDocument* document_;
        Uuid entityId_;
        std::string componentTypeId_;
        EditorComponent prototype_;
        bool valid_ = false;
    };

    /** @brief Removes a component from an entity, preserving its property values for undo. */
    class RemoveComponentCommand final : public EditorCommand
    {
    public:
        /** @brief Removes the first component of @p componentTypeId. */
        RemoveComponentCommand(SceneDocument& document,
                               const ComponentRegistry& registry,
                               Uuid entityId,
                               std::string componentTypeId);

        /**
         * @brief Removes the component at @p componentIndex.
         *
         * The inspector uses this one. A type-based remove takes the *first* instance, which is
         * wrong for a component whose descriptor allows several -- clicking Remove on the second
         * audio source would delete the first, and the two look identical afterwards.
         */
        RemoveComponentCommand(SceneDocument& document,
                               const ComponentRegistry& registry,
                               Uuid entityId,
                               std::size_t componentIndex);

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;

        /** @brief Returns false when the component is absent or its descriptor marks it required. */
        [[nodiscard]] bool isValid() const { return valid_; }

    private:
        SceneDocument* document_;
        Uuid entityId_;
        std::string componentTypeId_;
        EditorComponent removed_;
        std::size_t removedIndex_ = 0;
        bool valid_ = false;
    };
}
