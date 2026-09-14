// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/ComponentDescriptor.hpp"

#include "CNA/Editor/Core/Json.hpp"

#include <algorithm>

namespace CNA::Editor
{
    const PropertyDescriptor* ComponentDescriptor::findProperty(std::string_view name) const
    {
        const auto found = std::find_if(properties.begin(), properties.end(),
                                        [&](const PropertyDescriptor& property) { return property.name == name; });
        return found == properties.end() ? nullptr : &*found;
    }

    bool ComponentRegistry::registerComponent(ComponentDescriptor descriptor)
    {
        if (descriptor.typeId.empty()) { return false; }
        const std::string key = descriptor.typeId;
        descriptors_[key] = std::move(descriptor);
        return true;
    }

    const ComponentDescriptor* ComponentRegistry::find(std::string_view typeId) const
    {
        const auto found = descriptors_.find(std::string{typeId});
        return found == descriptors_.end() ? nullptr : &found->second;
    }

    std::vector<std::string> ComponentRegistry::getTypeIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(descriptors_.size());
        for (const auto& [typeId, descriptor] : descriptors_) { ids.push_back(typeId); }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    bool ComponentRegistry::unregisterComponent(std::string_view typeId)
    {
        return descriptors_.erase(std::string{typeId}) > 0;
    }
}

namespace CNA::Editor
{
    PropertyValue propertyValueFromJson(const JsonValue& json, const PropertyDescriptor& descriptor);

    namespace
    {
        /** @brief Reads one structure from @p json against @p fields. */
        PropertyValue::StructureValue readStructure(const JsonValue& json,
                                                    const std::vector<PropertyDescriptor>& fields)
        {
            PropertyValue::StructureValue structure;

            // Driven by the *schema*, not by the JSON. Walking the JSON instead would carry
            // through whatever a file happened to contain, including fields no descriptor
            // declares -- and the next save would write out a shape nothing can read back.
            for (const PropertyDescriptor& field : fields)
            {
                const JsonValue& fieldJson = json[std::string_view{field.name}];

                // Absent is not the same as present-and-empty. A field the document never carried
                // takes the descriptor's declared default -- which is what lets a structure gain a
                // field later without every document already written becoming one with a hole in
                // it. A field that *is* there is read even when its value is empty, because an
                // empty string somebody typed is a choice they made.
                if (fieldJson.isNull())
                {
                    structure.set(field.name, field.defaultValue);
                    continue;
                }

                structure.set(field.name, propertyValueFromJson(fieldJson, field));
            }

            return structure;
        }
    }

    PropertyValue propertyValueFromJson(const JsonValue& json, const PropertyDescriptor& descriptor)
    {
        if (descriptor.type == PropertyType::Structure)
        {
            // An absent or non-object value reads as the declared defaults rather than as an
            // empty structure: a field missing from a document is a field the document was written
            // before, and defaulting it is the only reading that keeps old files opening.
            if (!json.isObject())
            {
                return PropertyValue{readStructure(JsonValue{}, descriptor.structureFields)};
            }
            return PropertyValue{readStructure(json, descriptor.structureFields)};
        }

        if (descriptor.type == PropertyType::List
            && descriptor.elementType == PropertyType::Structure)
        {
            PropertyValue::ListValue list;
            for (const JsonValue& element : json.getElements())
            {
                list.items.push_back(PropertyValue{readStructure(element, descriptor.structureFields)});
            }
            return PropertyValue{std::move(list)};
        }

        return PropertyValue::fromJson(json, descriptor.type, descriptor.elementType);
    }
}
