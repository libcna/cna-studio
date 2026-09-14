// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Scene/StudioEntity.hpp"

#include <algorithm>

namespace CNA::Studio
{
    PropertyValue StudioComponent::getProperty(std::string_view name) const
    {
        const auto found = properties_.find(std::string{name});
        return found == properties_.end() ? PropertyValue{} : found->second;
    }

    PropertyValue StudioComponent::getPropertyOrDefault(std::string_view name,
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

    void StudioComponent::setProperty(std::string name, PropertyValue value)
    {
        properties_[std::move(name)] = std::move(value);
    }

    bool StudioComponent::hasProperty(std::string_view name) const
    {
        return properties_.find(std::string{name}) != properties_.end();
    }

    bool StudioComponent::removeProperty(std::string_view name)
    {
        return properties_.erase(std::string{name}) > 0;
    }

    void StudioComponent::applyDefaults(const ComponentDescriptor& descriptor)
    {
        for (const PropertyDescriptor& property : descriptor.properties)
        {
            if (hasProperty(property.name)) { continue; }
            setProperty(property.name, property.defaultValue.isEmpty()
                                           ? PropertyValue::defaultOf(property.type)
                                           : property.defaultValue);
        }
    }

    const StudioComponent* StudioEntity::findComponent(std::string_view typeId) const
    {
        const auto found = std::find_if(components_.begin(), components_.end(),
                                        [&](const StudioComponent& component) { return component.getTypeId() == typeId; });
        return found == components_.end() ? nullptr : &*found;
    }

    StudioComponent* StudioEntity::findComponent(std::string_view typeId)
    {
        const auto found = std::find_if(components_.begin(), components_.end(),
                                        [&](const StudioComponent& component) { return component.getTypeId() == typeId; });
        return found == components_.end() ? nullptr : &*found;
    }

    StudioComponent& StudioEntity::addComponent(StudioComponent component)
    {
        components_.push_back(std::move(component));
        return components_.back();
    }

    bool StudioEntity::removeComponentAt(std::size_t index)
    {
        if (index >= components_.size()) { return false; }
        components_.erase(components_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    std::size_t StudioEntity::indexOfComponent(std::string_view typeId) const
    {
        for (std::size_t index = 0; index < components_.size(); ++index)
        {
            if (components_[index].getTypeId() == typeId) { return index; }
        }
        return static_cast<std::size_t>(-1);
    }

    void StudioEntity::setStudioState(std::string name, PropertyValue value)
    {
        studioState_[std::move(name)] = std::move(value);
    }

    bool StudioEntity::removeStudioState(std::string_view name)
    {
        const auto found = studioState_.find(std::string{name});
        if (found == studioState_.end()) { return false; }

        studioState_.erase(found);
        return true;
    }
}
