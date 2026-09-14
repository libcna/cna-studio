// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/TransformGizmos3D.hpp"

#include <algorithm>
#include <cmath>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief The arms' colours: X red, Y green, Z blue, as every 3D tool since the first. */
        constexpr std::array<EditorColor, 3> kAxisColors{
            EditorColor{214, 92, 92, 255}, EditorColor{110, 200, 110, 255}, EditorColor{96, 132, 220, 255}};

        /** @brief The colour of the arm being dragged or hovered. */
        constexpr EditorColor kActiveColor{255, 210, 70, 255};

        /** @brief The scale gizmo's centre handle: no axis, so no axis colour. */
        constexpr EditorColor kUniformColor{210, 210, 216, 255};

        /** @brief Returns the distance from @p point to the segment @p from -> @p to, in pixels. */
        float distanceToSegment(const EditorVector2& point, const EditorVector2& from,
                                const EditorVector2& to)
        {
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float lengthSquared = dx * dx + dy * dy;
            if (lengthSquared <= 0.0f) { return std::hypot(point.x - from.x, point.y - from.y); }

            float t = ((point.x - from.x) * dx + (point.y - from.y) * dy) / lengthSquared;
            t = std::max(0.0f, std::min(1.0f, t));

            return std::hypot(point.x - (from.x + dx * t), point.y - (from.y + dy * t));
        }

        /** @brief Returns how many world units one viewport pixel spans at @p point's depth. */
        float worldUnitsPerPixelAt(const EditorCamera3D& camera, const EditorVector3& point)
        {
            const EditorVector2 viewport = camera.getViewportSize();
            if (viewport.y <= 0.0f) { return 1.0f; }

            if (camera.getProjection() == CameraProjection::Orthographic)
            {
                return camera.getOrthographicHeight() / viewport.y;
            }

            // A perspective gizmo has to be sized at the *entity's* depth, not the pivot's: an
            // entity twice as far away is drawn half the size, and an arm sized for the pivot
            // would be a manipulator that grows and shrinks as the user selects things.
            const float depth = dot(subtract(point, camera.getEye()), camera.getForward());
            const float usable = std::max(depth, camera.getNearPlane());
            return 2.0f * usable * std::tan(camera.getFieldOfView() * 0.5f) / viewport.y;
        }

        /** @brief Returns two unit vectors spanning the plane whose normal is @p normal. */
        std::pair<EditorVector3, EditorVector3> makePlaneBasis(const EditorVector3& normal)
        {
            // Any perpendicular will do -- the ring is a circle, so where it starts is arbitrary --
            // but it must not be parallel to the normal, which is what the choice below guarantees.
            const EditorVector3 seed =
                std::abs(normal.x) < 0.9f ? EditorVector3{1.0f, 0.0f, 0.0f} : EditorVector3{0.0f, 1.0f, 0.0f};

            const EditorVector3 planeX = normalize(cross(seed, normal));
            return {planeX, normalize(cross(normal, planeX))};
        }

        /**
         * @brief Returns where @p ray meets the plane through @p origin, as an angle in its basis.
         *
         * Nothing when the ray runs along the plane -- a ring seen exactly edge-on, where the
         * intersection is a line rather than a point and any angle would be a guess.
         */
        std::optional<float> angleOnPlane(const WorldRay& ray, const EditorVector3& origin,
                                          const EditorVector3& normal, const EditorVector3& planeX,
                                          const EditorVector3& planeY)
        {
            const float denominator = dot(normal, ray.direction);
            if (std::abs(denominator) < 1e-4f) { return std::nullopt; }

            const float distance = dot(subtract(origin, ray.origin), normal) / denominator;
            const EditorVector3 offset = subtract(ray.at(distance), origin);
            return std::atan2(dot(offset, planeY), dot(offset, planeX));
        }

        /** @brief Returns @p index's axis enumerator. */
        GizmoAxis3D axisAt(std::size_t index)
        {
            switch (index)
            {
                case 0: return GizmoAxis3D::X;
                case 1: return GizmoAxis3D::Y;
                default: return GizmoAxis3D::Z;
            }
        }

        /** @brief Returns @p axis's index, or 3 for None and All. */
        std::size_t indexOf(GizmoAxis3D axis)
        {
            switch (axis)
            {
                case GizmoAxis3D::X: return 0;
                case GizmoAxis3D::Y: return 1;
                case GizmoAxis3D::Z: return 2;
                default: return 3;
            }
        }

        /** @brief Returns true when @p point is inside the square of half-extent @p extent at @p center. */
        bool insideSquare(const EditorVector2& point, const EditorVector2& center, float extent)
        {
            return std::abs(point.x - center.x) <= extent && std::abs(point.y - center.y) <= extent;
        }

        /**
         * @brief Keeps a scale factor away from exactly zero, sign intact.
         *
         * The same rule the 2D gizmo follows: dragging a handle through the origin flips the
         * entity, which XNA's own negative scale supports and a user may well mean. Landing *on*
         * zero is not the same thing -- the entity vanishes, its bounds collapse, and it can no
         * longer be clicked to get it back.
         */
        float keepScalable(float factor)
        {
            constexpr float kSmallest = 0.001f;
            if (std::abs(factor) >= kSmallest) { return factor; }
            return factor < 0.0f ? -kSmallest : kSmallest;
        }

        /** @brief Returns @p color at the alpha a fade of @p fade implies. */
        EditorColor faded(const EditorColor& color, float fade)
        {
            // Down to a floor rather than to nothing. An arm that faded out completely would be an
            // axis the user has stopped being told about while it is still there to be dragged.
            constexpr float kFloor = 90.0f;
            const float alpha = kFloor + (255.0f - kFloor) * std::max(0.0f, std::min(1.0f, fade));
            return EditorColor{color.r, color.g, color.b, static_cast<std::uint8_t>(alpha)};
        }

        /** @brief Appends the four sides of the square of half-extent @p extent at @p center. */
        void appendSquare(std::vector<WireSegment>& out, const EditorVector2& center, float extent,
                          const EditorColor& color, float thickness)
        {
            const EditorVector2 topLeft{center.x - extent, center.y - extent};
            const EditorVector2 topRight{center.x + extent, center.y - extent};
            const EditorVector2 bottomRight{center.x + extent, center.y + extent};
            const EditorVector2 bottomLeft{center.x - extent, center.y + extent};

            out.push_back(WireSegment{topLeft, topRight, color, thickness});
            out.push_back(WireSegment{topRight, bottomRight, color, thickness});
            out.push_back(WireSegment{bottomRight, bottomLeft, color, thickness});
            out.push_back(WireSegment{bottomLeft, topLeft, color, thickness});
        }
    }

    const char* toString(GizmoAxis3D axis)
    {
        switch (axis)
        {
            case GizmoAxis3D::None: return "None";
            case GizmoAxis3D::X: return "X";
            case GizmoAxis3D::Y: return "Y";
            case GizmoAxis3D::Z: return "Z";
            case GizmoAxis3D::All: return "All";
        }
        return "None";
    }

    std::optional<TranslateGizmo3DLayout> computeTranslateGizmo3DLayout(
        const SceneDocument& scene, const EditorCamera3D& camera, const Uuid& entityId,
        GizmoSpace space, const std::optional<EditorVector3>& pivotWorld)
    {
        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        TranslateGizmo3DLayout layout;

        // The shared pivot when there is one, and the entity's own position otherwise. The axes
        // stay the primary selection's either way: a selection has no rotation of its own.
        layout.origin = pivotWorld.value_or(world->position);

        if (space == GizmoSpace::Local)
        {
            layout.axes = {rotate(world->rotation, EditorVector3{1.0f, 0.0f, 0.0f}),
                           rotate(world->rotation, EditorVector3{0.0f, 1.0f, 0.0f}),
                           rotate(world->rotation, EditorVector3{0.0f, 0.0f, 1.0f})};
        }

        // Sized in pixels and converted to world units, so the manipulator is the same size on
        // screen wherever the entity is -- the property that makes it grabbable at any zoom.
        layout.armLength = kGizmo3DScreenLength * worldUnitsPerPixelAt(camera, layout.origin);

        const std::optional<EditorVector2> screenOrigin = camera.worldToScreen(layout.origin);
        if (!screenOrigin)
        {
            // Behind the eye: there is nothing to draw and nothing to grab. Returning a layout
            // with a plausible-looking origin would put a manipulator on screen for an entity the
            // user cannot see.
            return std::nullopt;
        }
        layout.screenOrigin = *screenOrigin;

        for (std::size_t index = 0; index < 3; ++index)
        {
            const EditorVector3 tip = add(layout.origin, scale(layout.axes[index], layout.armLength));
            const std::optional<EditorVector2> screenTip = camera.worldToScreen(tip);

            layout.armVisible[index] = screenTip.has_value();
            layout.screenTips[index] = screenTip.value_or(layout.screenOrigin);
        }

        return layout;
    }

    GizmoAxis3D hitTestTranslateGizmo3D(const TranslateGizmo3DLayout& layout,
                                        const EditorVector2& screenPoint)
    {
        GizmoAxis3D best = GizmoAxis3D::None;
        float bestDistance = layout.grabTolerance;

        for (std::size_t index = 0; index < 3; ++index)
        {
            if (!layout.armVisible[index]) { continue; }

            const float distance =
                distanceToSegment(screenPoint, layout.screenOrigin, layout.screenTips[index]);

            // Strictly nearer, so that where two arms overlap -- which happens exactly when one
            // points almost at the camera -- the one the cursor is actually closest to wins,
            // rather than whichever comes first in a fixed order.
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = axisAt(index);
            }
        }

        return best;
    }

    std::vector<WireSegment> buildTranslateGizmo3DSegments(const TranslateGizmo3DLayout& layout,
                                                           GizmoAxis3D active)
    {
        std::vector<WireSegment> segments;
        segments.reserve(3);

        for (std::size_t index = 0; index < 3; ++index)
        {
            if (!layout.armVisible[index]) { continue; }

            const bool highlighted = active == axisAt(index);
            segments.push_back(WireSegment{layout.screenOrigin, layout.screenTips[index],
                                           highlighted ? kActiveColor : kAxisColors[index],
                                           highlighted ? 3.0f : 2.0f});
        }

        return segments;
    }

    EditorQuaternion quaternionFromAxisAngle(const EditorVector3& axis, float radians)
    {
        const EditorVector3 unit = normalize(axis);
        const float half = radians * 0.5f;
        const float sine = std::sin(half);
        return EditorQuaternion{unit.x * sine, unit.y * sine, unit.z * sine, std::cos(half)};
    }

    std::optional<RotateGizmo3DLayout> computeRotateGizmo3DLayout(
        const SceneDocument& scene, const EditorCamera3D& camera, const Uuid& entityId,
        GizmoSpace space, const std::optional<EditorVector3>& pivotWorld)
    {
        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        RotateGizmo3DLayout layout;
        layout.origin = pivotWorld.value_or(world->position);

        if (space == GizmoSpace::Local)
        {
            layout.axes = {rotate(world->rotation, EditorVector3{1.0f, 0.0f, 0.0f}),
                           rotate(world->rotation, EditorVector3{0.0f, 1.0f, 0.0f}),
                           rotate(world->rotation, EditorVector3{0.0f, 0.0f, 1.0f})};
        }

        layout.radius = kGizmo3DScreenLength * worldUnitsPerPixelAt(camera, layout.origin);

        if (!camera.worldToScreen(layout.origin)) { return std::nullopt; }

        for (std::size_t index = 0; index < 3; ++index)
        {
            // A ring seen edge-on projects to a straight line through the centre, where it overlaps
            // the other two and cannot be told apart from them -- and its plane is then nearly
            // parallel to the cursor ray, so a drag on it has no angle to measure. Dropped rather
            // than drawn: a handle that is on screen and cannot be used is worse than one that is
            // not there, and this is the same case the translate arm refuses.
            const float facing =
                std::abs(dot(layout.axes[index], normalize(subtract(layout.origin, camera.getEye()))));
            if (facing < 0.2f) { continue; }

            const auto [planeX, planeY] = makePlaneBasis(layout.axes[index]);

            std::vector<EditorVector2> ring;
            ring.reserve(static_cast<std::size_t>(kRotateGizmo3DSamples) + 1);

            for (int sample = 0; sample <= kRotateGizmo3DSamples; ++sample)
            {
                const float angle = 6.2831853f * static_cast<float>(sample)
                                    / static_cast<float>(kRotateGizmo3DSamples);
                const EditorVector3 point =
                    add(layout.origin, add(scale(planeX, std::cos(angle) * layout.radius),
                                           scale(planeY, std::sin(angle) * layout.radius)));

                const std::optional<EditorVector2> screen = camera.worldToScreen(point);

                // A sample behind the eye ends the ring rather than wrapping to a wrong pixel: the
                // polyline is what the hit-test measures against, so a fabricated point would be a
                // place the user could grab and nothing would happen.
                if (!screen) { break; }
                ring.push_back(*screen);
            }

            if (ring.size() >= 2) { layout.rings[index] = std::move(ring); }
        }

        return layout;
    }

    GizmoAxis3D hitTestRotateGizmo3D(const RotateGizmo3DLayout& layout, const EditorVector2& screenPoint)
    {
        GizmoAxis3D best = GizmoAxis3D::None;
        float bestDistance = layout.grabTolerance;

        for (std::size_t index = 0; index < 3; ++index)
        {
            const std::vector<EditorVector2>& ring = layout.rings[index];

            for (std::size_t sample = 0; sample + 1 < ring.size(); ++sample)
            {
                const float distance = distanceToSegment(screenPoint, ring[sample], ring[sample + 1]);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = axisAt(index);
                }
            }
        }

        return best;
    }

    std::vector<WireSegment> buildRotateGizmo3DSegments(const RotateGizmo3DLayout& layout,
                                                        GizmoAxis3D active)
    {
        std::vector<WireSegment> segments;

        for (std::size_t index = 0; index < 3; ++index)
        {
            const std::vector<EditorVector2>& ring = layout.rings[index];
            const bool highlighted = active == axisAt(index);

            for (std::size_t sample = 0; sample + 1 < ring.size(); ++sample)
            {
                segments.push_back(WireSegment{ring[sample], ring[sample + 1],
                                               highlighted ? kActiveColor : kAxisColors[index],
                                               highlighted ? 3.0f : 2.0f});
            }
        }

        return segments;
    }

    bool RotateGizmo3DDrag::begin(const SceneDocument& scene, const EditorCamera3D& camera,
                                  const RotateGizmo3DLayout& layout, const Uuid& entityId,
                                  const EditorVector2& cursor)
    {
        end();

        const GizmoAxis3D grabbed = hitTestRotateGizmo3D(layout, cursor);
        if (grabbed == GizmoAxis3D::None) { return false; }

        const EditorEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr) { return false; }

        const EditorComponent* transform = entity->findComponent(BuiltinComponentIds::kTransform);
        if (transform == nullptr) { return false; }

        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return false; }

        const std::size_t index = grabbed == GizmoAxis3D::X ? 0 : (grabbed == GizmoAxis3D::Y ? 1 : 2);
        const EditorVector3 normal = layout.axes[index];
        const auto [planeX, planeY] = makePlaneBasis(normal);

        const std::optional<float> angle =
            angleOnPlane(camera.screenToRay(cursor), layout.origin, normal, planeX, planeY);
        if (!angle) { return false; }

        axis_ = grabbed;
        entityId_ = entityId;
        normal_ = normal;
        origin_ = layout.origin;
        planeX_ = planeX;
        planeY_ = planeY;
        startAngle_ = *angle;
        startWorld_ = world->rotation;
        return true;
    }

    std::optional<float> RotateGizmo3DDrag::getDeltaAngle(const EditorCamera3D& camera,
                                                          const EditorVector2& cursor,
                                                          const GizmoSnap& snap) const
    {
        if (!isActive()) { return std::nullopt; }

        const std::optional<float> angle =
            angleOnPlane(camera.screenToRay(cursor), origin_, normal_, planeX_, planeY_);
        if (!angle) { return std::nullopt; }

        // Wrapped into (-pi, pi]: without it, dragging across the seam reports nearly a full turn
        // and the entity spins.
        float delta = *angle - startAngle_;
        constexpr float twoPi = 6.2831853f;
        while (delta <= -3.14159265f) { delta += twoPi; }
        while (delta > 3.14159265f) { delta -= twoPi; }

        // The *turn* is snapped, not the absolute angle: snapping the angle would straighten
        // whatever the drag touched the moment it was grabbed.
        if (snap.rotate > 0.0f) { delta = std::round(delta / snap.rotate) * snap.rotate; }
        if (delta == 0.0f) { return std::nullopt; }

        return delta;
    }

    std::optional<EditorQuaternion> RotateGizmo3DDrag::update(const SceneDocument& scene,
                                                              const EditorCamera3D& camera,
                                                              const EditorVector2& cursor,
                                                              const GizmoSnap& snap)
    {
        // The single-entity answer, computed from the same gesture a whole selection uses, so one
        // entity and twenty cannot disagree about how far the cursor turned.
        const std::optional<float> turn = getDeltaAngle(camera, cursor, snap);
        if (!turn) { return std::nullopt; }

        const float delta = *turn;

        // Turned in world space, stored in the parent's frame: the cursor described a world angle,
        // and a child of a rotated parent applying it locally would turn by a rotated fraction.
        //
        // The turn goes on the *left*, which is what makes it a world turn at all: multiply(a, b)
        // applies b first and then a, so the entity's existing rotation runs inside the new one.
        // The other order turns it about its own axes -- an entity already lying on its side would
        // then spin about a ring nobody drew, which is the same mistake as skipping the parent
        // frame, one level further in.
        const EditorQuaternion world = multiply(quaternionFromAxisAngle(normal_, delta), startWorld_);

        const EditorEntity* entity = scene.findEntity(entityId_);
        if (entity == nullptr || !entity->getParentId().isValid()) { return world; }

        const std::optional<WorldTransform> parent = computeWorldTransform(scene, entity->getParentId());
        if (!parent) { return world; }

        const EditorQuaternion inverse{-parent->rotation.x, -parent->rotation.y, -parent->rotation.z,
                                       parent->rotation.w};
        return multiply(inverse, world);
    }

    std::optional<ScaleGizmo3DLayout> computeScaleGizmo3DLayout(
        const SceneDocument& scene, const EditorCamera3D& camera, const Uuid& entityId,
        const std::optional<EditorVector3>& pivotWorld)
    {
        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        ScaleGizmo3DLayout layout;
        layout.origin = pivotWorld.value_or(world->position);

        // Always the entity's own axes. There is no space toggle to consult: a non-uniform scale
        // in world space is a shear, which this transform cannot express.
        layout.axes = {rotate(world->rotation, EditorVector3{1.0f, 0.0f, 0.0f}),
                       rotate(world->rotation, EditorVector3{0.0f, 1.0f, 0.0f}),
                       rotate(world->rotation, EditorVector3{0.0f, 0.0f, 1.0f})};

        layout.armLength = kGizmo3DScreenLength * worldUnitsPerPixelAt(camera, layout.origin);

        const std::optional<EditorVector2> screenOrigin = camera.worldToScreen(layout.origin);
        if (!screenOrigin) { return std::nullopt; }
        layout.screenOrigin = *screenOrigin;

        for (std::size_t index = 0; index < 3; ++index)
        {
            const EditorVector3 tip = add(layout.origin, scale(layout.axes[index], layout.armLength));

            // Clipped against the near plane rather than dropped when the far end is behind it,
            // which is the wireframe's own rule and the reason `projectSegment` is shared: every
            // point left after the clip is in front of the eye, so the arm still projects onto one
            // ray from the origin and only its length has changed.
            const std::optional<std::pair<EditorVector2, EditorVector2>> projected =
                projectSegment(camera, layout.origin, tip);

            if (!projected)
            {
                layout.armVisible[index] = false;
                layout.screenHandles[index] = layout.screenOrigin;
                continue;
            }

            const EditorVector2 offset{projected->second.x - layout.screenOrigin.x,
                                       projected->second.y - layout.screenOrigin.y};
            const float pixels = std::hypot(offset.x, offset.y);

            // An axis pointing exactly through the eye projects onto its own origin: no direction,
            // so nothing to draw along and nothing to drag along. The only case an arm is dropped,
            // and it takes an exact alignment to reach.
            if (pixels < 1e-3f)
            {
                layout.armVisible[index] = false;
                layout.screenHandles[index] = layout.screenOrigin;
                continue;
            }

            layout.armFade[index] = std::min(1.0f, pixels / kGizmo3DScreenLength);

            // The *direction* is the projection's, exactly; the *length* was a chosen constant to
            // begin with, so bounding it costs no truth and buys a handle that is neither hidden
            // under the centre one nor thrown off the panel by an arm pointing at the camera.
            constexpr float kMaximumArmPixels = kGizmo3DScreenLength * 1.5f;
            const float drawn =
                std::max(kScaleGizmo3DMinimumArmPixels, std::min(pixels, kMaximumArmPixels));

            layout.screenHandles[index] = EditorVector2{layout.screenOrigin.x + offset.x / pixels * drawn,
                                                        layout.screenOrigin.y + offset.y / pixels * drawn};
        }

        return layout;
    }

    GizmoAxis3D hitTestScaleGizmo3D(const ScaleGizmo3DLayout& layout, const EditorVector2& screenPoint)
    {
        // The centre first, as in 2D: it is the smaller target, it is what a press in the middle of
        // the gizmo means, and every arm is still reachable along the rest of its length. It is
        // also what covers the pixels an axis pointing through the eye has vacated.
        if (insideSquare(screenPoint, layout.screenOrigin, layout.centerExtent)) { return GizmoAxis3D::All; }

        // The end squares before the arms, so a press a hair off an arm's line but plainly on its
        // handle still counts -- the square is the part the eye aims at.
        for (std::size_t index = 0; index < 3; ++index)
        {
            if (!layout.armVisible[index]) { continue; }
            if (insideSquare(screenPoint, layout.screenHandles[index], layout.handleExtent))
            {
                return axisAt(index);
            }
        }

        GizmoAxis3D best = GizmoAxis3D::None;
        float bestDistance = layout.grabTolerance;

        for (std::size_t index = 0; index < 3; ++index)
        {
            if (!layout.armVisible[index]) { continue; }

            const float distance =
                distanceToSegment(screenPoint, layout.screenOrigin, layout.screenHandles[index]);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = axisAt(index);
            }
        }

        return best;
    }

    std::vector<WireSegment> buildScaleGizmo3DSegments(const ScaleGizmo3DLayout& layout,
                                                       GizmoAxis3D active)
    {
        std::vector<WireSegment> segments;
        segments.reserve(19);

        for (std::size_t index = 0; index < 3; ++index)
        {
            if (!layout.armVisible[index]) { continue; }

            const bool highlighted = active == axisAt(index);
            const float thickness = highlighted ? 3.0f : 2.0f;

            // The fade is not applied to a highlighted arm: the user is holding it, so how much
            // room it has left to be precise in is no longer the thing being reported.
            const EditorColor color = highlighted
                                          ? kActiveColor
                                          : faded(kAxisColors[index], layout.armFade[index]);

            segments.push_back(WireSegment{layout.screenOrigin, layout.screenHandles[index], color,
                                           thickness});
            appendSquare(segments, layout.screenHandles[index], layout.handleExtent, color, thickness);
        }

        // Last, so it draws over the arms that start underneath it -- which is also the order the
        // hit-test resolves them in.
        const bool uniform = active == GizmoAxis3D::All;
        appendSquare(segments, layout.screenOrigin, layout.centerExtent,
                     uniform ? kActiveColor : kUniformColor, uniform ? 3.0f : 2.0f);

        return segments;
    }

    bool ScaleGizmo3DDrag::begin(const SceneDocument& scene, const ScaleGizmo3DLayout& layout,
                                 const Uuid& entityId, const EditorVector2& cursor)
    {
        end();

        const GizmoAxis3D grabbed = hitTestScaleGizmo3D(layout, cursor);
        if (grabbed == GizmoAxis3D::None) { return false; }

        const EditorEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr) { return false; }

        const EditorComponent* transform = entity->findComponent(BuiltinComponentIds::kTransform);
        if (transform == nullptr) { return false; }

        const EditorVector2 offset{cursor.x - layout.screenOrigin.x, cursor.y - layout.screenOrigin.y};

        EditorVector2 direction{1.0f, 0.0f};
        float distance = 0.0f;

        if (grabbed == GizmoAxis3D::All)
        {
            // Radially: the uniform handle has no axis, so how far out the cursor is, in any
            // direction, is the whole of what it can be measuring.
            distance = std::hypot(offset.x, offset.y);
        }
        else
        {
            const std::size_t index = indexOf(grabbed);
            const EditorVector2 arm{layout.screenHandles[index].x - layout.screenOrigin.x,
                                    layout.screenHandles[index].y - layout.screenOrigin.y};
            const float armPixels = std::hypot(arm.x, arm.y);
            if (armPixels <= 0.0f) { return false; }

            direction = EditorVector2{arm.x / armPixels, arm.y / armPixels};
            distance = offset.x * direction.x + offset.y * direction.y;
        }

        // Every factor below is a division by this. A grab at the origin would scale by infinity,
        // so it is not a drag at all and the press falls through to whatever is underneath.
        constexpr float kSmallestGrab = 4.0f;
        if (std::abs(distance) < kSmallestGrab) { return false; }

        axis_ = grabbed;
        entityId_ = entityId;
        direction_ = direction;
        grabDistance_ = distance;
        startLocalScale_ =
            transform->getProperty("scale").get<EditorVector3>(EditorVector3{1.0f, 1.0f, 1.0f});
        return true;
    }

    float ScaleGizmo3DDrag::getFactor(const ScaleGizmo3DLayout& layout, const EditorVector2& cursor,
                                      const GizmoSnap& snap) const
    {
        if (!isActive()) { return 1.0f; }

        const EditorVector2 offset{cursor.x - layout.screenOrigin.x, cursor.y - layout.screenOrigin.y};

        const float distance = axis_ == GizmoAxis3D::All
                                   ? std::hypot(offset.x, offset.y)
                                   : offset.x * direction_.x + offset.y * direction_.y;

        // A ratio, not a difference: screen pixels are not scale units, and only a ratio is
        // independent of the camera the drag happens to be looking through. Snapped here rather
        // than per entity, because for a selection the factor is the quantity they share.
        return keepScalable(snapTo(distance / grabDistance_, snap.scale));
    }

    std::optional<EditorVector3> ScaleGizmo3DDrag::update(const ScaleGizmo3DLayout& layout,
                                                          const EditorVector2& cursor,
                                                          const GizmoSnap& snap) const
    {
        if (!isActive()) { return std::nullopt; }

        const float factor = getFactor(layout, cursor, snap);

        EditorVector3 result = startLocalScale_;
        switch (axis_)
        {
            case GizmoAxis3D::X: result.x = keepScalable(startLocalScale_.x * factor); break;
            case GizmoAxis3D::Y: result.y = keepScalable(startLocalScale_.y * factor); break;
            case GizmoAxis3D::Z: result.z = keepScalable(startLocalScale_.z * factor); break;
            case GizmoAxis3D::All:
                result = EditorVector3{keepScalable(startLocalScale_.x * factor),
                                       keepScalable(startLocalScale_.y * factor),
                                       keepScalable(startLocalScale_.z * factor)};
                break;
            case GizmoAxis3D::None: return std::nullopt;
        }

        // Unchanged is not an edit: an undo entry restoring the size the entity already was costs
        // the user a Ctrl+Z to reach a change they can see.
        if (result == startLocalScale_) { return std::nullopt; }
        return result;
    }

    EditorVector3 worldDeltaToLocal3D(const SceneDocument& scene, const Uuid& entityId,
                                      const EditorVector3& worldDelta)
    {
        const EditorEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr || !entity->getParentId().isValid()) { return worldDelta; }

        const std::optional<WorldTransform> parent = computeWorldTransform(scene, entity->getParentId());
        if (!parent) { return worldDelta; }

        // Undo the parent's rotation, then its scale. A zero scale on an axis is left alone rather
        // than divided by: the entity cannot be moved along an axis its parent has flattened, and
        // an infinity there would put it somewhere no undo could find.
        const EditorQuaternion inverse{-parent->rotation.x, -parent->rotation.y, -parent->rotation.z,
                                       parent->rotation.w};
        EditorVector3 local = rotate(inverse, worldDelta);

        if (parent->scale.x != 0.0f) { local.x /= parent->scale.x; }
        if (parent->scale.y != 0.0f) { local.y /= parent->scale.y; }
        if (parent->scale.z != 0.0f) { local.z /= parent->scale.z; }
        return local;
    }

    std::optional<float> closestPointOnAxis(const WorldRay& ray, const EditorVector3& origin,
                                            const EditorVector3& axis)
    {
        const float axisDotRay = dot(axis, ray.direction);
        const float denominator = 1.0f - axisDotRay * axisDotRay;

        // Parallel, or near enough that the answer is dominated by rounding: an arm pointing
        // straight at the camera, where a pixel of movement would otherwise fling the entity
        // across the level. Refusing is the only honest answer.
        if (denominator < 1e-4f) { return std::nullopt; }

        // The standard two-line closest-approach solution, with both directions unit length:
        // t = (b*e - d) / (1 - b*b), where b is the angle between them, d is the axis component of
        // the offset between their origins and e the ray's.
        const EditorVector3 toOrigin = subtract(origin, ray.origin);
        const float d = dot(axis, toOrigin);
        const float e = dot(ray.direction, toOrigin);
        return (axisDotRay * e - d) / denominator;
    }

    bool TranslateGizmo3DDrag::begin(const SceneDocument& scene, const EditorCamera3D& camera,
                                     const TranslateGizmo3DLayout& layout, const Uuid& entityId,
                                     const EditorVector2& cursor)
    {
        end();

        const GizmoAxis3D grabbed = hitTestTranslateGizmo3D(layout, cursor);
        if (grabbed == GizmoAxis3D::None) { return false; }

        const EditorEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr) { return false; }

        const EditorComponent* transform = entity->findComponent(BuiltinComponentIds::kTransform);
        if (transform == nullptr) { return false; }

        const std::size_t index = grabbed == GizmoAxis3D::X ? 0 : (grabbed == GizmoAxis3D::Y ? 1 : 2);
        const EditorVector3 direction = layout.axes[index];

        const std::optional<float> parameter =
            closestPointOnAxis(camera.screenToRay(cursor), layout.origin, direction);
        if (!parameter) { return false; }

        axis_ = grabbed;
        entityId_ = entityId;
        direction_ = direction;
        startWorld_ = layout.origin;
        startLocal_ = transform->getProperty("position").get<EditorVector3>();
        startParameter_ = *parameter;
        return true;
    }

    std::optional<EditorVector3> computeSelectionPivot3D(const SceneDocument& scene,
                                                          const std::vector<Uuid>& entityIds)
    {
        EditorVector3 total;
        std::size_t counted = 0;

        for (const Uuid& entityId : entityIds)
        {
            const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
            if (!world) { continue; }

            total = add(total, world->position);
            ++counted;
        }

        if (counted == 0) { return std::nullopt; }
        return scale(total, 1.0f / static_cast<float>(counted));
    }

    bool MultiTransform3D::begin(const SceneDocument& scene, const std::vector<Uuid>& entityIds,
                                 const EditorVector3& pivotWorld)
    {
        end();
        pivot_ = pivotWorld;

        for (const Uuid& entityId : findSelectionRoots(scene, entityIds))
        {
            const EditorEntity* entity = scene.findEntity(entityId);
            if (entity == nullptr) { continue; }

            const EditorComponent* transform = entity->findComponent(BuiltinComponentIds::kTransform);
            if (transform == nullptr) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
            if (!world) { continue; }

            Entry entry;
            entry.entityId = entityId;
            entry.startWorldPosition = world->position;
            entry.startLocal = transform->getProperty("position").get<EditorVector3>();
            entry.startWorldRotation = world->rotation;
            entry.startLocalScale =
                transform->getProperty("scale").get<EditorVector3>(EditorVector3{1.0f, 1.0f, 1.0f});

            EditorQuaternion parentRotation;
            if (entity->getParentId().isValid())
            {
                if (const std::optional<WorldTransform> parent =
                        computeWorldTransform(scene, entity->getParentId()))
                {
                    parentRotation = parent->rotation;
                }
            }
            entry.inverseParentRotation = EditorQuaternion{-parentRotation.x, -parentRotation.y,
                                                           -parentRotation.z, parentRotation.w};

            entries_.push_back(std::move(entry));
        }

        return isActive();
    }

    std::vector<EntityTransformEdit> MultiTransform3D::translate(const SceneDocument& scene,
                                                                 const EditorVector3& worldDelta) const
    {
        std::vector<EntityTransformEdit> edits;
        edits.reserve(entries_.size());

        for (const Entry& entry : entries_)
        {
            // Converted per entity, because the same world delta is a different local one under
            // each parent -- two siblings under differently rotated rigs move together in the
            // world and store different numbers.
            EntityTransformEdit edit;
            edit.entityId = entry.entityId;
            edit.position = add(entry.startLocal, worldDeltaToLocal3D(scene, entry.entityId, worldDelta));
            edits.push_back(edit);
        }

        return edits;
    }

    std::vector<EntityTransformEdit> MultiTransform3D::rotate(const SceneDocument& scene,
                                                              const EditorVector3& axis,
                                                              float radians) const
    {
        std::vector<EntityTransformEdit> edits;
        edits.reserve(entries_.size());

        const EditorQuaternion turn = quaternionFromAxisAngle(axis, radians);

        for (const Entry& entry : entries_)
        {
            // Carried around the pivot...
            const EditorVector3 moved =
                add(pivot_, CNA::Editor::rotate(turn, subtract(entry.startWorldPosition, pivot_)));

            EntityTransformEdit edit;
            edit.entityId = entry.entityId;
            edit.position =
                add(entry.startLocal,
                    worldDeltaToLocal3D(scene, entry.entityId, subtract(moved, entry.startWorldPosition)));

            // ...and turned by the same angle, in the world and then expressed in its parent's
            // frame, which is where `rotation` is stored and the only reason the inverse is kept.
            edit.rotation = multiply(entry.inverseParentRotation, multiply(turn, entry.startWorldRotation));
            edits.push_back(std::move(edit));
        }

        return edits;
    }

    std::vector<EntityTransformEdit> MultiTransform3D::scale(const SceneDocument& scene,
                                                             const std::array<EditorVector3, 3>& axes,
                                                             const EditorVector3& factor) const
    {
        std::vector<EntityTransformEdit> edits;
        edits.reserve(entries_.size());

        const std::array<float, 3> factors{factor.x, factor.y, factor.z};

        for (const Entry& entry : entries_)
        {
            // Distances from the pivot grow with the entities, or scaling a group up would leave
            // every member where it was and overlapping its neighbours. Along the *gizmo's* axes,
            // which are the arms the user grabbed: measuring the offset in world components would
            // stretch a selection along axes nobody touched whenever the primary is rotated.
            const EditorVector3 offset = subtract(entry.startWorldPosition, pivot_);

            EditorVector3 moved = pivot_;
            for (std::size_t index = 0; index < 3; ++index)
            {
                moved = add(moved, CNA::Editor::scale(axes[index], dot(offset, axes[index]) * factors[index]));
            }

            EntityTransformEdit edit;
            edit.entityId = entry.entityId;
            edit.position =
                add(entry.startLocal,
                    worldDeltaToLocal3D(scene, entry.entityId, subtract(moved, entry.startWorldPosition)));
            edit.scale = EditorVector3{keepScalable(entry.startLocalScale.x * factor.x),
                                       keepScalable(entry.startLocalScale.y * factor.y),
                                       keepScalable(entry.startLocalScale.z * factor.z)};
            edits.push_back(std::move(edit));
        }

        return edits;
    }

    std::optional<EditorVector3> TranslateGizmo3DDrag::getWorldDelta(const EditorCamera3D& camera,
                                                                     const EditorVector2& cursor,
                                                                     const GizmoSnap& snap) const
    {
        if (!isActive()) { return std::nullopt; }

        const std::optional<float> parameter =
            closestPointOnAxis(camera.screenToRay(cursor), startWorld_, direction_);
        if (!parameter) { return std::nullopt; }

        EditorVector3 world = add(startWorld_, scale(direction_, *parameter - startParameter_));

        if (snap.translate > 0.0f)
        {
            // The *result* is snapped, not the movement, so a dragged entity lands on the grid
            // rather than a grid-sized distance from wherever it started. Along the axis only:
            // rounding a coordinate the drag never touched would move the entity on an axis the
            // user deliberately constrained out.
            const float along = dot(world, direction_);
            const float rounded = std::round(along / snap.translate) * snap.translate;
            world = add(world, scale(direction_, rounded - along));
        }

        const EditorVector3 delta = subtract(world, startWorld_);
        if (delta == EditorVector3{}) { return std::nullopt; }
        return delta;
    }

    std::optional<EditorVector3> TranslateGizmo3DDrag::update(const SceneDocument& scene,
                                                              const EditorCamera3D& camera,
                                                              const EditorVector2& cursor,
                                                              const GizmoSnap& snap)
    {
        // The single-entity answer, computed from the same gesture a whole selection uses, so one
        // entity and twenty cannot disagree about how far the cursor went.
        const std::optional<EditorVector3> delta = getWorldDelta(camera, cursor, snap);
        if (!delta) { return std::nullopt; }

        return add(startLocal_, worldDeltaToLocal3D(scene, entityId_, *delta));
    }

}
