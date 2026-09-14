// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/EntityJson.hpp"

#include <algorithm>

namespace CNA::Editor
{
    namespace
    {
        /**
         * @brief Guesses a property type from raw JSON, for components with no descriptor.
         *
         * Only reached when a document references a component type the editor does not know -- a
         * plugin that failed to load, or a file authored by a newer build. The guess does not need
         * to be right for the inspector (which will not show an unknown component anyway); it needs
         * to be *lossless enough to write back out unchanged*, which this is.
         */
        PropertyType inferPropertyType(const JsonValue& json)
        {
            switch (json.getType())
            {
                case JsonType::Boolean: return PropertyType::Boolean;
                case JsonType::Number: return PropertyType::Float;
                case JsonType::String: return PropertyType::String;
                case JsonType::Array: {
                    // A short array of numbers is a vector, which is the guess the editor has made
                    // since Phase 0 and the overwhelmingly common case. A short array of anything
                    // else is not: reading ["ground", "solid"] as a Vector2 turns it into (0, 0)
                    // and writes that back, silently emptying a field on a component whose plugin
                    // is missing -- the one case the descriptor system promises to survive.
                    const auto& elements = json.getElements();
                    const bool allNumbers =
                        !elements.empty()
                        && std::all_of(elements.begin(), elements.end(), [](const JsonValue& element)
                                       { return element.getType() == JsonType::Number; });
                    if (!allNumbers) { return PropertyType::List; }

                    switch (elements.size())
                    {
                        case 2: return PropertyType::Vector2;
                        case 3: return PropertyType::Vector3;
                        case 4: return PropertyType::Vector4;
                        default: return PropertyType::List;
                    }
                }
                default: return PropertyType::String;
            }
        }
    }

    PropertyValue readUntypedJson(const JsonValue& json)
    {
        const PropertyType type = inferPropertyType(json);
        if (type != PropertyType::List) { return PropertyValue::fromJson(json, type); }

        // Element by element, because nothing declared what they are. A homogeneous list -- which
        // is what a list in a document always is in practice -- comes back exactly.
        PropertyValue::ListValue list;
        for (const JsonValue& element : json.getElements())
        {
            list.items.push_back(readUntypedJson(element));
        }
        return PropertyValue{std::move(list)};
    }

    JsonValue entityToJson(const EditorEntity& entity)
    {
        JsonValue entityJson = JsonValue::makeObject();
        entityJson.set("id", JsonValue{entity.getId().toString()});
        entityJson.set("name", JsonValue{entity.getName()});

        if (entity.getParentId().isValid())
        {
            entityJson.set("parent", JsonValue{entity.getParentId().toString()});
        }
        if (!entity.isEnabled()) { entityJson.set("enabled", JsonValue{false}); }
        if (entity.getSortOrder() != 0) { entityJson.set("sortOrder", JsonValue{entity.getSortOrder()}); }

        JsonValue componentsJson = JsonValue::makeObject();
        for (const EditorComponent& component : entity.getComponents())
        {
            JsonValue componentJson = JsonValue::makeObject();
            for (const auto& [name, value] : component.getProperties())
            {
                componentJson.set(name, value.toJson());
            }
            componentsJson.set(component.getTypeId(), std::move(componentJson));
        }
        entityJson.set("components", std::move(componentsJson));

        if (!entity.getEditorState().empty())
        {
            JsonValue editorStateJson = JsonValue::makeObject();
            for (const auto& [name, value] : entity.getEditorState())
            {
                editorStateJson.set(name, value.toJson());
            }
            entityJson.set("editorState", std::move(editorStateJson));
        }

        return entityJson;
    }

    EditorEntity entityFromJson(const JsonValue& json,
                                const ComponentRegistry& registry,
                                std::vector<std::string>& warnings)
    {
        EditorEntity entity;

        const Uuid id = Uuid::parse(json["id"].asString());
        entity.setId(id.isValid() ? id : Uuid::generate());
        if (!id.isValid())
        {
            warnings.push_back("entity '" + json["name"].asString("<unnamed>")
                               + "' had no valid id; a new one was generated");
        }

        entity.setName(json["name"].asString("Entity"));
        entity.setParentId(Uuid::parse(json["parent"].asString()));
        entity.setEnabled(json.contains("enabled") ? json["enabled"].asBoolean(true) : true);
        entity.setSortOrder(json["sortOrder"].asInt(0));

        for (const auto& [typeId, componentJson] : json["components"].getMembers())
        {
            EditorComponent component{typeId};
            const ComponentDescriptor* descriptor = registry.find(typeId);
            if (descriptor == nullptr)
            {
                warnings.push_back("entity '" + entity.getName() + "' uses unregistered component type '"
                                   + typeId + "'; its data is preserved but not editable");
            }

            for (const auto& [propertyName, propertyJson] : componentJson.getMembers())
            {
                const PropertyDescriptor* property =
                    descriptor != nullptr ? descriptor->findProperty(propertyName) : nullptr;
                if (property == nullptr)
                {
                    // No descriptor: read for byte fidelity rather than for meaning, so that
                    // opening and saving a document whose plugin is missing leaves the file alone.
                    component.setProperty(propertyName, readUntypedJson(propertyJson));
                    continue;
                }

                // Through the descriptor-aware reader, not `PropertyValue::fromJson`: a structure
                // cannot be decoded without the field schema, and that schema is on the descriptor
                // this loop already has in hand (ED-410).
                component.setProperty(propertyName, propertyValueFromJson(propertyJson, *property));
            }

            if (descriptor != nullptr) { component.applyDefaults(*descriptor); }
            entity.addComponent(std::move(component));
        }

        for (const auto& [name, valueJson] : json["editorState"].getMembers())
        {
            entity.setEditorState(name, readUntypedJson(valueJson));
        }

        return entity;
    }
}
