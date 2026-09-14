// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/PropertyValue.hpp"

#include <array>
#include <cstdio>

#include "CNA/Editor/Core/Json.hpp"

namespace CNA::Editor
{
    namespace
    {
        struct TypeName
        {
            PropertyType type;
            const char* name;
        };

        constexpr std::array<TypeName, 16> kTypeNames{{
            {PropertyType::None, "none"},
            {PropertyType::Boolean, "bool"},
            {PropertyType::Integer, "int"},
            {PropertyType::Float, "float"},
            {PropertyType::String, "string"},
            {PropertyType::Enum, "enum"},
            {PropertyType::Color, "color"},
            {PropertyType::Vector2, "vector2"},
            {PropertyType::Vector3, "vector3"},
            {PropertyType::Vector4, "vector4"},
            {PropertyType::Quaternion, "quaternion"},
            {PropertyType::Rectangle, "rectangle"},
            {PropertyType::AssetReference, "asset"},
            {PropertyType::EntityReference, "entity"},
            {PropertyType::List, "list"},
            {PropertyType::Structure, "structure"},
        }};

        /** @brief Reads @p count floats out of a JSON array, leaving missing slots at zero. */
        template <std::size_t Count>
        std::array<float, Count> readFloats(const JsonValue& json)
        {
            std::array<float, Count> values{};
            const auto& elements = json.getElements();
            for (std::size_t index = 0; index < Count && index < elements.size(); ++index)
            {
                values[index] = elements[index].asFloat();
            }
            return values;
        }

        JsonValue writeFloats(std::initializer_list<float> values)
        {
            JsonValue json = JsonValue::makeArray();
            for (const float value : values) { json.append(JsonValue{static_cast<double>(value)}); }
            return json;
        }

        std::string formatFloat(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
            return buffer;
        }
    }

    const char* toString(PropertyType type)
    {
        for (const auto& entry : kTypeNames)
        {
            if (entry.type == type) { return entry.name; }
        }
        return "none";
    }

    PropertyType parsePropertyType(std::string_view text)
    {
        for (const auto& entry : kTypeNames)
        {
            if (text == entry.name) { return entry.type; }
        }
        return PropertyType::None;
    }

    PropertyType PropertyValue::getType() const
    {
        // The order here must match the Storage alternative order exactly.
        switch (storage_.index())
        {
            case 0: return PropertyType::None;
            case 1: return PropertyType::Boolean;
            case 2: return PropertyType::Integer;
            case 3: return PropertyType::Float;
            case 4: return PropertyType::String;
            case 5: return PropertyType::Enum;
            case 6: return PropertyType::Color;
            case 7: return PropertyType::Vector2;
            case 8: return PropertyType::Vector3;
            case 9: return PropertyType::Vector4;
            case 10: return PropertyType::Quaternion;
            case 11: return PropertyType::Rectangle;
            case 12: return PropertyType::AssetReference;
            case 13: return PropertyType::EntityReference;
            case 14: return PropertyType::List;
            case 15: return PropertyType::Structure;
            default: return PropertyType::None;
        }
    }

    JsonValue PropertyValue::toJson() const
    {
        switch (getType())
        {
            case PropertyType::None: return JsonValue{};
            case PropertyType::Boolean: return JsonValue{get<bool>()};
            case PropertyType::Integer: return JsonValue{get<std::int64_t>()};
            case PropertyType::Float: return JsonValue{static_cast<double>(get<float>())};
            case PropertyType::String: return JsonValue{get<std::string>()};
            case PropertyType::Enum: return JsonValue{get<EnumValue>().name};
            case PropertyType::Color: {
                const EditorColor color = get<EditorColor>();
                JsonValue json = JsonValue::makeArray();
                json.append(JsonValue{static_cast<int>(color.r)});
                json.append(JsonValue{static_cast<int>(color.g)});
                json.append(JsonValue{static_cast<int>(color.b)});
                json.append(JsonValue{static_cast<int>(color.a)});
                return json;
            }
            case PropertyType::Vector2: {
                const EditorVector2 value = get<EditorVector2>();
                return writeFloats({value.x, value.y});
            }
            case PropertyType::Vector3: {
                const EditorVector3 value = get<EditorVector3>();
                return writeFloats({value.x, value.y, value.z});
            }
            case PropertyType::Vector4: {
                const EditorVector4 value = get<EditorVector4>();
                return writeFloats({value.x, value.y, value.z, value.w});
            }
            case PropertyType::Quaternion: {
                const EditorQuaternion value = get<EditorQuaternion>();
                return writeFloats({value.x, value.y, value.z, value.w});
            }
            case PropertyType::Rectangle: {
                const EditorRectangle value = get<EditorRectangle>();
                JsonValue json = JsonValue::makeArray();
                json.append(JsonValue{value.x});
                json.append(JsonValue{value.y});
                json.append(JsonValue{value.width});
                json.append(JsonValue{value.height});
                return json;
            }
            case PropertyType::AssetReference: {
                const Uuid id = get<AssetReference>().id;
                return id.isValid() ? JsonValue{id.toString()} : JsonValue{};
            }
            case PropertyType::EntityReference: {
                const Uuid id = get<EntityReference>().id;
                return id.isValid() ? JsonValue{id.toString()} : JsonValue{};
            }
            case PropertyType::Structure: {
                // A JSON object keyed by the field names the value itself carries. No descriptor is
                // needed to *write* one, unlike to read one: a value knows what its fields are
                // called, and reading is where a name has to be matched against a declared schema.
                JsonValue json = JsonValue::makeObject();
                for (const StructureValue::Field& field : get<StructureValue>().fields)
                {
                    json.set(field.first, field.second.toJson());
                }
                return json;
            }
            case PropertyType::List: {
                // A plain JSON array of the elements' own encodings, with no type tag per item.
                // The descriptor declares the element type, so a tag would be a second source of
                // truth -- and the first one to disagree wins by accident.
                JsonValue json = JsonValue::makeArray();
                for (const PropertyValue& item : get<ListValue>().items)
                {
                    json.append(item.toJson());
                }
                return json;
            }
        }
        return JsonValue{};
    }

