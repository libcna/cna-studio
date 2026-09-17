// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Scene/EntityJson.hpp"

#include <algorithm>

namespace CNA::Studio
{
    namespace
    {
        /**
         * @brief The reserved key a component's reference types are written under.
         *
         * `plan.md` STUDIO-09017. Inside the component's own object rather than beside it, so the
         * hint travels with the thing it describes. The `$` is what makes it reserved:
         * `describeStudioAssetNameProblem` does not govern property names, but every property in
         * this project's components and plugins is an identifier, and `$refs` is not one.
         *
         * The exported runtime (`Runtime/SceneLoader.hpp`) keeps a component's JSON verbatim and
         * reads named fields out of it, so an extra key is invisible to a shipped game.
         */
        constexpr const char* kReferenceTypesKey = "$refs";

        /** @brief The name a reference type is written as, or null when it is not one. */
        const char* referenceTypeName(PropertyType type)
        {
            // Only the two that serialise as a bare UUID string, which is what makes them
            // indistinguishable from a string property that happens to hold one. Everything else
            // `readUntypedJson` can infer from the JSON's own shape.
            if (type == PropertyType::AssetReference) { return "asset"; }
            if (type == PropertyType::EntityReference) { return "entity"; }
            return nullptr;
        }

        /** @brief The property type @p name denotes, or String when it denotes neither. */
        PropertyType referenceTypeFor(std::string_view name)
        {
            if (name == "asset") { return PropertyType::AssetReference; }
            if (name == "entity") { return PropertyType::EntityReference; }
            return PropertyType::String;
        }

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

    JsonValue entityToJson(const StudioEntity& entity)
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
        for (const StudioComponent& component : entity.getComponents())
        {
            JsonValue componentJson = JsonValue::makeObject();
            JsonValue referenceTypes = JsonValue::makeObject();

            for (const auto& [name, value] : component.getProperties())
            {
                componentJson.set(name, value.toJson());

                // An asset and an entity reference both serialise as a bare UUID string, which is
                // indistinguishable from a string property holding one -- so a build with no
                // descriptor for this component read them back as strings, and both the dependency
                // index and the missing-reference report went blind to exactly the file that a
                // failed plugin makes (STUDIO-09017).
                //
                // Written whenever the type is *known*, which is when the descriptor is present --
                // the one moment the information exists to record. Writing it only where it is
                // needed is impossible: by the time it is needed it has already been lost.
                if (const char* reference = referenceTypeName(value.getType()); reference != nullptr)
                {
                    referenceTypes.set(name, JsonValue{std::string{reference}});
                }
            }

            if (!referenceTypes.getMembers().empty())
            {
                componentJson.set(kReferenceTypesKey, std::move(referenceTypes));
            }
            componentsJson.set(component.getTypeId(), std::move(componentJson));
        }
        entityJson.set("components", std::move(componentsJson));

        if (!entity.getStudioState().empty())
        {
            JsonValue studioStateJson = JsonValue::makeObject();
            for (const auto& [name, value] : entity.getStudioState())
            {
                studioStateJson.set(name, value.toJson());
            }
            entityJson.set("editorState", std::move(studioStateJson));
        }

        return entityJson;
    }

    StudioEntity entityFromJson(const JsonValue& json,
                                const ComponentRegistry& registry,
                                std::vector<std::string>& warnings)
    {
        StudioEntity entity;

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
            StudioComponent component{typeId};
            const ComponentDescriptor* descriptor = registry.find(typeId);
            if (descriptor == nullptr)
            {
                warnings.push_back("entity '" + entity.getName() + "' uses unregistered component type '"
                                   + typeId + "'; its data is preserved but not editable");
            }

            const JsonValue& referenceTypes = componentJson[kReferenceTypesKey];

            for (const auto& [propertyName, propertyJson] : componentJson.getMembers())
            {
                // The hint itself is not a property. A component that grew one would write it back
                // out twice and, worse, show it in an inspector as a field nobody declared.
                if (propertyName == kReferenceTypesKey) { continue; }

                const PropertyDescriptor* property =
                    descriptor != nullptr ? descriptor->findProperty(propertyName) : nullptr;
                if (property == nullptr)
                {
                    // The type the file recorded, when it recorded one (STUDIO-09017). This is the
                    // whole of the fix: without it a reference on a component whose plugin failed
                    // to load came back as a String, and the file a failed plugin produces is
                    // exactly the one a dependency view is opened for.
                    if (const JsonValue& hint = referenceTypes[propertyName]; !hint.isNull())
                    {
                        const PropertyType hinted = referenceTypeFor(hint.asString());
                        if (hinted != PropertyType::String)
                        {
                            component.setProperty(propertyName,
                                                  PropertyValue::fromJson(propertyJson, hinted));
                            continue;
                        }
                    }

                    // No descriptor and no hint: read for byte fidelity rather than for meaning, so
                    // that opening and saving a document whose plugin is missing leaves the file
                    // alone.
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
            entity.setStudioState(name, readUntypedJson(valueJson));
        }

        return entity;
    }
}
