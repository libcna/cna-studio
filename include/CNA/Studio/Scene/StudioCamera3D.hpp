// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Scene/StudioCamera3D.hpp
 * @brief The 3D editor camera: perspective or orthographic, orbit or fly (plan.md ED-400).
 *
 * The 3D counterpart of `StudioCamera2D`, and deliberately its twin in every structural respect.
 * It is the editor's *own* camera -- never an entity, never serialised into the scene (D-07) -- it
 * lives in `cna-studio-scene` rather than the viewport module, and it is CNA-free, so framing,
 * projection and picking are all unit-testable with no window and no GPU.
 *
 * **One camera, two navigation styles, no modes.** Orbit and fly are usually built as a mode flag
 * with two sets of state that drift apart; here they are two ways of moving *one* state --
 * @ref StudioCamera3D::getPivot "a pivot", a distance and a yaw/pitch pair. Orbiting turns the eye
 * about the pivot; flying moves the pivot and carries the eye with it. Every navigation call leaves
 * a consistent camera behind, so the user can orbit, fly, orbit again and never find the camera
 * spinning about a point it left minutes ago.
 *
 * **Coordinate conventions: Y grows downward**, exactly as in `StudioCamera2D` and in every
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

#include "CNA/Studio/Core/MeshData.hpp"
#include "CNA/Studio/Core/StudioMatrix.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"

