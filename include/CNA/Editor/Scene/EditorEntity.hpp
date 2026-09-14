// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/EditorEntity.hpp
 * @brief The editor-side entity and component representation.
 *
 * This is an *editor document* type, not a runtime type. It is intentionally not an ECS and does
 * not ask CNA to become one (ANALYSIS.md decision D-04): CNA stays an XNA-compatible framework
 * where the game owns its own object model, and the editor's entity graph is compiled down into
 * whatever the game actually wants at load time.
 *
 * Components store their fields in a name-keyed property map rather than in real C++ members. The
 * cost is a map lookup per field access; the payoff is that a plugin-supplied component type needs
 * no compiled code in the editor, and that a scene referencing a component type whose plugin is
 * missing still round-trips through save/load without losing data.
 */

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/PropertyValue.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    class JsonValue;

    /**
     * @brief One component instance on an entity.
     *
     * Properties are stored in a std::map so that serialisation order is deterministic
     * (alphabetical) regardless of the order the user edited them in -- otherwise every save would
     * produce a spurious git diff.
     */
    class EditorComponent
    {
    public:
        EditorComponent() = default;
        explicit EditorComponent(std::string typeId) : typeId_(std::move(typeId)) {}

        /** @brief Returns the component type id, e.g. "CNA.Transform". */
        [[nodiscard]] const std::string& getTypeId() const { return typeId_; }

        /** @brief Sets the component type id. Only meaningful before properties are populated. */
        void setTypeId(std::string typeId) { typeId_ = std::move(typeId); }

        /**
         * @brief Returns the value of @p name, or an empty PropertyValue when unset.
         *
         * "Unset" and "set to the default" are deliberately indistinguishable here; the descriptor
         * supplies the default, and getPropertyOrDefault() applies it.
         */
        [[nodiscard]] PropertyValue getProperty(std::string_view name) const;

        /** @brief Returns the value of @p name, falling back to @p descriptor's declared default. */
        [[nodiscard]] PropertyValue getPropertyOrDefault(std::string_view name,
                                                         const ComponentDescriptor* descriptor) const;

        /** @brief Sets @p name to @p value, adding the property when it does not yet exist. */
        void setProperty(std::string name, PropertyValue value);

        /** @brief Returns true when @p name has a stored value. */
        [[nodiscard]] bool hasProperty(std::string_view name) const;

        /** @brief Removes @p name; returns true when something was removed. */
        bool removeProperty(std::string_view name);

        /** @brief Returns every stored property, in deterministic (alphabetical) order. */
        [[nodiscard]] const std::map<std::string, PropertyValue>& getProperties() const { return properties_; }

        /** @brief Fills in every property @p descriptor declares that is not already set. */
        void applyDefaults(const ComponentDescriptor& descriptor);

    private:
        std::string typeId_;
        std::map<std::string, PropertyValue> properties_;
    };

    /**
     * @brief One entity in a scene document.
     *
     * The parent link is stored on the child rather than as a child list on the parent, because
     * every operation the editor actually performs -- reparent, delete, "which entities are roots"
     * -- is cheaper and harder to corrupt that way. SceneDocument derives ordered child lists on
     * demand and enforces the acyclicity invariant.
     */
    class EditorEntity
    {
    public:
        EditorEntity() = default;
        EditorEntity(Uuid id, std::string name) : id_(id), name_(std::move(name)) {}

        [[nodiscard]] const Uuid& getId() const { return id_; }
        void setId(Uuid id) { id_ = id; }

        [[nodiscard]] const std::string& getName() const { return name_; }
        void setName(std::string name) { name_ = std::move(name); }

        /** @brief Returns the parent entity id, or the nil Uuid when this is a root entity. */
        [[nodiscard]] const Uuid& getParentId() const { return parentId_; }
        void setParentId(Uuid parentId) { parentId_ = parentId; }

        /** @brief Whether the entity is active. Disabled entities are still saved and still shown. */
        [[nodiscard]] bool isEnabled() const { return enabled_; }
        void setEnabled(bool enabled) { enabled_ = enabled; }

        /**
         * @brief Sibling ordering key within the parent.
         *
         * Explicit rather than implicit-by-insertion so that reordering in the hierarchy panel is
         * a plain property change -- undoable through the same SetProperty path as everything else.
         */
        [[nodiscard]] int getSortOrder() const { return sortOrder_; }
        void setSortOrder(int sortOrder) { sortOrder_ = sortOrder; }

        /** @brief Returns every component on this entity, in the order they were added. */
        [[nodiscard]] const std::vector<EditorComponent>& getComponents() const { return components_; }
        [[nodiscard]] std::vector<EditorComponent>& getComponents() { return components_; }

        /** @brief Returns the first component of @p typeId, or nullptr when there is none. */
        [[nodiscard]] const EditorComponent* findComponent(std::string_view typeId) const;
        [[nodiscard]] EditorComponent* findComponent(std::string_view typeId);

        /** @brief Appends @p component and returns a reference to the stored copy. */
        EditorComponent& addComponent(EditorComponent component);

        /** @brief Removes the component at @p index; returns false when @p index is out of range. */
        bool removeComponentAt(std::size_t index);

        /** @brief Returns the index of the first component of @p typeId, or npos. */
        [[nodiscard]] std::size_t indexOfComponent(std::string_view typeId) const;

        /**
         * @brief Editor-only, non-runtime state.
         *
         * Tree expansion, the editor icon override, layer colour, user notes. Kept separate from
         * the component data so that the runtime scene compiler can drop it wholesale rather than
         * having to know which fields are cosmetic (ANALYSIS.md decision D-07).
         */
        [[nodiscard]] const std::map<std::string, PropertyValue>& getEditorState() const { return editorState_; }
        void setEditorState(std::string name, PropertyValue value);

        /**
         * @brief Removes an editor-state key. Returns true when something was removed.
         *
         * Needed when a subtree changes role: an instance carries prefab links, and the prefab
         * rebuilt from it must not, or every future instance would be born claiming to be an
         * instance of something else.
         */
        bool removeEditorState(std::string_view name);

    private:
        Uuid id_;
        std::string name_;
        Uuid parentId_;
        bool enabled_ = true;
        int sortOrder_ = 0;
        std::vector<EditorComponent> components_;
        std::map<std::string, PropertyValue> editorState_;
    };
}
