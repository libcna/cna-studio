// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Scene/SceneTransform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"

namespace CNA::Studio
{
    void WorldBounds2D::encapsulate(const StudioVector2& point)
    {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
    }

    WorldBounds2D WorldBounds2D::combine(const WorldBounds2D& a, const WorldBounds2D& b)
    {
        if (a.isEmpty()) { return b; }
        if (b.isEmpty()) { return a; }

        WorldBounds2D result = a;
        result.encapsulate(b.min);
        result.encapsulate(b.max);
        return result;
    }

    StudioQuaternion multiply(const StudioQuaternion& a, const StudioQuaternion& b)
    {
        return StudioQuaternion{
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    StudioVector3 rotate(const StudioQuaternion& rotation, const StudioVector3& vector)
    {
        // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v). Cheaper than building a matrix and
        // avoids the normalisation a matrix path would want.
        const float x = rotation.x;
        const float y = rotation.y;
        const float z = rotation.z;
        const float w = rotation.w;

        const float tx = 2.0f * (y * vector.z - z * vector.y);
        const float ty = 2.0f * (z * vector.x - x * vector.z);
        const float tz = 2.0f * (x * vector.y - y * vector.x);

        return StudioVector3{
            vector.x + w * tx + (y * tz - z * ty),
            vector.y + w * ty + (z * tx - x * tz),
            vector.z + w * tz + (x * ty - y * tx)};
    }

    StudioQuaternion quaternionFromZRotation(float radians)
    {
        const float half = radians * 0.5f;
        return StudioQuaternion{0.0f, 0.0f, std::sin(half), std::cos(half)};
    }

    float zRotationOf(const StudioQuaternion& rotation)
    {
        return std::atan2(2.0f * (rotation.w * rotation.z + rotation.x * rotation.y),
                          1.0f - 2.0f * (rotation.y * rotation.y + rotation.z * rotation.z));
    }

    StudioQuaternion quaternionFromEulerDegrees(const StudioVector3& degrees)
    {
        constexpr float kToRadians = 3.14159265358979323846f / 180.0f;

        const float halfPitch = degrees.x * kToRadians * 0.5f;
        const float halfYaw = degrees.y * kToRadians * 0.5f;
        const float halfRoll = degrees.z * kToRadians * 0.5f;

        const float sinPitch = std::sin(halfPitch);
        const float cosPitch = std::cos(halfPitch);
        const float sinYaw = std::sin(halfYaw);
        const float cosYaw = std::cos(halfYaw);
        const float sinRoll = std::sin(halfRoll);
        const float cosRoll = std::cos(halfRoll);

        // Term for term what XNA's Quaternion.CreateFromYawPitchRoll computes.
        return StudioQuaternion{
            (cosYaw * sinPitch * cosRoll) + (sinYaw * cosPitch * sinRoll),
            (sinYaw * cosPitch * cosRoll) - (cosYaw * sinPitch * sinRoll),
            (cosYaw * cosPitch * sinRoll) - (sinYaw * sinPitch * cosRoll),
            (cosYaw * cosPitch * cosRoll) + (sinYaw * sinPitch * sinRoll)};
    }

    StudioVector3 eulerDegreesOf(const StudioQuaternion& rotation)
    {
        constexpr float kToDegrees = 180.0f / 3.14159265358979323846f;

        // asin(-0) and atan2(-0, 1) both give negative zero, which an inspector renders as
        // "-0.000" -- indistinguishable from a bug to anyone reading it.
        const auto withoutNegativeZero = [](float value) { return value == 0.0f ? 0.0f : value; };

        const float x = rotation.x;
        const float y = rotation.y;
        const float z = rotation.z;
        const float w = rotation.w;

        // The three matrix entries the Y-X-Z extraction needs, straight from the quaternion. Only
        // these are built: forming the whole matrix to read four of its nine entries is waste.
        const float m12 = 2.0f * (y * z - w * x);   // -sin(pitch)
        const float m10 = 2.0f * (x * y + w * z);   //  cos(pitch) * sin(roll)
        const float m11 = 1.0f - 2.0f * (x * x + z * z); // cos(pitch) * cos(roll)
        const float m02 = 2.0f * (x * z + w * y);   //  cos(pitch) * sin(yaw)
        const float m22 = 1.0f - 2.0f * (x * x + y * y); // cos(pitch) * cos(yaw)

        const float sinPitch = std::clamp(-m12, -1.0f, 1.0f);
        const float pitch = std::asin(sinPitch);

        // Gimbal lock: with the pitch at a pole, cos(pitch) is zero and yaw and roll are no longer
        // separable -- every (yaw, roll) pair with the same sum names the same rotation. Splitting
        // it arbitrarily would make the inspector jitter between equivalent answers as the last
        // bits of the quaternion moved, so the whole turn is reported as yaw and roll pinned to 0.
        constexpr float kPoleEpsilon = 1.0e-4f;
        if (std::fabs(sinPitch) > 1.0f - kPoleEpsilon)
        {
            // At +90 the rotation depends only on (yaw - roll), at -90 only on (yaw + roll), which
            // is why the sign differs between the two poles rather than being one formula.
            const float m01 = 2.0f * (x * y - w * z);
            const float m00 = 1.0f - 2.0f * (y * y + z * z);
            const float yawAtPole = sinPitch > 0.0f ? std::atan2(m01, m00) : std::atan2(-m01, m00);

            return StudioVector3{withoutNegativeZero(pitch * kToDegrees),
                                 withoutNegativeZero(yawAtPole * kToDegrees), 0.0f};
        }

        return StudioVector3{withoutNegativeZero(pitch * kToDegrees),
                             withoutNegativeZero(std::atan2(m02, m22) * kToDegrees),
                             withoutNegativeZero(std::atan2(m10, m11) * kToDegrees)};
    }

    std::optional<WorldTransform> computeWorldTransform(const SceneDocument& scene, const Uuid& entityId)
    {
        if (scene.findEntity(entityId) == nullptr) { return std::nullopt; }

        // Walk up to the root collecting the chain, then compose downwards. Composing on the way
        // up would require inverting each step, and the chains here are a handful of links deep.
        std::vector<const StudioEntity*> chain;
        Uuid current = entityId;
        for (std::size_t step = 0; step <= scene.getEntityCount(); ++step)
        {
            const StudioEntity* entity = scene.findEntity(current);
            if (entity == nullptr) { break; }
            chain.push_back(entity);
            current = entity->getParentId();
            if (!current.isValid()) { break; }
        }

        WorldTransform world;
        for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
        {
            const StudioComponent* transform = (*iterator)->findComponent(BuiltinComponentIds::kTransform);

            StudioVector3 localPosition;
            StudioQuaternion localRotation;
            StudioVector3 localScale{1.0f, 1.0f, 1.0f};

            if (transform != nullptr)
            {
                localPosition = transform->getProperty("position").get<StudioVector3>(localPosition);
                localRotation = transform->getProperty("rotation").get<StudioQuaternion>(localRotation);
                localScale = transform->getProperty("scale").get<StudioVector3>(localScale);
            }

            // Standard TRS composition: the child's local offset is scaled and rotated by the
            // parent before being added to the parent's position.
            const StudioVector3 scaled{localPosition.x * world.scale.x,
                                       localPosition.y * world.scale.y,
                                       localPosition.z * world.scale.z};
            const StudioVector3 rotated = rotate(world.rotation, scaled);

            world.position = StudioVector3{world.position.x + rotated.x,
                                           world.position.y + rotated.y,
                                           world.position.z + rotated.z};
            world.rotation = multiply(world.rotation, localRotation);
            world.scale = StudioVector3{world.scale.x * localScale.x,
                                        world.scale.y * localScale.y,
                                        world.scale.z * localScale.z};
        }

        return world;
    }

    StudioMatrix toWorldMatrix(const WorldTransform& transform)
    {
        return multiply(multiply(createScale(transform.scale),
                                 createFromQuaternion(transform.rotation)),
                        createTranslation(transform.position));
    }

    std::optional<WorldBounds2D> computeEntityBounds2D(const SceneDocument& scene,
                                                       const Uuid& entityId,
                                                       const SpriteSizeProvider& sizeProvider)
    {
        const StudioEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr) { return std::nullopt; }

        const StudioComponent* sprite = entity->findComponent(BuiltinComponentIds::kSpriteRenderer);
        if (sprite == nullptr) { return std::nullopt; }

        const std::optional<WorldTransform> world = computeWorldTransform(scene, entityId);
        if (!world) { return std::nullopt; }

        // Size, in order of preference: the source rectangle when it selects a sub-region, then the
        // whole texture, then a fixed extent. The last matters -- a sprite whose texture failed to
        // import must still be clickable, or the entity cannot be selected and therefore cannot be
        // fixed.
        StudioVector2 size;

        // An animation drives the sprite, so its frame size is the sprite's size. Without this the
        // clickable rectangle would be the whole sheet -- a sixteen-frame walk cycle would be
        // sixteen times too wide to click accurately, and Frame Selected would zoom out to fit it.
        const StudioComponent* animation = entity->findComponent(BuiltinComponentIds::kSpriteAnimation);
        if (animation != nullptr)
        {
            const auto frameWidth = animation->getProperty("frameWidth").get<std::int64_t>(0);
            const auto frameHeight = animation->getProperty("frameHeight").get<std::int64_t>(0);
            if (frameWidth > 0 && frameHeight > 0)
            {
                size = StudioVector2{static_cast<float>(frameWidth), static_cast<float>(frameHeight)};
            }
        }

        const StudioRectangle source = sprite->getProperty("sourceRectangle").get<StudioRectangle>();
        if (size.x > 0.0f && size.y > 0.0f)
        {
            // Already answered by the animation.
        }
        else if (!source.isEmpty())
        {
            size = StudioVector2{static_cast<float>(source.width), static_cast<float>(source.height)};
        }
        else
        {
            const Uuid textureId = sprite->getProperty("texture").get<PropertyValue::AssetReference>().id;
            if (sizeProvider && textureId.isValid()) { size = sizeProvider(textureId); }
        }
        if (size.x <= 0.0f || size.y <= 0.0f)
        {
            size = StudioVector2{kUnknownSpriteExtent, kUnknownSpriteExtent};
        }

        const StudioVector2 origin = sprite->getProperty("origin").get<StudioVector2>();

        // Corners in the sprite's own space, relative to its origin, then scaled, rotated and
        // translated into the world. Rotating first and taking the AABB afterwards is what makes a
        // rotated sprite's box actually cover it.
        const float left = -origin.x;
        const float top = -origin.y;
        const float right = left + size.x;
        const float bottom = top + size.y;

        const StudioVector2 corners[4] = {
            StudioVector2{left, top},
            StudioVector2{right, top},
            StudioVector2{right, bottom},
            StudioVector2{left, bottom},
        };

        WorldBounds2D bounds;
        bounds.min = StudioVector2{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        bounds.max = StudioVector2{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

        for (const StudioVector2& corner : corners)
        {
            const StudioVector3 scaledCorner{corner.x * world->scale.x, corner.y * world->scale.y, 0.0f};
            const StudioVector3 rotatedCorner = rotate(world->rotation, scaledCorner);
            bounds.encapsulate(StudioVector2{world->position.x + rotatedCorner.x,
                                             world->position.y + rotatedCorner.y});
        }

        return bounds;
    }

    std::optional<WorldBounds2D> computeHierarchyBounds2D(const SceneDocument& scene,
                                                          const Uuid& entityId,
                                                          const SpriteSizeProvider& sizeProvider)
    {
        if (scene.findEntity(entityId) == nullptr) { return std::nullopt; }

        std::optional<WorldBounds2D> result = computeEntityBounds2D(scene, entityId, sizeProvider);

        for (const Uuid& childId : scene.getChildren(entityId))
        {
            const std::optional<WorldBounds2D> childBounds =
                computeHierarchyBounds2D(scene, childId, sizeProvider);
            if (!childBounds) { continue; }
            result = result ? WorldBounds2D::combine(*result, *childBounds) : childBounds;
        }

        return result;
    }
}
