// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/EditorCamera3D.hpp
 * @brief The 3D editor camera: perspective or orthographic, orbit or fly (plan.md ED-400).
 *
 * The 3D counterpart of `EditorCamera2D`, and deliberately its twin in every structural respect.
 * It is the editor's *own* camera -- never an entity, never serialised into the scene (D-07) -- it
 * lives in `cna-editor-scene` rather than the viewport module, and it is CNA-free, so framing,
 * projection and picking are all unit-testable with no window and no GPU.
 *
 * **One camera, two navigation styles, no modes.** Orbit and fly are usually built as a mode flag
 * with two sets of state that drift apart; here they are two ways of moving *one* state --
 * @ref EditorCamera3D::getPivot "a pivot", a distance and a yaw/pitch pair. Orbiting turns the eye
 * about the pivot; flying moves the pivot and carries the eye with it. Every navigation call leaves
 * a consistent camera behind, so the user can orbit, fly, orbit again and never find the camera
 * spinning about a point it left minutes ago.
 *
 * **Coordinate conventions: Y grows downward**, exactly as in `EditorCamera2D` and in every
 * `SpriteBatch` coordinate a game already uses. So an entity at y = 300 is *below* the origin in
 * both views, and switching between them moves the camera without moving the scene.
 *
 * That is a decision, not an accident, and it is worth knowing what it costs. XNA's 3D side is
 * Y-up: `Matrix::CreateLookAt`, `BasicEffect` and every model a game loads assume it. This camera
 * keeps the underlying arithmetic Y-up -- the view matrix, the axes, the ray casts are all
 * ordinary right-handed maths -- and mirrors the *projection's* Y, which is precisely the
 * conversion between a Y-down and a Y-up frame. The two views therefore agree with each other and
 * with the 2D runtime, at the price that the image is a mirror of what a Y-up 3D renderer would
 * produce from the same numbers. When ED-402 draws real models through `BasicEffect`, that pass
 * has to apply the same mirror, or the models will disagree with everything around them.
 */

#include <optional>

