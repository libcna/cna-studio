// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Scene/StudioIcons.hpp"

#include <cmath>
#include <optional>

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"

namespace CNA::Studio
{
    StudioIconKind getStudioIconKind(const StudioEntity& entity)
    {
        // Without a transform there is no position to put an icon at, so there is nothing to draw
        // and nothing to click even if the entity qualifies in every other way.
        if (entity.findComponent(BuiltinComponentIds::kTransform) == nullptr)
        {
            return StudioIconKind::None;
        }

        if (entity.findComponent(BuiltinComponentIds::kCamera) != nullptr)
        {
            return StudioIconKind::Camera;
        }
        if (entity.findComponent(BuiltinComponentIds::kLight) != nullptr)
        {
            return StudioIconKind::Light;
        }
        if (entity.findComponent(BuiltinComponentIds::kAudioSource) != nullptr)
        {
            return StudioIconKind::AudioSource;
        }
        if (entity.findComponent(BuiltinComponentIds::kModelRenderer) != nullptr)
        {
            return StudioIconKind::Model;
        }

        // Nothing that draws, so nothing would show this entity but the small bounds box every
        // entity gets -- which reads as a tiny object rather than as a marker
        // (`plan.md` STUDIO-11009). A sprite, an animated sprite or a tilemap *is* geometry and is
        // left alone: giving those a marker badge as well would put a second mark on something the
        // viewport already draws.
        static constexpr const char* kDrawing[] = {
            BuiltinComponentIds::kSpriteRenderer,
            BuiltinComponentIds::kSpriteAnimation,
            BuiltinComponentIds::kTilemap,
        };
        for (const char* component : kDrawing)
        {
            if (entity.findComponent(component) != nullptr) { return StudioIconKind::None; }
        }

        return StudioIconKind::Empty;
    }

    std::vector<StudioIconPlacement> collectStudioIcons(const SceneDocument& scene,
                                                        const StudioCamera2D& camera)
    {
        std::vector<StudioIconPlacement> icons;

        for (const StudioEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const StudioIconKind kind = getStudioIconKind(entity);
            if (kind == StudioIconKind::None) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entity.getId());
            if (!world) { continue; }

            icons.push_back(StudioIconPlacement{
                entity.getId(), kind,
                camera.worldToScreen(StudioVector2{world->position.x, world->position.y})});
        }

        return icons;
    }

    bool hitTestStudioIcon(const StudioVector2& center, const StudioVector2& screenPoint)
    {
        // A square rather than the badge's exact silhouette: the target is small already, and
        // making it harder to hit than it looks is the one thing a click target must never be.
        return std::fabs(screenPoint.x - center.x) <= kStudioIconExtent
            && std::fabs(screenPoint.y - center.y) <= kStudioIconExtent;
    }
}