    PropertyValue PropertyValue::fromJson(const JsonValue& json, PropertyType type,
                                          PropertyType elementType)
    {
        switch (type)
        {
            case PropertyType::None: return PropertyValue{};
            case PropertyType::Boolean: return PropertyValue{json.asBoolean()};
            case PropertyType::Integer: return PropertyValue{static_cast<std::int64_t>(json.asNumber())};
            case PropertyType::Float: return PropertyValue{json.asFloat()};
            case PropertyType::String: return PropertyValue{json.asString()};
            case PropertyType::Enum: return PropertyValue{EnumValue{json.asString()}};
            case PropertyType::Color: {
                const auto& elements = json.getElements();
                EditorColor color;
                if (elements.size() >= 4)
                {
                    color.r = static_cast<std::uint8_t>(elements[0].asInt(255));
                    color.g = static_cast<std::uint8_t>(elements[1].asInt(255));
                    color.b = static_cast<std::uint8_t>(elements[2].asInt(255));
                    color.a = static_cast<std::uint8_t>(elements[3].asInt(255));
                }
                return PropertyValue{color};
            }
            case PropertyType::Vector2: {
                const auto values = readFloats<2>(json);
                return PropertyValue{EditorVector2{values[0], values[1]}};
            }
            case PropertyType::Vector3: {
                const auto values = readFloats<3>(json);
                return PropertyValue{EditorVector3{values[0], values[1], values[2]}};
            }
            case PropertyType::Vector4: {
                const auto values = readFloats<4>(json);
                return PropertyValue{EditorVector4{values[0], values[1], values[2], values[3]}};
            }
            case PropertyType::Quaternion: {
                // An absent or malformed quaternion must default to identity, not to all-zero:
                // an all-zero quaternion is not a rotation and would collapse the transform.
                const auto& elements = json.getElements();
                if (elements.size() < 4) { return PropertyValue{EditorQuaternion{}}; }
                const auto values = readFloats<4>(json);
                return PropertyValue{EditorQuaternion{values[0], values[1], values[2], values[3]}};
            }
            case PropertyType::Rectangle: {
                const auto& elements = json.getElements();
                EditorRectangle rectangle;
                if (elements.size() >= 4)
                {
                    rectangle.x = elements[0].asInt();
                    rectangle.y = elements[1].asInt();
                    rectangle.width = elements[2].asInt();
                    rectangle.height = elements[3].asInt();
                }
                return PropertyValue{rectangle};
            }
            case PropertyType::AssetReference:
                return PropertyValue{AssetReference{Uuid::parse(json.asString())}};
            case PropertyType::EntityReference:
                return PropertyValue{EntityReference{Uuid::parse(json.asString())}};
            case PropertyType::Structure:
                // Undecodable without the field schema, which this overload does not have. The
                // one that does is `propertyValueFromJson` in ComponentDescriptor.hpp; returning
                // an empty structure here is what a caller that has not been updated gets, and it
                // is a visible nothing rather than a silently wrong something.
                return PropertyValue{StructureValue{}};
            case PropertyType::List: {
                ListValue list;

                // An undeclared element type reads back as an empty list rather than as guessed
                // values. Guessing would produce a list the inspector cannot edit and the next save
                // would write out in a shape nothing declared.
                if (elementType == PropertyType::None || elementType == PropertyType::List)
                {
                    return PropertyValue{std::move(list)};
                }

                for (const JsonValue& element : json.getElements())
                {
                    list.items.push_back(fromJson(element, elementType));
                }
                return PropertyValue{std::move(list)};
            }
        }
        return PropertyValue{};
    }

