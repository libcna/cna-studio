// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Scene/StudioCamera2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/StudioIcons.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"

namespace CNA::Studio
{
    void StudioCamera2D::setZoom(float zoom)
    {
        zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
    }

    StudioVector2 StudioCamera2D::worldToScreen(const StudioVector2& world) const
    {
        return StudioVector2{(world.x - center_.x) * zoom_ + viewportSize_.x * 0.5f,
                             (world.y - center_.y) * zoom_ + viewportSize_.y * 0.5f};
    }

    StudioVector2 StudioCamera2D::screenToWorld(const StudioVector2& screen) const
    {
        if (zoom_ <= 0.0f) { return center_; }
        return StudioVector2{(screen.x - viewportSize_.x * 0.5f) / zoom_ + center_.x,
                             (screen.y - viewportSize_.y * 0.5f) / zoom_ + center_.y};
    }

    WorldBounds2D StudioCamera2D::getVisibleBounds() const
    {
        WorldBounds2D bounds;
        bounds.min = screenToWorld(StudioVector2{0.0f, 0.0f});
        bounds.max = screenToWorld(viewportSize_);
        return bounds;
    }

    void StudioCamera2D::panByScreenDelta(const StudioVector2& screenDelta)
    {
        if (zoom_ <= 0.0f) { return; }
        // Dragging right moves the *content* right, which means the camera moves left.
        center_ = StudioVector2{center_.x - screenDelta.x / zoom_, center_.y - screenDelta.y / zoom_};
    }

    void StudioCamera2D::zoomAt(const StudioVector2& screenAnchor, float factor)
    {
        if (factor <= 0.0f) { return; }

        const StudioVector2 anchorWorld = screenToWorld(screenAnchor);
        setZoom(zoom_ * factor);

        // Re-derive where the anchor landed after the zoom and shift the centre by the error, so
        // the world point under the cursor is exactly where it started. Computing the new centre
        // algebraically would give the same answer but would not stay correct if setZoom clamped.
        const StudioVector2 anchorAfter = worldToScreen(anchorWorld);
        center_ = StudioVector2{center_.x + (anchorAfter.x - screenAnchor.x) / zoom_,
                                center_.y + (anchorAfter.y - screenAnchor.y) / zoom_};
    }

    void StudioCamera2D::frame(const WorldBounds2D& bounds, float marginFraction)
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
                                 const StudioCamera2D& camera,
                                 const StudioVector2& screenPoint,
                                 const SpriteSizeProvider& sizeProvider)
    {
        ScenePickResult result;
        result.worldPoint = camera.screenToWorld(screenPoint);

        float bestDepth = std::numeric_limits<float>::max();

        for (const StudioEntity& entity : scene.getEntities())
        {
            // A disabled entity is still visible in the hierarchy but is not part of the scene the
            // user is looking at, so clicking where it would be must not select it.
            if (!entity.isEnabled()) { continue; }

            const std::optional<WorldBounds2D> bounds =
                computeEntityBounds2D(scene, entity.getId(), sizeProvider);
            if (!bounds || !bounds->contains(result.worldPoint)) { continue; }

            float depth = 0.0f;
            if (const StudioComponent* sprite = entity.findComponent(BuiltinComponentIds::kSpriteRenderer))
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
        for (const StudioIconPlacement& icon : collectStudioIcons(scene, camera))
        {
            if (hitTestStudioIcon(icon.center, screenPoint)) { result.entityId = icon.entityId; }
        }

        return result;
    }

    std::vector<Uuid> pickEntitiesIn(const SceneDocument& scene, const StudioCamera2D& camera,
                                     const StudioVector2& from, const StudioVector2& to,
                                     const SpriteSizeProvider& sizeProvider)
    {
        std::vector<Uuid> picked;

        // Normalised here rather than at every call site: a band is dragged in whichever direction
        // the user started in, and up-and-left is as ordinary as down-and-right.
        const float left = std::min(from.x, to.x);
        const float right = std::max(from.x, to.x);
        const float top = std::min(from.y, to.y);
        const float bottom = std::max(from.y, to.y);

        for (const StudioEntity& entity : scene.getEntities())
        {
            // The same rule the click picker has: a disabled entity is not part of the scene the
            // user is looking at, so a band drawn over where it would be must not take it.
            if (!entity.isEnabled()) { continue; }

            const std::optional<WorldBounds2D> bounds =
                computeEntityBounds2D(scene, entity.getId(), sizeProvider);

            // An entity with no bounds is one the click picker finds by its icon, and an icon is a
            // fixed size on screen: boxed at the point it is drawn at, so a band over a camera
            // takes the camera.
            StudioVector2 low;
            StudioVector2 high;
            if (bounds)
            {
                low = camera.worldToScreen(bounds->min);
                high = camera.worldToScreen(bounds->max);
            }
            else
            {
                const std::optional<WorldTransform> world =
                    computeWorldTransform(scene, entity.getId());
                if (!world) { continue; }
                if (getStudioIconKind(entity) == StudioIconKind::None) { continue; }

                const StudioVector2 center =
                    camera.worldToScreen(StudioVector2{world->position.x, world->position.y});
                low = StudioVector2{center.x - kStudioIconExtent, center.y - kStudioIconExtent};
                high = StudioVector2{center.x + kStudioIconExtent, center.y + kStudioIconExtent};
            }

            // The camera can mirror either axis, so the projected corners are not sorted.
            const float entityLeft = std::min(low.x, high.x);
            const float entityRight = std::max(low.x, high.x);
            const float entityTop = std::min(low.y, high.y);
            const float entityBottom = std::max(low.y, high.y);

            // Overlap, not enclosure, with touching edges counting: a band dragged exactly along an
            // entity's edge is one the user meant to include it.
            if (entityRight < left || entityLeft > right) { continue; }
            if (entityBottom < top || entityTop > bottom) { continue; }

            picked.push_back(entity.getId());
        }

        return picked;
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
