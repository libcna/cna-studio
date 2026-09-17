// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Scene/AssetDrop.hpp"

#include "CNA/Studio/Core/ComponentDescriptor.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"

namespace CNA::Studio
{
    namespace
    {
        /** @brief The component a kind of asset is used through, and the property that names it. */
        struct DropTarget
        {
            const char* componentTypeId = nullptr;
            const char* propertyName = nullptr;
        };

        DropTarget targetFor(AssetType type)
        {
            switch (type)
            {
                case AssetType::Texture2D:
                    return {BuiltinComponentIds::kSpriteRenderer, "texture"};
                case AssetType::Model:
                    return {BuiltinComponentIds::kModelRenderer, "model"};

                // Both are played through the same component, which is what CNA offers: the
                // difference between a sound effect and a song is how it is *loaded*, and that is
                // the importer's business rather than the scene's.
                case AssetType::SoundEffect:
                case AssetType::Song:
                    return {BuiltinComponentIds::kAudioSource, "clip"};

                default:
                    return {};
            }
        }

        /** @brief The last segment of a project-relative path, without its extension. */
        std::string entityNameFor(const std::string& sourcePath)
        {
            const std::size_t slash = sourcePath.find_last_of('/');
            std::string name =
                slash == std::string::npos ? sourcePath : sourcePath.substr(slash + 1);

            const std::size_t dot = name.find_last_of('.');
            if (dot != std::string::npos && dot != 0) { name = name.substr(0, dot); }

            // An entity called "Entity" in a scene of forty is one nobody finds twice, and an
            // entity called "" is worse. The file name is what the user was just looking at.
            return name.empty() ? std::string{"Entity"} : name;
        }
    }

    StudioAssetDropKind studioAssetDropKind(AssetType type)
    {
        if (type == AssetType::Prefab) { return StudioAssetDropKind::Prefab; }
        return targetFor(type).componentTypeId != nullptr ? StudioAssetDropKind::Entity
                                                          : StudioAssetDropKind::Unsupported;
    }

    std::string describeStudioAssetDropRefusal(const AssetRecord& record)
    {
        if (studioAssetDropKind(record.type) != StudioAssetDropKind::Unsupported) { return {}; }

        // Named rather than generic. "That cannot be dropped here" is a sentence people read twice
        // and learn nothing from; the kind is what tells them whether they grabbed the wrong file.
        if (record.type == AssetType::Scene)
        {
            // The one refusal that is a *different action* rather than a missing one, so it points
            // at the action instead of apologising.
            return "A scene is opened rather than placed in another scene.";
        }
        return std::string{"There is nothing in a scene that uses a "} + toString(record.type)
             + " directly.";
    }

    bool studioEntityForAsset(const AssetDatabase& assets, const Uuid& assetId,
                              const ComponentRegistry& registry, const StudioVector3& position,
                              StudioEntity& outEntity)
    {
        const AssetRecord* record = assets.find(assetId);
        if (record == nullptr) { return false; }

        const DropTarget target = targetFor(record->type);
        if (target.componentTypeId == nullptr) { return false; }

        StudioEntity entity{Uuid::generate(), entityNameFor(record->sourcePath)};

        // A transform first, and always. An entity without one has no position, so every viewport
        // operation would have to special-case it -- and a dropped asset that cannot be moved is
        // not something anybody wants.
        StudioComponent transform{BuiltinComponentIds::kTransform};
        if (const ComponentDescriptor* descriptor = registry.find(BuiltinComponentIds::kTransform))
        {
            transform.applyDefaults(*descriptor);
        }
        transform.setProperty("position", PropertyValue{position});
        entity.addComponent(std::move(transform));

        StudioComponent component{target.componentTypeId};
        if (const ComponentDescriptor* descriptor = registry.find(target.componentTypeId))
        {
            // Defaults before the reference, so a dropped asset behaves like one added through the
            // inspector rather than like an entity carrying one property and no others.
            component.applyDefaults(*descriptor);
        }
        component.setProperty(target.propertyName,
                              PropertyValue{PropertyValue::AssetReference{assetId}});
        entity.addComponent(std::move(component));

        outEntity = std::move(entity);
        return true;
    }
}