    PropertyValue PropertyValue::defaultOf(PropertyType type)
    {
        switch (type)
        {
            case PropertyType::None: return PropertyValue{};
            case PropertyType::Boolean: return PropertyValue{false};
            case PropertyType::Integer: return PropertyValue{static_cast<std::int64_t>(0)};
            case PropertyType::Float: return PropertyValue{0.0f};
            case PropertyType::String: return PropertyValue{std::string{}};
            case PropertyType::Enum: return PropertyValue{EnumValue{}};
            case PropertyType::Color: return PropertyValue{EditorColor{}};
            case PropertyType::Vector2: return PropertyValue{EditorVector2{}};
            case PropertyType::Vector3: return PropertyValue{EditorVector3{}};
            case PropertyType::Vector4: return PropertyValue{EditorVector4{}};
            case PropertyType::Quaternion: return PropertyValue{EditorQuaternion{}};
            case PropertyType::Rectangle: return PropertyValue{EditorRectangle{}};
            case PropertyType::AssetReference: return PropertyValue{AssetReference{}};
            case PropertyType::EntityReference: return PropertyValue{EntityReference{}};
            case PropertyType::List: return PropertyValue{ListValue{}};
            case PropertyType::Structure: return PropertyValue{StructureValue{}};
        }
        return PropertyValue{};
    }

    std::string PropertyValue::toDisplayString() const
    {
        switch (getType())
        {
            case PropertyType::None: return "<none>";
            case PropertyType::Boolean: return get<bool>() ? "true" : "false";
            case PropertyType::Integer: return std::to_string(get<std::int64_t>());
            case PropertyType::Float: return formatFloat(get<float>());
            case PropertyType::String: return get<std::string>();
            case PropertyType::Enum: return get<EnumValue>().name;
            case PropertyType::Color: {
                const EditorColor color = get<EditorColor>();
                return "(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", "
                     + std::to_string(color.b) + ", " + std::to_string(color.a) + ")";
            }
            case PropertyType::Vector2: {
                const EditorVector2 value = get<EditorVector2>();
                return "(" + formatFloat(value.x) + ", " + formatFloat(value.y) + ")";
            }
            case PropertyType::Vector3: {
                const EditorVector3 value = get<EditorVector3>();
                return "(" + formatFloat(value.x) + ", " + formatFloat(value.y) + ", "
                     + formatFloat(value.z) + ")";
            }
            case PropertyType::Vector4: {
                const EditorVector4 value = get<EditorVector4>();
                return "(" + formatFloat(value.x) + ", " + formatFloat(value.y) + ", "
                     + formatFloat(value.z) + ", " + formatFloat(value.w) + ")";
            }
            case PropertyType::Quaternion: {
                const EditorQuaternion value = get<EditorQuaternion>();
                return "(" + formatFloat(value.x) + ", " + formatFloat(value.y) + ", "
                     + formatFloat(value.z) + ", " + formatFloat(value.w) + ")";
            }
            case PropertyType::Rectangle: {
                const EditorRectangle value = get<EditorRectangle>();
                return "(" + std::to_string(value.x) + ", " + std::to_string(value.y) + ", "
                     + std::to_string(value.width) + ", " + std::to_string(value.height) + ")";
            }
            case PropertyType::AssetReference: {
                const Uuid id = get<AssetReference>().id;
                return id.isValid() ? "asset:" + id.toString() : "asset:<none>";
            }
            case PropertyType::EntityReference: {
                const Uuid id = get<EntityReference>().id;
                return id.isValid() ? "entity:" + id.toString() : "entity:<none>";
            }
            case PropertyType::List: {
                // A count rather than the contents. This string ends up in undo labels and console
                // lines, where a list of two hundred tiles would drown everything around it.
                const std::size_t count = get<ListValue>().items.size();
                return count == 1 ? "1 item" : std::to_string(count) + " items";
            }
            case PropertyType::Structure: {
                // The fields' own display strings, comma-separated and named. A structure is small
                // by construction -- it has as many fields as a descriptor declared -- so unlike a
                // list there is nothing here that could drown an undo label.
                std::string text = "{";
                bool first = true;
                for (const StructureValue::Field& field : get<StructureValue>().fields)
                {
                    if (!first) { text += ", "; }
                    first = false;
                    text += field.first + ": " + field.second.toDisplayString();
                }
                return text + "}";
            }
        }
        return "<none>";
    }
}

namespace CNA::Editor
{
    const PropertyValue* PropertyValue::StructureValue::find(std::string_view name) const
    {
        for (const Field& field : fields)
        {
            if (field.first == name) { return &field.second; }
        }
        return nullptr;
    }

    void PropertyValue::StructureValue::set(std::string name, PropertyValue value)
    {
        for (Field& field : fields)
        {
            if (field.first == name)
            {
                field.second = std::move(value);
                return;
            }
        }

        // Appended rather than inserted in some sorted position: the declared order is what the
        // inspector draws and what the JSON comes out in, so a field arriving late belongs at the
        // end where a reader will notice it rather than in the middle where it looks original.
        fields.emplace_back(std::move(name), std::move(value));
    }
}
