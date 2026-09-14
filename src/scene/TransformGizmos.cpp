// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/TransformGizmos.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns the distance from @p point to the segment @p a -- @p b. */
        float distanceToSegment(const EditorVector2& point, const EditorVector2& a, const EditorVector2& b)
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float lengthSquared = dx * dx + dy * dy;
            if (lengthSquared <= 0.0f)
            {
                return std::hypot(point.x - a.x, point.y - a.y);
            }

            // Projection parameter, clamped so the nearest point stays on the segment rather than
            // on its infinite line -- otherwise the arms would be grabbable far past their tips.
            const float t = std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared,
                                       0.0f, 1.0f);
            return std::hypot(point.x - (a.x + dx * t), point.y - (a.y + dy * t));
        }

        /** @brief Returns true when @p point is inside the square of half-extent @p extent at @p center. */
        bool insideSquare(const EditorVector2& point, const EditorVector2& center, float extent)
        {
            return std::fabs(point.x - center.x) <= extent && std::fabs(point.y - center.y) <= extent;
        }

        /** @brief Returns the transform component of @p entityId, or nullptr. */
        const EditorComponent* transformOf(const SceneDocument& scene, const Uuid& entityId)
        {
            const EditorEntity* entity = scene.findEntity(entityId);
            if (entity == nullptr) { return nullptr; }
            return entity->findComponent(BuiltinComponentIds::kTransform);
        }

        /**
         * @brief Returns the entity's world Z rotation, or 0 when there is nothing to read.
         *
         * Only Z, because that is the only axis a 2D viewport can show a turn about. A pitched or
         * yawed entity still gets a usable gizmo -- its arms are its Z-projected axes -- rather
         * than none at all, which is the honest 2D answer until the 3D viewport lands (ED-400).
         */
        float worldAngleOf(const SceneDocument& scene, const Uuid& entityId)
        {
            const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
            return world ? zRotationOf(world->rotation) : 0.0f;
        }

        /**
         * @brief Returns the two screen-space arm directions for @p angle.
         *
         * The camera has no rotation and Y points down in both spaces, so a world angle is a screen
         * angle unchanged. If a rotating camera ever arrives, this is the one place that has to
         * learn about it.
         */
        std::pair<EditorVector2, EditorVector2> axesForAngle(float angle)
        {
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            return {EditorVector2{cosine, sine}, EditorVector2{-sine, cosine}};
        }

        /** @brief Returns the inverse of the unit quaternion @p rotation. */
        EditorQuaternion inverseOf(const EditorQuaternion& rotation)
        {
            return EditorQuaternion{-rotation.x, -rotation.y, -rotation.z, rotation.w};
        }

        /** @brief Returns the parent's world rotation, or identity for a root entity. */
        EditorQuaternion parentWorldRotation(const SceneDocument& scene, const Uuid& entityId)
        {
            const EditorEntity* entity = scene.findEntity(entityId);
            if (entity == nullptr) { return EditorQuaternion{}; }

            const Uuid parentId = entity->getParentId();
            if (!parentId.isValid()) { return EditorQuaternion{}; }

            const std::optional<WorldTransform> parent = computeWorldTransform(scene, parentId);
            return parent ? parent->rotation : EditorQuaternion{};
        }

        /**
         * @brief Keeps a scale factor away from exactly zero, sign intact.
         *
         * Dragging a handle through the origin flips the entity, which is a legitimate edit and one
         * XNA's own negative scale supports. Landing *on* zero is not: the entity vanishes, its
         * bounds collapse, and it can no longer be clicked to get it back.
         */
        float keepScalable(float factor)
        {
            constexpr float kSmallest = 0.001f;
            if (std::fabs(factor) >= kSmallest) { return factor; }
            return factor < 0.0f ? -kSmallest : kSmallest;
        }
    }

    float snapTo(float value, float step)
    {
        if (step <= 0.0f) { return value; }
        return std::round(value / step) * step;
    }

    const char* toString(GizmoSpace space)
    {
        return space == GizmoSpace::Local ? "Local" : "World";
    }

    EditorVector2 RotateGizmoLayout::getPointAt(float radians) const
    {
        return EditorVector2{origin.x + std::cos(radians) * radius, origin.y + std::sin(radians) * radius};
    }

    std::optional<TranslateGizmoLayout> computeTranslateGizmoLayout(const SceneDocument& scene,
                                                                    const EditorCamera2D& camera,
                                                                    const Uuid& entityId,
                                                                    GizmoSpace space)
    {
        if (transformOf(scene, entityId) == nullptr) { return std::nullopt; }

        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        TranslateGizmoLayout layout;
        layout.origin = camera.worldToScreen(EditorVector2{world->position.x, world->position.y});

        if (space == GizmoSpace::Local)
        {
            const auto [xAxis, yAxis] = axesForAngle(zRotationOf(world->rotation));
            layout.xAxis = xAxis;
            layout.yAxis = yAxis;
        }
        return layout;
    }

    std::optional<RotateGizmoLayout> computeRotateGizmoLayout(const SceneDocument& scene,
                                                              const EditorCamera2D& camera,
                                                              const Uuid& entityId)
    {
        if (transformOf(scene, entityId) == nullptr) { return std::nullopt; }

        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        RotateGizmoLayout layout;
        layout.origin = camera.worldToScreen(EditorVector2{world->position.x, world->position.y});
        layout.angle = zRotationOf(world->rotation);
        return layout;
    }

    std::optional<ScaleGizmoLayout> computeScaleGizmoLayout(const SceneDocument& scene,
                                                            const EditorCamera2D& camera,
                                                            const Uuid& entityId)
    {
        if (transformOf(scene, entityId) == nullptr) { return std::nullopt; }

        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        ScaleGizmoLayout layout;
        layout.origin = camera.worldToScreen(EditorVector2{world->position.x, world->position.y});

        // Always local: see GizmoSpace. The arms are the axes the stored numbers actually belong to.
        const auto [xAxis, yAxis] = axesForAngle(zRotationOf(world->rotation));
        layout.xAxis = xAxis;
        layout.yAxis = yAxis;
        return layout;
    }

    GizmoHandle hitTestTranslateGizmo(const TranslateGizmoLayout& layout, const EditorVector2& screenPoint)
    {
        // The centre is tested first and wins where it overlaps the arms: it is the smaller target,
        // and it is what a user aiming at the middle expects. The arms stay reachable along the
        // rest of their length.
        if (insideSquare(screenPoint, layout.origin, layout.centerExtent)) { return GizmoHandle::Both; }

        if (distanceToSegment(screenPoint, layout.origin, layout.getXTip()) <= layout.grabTolerance)
        {
            return GizmoHandle::XAxis;
        }
        if (distanceToSegment(screenPoint, layout.origin, layout.getYTip()) <= layout.grabTolerance)
        {
            return GizmoHandle::YAxis;
        }

        return GizmoHandle::None;
    }

    GizmoHandle hitTestRotateGizmo(const RotateGizmoLayout& layout, const EditorVector2& screenPoint)
    {
        // A band around the ring, not a disc: the inside of the circle is where the entity is, and
        // grabbing there would make the sprite itself unclickable at every zoom.
        const float distance = std::hypot(screenPoint.x - layout.origin.x, screenPoint.y - layout.origin.y);
        return std::fabs(distance - layout.radius) <= layout.grabTolerance ? GizmoHandle::ZAxis
                                                                           : GizmoHandle::None;
    }

    GizmoHandle hitTestScaleGizmo(const ScaleGizmoLayout& layout, const EditorVector2& screenPoint)
    {
        if (insideSquare(screenPoint, layout.origin, layout.centerExtent)) { return GizmoHandle::Both; }

        // The end squares are tested before the arms, so a press that is a hair off the arm's line
        // but plainly on its handle still counts -- the square is the part the eye aims at.
        if (insideSquare(screenPoint, layout.getXTip(), layout.handleExtent)) { return GizmoHandle::XAxis; }
        if (insideSquare(screenPoint, layout.getYTip(), layout.handleExtent)) { return GizmoHandle::YAxis; }

        if (distanceToSegment(screenPoint, layout.origin, layout.getXTip()) <= layout.grabTolerance)
        {
            return GizmoHandle::XAxis;
        }
        if (distanceToSegment(screenPoint, layout.origin, layout.getYTip()) <= layout.grabTolerance)
        {
            return GizmoHandle::YAxis;
        }

        return GizmoHandle::None;
    }

    EditorVector2 worldDeltaToLocal(const SceneDocument& scene,
                                    const Uuid& entityId,
                                    const EditorVector2& worldDelta)
    {
        const EditorEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr) { return worldDelta; }

        const Uuid parentId = entity->getParentId();
        if (!parentId.isValid()) { return worldDelta; }

        const std::optional<WorldTransform> parent = computeWorldTransform(scene, parentId);
        if (!parent) { return worldDelta; }

        // Undo the parent's rotation, then its scale -- the inverse of how a local offset becomes a
        // world one in computeWorldTransform. Skipping this gives a gizmo that is exact on root
        // entities and drifts on every child of a rotated or scaled parent.
        const EditorVector3 unrotated =
            rotate(inverseOf(parent->rotation), EditorVector3{worldDelta.x, worldDelta.y, 0.0f});

        const float scaleX = parent->scale.x != 0.0f ? parent->scale.x : 1.0f;
        const float scaleY = parent->scale.y != 0.0f ? parent->scale.y : 1.0f;
        return EditorVector2{unrotated.x / scaleX, unrotated.y / scaleY};
    }

    bool TranslateGizmoDrag::begin(const SceneDocument& scene,
                                   const EditorCamera2D& camera,
                                   const Uuid& entityId,
                                   GizmoHandle handle,
                                   const EditorVector2& screenPoint,
                                   GizmoSpace space)
    {
        end();
        if (handle == GizmoHandle::None) { return false; }

        const EditorComponent* transform = transformOf(scene, entityId);
        if (transform == nullptr) { return false; }

        handle_ = handle;
        entityId_ = entityId;
        grabWorld_ = camera.screenToWorld(screenPoint);
        startLocalPosition_ = transform->getProperty("position").get<EditorVector3>();

        // The constraint is recorded at the press and never recomputed, so nothing that changes the
        // entity's rotation mid-drag can bend the axis it is already moving along.
        const auto [xAxis, yAxis] =
            axesForAngle(space == GizmoSpace::Local ? worldAngleOf(scene, entityId) : 0.0f);
        constraintAxis_ = handle == GizmoHandle::YAxis ? yAxis : xAxis;
        return true;
    }

    EditorVector2 TranslateGizmoDrag::getWorldDelta(const EditorCamera2D& camera,
                                                    const EditorVector2& screenPoint,
                                                    const GizmoSnap& snap) const
    {
        if (!isActive()) { return EditorVector2{}; }

        // Measured from where the drag *began*, never accumulated frame to frame. Accumulating is
        // the classic source of gizmo drift: rounding compounds and the object separates from the
        // cursor over a long drag.
        const EditorVector2 nowWorld = camera.screenToWorld(screenPoint);
        EditorVector2 worldDelta{nowWorld.x - grabWorld_.x, nowWorld.y - grabWorld_.y};

        if (handle_ == GizmoHandle::XAxis || handle_ == GizmoHandle::YAxis)
        {
            // Projected onto the arm rather than zeroed component-wise, because in local space the
            // arm is not a coordinate axis. In world space the two are the same arithmetic.
            const float along = worldDelta.x * constraintAxis_.x + worldDelta.y * constraintAxis_.y;
            worldDelta = EditorVector2{constraintAxis_.x * along, constraintAxis_.y * along};
        }

        // A snapped *world* delta is what a multi-selection needs: rounding each entity's own
        // position would spread a group out onto the grid instead of moving it as one thing.
        if (snap.translate > 0.0f)
        {
            if (handle_ != GizmoHandle::YAxis)
            {
                worldDelta.x = snapTo(worldDelta.x, snap.translate);
            }
            if (handle_ != GizmoHandle::XAxis)
            {
                worldDelta.y = snapTo(worldDelta.y, snap.translate);
            }
        }
        return worldDelta;
    }

    std::optional<EditorVector3> TranslateGizmoDrag::update(const SceneDocument& scene,
                                                            const EditorCamera2D& camera,
                                                            const EditorVector2& screenPoint,
                                                            const GizmoSnap& snap) const
    {
        if (!isActive()) { return std::nullopt; }

        const EditorVector2 nowWorld = camera.screenToWorld(screenPoint);
        EditorVector2 worldDelta{nowWorld.x - grabWorld_.x, nowWorld.y - grabWorld_.y};

        if (handle_ == GizmoHandle::XAxis || handle_ == GizmoHandle::YAxis)
        {
            const float along = worldDelta.x * constraintAxis_.x + worldDelta.y * constraintAxis_.y;
            worldDelta = EditorVector2{constraintAxis_.x * along, constraintAxis_.y * along};
        }

        const EditorVector2 localDelta = worldDeltaToLocal(scene, entityId_, worldDelta);
        EditorVector3 position{startLocalPosition_.x + localDelta.x,
                               startLocalPosition_.y + localDelta.y,
                               startLocalPosition_.z};

        // The *result* is snapped, not the delta. Snapping the movement would land an entity that
        // started at 3.7 on 13.7 rather than on 10 -- which is not what a grid is for.
        //
        // Only the axes the handle allows, though. Snapping one the drag deliberately constrained
        // out would move the entity along an axis the user just said not to touch -- and it is
        // *invisible*, because the arm being dragged is the one they are watching.
        if (snap.translate > 0.0f)
        {
            if (handle_ != GizmoHandle::YAxis) { position.x = snapTo(position.x, snap.translate); }
            if (handle_ != GizmoHandle::XAxis) { position.y = snapTo(position.y, snap.translate); }
        }
        return position;
    }

    bool RotateGizmoDrag::begin(const SceneDocument& scene,
                                const RotateGizmoLayout& layout,
                                const Uuid& entityId,
                                const EditorVector2& screenPoint)
    {
        end();

        if (transformOf(scene, entityId) == nullptr) { return false; }

        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return false; }

        const float dx = screenPoint.x - layout.origin.x;
        const float dy = screenPoint.y - layout.origin.y;

        // An angle needs a direction, and a press within a pixel of the centre has none. Refusing
        // beats starting a drag that snaps the entity to whatever atan2 made of the noise.
        if (std::hypot(dx, dy) < 1.0f) { return false; }

        active_ = true;
        entityId_ = entityId;
        startPointerAngle_ = std::atan2(dy, dx);
        startWorldRotation_ = world->rotation;
        inverseParentRotation_ = inverseOf(parentWorldRotation(scene, entityId));
        return true;
    }

    float RotateGizmoDrag::getDeltaAngle(const RotateGizmoLayout& layout,
                                         const EditorVector2& screenPoint,
                                         const GizmoSnap& snap) const
    {
        if (!active_) { return 0.0f; }

        const float now = std::atan2(screenPoint.y - layout.origin.y, screenPoint.x - layout.origin.x);

        // Wrapped into (-pi, pi]: without this, dragging across the -pi/+pi seam would report a
        // turn of nearly a full circle in the wrong direction, and the entity would spin.
        constexpr float kPi = 3.14159265358979323846f;
        float delta = now - startPointerAngle_;
        while (delta > kPi) { delta -= 2.0f * kPi; }
        while (delta <= -kPi) { delta += 2.0f * kPi; }

        // The turn is snapped, not the resulting angle: an entity that was at 7 degrees and is
        // turned by a snapped 15 lands on 22, which is what "rotate it by a quarter" means. The
        // alternative -- snapping the absolute angle -- silently straightens whatever it touches.
        return snapTo(delta, snap.rotate);
    }

    std::optional<EditorQuaternion> RotateGizmoDrag::update(const RotateGizmoLayout& layout,
                                                            const EditorVector2& screenPoint,
                                                            const GizmoSnap& snap) const
    {
        if (!active_) { return std::nullopt; }

        // The turn happens in world space -- the cursor is describing a world angle -- and is then
        // expressed in the parent's frame, since `rotation` is stored relative to the parent. For a
        // root entity the parent term is identity and this is just the world turn.
        const EditorQuaternion turned =
            multiply(quaternionFromZRotation(getDeltaAngle(layout, screenPoint, snap)), startWorldRotation_);
        return multiply(inverseParentRotation_, turned);
    }

    bool ScaleGizmoDrag::begin(const SceneDocument& scene,
                               const ScaleGizmoLayout& layout,
                               const Uuid& entityId,
                               GizmoHandle handle,
                               const EditorVector2& screenPoint)
    {
        end();
        if (handle == GizmoHandle::None) { return false; }

        const EditorComponent* transform = transformOf(scene, entityId);
        if (transform == nullptr) { return false; }

        const EditorVector2 offset{screenPoint.x - layout.origin.x, screenPoint.y - layout.origin.y};

        // How far out the grab was, measured exactly the way update() measures: along the arm for a
        // single axis, radially for the uniform handle.
        float distance = 0.0f;
        switch (handle)
        {
            case GizmoHandle::XAxis: distance = offset.x * layout.xAxis.x + offset.y * layout.xAxis.y; break;
            case GizmoHandle::YAxis: distance = offset.x * layout.yAxis.x + offset.y * layout.yAxis.y; break;
            default: distance = std::hypot(offset.x, offset.y); break;
        }

        // Every factor below is a division by this. A grab at the origin would scale by infinity,
        // so it is not a drag at all -- the press falls through to whatever is underneath.
        constexpr float kSmallestGrab = 4.0f;
        if (std::fabs(distance) < kSmallestGrab) { return false; }

        handle_ = handle;
        entityId_ = entityId;
        grabDistance_ = distance;
        startLocalScale_ =
            transform->getProperty("scale").get<EditorVector3>(EditorVector3{1.0f, 1.0f, 1.0f});
        return true;
    }

    float ScaleGizmoDrag::getFactor(const ScaleGizmoLayout& layout,
                                    const EditorVector2& screenPoint,
                                    const GizmoSnap& snap) const
    {
        if (!isActive()) { return 1.0f; }

        const EditorVector2 offset{screenPoint.x - layout.origin.x, screenPoint.y - layout.origin.y};

        float distance = 0.0f;
        switch (handle_)
        {
            case GizmoHandle::XAxis: distance = offset.x * layout.xAxis.x + offset.y * layout.xAxis.y; break;
            case GizmoHandle::YAxis: distance = offset.x * layout.yAxis.x + offset.y * layout.yAxis.y; break;
            default: distance = std::hypot(offset.x, offset.y); break;
        }

        // A ratio, not a difference: screen pixels are not scale units, and only the ratio is
        // independent of the zoom the drag happens to be at. Snapped here for a multi-selection,
        // where the factor is the shared quantity -- rounding each entity's own scale would leave a
        // group at different sizes than it started in proportion to each other.
        return keepScalable(snapTo(distance / grabDistance_, snap.scale));
    }

    std::optional<EditorVector3> ScaleGizmoDrag::update(const ScaleGizmoLayout& layout,
                                                        const EditorVector2& screenPoint,
                                                        const GizmoSnap& snap) const
    {
        if (!isActive()) { return std::nullopt; }

        const EditorVector2 offset{screenPoint.x - layout.origin.x, screenPoint.y - layout.origin.y};

        float distance = 0.0f;
        switch (handle_)
        {
            case GizmoHandle::XAxis: distance = offset.x * layout.xAxis.x + offset.y * layout.xAxis.y; break;
            case GizmoHandle::YAxis: distance = offset.x * layout.yAxis.x + offset.y * layout.yAxis.y; break;
            default: distance = std::hypot(offset.x, offset.y); break;
        }

        const float factor = keepScalable(distance / grabDistance_);

        EditorVector3 scale = startLocalScale_;

        // The resulting scale is snapped, not the factor: what a user wants from a snapped scale
        // drag is an entity at exactly 2, not one at 1.7 multiplied by a round number.
        if (handle_ != GizmoHandle::YAxis)
        {
            scale.x = keepScalable(snapTo(startLocalScale_.x * factor, snap.scale));
        }
        if (handle_ != GizmoHandle::XAxis)
        {
            scale.y = keepScalable(snapTo(startLocalScale_.y * factor, snap.scale));
        }
        return scale;
    }

    std::optional<EditorVector2> computeSelectionPivot(const SceneDocument& scene,
                                                       const std::vector<Uuid>& entityIds)
    {
        EditorVector2 total;
        std::size_t counted = 0;

        for (const Uuid& entityId : entityIds)
        {
            const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
            if (!world) { continue; }

            total.x += world->position.x;
            total.y += world->position.y;
            ++counted;
        }

        if (counted == 0) { return std::nullopt; }
        return EditorVector2{total.x / static_cast<float>(counted),
                             total.y / static_cast<float>(counted)};
    }

    std::vector<Uuid> findSelectionRoots(const SceneDocument& scene, const std::vector<Uuid>& entityIds)
    {
        std::vector<Uuid> roots;

        for (const Uuid& entityId : entityIds)
        {
            // Walk up to the scene root looking for another selected entity. The chain is a
            // handful of links deep, and the bound is the entity count, so a cycle -- which the
            // document forbids but a hand-edited file could still contain -- cannot hang this.
            bool hasSelectedAncestor = false;
            const EditorEntity* entity = scene.findEntity(entityId);

            for (std::size_t step = 0; entity != nullptr && step <= scene.getEntityCount(); ++step)
            {
                const Uuid parentId = entity->getParentId();
                if (!parentId.isValid()) { break; }

                if (std::find(entityIds.begin(), entityIds.end(), parentId) != entityIds.end())
                {
                    hasSelectedAncestor = true;
                    break;
                }
                entity = scene.findEntity(parentId);
            }

            if (!hasSelectedAncestor) { roots.push_back(entityId); }
        }
        return roots;
    }

    bool MultiTransformDrag::begin(const SceneDocument& scene,
                                   const std::vector<Uuid>& entityIds,
                                   const EditorVector2& pivotWorld)
    {
        end();
        pivot_ = pivotWorld;

        for (const Uuid& entityId : findSelectionRoots(scene, entityIds))
        {
            const EditorComponent* transform = transformOf(scene, entityId);
            if (transform == nullptr) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
            if (!world) { continue; }

            Entry entry;
            entry.entityId = entityId;
            entry.startWorldPosition = EditorVector2{world->position.x, world->position.y};
            entry.startLocalPosition = transform->getProperty("position").get<EditorVector3>();
            entry.startWorldRotation = world->rotation;
            entry.inverseParentRotation = inverseOf(parentWorldRotation(scene, entityId));
            entry.startLocalScale =
                transform->getProperty("scale").get<EditorVector3>(EditorVector3{1.0f, 1.0f, 1.0f});
            entries_.push_back(std::move(entry));
        }

        return isActive();
    }

    std::vector<EntityTransformEdit> MultiTransformDrag::translate(const SceneDocument& scene,
                                                                   const EditorVector2& worldDelta) const
    {
        std::vector<EntityTransformEdit> edits;
        edits.reserve(entries_.size());

        for (const Entry& entry : entries_)
        {
            // Every entity moves by the same *world* delta and turns it into its own local units,
            // so a selection spanning several parents keeps its shape.
            const EditorVector2 localDelta = worldDeltaToLocal(scene, entry.entityId, worldDelta);

            EntityTransformEdit edit;
            edit.entityId = entry.entityId;
            edit.position = EditorVector3{entry.startLocalPosition.x + localDelta.x,
                                          entry.startLocalPosition.y + localDelta.y,
                                          entry.startLocalPosition.z};
            edits.push_back(std::move(edit));
        }
        return edits;
    }

    std::vector<EntityTransformEdit> MultiTransformDrag::rotate(const SceneDocument& scene,
                                                                float radians) const
    {
        std::vector<EntityTransformEdit> edits;
        edits.reserve(entries_.size());

        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);

        for (const Entry& entry : entries_)
        {
            // Carried around the pivot...
            const float offsetX = entry.startWorldPosition.x - pivot_.x;
            const float offsetY = entry.startWorldPosition.y - pivot_.y;
            const EditorVector2 movedWorld{pivot_.x + offsetX * cosine - offsetY * sine,
                                           pivot_.y + offsetX * sine + offsetY * cosine};

            const EditorVector2 worldDelta{movedWorld.x - entry.startWorldPosition.x,
                                           movedWorld.y - entry.startWorldPosition.y};
            const EditorVector2 localDelta = worldDeltaToLocal(scene, entry.entityId, worldDelta);

            EntityTransformEdit edit;
            edit.entityId = entry.entityId;
            edit.position = EditorVector3{entry.startLocalPosition.x + localDelta.x,
                                          entry.startLocalPosition.y + localDelta.y,
                                          entry.startLocalPosition.z};

            // ...and turned by the same angle, expressed in its parent's frame like every other
            // rotation this editor writes.
            edit.rotation = multiply(entry.inverseParentRotation,
                                     multiply(quaternionFromZRotation(radians), entry.startWorldRotation));
            edits.push_back(std::move(edit));
        }
        return edits;
    }

    std::vector<EntityTransformEdit> MultiTransformDrag::scale(const SceneDocument& scene,
                                                               const EditorVector2& factor) const
    {
        std::vector<EntityTransformEdit> edits;
        edits.reserve(entries_.size());

        for (const Entry& entry : entries_)
        {
            // Distances from the pivot scale with the entities. Without this, scaling a group up
            // would leave every member in place and overlapping its neighbours.
            const EditorVector2 movedWorld{pivot_.x + (entry.startWorldPosition.x - pivot_.x) * factor.x,
                                           pivot_.y + (entry.startWorldPosition.y - pivot_.y) * factor.y};

            const EditorVector2 worldDelta{movedWorld.x - entry.startWorldPosition.x,
                                           movedWorld.y - entry.startWorldPosition.y};
            const EditorVector2 localDelta = worldDeltaToLocal(scene, entry.entityId, worldDelta);

            EntityTransformEdit edit;
            edit.entityId = entry.entityId;
            edit.position = EditorVector3{entry.startLocalPosition.x + localDelta.x,
                                          entry.startLocalPosition.y + localDelta.y,
                                          entry.startLocalPosition.z};
            edit.scale = EditorVector3{keepScalable(entry.startLocalScale.x * factor.x),
                                       keepScalable(entry.startLocalScale.y * factor.y),
                                       entry.startLocalScale.z};
            edits.push_back(std::move(edit));
        }
        return edits;
    }
}
