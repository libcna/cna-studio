// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Scene/SceneTransform.hpp
 * @brief Resolving an entity's world transform, and its 2D bounds in world space.
 *
 * Deliberately CNA-free, and deliberately not a matrix library. The viewport, the picker and the
 * gizmo all need the same three questions answered — where is this entity in the world, how big is
 * it, and what did the user just click on — and answering them here means every one of them is
 * unit-testable with no window, no GPU and no CNA checkout.
 *
 * The composition is full 3D (position, quaternion rotation, non-uniform scale) even though only
 * the 2D paths are used today. Getting the parent chain right once is cheaper than getting it right
 * twice, and Phase 3 needs the 3D form anyway.
 */

#include <functional>
#include <optional>

#include "CNA/Studio/Core/StudioMath.hpp"
#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
{
    class SceneDocument;

    /** @brief An entity's absolute position, orientation and scale. */
    struct WorldTransform
    {
        StudioVector3 position;
        StudioQuaternion rotation;
        StudioVector3 scale{1.0f, 1.0f, 1.0f};
    };

    /** @brief An axis-aligned bounding rectangle in world space. */
    struct WorldBounds2D
    {
        StudioVector2 min;
        StudioVector2 max;

        [[nodiscard]] bool isEmpty() const { return max.x <= min.x || max.y <= min.y; }

        /** @brief Returns true when @p point lies inside, edges included. */
        [[nodiscard]] bool contains(const StudioVector2& point) const
        {
            return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
        }

        /** @brief Returns the centre point. */
        [[nodiscard]] StudioVector2 getCenter() const
        {
            return StudioVector2{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
        }

        /** @brief Grows the rectangle to include @p point. */
        void encapsulate(const StudioVector2& point);

        /** @brief Returns the union of two rectangles, ignoring empty ones. */
        [[nodiscard]] static WorldBounds2D combine(const WorldBounds2D& a, const WorldBounds2D& b);
    };

    /** @brief Multiplies two quaternions: the rotation @p a followed by @p b. */
    [[nodiscard]] StudioQuaternion multiply(const StudioQuaternion& a, const StudioQuaternion& b);

    /** @brief Rotates @p vector by @p rotation. */
    [[nodiscard]] StudioVector3 rotate(const StudioQuaternion& rotation, const StudioVector3& vector);

    /** @brief Returns a rotation of @p radians about the Z axis — the only axis 2D editing uses. */
    [[nodiscard]] StudioQuaternion quaternionFromZRotation(float radians);

    /** @brief Returns the Z-axis angle of @p rotation in radians, ignoring any X or Y component. */
    [[nodiscard]] float zRotationOf(const StudioQuaternion& rotation);

    /**
     * @brief Returns the quaternion for the Euler angles in @p degrees (x pitch, y yaw, z roll).
     *
     * The convention is XNA's own: the composition `Quaternion.CreateFromYawPitchRoll` produces,
     * which is Y then X then Z applied intrinsically. Matching XNA rather than inventing a
     * convention matters because the game reads these values back through XNA's types -- an editor
     * that agreed with itself but not with the runtime would show angles the game does not produce.
     */
    [[nodiscard]] StudioQuaternion quaternionFromEulerDegrees(const StudioVector3& degrees);

    /**
     * @brief Returns the Euler angles in degrees that produce @p rotation.
     *
     * The inverse of quaternionFromEulerDegrees, in the same convention. Pitch is clamped into
     * [-90, 90] and, at the poles, the yaw/roll split is degenerate -- infinitely many pairs give
     * the same rotation, so roll is pinned to zero and the whole turn is reported as yaw. That is a
     * property of Euler angles, not a shortcut: the alternative is a value that jitters between
     * equivalent answers as the last bits of the quaternion move.
     */
    [[nodiscard]] StudioVector3 eulerDegreesOf(const StudioQuaternion& rotation);

    /**
     * @brief Composes @p entityId's transform with every ancestor's.
     *
     * @return The world transform, or std::nullopt when @p entityId is not in @p scene.
     */
    [[nodiscard]] std::optional<WorldTransform> computeWorldTransform(const SceneDocument& scene,
                                                                      const Uuid& entityId);

    /**
     * @brief Returns the texel size of the asset @p assetId, or (0, 0) when unknown.
     *
     * Supplied by the caller because asset dimensions live behind the asset database and, for a
     * texture that has never been imported, may not be known at all. Keeping it a callback is what
     * lets the bounds and picking logic be tested with no asset database in sight.
     */
    using SpriteSizeProvider = std::function<StudioVector2(const Uuid& assetId)>;

    /**
     * @brief The extent used for a sprite whose texture size is unknown.
     *
     * A sprite with no resolvable size still has to be clickable, or an entity whose texture failed
     * to import becomes impossible to select and therefore impossible to fix.
     */
    inline constexpr float kUnknownSpriteExtent = 64.0f;

    /**
     * @brief Returns @p entityId's world-space 2D bounds.
     *
     * Uses the sprite's source rectangle when it selects a sub-region, otherwise the whole texture
     * as reported by @p sizeProvider, otherwise kUnknownSpriteExtent. The sprite's origin and the
     * entity's world scale and Z rotation are all applied, so a rotated sprite yields the AABB of
     * its rotated corners rather than its unrotated box.
     *
     * @return The bounds, or std::nullopt when the entity has no drawable component.
     */
    [[nodiscard]] std::optional<WorldBounds2D> computeEntityBounds2D(const SceneDocument& scene,
                                                                     const Uuid& entityId,
                                                                     const SpriteSizeProvider& sizeProvider);

    /**
     * @brief Returns bounds covering @p entityId and all of its descendants.
     *
     * What "frame the selection" needs: framing a parent whose own sprite is tiny but whose
     * children spread across the level should show the children.
     */
    [[nodiscard]] std::optional<WorldBounds2D> computeHierarchyBounds2D(const SceneDocument& scene,
                                                                        const Uuid& entityId,
                                                                        const SpriteSizeProvider& sizeProvider);
}