#include "CNA/Editor/Core/EditorMatrix.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    /** @brief How the camera projects the world onto the viewport. */
    enum class CameraProjection
    {
        Perspective,
        Orthographic
    };

    /** @brief Returns the stable name of @p projection, as menus and settings use it. */
    [[nodiscard]] const char* toString(CameraProjection projection);

    /** @brief An axis-aligned box in world space. */
    struct WorldBounds3D
    {
        EditorVector3 min;
        EditorVector3 max;

        [[nodiscard]] bool isEmpty() const { return max.x < min.x || max.y < min.y || max.z < min.z; }

        /** @brief Returns the centre point. */
        [[nodiscard]] EditorVector3 getCenter() const
        {
            return EditorVector3{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
        }

        /** @brief Returns the full extent along each axis. */
        [[nodiscard]] EditorVector3 getSize() const { return subtract(max, min); }

        /** @brief Returns the radius of the sphere enclosing the box. */
        [[nodiscard]] float getRadius() const { return length(getSize()) * 0.5f; }

        /** @brief Returns true when @p point lies inside, faces included. */
        [[nodiscard]] bool contains(const EditorVector3& point) const
        {
            return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y
                   && point.z >= min.z && point.z <= max.z;
        }

        /** @brief Grows the box to include @p point. */
        void encapsulate(const EditorVector3& point);

        /** @brief Returns the union of two boxes, ignoring empty ones. */
        [[nodiscard]] static WorldBounds3D combine(const WorldBounds3D& a, const WorldBounds3D& b);

        /** @brief Returns an empty box, i.e. one that any point encapsulated into it defines. */
        [[nodiscard]] static WorldBounds3D makeEmpty();
    };

    /** @brief A ray in world space, as screen-to-world picking produces. */
    struct WorldRay
    {
        EditorVector3 origin;

        /** @brief Unit-length direction. */
        EditorVector3 direction{0.0f, 0.0f, -1.0f};

        /** @brief Returns the point @p distance along the ray. */
        [[nodiscard]] EditorVector3 at(float distance) const { return add(origin, scale(direction, distance)); }
    };

    /**
     * @brief A perspective or orthographic camera over a viewport of a given pixel size.
     *
     * The state is a pivot, a distance, a yaw and a pitch. The eye is *derived* from those rather
     * than stored beside them: two representations of where the camera is would be two things to
     * keep in step, and every orbit would be a chance for them to disagree.
     */
    class EditorCamera3D
    {
    public:
        /** @brief Pitch is clamped just inside vertical, so the view direction never becomes `up`. */
        static constexpr float kMaxPitchRadians = 1.5533431f;  // 89 degrees.

        /** @brief Orbit distance limits. Below the first, the near plane eats the pivot. */
        static constexpr float kMinDistance = 0.01f;
        static constexpr float kMaxDistance = 100000.0f;

        /** @brief The default vertical field of view, in radians (50 degrees). */
        static constexpr float kDefaultFieldOfView = 0.8726646f;

        /** @brief Returns the point the camera orbits and looks at. */
        [[nodiscard]] const EditorVector3& getPivot() const { return pivot_; }
        void setPivot(const EditorVector3& pivot) { pivot_ = pivot; }

        /** @brief Returns the distance from the eye to the pivot. */
        [[nodiscard]] float getDistance() const { return distance_; }

        /** @brief Sets the orbit distance, clamped to [kMinDistance, kMaxDistance]. */
        void setDistance(float distance);

        /** @brief Returns the yaw in radians: rotation about world Y, zero looking down -Z. */
        [[nodiscard]] float getYaw() const { return yaw_; }
        void setYaw(float radians);

        /** @brief Returns the pitch in radians, positive looking downward on screen. */
        [[nodiscard]] float getPitch() const { return pitch_; }

        /** @brief Sets the pitch, clamped to +/-kMaxPitchRadians. */
        void setPitch(float radians);

        /** @brief Returns the eye position, derived from the pivot, distance, yaw and pitch. */
        [[nodiscard]] EditorVector3 getEye() const;

        /** @brief Returns the unit vector the camera looks along. */
        [[nodiscard]] EditorVector3 getForward() const;

        /** @brief Returns the camera's unit right vector. */
        [[nodiscard]] EditorVector3 getRight() const;

        /** @brief Returns the camera's unit up vector, meaning up *on screen* (towards -Y). */
        [[nodiscard]] EditorVector3 getUp() const;

        [[nodiscard]] CameraProjection getProjection() const { return projection_; }
        void setProjection(CameraProjection projection) { projection_ = projection; }

        /** @brief Returns the vertical field of view in radians. Perspective only. */
        [[nodiscard]] float getFieldOfView() const { return fieldOfView_; }

        /** @brief Sets the vertical field of view, clamped to a usable range. */
        void setFieldOfView(float radians);

        /**
         * @brief Returns the world-space height the orthographic projection shows.
         *
         * Derived from the distance rather than stored, so that switching projection keeps the
         * subject the same size on screen: an orthographic view is the perspective one's extent at
         * the pivot, which is where the user is looking. A stored height would make the toggle a
         * jump cut, and the whole point of the toggle is to compare the same framing two ways.
         */
        [[nodiscard]] float getOrthographicHeight() const;

        [[nodiscard]] float getNearPlane() const { return nearPlane_; }

        /**
         * @brief Returns the distance along the view direction at which geometry becomes visible.
         *
         * Negative under the orthographic projection, whose near plane sits *behind* the eye --
         * see getProjectionMatrix(). Anything clipping against the near plane has to ask rather
         * than read `getNearPlane()`, or an orthographic view loses everything beside the camera.
         */
        [[nodiscard]] float getNearClipDistance() const;
        [[nodiscard]] float getFarPlane() const { return farPlane_; }

        /** @brief Sets the depth range. Ignored unless 0 < @p nearPlane < @p farPlane. */
        void setClipPlanes(float nearPlane, float farPlane);

        /** @brief Returns the viewport size in pixels. */
        [[nodiscard]] const EditorVector2& getViewportSize() const { return viewportSize_; }
        void setViewportSize(const EditorVector2& size) { viewportSize_ = size; }

        /** @brief Returns the view matrix. */
        [[nodiscard]] EditorMatrix getViewMatrix() const;

        /** @brief Returns the projection matrix for the current mode and viewport. */
        [[nodiscard]] EditorMatrix getProjectionMatrix() const;

        /** @brief Returns the view matrix multiplied by the projection matrix. */
        [[nodiscard]] EditorMatrix getViewProjectionMatrix() const;

        /**
         * @brief Projects @p world to viewport pixels, origin at the top-left.
         *
         * @return The screen point, or std::nullopt when the point is behind the eye -- where the
         *         perspective divide yields a coordinate that looks ordinary and is mirrored
         *         through the origin. A caller drawing a line has to know, or a vertex passing
         *         behind the camera sends its edge across the screen.
         */
        [[nodiscard]] std::optional<EditorVector2> worldToScreen(const EditorVector3& world) const;

        /** @brief Returns the world-space ray through @p screen, origin on the near plane. */
        [[nodiscard]] WorldRay screenToRay(const EditorVector2& screen) const;

        /**
         * @brief Orbits the eye about the pivot.
         *
         * @param yawRadians Rotation about world Y.
         * @param pitchRadians Rotation towards or away from vertical, clamped.
         */
        void orbit(float yawRadians, float pitchRadians);

        /**
         * @brief Turns the camera in place, keeping the eye and carrying the pivot around it.
         *
         * The fly-mode counterpart of orbit. The pivot follows so that a subsequent orbit turns
         * about what the user is now looking at rather than about wherever they were before.
         */
        void look(float yawRadians, float pitchRadians);

        /**
         * @brief Moves both eye and pivot by @p delta expressed in the camera's own axes.
         *
         * @param delta x is right, y is up, z is forward -- the axes a fly control speaks in.
         */
        void moveLocal(const EditorVector3& delta);

        /**
         * @brief Pans by a screen-space drag, in pixels, keeping the world under the cursor.
         *
         * Taking pixels rather than world units for the same reason `EditorCamera2D::panByScreenDelta`
         * does: converting on the caller's side is how a drag drifts away from the pointer.
         */
        void panByScreenDelta(const EditorVector2& screenDelta);

        /**
         * @brief Multiplies the orbit distance by @p factor, moving the eye towards the pivot.
         *
         * Dollying rather than changing the field of view: a narrowing field of view flattens the
         * scene, and a user zooming in to place something wants a closer look at it, not a
         * telephoto rendering of it.
         */
        void dolly(float factor);

        /** @brief Moves and zooms so that @p bounds fills the view with a margin. */
        void frame(const WorldBounds3D& bounds, float marginFraction = 0.1f);

    private:
        EditorVector3 pivot_;
        float distance_ = 10.0f;
        float yaw_ = 0.0f;
        // Straight at the scene plane, so entering the 3D view shows what the 2D one was showing
        // and the user orbits *away* from a picture they recognise rather than towards one.
        float pitch_ = 0.0f;
        float fieldOfView_ = kDefaultFieldOfView;
        float nearPlane_ = 0.1f;
        float farPlane_ = 5000.0f;
        CameraProjection projection_ = CameraProjection::Perspective;
        EditorVector2 viewportSize_{1280.0f, 720.0f};
    };

    /**
     * @brief Returns @p entityId's world-space 3D bounds, or std::nullopt when it has none.
     *
     * The 3D counterpart of `computeEntityBounds2D`, and until ED-402 lands a model renderer it
     * answers the same question in three dimensions: a sprite is a flat box in the XY plane, and
     * anything else with a transform is the small box that makes an icon clickable.
     */
    [[nodiscard]] std::optional<WorldBounds3D> computeEntityBounds3D(const SceneDocument& scene,
                                                                      const Uuid& entityId,
                                                                      const SpriteSizeProvider& sizeProvider);

    /** @brief Returns bounds covering @p entityId and all of its descendants. */
    [[nodiscard]] std::optional<WorldBounds3D> computeHierarchyBounds3D(const SceneDocument& scene,
                                                                         const Uuid& entityId,
                                                                         const SpriteSizeProvider& sizeProvider);

    /** @brief Returns bounds covering every entity in @p scene, or std::nullopt when it has none. */
    [[nodiscard]] std::optional<WorldBounds3D> computeSceneBounds3D(const SceneDocument& scene,
                                                                     const SpriteSizeProvider& sizeProvider);
}
