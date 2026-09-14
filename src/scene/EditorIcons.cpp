// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/EditorIcons.hpp"

#include <cmath>
#include <optional>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    EditorIconKind getEditorIconKind(const EditorEntity& entity)
    {
        // Without a transform there is no position to put an icon at, so there is nothing to draw
        // and nothing to click even if the entity qualifies in every other way.
        if (entity.findComponent(BuiltinComponentIds::kTransform) == nullptr)
        {
            return EditorIconKind::None;
        }

        if (entity.findComponent(BuiltinComponentIds::kCamera) != nullptr)
        {
            return EditorIconKind::Camera;
        }
        if (entity.findComponent(BuiltinComponentIds::kLight) != nullptr)
        {
            return EditorIconKind::Light;
        }
        if (entity.findComponent(BuiltinComponentIds::kAudioSource) != nullptr)
        {
            return EditorIconKind::AudioSource;
        }
        if (entity.findComponent(BuiltinComponentIds::kModelRenderer) != nullptr)
        {
            return EditorIconKind::Model;
        }

        return EditorIconKind::None;
    }

    std::vector<EditorIconPlacement> collectEditorIcons(const SceneDocument& scene,
                                                        const EditorCamera2D& camera)
    {
        std::vector<EditorIconPlacement> icons;

        for (const EditorEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const EditorIconKind kind = getEditorIconKind(entity);
            if (kind == EditorIconKind::None) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entity.getId());
            if (!world) { continue; }

            icons.push_back(EditorIconPlacement{
                entity.getId(), kind,
                camera.worldToScreen(EditorVector2{world->position.x, world->position.y})});
        }

        return icons;
    }

    bool hitTestEditorIcon(const EditorVector2& center, const EditorVector2& screenPoint)
    {
        // A square rather than the badge's exact silhouette: the target is small already, and
        // making it harder to hit than it looks is the one thing a click target must never be.
        return std::fabs(screenPoint.x - center.x) <= kEditorIconExtent
            && std::fabs(screenPoint.y - center.y) <= kEditorIconExtent;
    }
}
