// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/EditorCamera3D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief The world-space half-extent given to an entity that draws nothing. */
        constexpr float kIconWorldExtent = 8.0f;

        /** @brief Returns @p value clamped to [@p low, @p high]. */
        float clampTo(float value, float low, float high) { return std::max(low, std::min(high, value)); }
    }

    const char* toString(CameraProjection projection)
    {
        switch (projection)
        {
            case CameraProjection::Perspective: return "Perspective";
            case CameraProjection::Orthographic: return "Orthographic";
        }
        return "Perspective";
    }

    void WorldBounds3D::encapsulate(const EditorVector3& point)
    {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    WorldBounds3D WorldBounds3D::combine(const WorldBounds3D& a, const WorldBounds3D& b)
    {
        if (a.isEmpty()) { return b; }
        if (b.isEmpty()) { return a; }

        WorldBounds3D result = a;
        result.encapsulate(b.min);
        result.encapsulate(b.max);
        return result;
    }

    WorldBounds3D WorldBounds3D::makeEmpty()
    {
        // Inverted rather than zero-sized: a box at the origin would drag every union towards the
        // origin, so a scene of entities standing well away from it would frame empty space.
        constexpr float infinity = std::numeric_limits<float>::max();
        return WorldBounds3D{EditorVector3{infinity, infinity, infinity},
                             EditorVector3{-infinity, -infinity, -infinity}};
    }

    void EditorCamera3D::setDistance(float distance)
    {
        distance_ = clampTo(distance, kMinDistance, kMaxDistance);
    }

    void EditorCamera3D::setYaw(float radians)
    {
        // Wrapped rather than clamped: yaw has no ends, and letting it grow without bound would
        // eventually cost precision in a camera the user merely spun for a while.
        constexpr float twoPi = 6.2831853f;
        yaw_ = std::fmod(radians, twoPi);
        if (yaw_ <= -3.14159265f) { yaw_ += twoPi; }
        else if (yaw_ > 3.14159265f) { yaw_ -= twoPi; }
    }

    void EditorCamera3D::setPitch(float radians)
    {
        pitch_ = clampTo(radians, -kMaxPitchRadians, kMaxPitchRadians);
    }

    void EditorCamera3D::setFieldOfView(float radians)
    {
        // Between about 6 and 150 degrees. The ends are where a projection stops being useful
        // rather than where it stops being computable.
        fieldOfView_ = clampTo(radians, 0.1f, 2.6f);
    }

    void EditorCamera3D::setClipPlanes(float nearPlane, float farPlane)
    {
        if (nearPlane <= 0.0f || farPlane <= nearPlane) { return; }
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
    }

    EditorVector3 EditorCamera3D::getForward() const
    {
        // Yaw about world Y, then pitch. Positive pitch looks *downward on screen*, which is what a
        // user dragging downwards on an orbit expects to happen to the horizon -- and in this
        // camera's Y-down world, downward on screen is towards +Y.
        const float cosPitch = std::cos(pitch_);
        return EditorVector3{-std::sin(yaw_) * cosPitch, std::sin(pitch_), -std::cos(yaw_) * cosPitch};
    }

    EditorVector3 EditorCamera3D::getRight() const
    {
        return normalize(cross(getForward(), EditorVector3{0.0f, 1.0f, 0.0f}));
    }

    EditorVector3 EditorCamera3D::getUp() const
    {
        // Up *on screen*, which in this camera's Y-down world points towards -Y. The order of the
        // cross product is what carries that: the other order gives the Y-up basis vector, and a
        // fly control built on it would move the camera down when the user asked for up.
        return normalize(cross(getForward(), getRight()));
    }

    EditorVector3 EditorCamera3D::getEye() const
    {
        return subtract(pivot_, scale(getForward(), distance_));
    }

    float EditorCamera3D::getOrthographicHeight() const
    {
        return 2.0f * distance_ * std::tan(fieldOfView_ * 0.5f);
    }

    EditorMatrix EditorCamera3D::getViewMatrix() const
    {
        return createLookAt(getEye(), pivot_, EditorVector3{0.0f, 1.0f, 0.0f});
    }

    float EditorCamera3D::getNearClipDistance() const
    {
        return projection_ == CameraProjection::Orthographic ? -farPlane_ : nearPlane_;
    }

    EditorMatrix EditorCamera3D::getProjectionMatrix() const
    {
        const float aspect =
            viewportSize_.y > 0.0f ? viewportSize_.x / viewportSize_.y : 1.0f;

        if (projection_ == CameraProjection::Orthographic)
        {
            const float height = getOrthographicHeight();

            // The near plane is pushed *behind* the eye for the orthographic view. A perspective
            // camera cannot see what is beside it, so a near plane in front of the eye costs it
            // nothing; an orthographic one can, and clipping away everything between the eye and
            // the pivot would hide exactly the half of the scene the user dollied towards.
            return createOrthographic(height * aspect, height, getNearClipDistance(), farPlane_);
        }

        return createPerspectiveFieldOfView(fieldOfView_, aspect, nearPlane_, farPlane_);
    }

    EditorMatrix EditorCamera3D::getViewProjectionMatrix() const
    {
        // The Y mirror that makes this a Y-down camera. It belongs in the projection rather than
        // in the view: mirroring the view's up vector is a 180-degree roll, which fixes the
        // vertical and puts world +X on the left, while a projection mirror is the actual
        // conversion between a Y-down and a Y-up frame and leaves X alone.
        //
        // Everything downstream inherits it for free -- worldToScreen and screenToRay both go
        // through this one matrix, so picking, the gizmo and the wireframe cannot disagree with
        // what is drawn.
        static const EditorMatrix kFlipY = createScale(EditorVector3{1.0f, -1.0f, 1.0f});

        return multiply(getViewMatrix(), multiply(getProjectionMatrix(), kFlipY));
    }

    std::optional<EditorVector2> EditorCamera3D::worldToScreen(const EditorVector3& world) const
    {
        float w = 0.0f;
        const EditorVector3 clip = transformWithPerspective(getViewProjectionMatrix(), world, w);
        if (w <= 0.0f) { return std::nullopt; }

        // Clip space is [-1, 1] across and up; the viewport's origin is its top-left, so Y flips.
        return EditorVector2{(clip.x * 0.5f + 0.5f) * viewportSize_.x,
                             (0.5f - clip.y * 0.5f) * viewportSize_.y};
    }

    WorldRay EditorCamera3D::screenToRay(const EditorVector2& screen) const
    {
        WorldRay ray;
        ray.origin = getEye();
        ray.direction = getForward();

        const std::optional<EditorMatrix> inverse = invert(getViewProjectionMatrix());
        if (!inverse || viewportSize_.x <= 0.0f || viewportSize_.y <= 0.0f) { return ray; }

        const float clipX = (screen.x / viewportSize_.x) * 2.0f - 1.0f;
        const float clipY = 1.0f - (screen.y / viewportSize_.y) * 2.0f;

        // Two points, one on each clip-space depth limit, unprojected and subtracted. Unprojecting
        // a single point and using the eye as the origin works for a perspective camera and fails
        // for an orthographic one, where every ray starts somewhere different.
        float nearW = 0.0f;
        float farW = 0.0f;
        const EditorVector3 nearPoint =
            transformWithPerspective(*inverse, EditorVector3{clipX, clipY, 0.0f}, nearW);
        const EditorVector3 farPoint =
            transformWithPerspective(*inverse, EditorVector3{clipX, clipY, 1.0f}, farW);

        const EditorVector3 direction = normalize(subtract(farPoint, nearPoint));
        if (length(direction) <= 0.0f) { return ray; }

        ray.origin = nearPoint;
        ray.direction = direction;
        return ray;
    }

    void EditorCamera3D::orbit(float yawRadians, float pitchRadians)
    {
        setYaw(yaw_ + yawRadians);
        setPitch(pitch_ + pitchRadians);
    }

    void EditorCamera3D::look(float yawRadians, float pitchRadians)
    {
        // Turning in place: the eye is what stays put, so it is recovered before the angles change
        // and the pivot is put back in front of the new direction afterwards.
        const EditorVector3 eye = getEye();
        orbit(yawRadians, pitchRadians);
        pivot_ = add(eye, scale(getForward(), distance_));
    }

    void EditorCamera3D::moveLocal(const EditorVector3& delta)
    {
        const EditorVector3 offset = add(add(scale(getRight(), delta.x), scale(getUp(), delta.y)),
                                         scale(getForward(), delta.z));
        pivot_ = add(pivot_, offset);
    }

    void EditorCamera3D::panByScreenDelta(const EditorVector2& screenDelta)
    {
        if (viewportSize_.y <= 0.0f) { return; }

        // World units per pixel at the pivot's depth. Using the pivot's depth is what makes the
        // drag track the cursor for whatever the user is looking at, which is the only depth at
        // which a perspective pan *can* track it.
        const float worldPerPixel = getOrthographicHeight() / viewportSize_.y;

        // Dragging right moves the world right, i.e. the camera left; screen Y points down while
        // the camera's up vector points up, so that one does not need negating twice.
        moveLocal(EditorVector3{-screenDelta.x * worldPerPixel, screenDelta.y * worldPerPixel, 0.0f});
    }

    void EditorCamera3D::dolly(float factor)
    {
        if (factor <= 0.0f) { return; }
        setDistance(distance_ * factor);
    }

    void EditorCamera3D::frame(const WorldBounds3D& bounds, float marginFraction)
    {
        if (bounds.isEmpty()) { return; }

        pivot_ = bounds.getCenter();

        const float radius = bounds.getRadius();
        if (radius <= 0.0f)
        {
            // A single point has no extent to fit, so only the centre moves. Choosing a distance
            // from nothing would zoom to an arbitrary place and call it framing.
            return;
        }

        // The distance at which a sphere of this radius fills the *vertical* field of view, since
        // that is the smaller of the two on any viewport wider than it is tall. The horizontal
        // field is checked as well, because a tall narrow panel inverts that.
        const float margin = 1.0f + 2.0f * std::max(0.0f, marginFraction);
        const float halfFov = fieldOfView_ * 0.5f;
        float required = radius * margin / std::sin(halfFov);

        const float aspect = viewportSize_.y > 0.0f ? viewportSize_.x / viewportSize_.y : 1.0f;
        if (aspect < 1.0f && aspect > 0.0f)
        {
            const float halfHorizontal = std::atan(std::tan(halfFov) * aspect);
            required = std::max(required, radius * margin / std::sin(halfHorizontal));
        }

        setDistance(required);
    }

    std::optional<WorldBounds3D> computeEntityBounds3D(const SceneDocument& scene, const Uuid& entityId,
                                                       const SpriteSizeProvider& sizeProvider)
    {
        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        // A sprite is a flat box in the XY plane, so its 2D bounds are already the answer for two
        // of the three axes -- and are computed by the one function that knows about source
        // rectangles, animation frames and origins. Duplicating that here to add a Z of zero would
        // be a second answer to a question with one.
        const std::optional<WorldBounds2D> flat = computeEntityBounds2D(scene, entityId, sizeProvider);
        if (flat)
        {
            return WorldBounds3D{EditorVector3{flat->min.x, flat->min.y, world->position.z},
                                 EditorVector3{flat->max.x, flat->max.y, world->position.z}};
        }

        // Everything else -- a camera, a light, a bare Transform -- is a box around its position.
        // The 2D viewport leaves these out of framing because it draws them as fixed-size icons;
        // in a 3D view they are the only thing many scenes contain until ED-402 lands, and framing
        // a scene of them must not return "nothing to look at".
        const EditorVector3 extent{kIconWorldExtent, kIconWorldExtent, kIconWorldExtent};
        return WorldBounds3D{subtract(world->position, extent), add(world->position, extent)};
    }

    std::optional<WorldBounds3D> computeHierarchyBounds3D(const SceneDocument& scene, const Uuid& entityId,
                                                          const SpriteSizeProvider& sizeProvider)
    {
        if (scene.findEntity(entityId) == nullptr) { return std::nullopt; }

        std::optional<WorldBounds3D> result = computeEntityBounds3D(scene, entityId, sizeProvider);

        for (const Uuid& childId : scene.getChildren(entityId))
        {
            const std::optional<WorldBounds3D> childBounds =
                computeHierarchyBounds3D(scene, childId, sizeProvider);
            if (!childBounds) { continue; }
            result = result ? WorldBounds3D::combine(*result, *childBounds) : childBounds;
        }

        return result;
    }

    std::optional<WorldBounds3D> computeSceneBounds3D(const SceneDocument& scene,
                                                      const SpriteSizeProvider& sizeProvider)
    {
        std::optional<WorldBounds3D> result;

        for (const EditorEntity& entity : scene.getEntities())
        {
            const std::optional<WorldBounds3D> bounds =
                computeEntityBounds3D(scene, entity.getId(), sizeProvider);
            if (!bounds) { continue; }
            result = result ? WorldBounds3D::combine(*result, *bounds) : bounds;
        }

        return result;
    }
}