namespace CNA::Studio
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
        StudioVector3 min;
        StudioVector3 max;

        [[nodiscard]] bool isEmpty() const { return max.x < min.x || max.y < min.y || max.z < min.z; }

        /** @brief Returns the centre point. */
        [[nodiscard]] StudioVector3 getCenter() const
        {
            return StudioVector3{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
        }

        /** @brief Returns the full extent along each axis. */
        [[nodiscard]] StudioVector3 getSize() const { return subtract(max, min); }

        /** @brief Returns the radius of the sphere enclosing the box. */
        [[nodiscard]] float getRadius() const { return length(getSize()) * 0.5f; }

        /** @brief Returns true when @p point lies inside, faces included. */
        [[nodiscard]] bool contains(const StudioVector3& point) const
        {
            return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y
                   && point.z >= min.z && point.z <= max.z;
        }

        /** @brief Grows the box to include @p point. */
        void encapsulate(const StudioVector3& point);

        /** @brief Returns the union of two boxes, ignoring empty ones. */
        [[nodiscard]] static WorldBounds3D combine(const WorldBounds3D& a, const WorldBounds3D& b);

        /** @brief Returns an empty box, i.e. one that any point encapsulated into it defines. */
        [[nodiscard]] static WorldBounds3D makeEmpty();
    };

    /** @brief A ray in world space, as screen-to-world picking produces. */
    struct WorldRay
    {
        StudioVector3 origin;

        /** @brief Unit-length direction. */
        StudioVector3 direction{0.0f, 0.0f, -1.0f};

        /** @brief Returns the point @p distance along the ray. */
        [[nodiscard]] StudioVector3 at(float distance) const { return add(origin, scale(direction, distance)); }
    };

    /**
     * @brief A perspective or orthographic camera over a viewport of a given pixel size.
     *
     * The state is a pivot, a distance, a yaw and a pitch. The eye is *derived* from those rather
     * than stored beside them: two representations of where the camera is would be two things to
     * keep in step, and every orbit would be a chance for them to disagree.
     */
    class StudioCamera3D
    {
    public:
        /**
         * @brief Pitch is clamped to straight up and straight down, which are both representable.
         *
         * A right angle exactly, not just inside one. It used to be 89 degrees, to keep the view
         * direction from becoming parallel to the reference up vector -- which `getRight` and
         * `getViewMatrix` divided by. Both are now written so that the pole is an ordinary point
         * rather than a singularity, so the clamp can be where a user expects it: orbiting stops
         * when you are looking straight down, and a Top view is a top view rather than one degree
         * short of one (`plan.md` STUDIO-11004).
         */
        static constexpr float kMaxPitchRadians = 1.5707964f;  // 90 degrees.

        /** @brief Orbit distance limits. Below the first, the near plane eats the pivot. */
        static constexpr float kMinDistance = 0.01f;
        static constexpr float kMaxDistance = 100000.0f;

        /** @brief The default vertical field of view, in radians (50 degrees). */
        static constexpr float kDefaultFieldOfView = 0.8726646f;

        /** @brief Returns the point the camera orbits and looks at. */
        [[nodiscard]] const StudioVector3& getPivot() const { return pivot_; }
        void setPivot(const StudioVector3& pivot) { pivot_ = pivot; }

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
        [[nodiscard]] StudioVector3 getEye() const;

        /** @brief Returns the unit vector the camera looks along. */
        [[nodiscard]] StudioVector3 getForward() const;

        /** @brief Returns the camera's unit right vector. */
        [[nodiscard]] StudioVector3 getRight() const;

        /** @brief Returns the camera's unit up vector, meaning up *on screen* (towards -Y). */
        [[nodiscard]] StudioVector3 getUp() const;

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
        [[nodiscard]] const StudioVector2& getViewportSize() const { return viewportSize_; }
        void setViewportSize(const StudioVector2& size) { viewportSize_ = size; }

        /** @brief Returns the view matrix. */
        [[nodiscard]] StudioMatrix getViewMatrix() const;

        /** @brief Returns the projection matrix for the current mode and viewport. */
        [[nodiscard]] StudioMatrix getProjectionMatrix() const;

        /** @brief Returns the view matrix multiplied by the projection matrix. */
        [[nodiscard]] StudioMatrix getViewProjectionMatrix() const;

        /**
         * @brief Projects @p world to viewport pixels, origin at the top-left.
         *
         * @return The screen point, or std::nullopt when the point is behind the eye -- where the
         *         perspective divide yields a coordinate that looks ordinary and is mirrored
         *         through the origin. A caller drawing a line has to know, or a vertex passing
         *         behind the camera sends its edge across the screen.
         */
        [[nodiscard]] std::optional<StudioVector2> worldToScreen(const StudioVector3& world) const;

        /** @brief Returns the world-space ray through @p screen, origin on the near plane. */
        [[nodiscard]] WorldRay screenToRay(const StudioVector2& screen) const;

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
        void moveLocal(const StudioVector3& delta);

        /**
         * @brief Pans by a screen-space drag, in pixels, keeping the world under the cursor.
         *
         * Taking pixels rather than world units for the same reason `StudioCamera2D::panByScreenDelta`
         * does: converting on the caller's side is how a drag drifts away from the pointer.
         */
        void panByScreenDelta(const StudioVector2& screenDelta);

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
        StudioVector3 pivot_;
        float distance_ = 10.0f;
        float yaw_ = 0.0f;
        // Straight at the scene plane, so entering the 3D view shows what the 2D one was showing
        // and the user orbits *away* from a picture they recognise rather than towards one.
        float pitch_ = 0.0f;
        float fieldOfView_ = kDefaultFieldOfView;
        float nearPlane_ = 0.1f;
        float farPlane_ = 5000.0f;
        CameraProjection projection_ = CameraProjection::Perspective;
        StudioVector2 viewportSize_{1280.0f, 720.0f};
    };

    /**
     * @brief One of the six axis-aligned views a 3D editor offers.
     *
     * Named for where the camera *is*, which is how every editor names them and the opposite of
     * how the view direction reads: the Front view looks backwards along -Z from in front of the
     * subject.
     */
    enum class StudioStandardView
    {
        Front,
        Back,
        Left,
        Right,
        Top,
        Bottom,
    };

    /** @brief Returns a stable English name for @p view, for menus, logs and tests. */
    [[nodiscard]] const char* toString(StudioStandardView view);

    /**
     * @brief Points @p camera along @p view's axis, keeping where it is and how far back.
     *
     * `plan.md` STUDIO-11004. Orientation only: the pivot and the distance are what the user
     * framed and a standard view is a question about *angle*, so changing them would make every
     * one of these six a navigation as well as a rotation. Focus Selected is the command that
     * moves the camera, and the two compose — focus, then Top, and the subject is still framed.
     *
     * The projection is left alone for the same reason, and that is the less obvious half. Some
     * editors switch to orthographic here, on the grounds that an axis-aligned view is usually
     * wanted for measuring; this does not, because the toggle exists separately and a command that
     * silently did two things would be one a user cannot undo half of.
     *
     * Top and Bottom set the yaw to zero rather than keeping it. The other four fix the yaw
     * anyway, and leaving it alone at the poles would make Top mean six different framings
     * depending on where the user happened to be orbiting — the point of a standard view is that
     * pressing it twice from different places gives the same picture.
     */
    void studioApplyStandardView(StudioCamera3D& camera, StudioStandardView view);

    /**
     * @brief Returns @p entityId's world-space 3D bounds, or std::nullopt when it has none.
     *
     * The 3D counterpart of `computeEntityBounds2D`. An imported model is measured by its mesh, a
     * sprite is a flat box in the XY plane, and anything else with a transform is the small box
     * that makes an icon clickable -- in that order, matching the order the viewport draws them
     * in, so what is clicked is what is seen.
     *
     * @param meshProvider Where an imported model's geometry comes from. Empty is the behaviour
     *        before `plan.md` STUDIO-11008: a model then measures as the icon-sized box, which is
     *        the same answer it gets when its mesh has not landed yet and is why this is a default
     *        rather than a required argument -- every existing caller keeps meaning what it meant.
     *
     *        Passing one is what a *viewport* should do, and the reason is that not passing one
     *        was a real defect: a model is drawn at whatever size its mesh is and was picked and
     *        framed against an eight-unit box at its origin, so clicking a large model missed it
     *        everywhere but the middle and Focus Selected flew the camera inside it.
     */
    [[nodiscard]] std::optional<WorldBounds3D> computeEntityBounds3D(const SceneDocument& scene,
                                                                      const Uuid& entityId,
                                                                      const SpriteSizeProvider& sizeProvider,
                                                                      const MeshProvider& meshProvider = {});

    /** @brief Returns bounds covering @p entityId and all of its descendants. */
    [[nodiscard]] std::optional<WorldBounds3D> computeHierarchyBounds3D(const SceneDocument& scene,
                                                                         const Uuid& entityId,
                                                                         const SpriteSizeProvider& sizeProvider,
                                                                         const MeshProvider& meshProvider = {});

    /** @brief Returns bounds covering every entity in @p scene, or std::nullopt when it has none. */
    [[nodiscard]] std::optional<WorldBounds3D> computeSceneBounds3D(const SceneDocument& scene,
                                                                     const SpriteSizeProvider& sizeProvider,
                                                                     const MeshProvider& meshProvider = {});

    /**
     * @brief Returns @p bounds transformed by @p matrix and re-bounded about the result.
     *
     * A rotated box is not a box, so the answer is the axis-aligned extent of the eight
     * transformed corners -- larger than the original whenever the rotation is not a multiple of a
     * quarter turn, which is correct and is the price of an axis-aligned bound. Transforming only
     * `min` and `max` would be the fast wrong answer: under any rotation those two corners no
     * longer span the shape, and the box comes out too small and off-centre.
     */
    [[nodiscard]] WorldBounds3D transformBounds3D(const WorldBounds3D& bounds,
                                                  const StudioMatrix& matrix);
}
