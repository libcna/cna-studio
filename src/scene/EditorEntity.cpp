// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/EditorEntity.hpp"

#include <algorithm>

namespace CNA::Editor
{
    PropertyValue EditorComponent::getProperty(std::string_view name) const
    {
        const auto found = properties_.find(std::string{name});
        return found == properties_.end() ? PropertyValue{} : found->second;
    }

    PropertyValue EditorComponent::getPropertyOrDefault(std::string_view name,
                                                        const ComponentDescriptor* descriptor) const
    {
        const auto found = properties_.find(std::string{name});
        if (found != properties_.end()) { return found->second; }
        if (descriptor != nullptr)
        {
            if (const PropertyDescriptor* property = descriptor->findProperty(name))
            {
                return property->defaultValue.isEmpty() ? PropertyValue::defaultOf(property->type)
                                                        : property->defaultValue;
            }
        }
        return PropertyValue{};
    }

    void EditorComponent::setProperty(std::string name, PropertyValue value)
    {
        properties_[std::move(name)] = std::move(value);
    }

    bool EditorComponent::hasProperty(std::string_view name) const
    {
        return properties_.find(std::string{name}) != properties_.end();
    }

    bool EditorComponent::removeProperty(std::string_view name)
    {
        return properties_.erase(std::string{name}) > 0;
    }

    void EditorComponent::applyDefaults(const ComponentDescriptor& descriptor)
    {
        for (const PropertyDescriptor& property : descriptor.properties)
        {
            if (hasProperty(property.name)) { continue; }
            setProperty(property.name, property.defaultValue.isEmpty()
                                           ? PropertyValue::defaultOf(property.type)
                                           : property.defaultValue);
        }
    }

    const EditorComponent* EditorEntity::findComponent(std::string_view typeId) const
    {
        const auto found = std::find_if(components_.begin(), components_.end(),
                                        [&](const EditorComponent& component) { return component.getTypeId() == typeId; });
        return found == components_.end() ? nullptr : &*found;
    }

    EditorComponent* EditorEntity::findComponent(std::string_view typeId)
    {
        const auto found = std::find_if(components_.begin(), components_.end(),
                                        [&](const EditorComponent& component) { return component.getTypeId() == typeId; });
        return found == components_.end() ? nullptr : &*found;
    }

    EditorComponent& EditorEntity::addComponent(EditorComponent component)
    {
        components_.push_back(std::move(component));
        return components_.back();
    }

    bool EditorEntity::removeComponentAt(std::size_t index)
    {
        if (index >= components_.size()) { return false; }
        components_.erase(components_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    std::size_t EditorEntity::indexOfComponent(std::string_view typeId) const
    {
        for (std::size_t index = 0; index < components_.size(); ++index)
        {
            if (components_[index].getTypeId() == typeId) { return index; }
        }
        return static_cast<std::size_t>(-1);
    }

    void EditorEntity::setEditorState(std::string name, PropertyValue value)
    {
        editorState_[std::move(name)] = std::move(value);
    }

    bool EditorEntity::removeEditorState(std::string_view name)
    {
        const auto found = editorState_.find(std::string{name});
        if (found == editorState_.end()) { return false; }

        editorState_.erase(found);
        return true;
    }
}
