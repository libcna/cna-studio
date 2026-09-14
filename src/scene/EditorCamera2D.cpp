// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/EditorCamera2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/EditorIcons.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    void EditorCamera2D::setZoom(float zoom)
    {
        zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
    }

    EditorVector2 EditorCamera2D::worldToScreen(const EditorVector2& world) const
    {
        return EditorVector2{(world.x - center_.x) * zoom_ + viewportSize_.x * 0.5f,
                             (world.y - center_.y) * zoom_ + viewportSize_.y * 0.5f};
    }

    EditorVector2 EditorCamera2D::screenToWorld(const EditorVector2& screen) const
    {
        if (zoom_ <= 0.0f) { return center_; }
        return EditorVector2{(screen.x - viewportSize_.x * 0.5f) / zoom_ + center_.x,
                             (screen.y - viewportSize_.y * 0.5f) / zoom_ + center_.y};
    }

    WorldBounds2D EditorCamera2D::getVisibleBounds() const
    {
        WorldBounds2D bounds;
        bounds.min = screenToWorld(EditorVector2{0.0f, 0.0f});
        bounds.max = screenToWorld(viewportSize_);
        return bounds;
    }

    void EditorCamera2D::panByScreenDelta(const EditorVector2& screenDelta)
    {
        if (zoom_ <= 0.0f) { return; }
        // Dragging right moves the *content* right, which means the camera moves left.
        center_ = EditorVector2{center_.x - screenDelta.x / zoom_, center_.y - screenDelta.y / zoom_};
    }

    void EditorCamera2D::zoomAt(const EditorVector2& screenAnchor, float factor)
    {
        if (factor <= 0.0f) { return; }

        const EditorVector2 anchorWorld = screenToWorld(screenAnchor);
        setZoom(zoom_ * factor);

        // Re-derive where the anchor landed after the zoom and shift the centre by the error, so
        // the world point under the cursor is exactly where it started. Computing the new centre
        // algebraically would give the same answer but would not stay correct if setZoom clamped.
        const EditorVector2 anchorAfter = worldToScreen(anchorWorld);
        center_ = EditorVector2{center_.x + (anchorAfter.x - screenAnchor.x) / zoom_,
                                center_.y + (anchorAfter.y - screenAnchor.y) / zoom_};
    }

    void EditorCamera2D::frame(const WorldBounds2D& bounds, float marginFraction)
    {
        if (bounds.isEmpty())
        {
            // Degenerate bounds still carry a position -- a single point entity, say -- so
            // recentring is the useful part and the zoom is left alone.
            center_ = bounds.min;
            return;
        }

        center_ = bounds.getCenter();

        if (viewportSize_.x <= 0.0f || viewportSize_.y <= 0.0f) { return; }

        const float margin = std::clamp(marginFraction, 0.0f, 0.45f);
        const float usableWidth = viewportSize_.x * (1.0f - margin * 2.0f);
        const float usableHeight = viewportSize_.y * (1.0f - margin * 2.0f);

        const float width = bounds.max.x - bounds.min.x;
        const float height = bounds.max.y - bounds.min.y;

        // The smaller of the two fits both axes; using the larger would crop.
        setZoom(std::min(usableWidth / width, usableHeight / height));
    }

    ScenePickResult pickEntityAt(const SceneDocument& scene,
                                 const EditorCamera2D& camera,
                                 const EditorVector2& screenPoint,
                                 const SpriteSizeProvider& sizeProvider)
    {
        ScenePickResult result;
        result.worldPoint = camera.screenToWorld(screenPoint);

        float bestDepth = std::numeric_limits<float>::max();

        for (const EditorEntity& entity : scene.getEntities())
        {
            // A disabled entity is still visible in the hierarchy but is not part of the scene the
            // user is looking at, so clicking where it would be must not select it.
            if (!entity.isEnabled()) { continue; }

            const std::optional<WorldBounds2D> bounds =
                computeEntityBounds2D(scene, entity.getId(), sizeProvider);
            if (!bounds || !bounds->contains(result.worldPoint)) { continue; }

            float depth = 0.0f;
            if (const EditorComponent* sprite = entity.findComponent(BuiltinComponentIds::kSpriteRenderer))
            {
                depth = sprite->getProperty("layerDepth").get<float>(0.0f);
            }

            // XNA's convention: 0 is front, 1 is back. `<=` rather than `<` breaks ties towards
            // the entity later in the document -- the one drawn last, and so the one on top.
            if (depth <= bestDepth)
            {
                bestDepth = depth;
                result.entityId = entity.getId();
            }
        }

        // Icons are tested last and override whatever the sprite pass found, because they are drawn
        // last: they are editor artefacts on top of the scene. Losing to a sprite would make a
        // camera parked over the level art unselectable exactly where the user can see it.
        //
        // Within the icons themselves the last match wins, which is document order -- the same
        // order the renderer draws them in, so the picker and the viewport agree about overlap
        // without either needing to know how the other sorts.
        for (const EditorIconPlacement& icon : collectEditorIcons(scene, camera))
        {
            if (hitTestEditorIcon(icon.center, screenPoint)) { result.entityId = icon.entityId; }
        }

        return result;
    }

    float chooseGridSpacing(float zoom, float targetPixels)
    {
        if (zoom <= 0.0f) { return 0.0f; }

        const float targetWorld = targetPixels / zoom;
        const float decade = std::pow(10.0f, std::floor(std::log10(std::max(targetWorld, 1e-6f))));
        const float normalised = targetWorld / decade;

        if (normalised < 2.0f) { return decade; }
        if (normalised < 5.0f) { return decade * 2.0f; }
        return decade * 5.0f;
    }
}
