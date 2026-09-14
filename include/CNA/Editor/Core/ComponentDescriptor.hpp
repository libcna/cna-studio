// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/ComponentDescriptor.hpp
 * @brief Hand-written reflection metadata: what a component type is and which properties it has.
 *
 * This is the substitute for the language-level reflection C++ does not have, and it is the single
 * most load-bearing piece of the editor SDK (ANALYSIS.md decision D-05). Everything downstream is
 * generic over it:
 *
 * - the inspector builds its widgets by walking a descriptor's properties;
 * - SetPropertyCommand undoes a change to any property of any component without knowing the type;
 * - the scene serialiser reads and writes component JSON without a per-type code path;
 * - a plugin can register a component type at runtime and get all three for free.
 *
 * A descriptor is *data*, not a C++ type: EditorComponent stores its properties in a name-keyed
 * map rather than in real member variables. That is what lets the editor round-trip a scene
 * containing components from a plugin that failed to load, instead of silently dropping them.
 */

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/Editor/Core/PropertyValue.hpp"

namespace CNA::Editor
{
    /** @brief One editable field on a component type. */
    struct PropertyDescriptor
    {
        /** @brief Stable serialised name. Never change this without a format migration. */
        std::string name;

        /** @brief Label shown in the inspector. Falls back to name when empty. */
        std::string displayName;

        /** @brief Which PropertyValue alternative this field holds. */
        PropertyType type = PropertyType::None;

        /** @brief Value used when a scene file omits the field, and by AddComponentCommand. */
        PropertyValue defaultValue;

        /** @brief Allowed names when type is PropertyType::Enum; ignored otherwise. */
        std::vector<std::string> enumOptions;

        /**
         * @brief What a List's elements are, when @c type is PropertyType::List.
         *
         * Declared rather than inferred from the stored value. An empty list has no element to
         * infer from, and a list whose type followed its contents could never be edited back from
         * empty. Nested lists are not allowed: a list of lists is a table, and a table wants its
         * own type rather than a widget that recurses.
         */
        PropertyType elementType = PropertyType::None;

        /**
         * @brief The fields of a structure, when @c type -- or @c elementType -- is Structure.
         *
         * Declared here rather than inferred from a stored value, for the reason `elementType` is:
         * a structure that has never been written has no fields to infer from, and one whose shape
         * followed its contents could not be edited back from empty.
         *
         * `std::vector<PropertyDescriptor>` of an incomplete type, which is legal and is the same
         * trick `PropertyValue::ListValue` uses. A field may not itself be a structure or a list;
         * nothing enforces that at compile time, and `ED-410` is the only consumer, so the rule is
         * stated rather than checked.
         */
        std::vector<PropertyDescriptor> structureFields;

        /**
         * @brief Which kind of asset an AssetReference field accepts, e.g. "Texture2D".
         *
         * Empty means any. Held as a string rather than as an AssetType so that this header stays
         * in `cna-editor-core`, below the asset database that owns that enumeration -- and so a
         * plugin can name an asset kind the editor was never compiled against.
         *
         * The inspector uses it to refuse a drop of the wrong kind. Nothing enforces it in the
         * document model: a scene file that points a texture slot at a sound still loads, because
         * refusing to open a project over a bad reference is worse than showing it.
         */
        std::string assetType;

        /** @brief Tooltip shown in the inspector. */
        std::string tooltip;

        /**
         * @brief Inclusive numeric range hint for Integer and Float properties.
         *
         * When minimum < maximum the inspector may present a slider instead of a text field. The
         * document model does *not* enforce the range -- a hand-edited scene file that exceeds it
         * still loads, because refusing to load a scene over a cosmetic hint would be worse than
         * showing an out-of-range value.
         */
        double minimum = 0.0;
        double maximum = 0.0;

        /** @brief When true the inspector shows the value but does not let the user change it. */
        bool readOnly = false;
    };

    /**
     * @brief Reads @p json as the value @p descriptor declares, structures included.
     *
     * A free function rather than an overload of `PropertyValue::fromJson`, and the reason is a
     * layering one: decoding a structure needs the *field schema*, that schema lives on a
     * `PropertyDescriptor`, and `PropertyDescriptor` is declared here -- above `PropertyValue`'s
     * header rather than below it. So the structured reader is where both are in scope, and
     * everything flat still goes through the original.
     *
     * A field the JSON does not mention comes back as its declared default rather than as nothing,
     * which is what lets a structure gain a field without every document already written becoming
     * a document with a hole in it. A field the *schema* does not mention is dropped: keeping it
     * would mean writing back a shape nothing declared, and the next reader would have no way to
     * know what it meant.
     */
    [[nodiscard]] PropertyValue propertyValueFromJson(const JsonValue& json,
                                                      const PropertyDescriptor& descriptor);
    /**
     * @brief The metadata for one component type.
     *
     * @c typeId is the identity that appears in scene files (e.g. "CNA.Transform",
     * "Game.PlayerSpawn"). It is namespaced by convention so that a plugin cannot collide with a
     * built-in type: built-ins use the "CNA." prefix, game code uses "Game.", and a plugin uses
     * its own reverse-DNS id.
     */
    struct ComponentDescriptor
    {
        std::string typeId;
        std::string displayName;

        /** @brief Inspector grouping, e.g. "Rendering" or "Audio". Empty means ungrouped. */
        std::string category;

        /** @brief Ordered properties. Order is the inspector's display order. */
        std::vector<PropertyDescriptor> properties;

        /**
         * @brief When true, an entity may hold at most one instance of this type.
         *
         * Transform is the canonical example. AddComponentCommand refuses to add a second one.
         */
        bool unique = true;

        /**
         * @brief When true, RemoveComponentCommand refuses to remove this component.
         *
         * Set for Transform: an entity without a transform has no position, and every viewport
         * operation would have to special-case it.
         */
        bool required = false;

        /** @brief Returns the property named @p name, or nullptr when this type has no such field. */
        [[nodiscard]] const PropertyDescriptor* findProperty(std::string_view name) const;
    };

    /**
     * @brief The set of component types the editor currently knows about.
     *
     * Populated at start-up with the built-ins (BuiltinComponents.hpp) and then extended by each
     * loaded plugin. Registration is *additive and non-destructive*: re-registering an existing
     * typeId replaces the descriptor, which is what makes plugin hot-reload possible, but it never
     * touches already-loaded documents -- their components keep whatever properties they were
     * loaded with, and the inspector simply shows the new schema.
     */
    class ComponentRegistry
    {
    public:
        /**
         * @brief Registers or replaces a component type.
         * @return False when @p descriptor has an empty typeId, in which case nothing is stored.
         */
        bool registerComponent(ComponentDescriptor descriptor);

        /** @brief Returns the descriptor for @p typeId, or nullptr when it is not registered. */
        [[nodiscard]] const ComponentDescriptor* find(std::string_view typeId) const;

        /** @brief Returns true when @p typeId is registered. */
        [[nodiscard]] bool contains(std::string_view typeId) const { return find(typeId) != nullptr; }

        /** @brief Returns every registered type id, sorted, for the "Add Component" menu. */
        [[nodiscard]] std::vector<std::string> getTypeIds() const;

        /** @brief Returns the number of registered types. */
        [[nodiscard]] std::size_t getCount() const { return descriptors_.size(); }

        /** @brief Removes @p typeId. Used when a plugin is unloaded. */
        bool unregisterComponent(std::string_view typeId);

    private:
        std::unordered_map<std::string, ComponentDescriptor> descriptors_;
    };
}
