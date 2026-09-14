// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/PropertyValue.hpp
 * @brief The dynamically-typed value every reflected component property is read and written as.
 *
 * C++ has no usable built-in reflection, so the editor carries its own metadata (see
 * ComponentDescriptor). The inspector, the undo stack and the serialiser all speak this one
 * type, which is what lets a single generic SetPropertyCommand undo a change to *any* property of
 * *any* component -- including component types supplied by a plugin that the editor was never
 * compiled against.
 */

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    class JsonValue;

    /** @brief Names the alternative a PropertyValue currently holds. */
    enum class PropertyType
    {
        /** @brief No value. Returned when a property lookup misses. */
        None,
        Boolean,
        Integer,
        Float,
        String,
        /** @brief A named choice; the value is the option's string name, not its ordinal. */
        Enum,
        Color,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Rectangle,
        /** @brief A reference to an asset by its stable AssetDatabase id. */
        AssetReference,
        /** @brief A reference to another entity in the same scene document. */
        EntityReference,

        /**
         * @brief An ordered, homogeneous list of values.
         *
         * Added last on purpose. `toString(PropertyType)` is on the editor-to-player wire, so the
         * names must stay stable, and appending rather than inserting keeps every existing name
         * where it was.
         *
         * The element type is *declared*, on the PropertyDescriptor, rather than inferred from the
         * first element: a list that is empty has no first element, and a list whose type depended
         * on its contents could not be edited back from empty.
         */
        List,

        /**
         * @brief A fixed set of *named* fields, whose schema the descriptor declares.
         *
         * Appended after List for the reason List was appended after everything: `toString` is on
         * the editor-to-player wire, so inserting a name would renumber nothing but would change
         * what a name means in a build that had not been updated.
         *
         * **This exists because ED-410 asked for it**, and that is the whole justification. It was
         * left unbuilt through all of Phase 2 (ED-311) precisely because nothing needed it, and a
         * schema designed against no consumer is a schema designed wrong. The consumer is a
         * `ModelRenderer`'s per-part material list, whose element is a part name and a material
         * reference -- two fields of different types, which no list of a single type can carry.
         *
         * A structure's fields may not themselves be structures or lists. One level, for the same
         * reason `elementType` forbids a list of lists: two levels is a document model, and this
         * one has exactly one consumer to answer to.
         */
        Structure
    };

    /** @brief Returns the stable textual name of @p type as written into scene files. */
    const char* toString(PropertyType type);

    /** @brief Parses the textual name produced by toString(); returns PropertyType::None on failure. */
    PropertyType parsePropertyType(std::string_view text);

    /**
     * @brief A tagged union covering every property type the inspector can edit.
     *
     * Enum and AssetReference/EntityReference are deliberately *distinct* alternatives rather than
     * plain strings and UUIDs. The inspector needs the distinction to pick a widget (a combo box
     * versus a text field, an asset picker versus an entity picker), and the asset database needs
     * it to walk a scene's outbound references without consulting the descriptor for every field.
     */
    class PropertyValue
    {
    public:
        /** @brief Wraps a string so the variant can tell an enum name from an ordinary string. */
        struct EnumValue
        {
            std::string name;
            friend bool operator==(const EnumValue& lhs, const EnumValue& rhs) { return lhs.name == rhs.name; }
        };

        /** @brief Wraps a Uuid so the variant can tell an asset reference from an entity reference. */
        struct AssetReference
        {
            Uuid id;
            friend bool operator==(const AssetReference& lhs, const AssetReference& rhs) { return lhs.id == rhs.id; }
        };

        /** @brief Wraps a Uuid so the variant can tell an entity reference from an asset reference. */
        struct EntityReference
        {
            Uuid id;
            friend bool operator==(const EntityReference& lhs, const EntityReference& rhs) { return lhs.id == rhs.id; }
        };

        /**
         * @brief An ordered list of values, all of the declared element type.
         *
         * A struct rather than a bare vector so the variant can hold it: `std::vector` of an
         * incomplete type is well-defined, which is what makes a PropertyValue able to contain
         * PropertyValues at all.
         */
        /**
         * @brief A structure's fields, in the order the descriptor declares them.
         *
         * A vector of name/value pairs rather than a map, and the order is load-bearing twice: the
         * inspector draws fields in it, and the JSON it writes comes out in it, so a document
         * round-trips byte for byte instead of in whatever order a hash landed on.
         */
        struct StructureValue
        {
            /**
             * @brief A field: its name and its value.
             *
             * A `std::pair` inside a vector rather than a named struct with a `PropertyValue`
             * member, and the reason is a language rule rather than a preference: `PropertyValue`
             * is incomplete inside its own definition, so it cannot be a direct member of anything
             * declared here. `std::vector` accepts an incomplete element type and `ListValue`
             * already relies on exactly that.
             */
            using Field = std::pair<std::string, PropertyValue>;

            std::vector<Field> fields;

            /**
             * @brief Returns the field named @p name, or nullptr.
             *
             * **The pointer is into this structure, so this structure has to outlive it.** That is
             * ordinary, and it is a trap in one specific shape worth naming: `PropertyValue::get`
             * hands back a *copy*, so
             *
             * ```
             * const PropertyValue* p = value.get<StructureValue>().find("part");  // dangling
             * ```
             *
             * leaves `p` pointing into a temporary that dies at the end of that statement. The read
             * that follows returns whatever is left there, which is not a crash and not obviously
             * wrong -- in the case that found this, a silently empty part name. Bind the structure
             * first:
             *
             * ```
             * const auto& structure = value.get<StructureValue>();
             * const PropertyValue* p = structure.find("part");                    // fine
             * ```
             */
            [[nodiscard]] const PropertyValue* find(std::string_view name) const;

            /** @brief Sets @p name, appending it when it is not already there. */
            void set(std::string name, PropertyValue value);

            friend bool operator==(const StructureValue& lhs, const StructureValue& rhs)
            {
                return lhs.fields == rhs.fields;
            }
        };

        struct ListValue
        {
            std::vector<PropertyValue> items;

            friend bool operator==(const ListValue& lhs, const ListValue& rhs)
            {
                return lhs.items == rhs.items;
            }
        };

        using Storage = std::variant<std::monostate,
                                     bool,
                                     std::int64_t,
                                     float,
                                     std::string,
                                     EnumValue,
                                     EditorColor,
                                     EditorVector2,
                                     EditorVector3,
                                     EditorVector4,
                                     EditorQuaternion,
                                     EditorRectangle,
                                     AssetReference,
                                     EntityReference,
                                     ListValue,
                                     StructureValue>;

        PropertyValue() = default;
        PropertyValue(StructureValue value) : storage_(std::move(value)) {}
        PropertyValue(bool value) : storage_(value) {}
        PropertyValue(int value) : storage_(static_cast<std::int64_t>(value)) {}
        PropertyValue(std::int64_t value) : storage_(value) {}
        PropertyValue(float value) : storage_(value) {}
        PropertyValue(double value) : storage_(static_cast<float>(value)) {}
        PropertyValue(std::string value) : storage_(std::move(value)) {}
        PropertyValue(const char* value) : storage_(std::string{value ? value : ""}) {}
        PropertyValue(EnumValue value) : storage_(std::move(value)) {}
        PropertyValue(EditorColor value) : storage_(value) {}
        PropertyValue(EditorVector2 value) : storage_(value) {}
        PropertyValue(EditorVector3 value) : storage_(value) {}
        PropertyValue(EditorVector4 value) : storage_(value) {}
        PropertyValue(EditorQuaternion value) : storage_(value) {}
        PropertyValue(EditorRectangle value) : storage_(value) {}
        PropertyValue(AssetReference value) : storage_(value) {}
        PropertyValue(EntityReference value) : storage_(value) {}
        PropertyValue(ListValue value) : storage_(std::move(value)) {}

        /** @brief Returns which alternative is held. */
        [[nodiscard]] PropertyType getType() const;

        /** @brief Returns true when no value is held. */
        [[nodiscard]] bool isEmpty() const { return std::holds_alternative<std::monostate>(storage_); }

        /**
         * @brief Returns the held value when it is a @p T, otherwise @p fallback.
         *
         * Never throws, unlike std::get: an inspector reading a property whose type changed under
         * it (a plugin reload, a hand-edited scene file) should degrade to the default rather than
         * take the process down.
         */
        template <typename T>
        [[nodiscard]] T get(T fallback = T{}) const
        {
            const T* held = std::get_if<T>(&storage_);
            return held != nullptr ? *held : fallback;
        }

        /** @brief Returns the underlying variant, for callers that want to std::visit it. */
        [[nodiscard]] const Storage& getStorage() const { return storage_; }

        friend bool operator==(const PropertyValue& lhs, const PropertyValue& rhs)
        {
            return lhs.storage_ == rhs.storage_;
        }
        friend bool operator!=(const PropertyValue& lhs, const PropertyValue& rhs) { return !(lhs == rhs); }

        /**
         * @brief Serialises to the JSON shape documented in docs/FORMATS.md.
         *
         * Scalars become JSON scalars; vectors and colours become fixed-length arrays; references
         * become their UUID string. The type is *not* embedded -- the ComponentDescriptor supplies
         * it on load, which keeps scene files small and readable.
         */
        [[nodiscard]] JsonValue toJson() const;

        /**
         * @brief Reads a value of the type @p type demands out of @p json.
         *
         * @param json The serialised form produced by toJson().
         * @param type The type the ComponentDescriptor declares for this property.
         * @param elementType The element type when @p type is List; ignored otherwise. None means
         *        the list reads back empty, which is the honest answer when nothing declared what
         *        its elements are.
         * @return The parsed value, or an empty PropertyValue when @p json does not fit @p type.
         */
        static PropertyValue fromJson(const JsonValue& json, PropertyType type,
                                      PropertyType elementType = PropertyType::None);

        /** @brief Returns the natural zero value for @p type; used when a scene file omits a field. */
        static PropertyValue defaultOf(PropertyType type);

        /** @brief Returns a short human-readable rendering, for the console and for command labels. */
        [[nodiscard]] std::string toDisplayString() const;

    private:
        Storage storage_;
    };
}
