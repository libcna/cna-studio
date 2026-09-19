// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Scene/TransformGizmos3D.hpp
 * @brief The translate, rotate and scale manipulators for the 3D viewport (ED-408, ED-409).
 *
 * The 3D counterpart of `TransformGizmos.hpp`, and split the same way: layout, hit-test and drag
 * are here, CNA-free and tested in CI, and the renderer is handed finished line segments. What a
 * user can grab is a decision, and decisions do not need a GPU.
 *
 * It is a separate file rather than a mode on the 2D gizmos because almost nothing is shared. The
 * 2D manipulators lay out *in screen space* against `StudioCamera2D` -- two arms, a fixed pixel
 * length, a screen-space drag -- while this one lays out in the world and answers a different
 * question each frame: where along this world-space line is the cursor pointing? The only genuine
 * overlap is `GizmoSnap`, which is reused rather than redeclared.
 *
 * All three manipulators are here. They do not share a solver, because each needs a different
 * thing from the world: translate needs a *line* to slide along, rotate needs a *plane* to measure
 * an angle in, and scale needs neither -- a scale factor is a unitless ratio, and the screen always
 * has one. That is why the edge-on case, which forces rotate to drop a ring altogether, costs scale
 * nothing but contrast.
 */

#include <array>
#include <optional>
#include <vector>

#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/Scene/SceneWireframe.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/TransformGizmos.hpp"

namespace CNA::Studio
{
    /** @brief Which arm of a 3D manipulator a cursor is over. */
    enum class GizmoAxis3D
    {
        None,
        X,
        Y,
        Z,

        /**
         * @brief All three axes at once. Only the scale gizmo has such a handle.
         *
         * Translating or rotating "along everything" means nothing -- there is no direction and no
         * plane -- but scaling by one factor on all three axes is the commonest scale of all, and
         * the 2D gizmo has had the same handle since ED-401.
         */
        All,

        /**
         * @brief The plane spanned by the X and Y arms. Only the translate gizmo has such handles.
         *
         * `plan.md` STUDIO-12001. A rotation is about an axis and a scale is along one, so neither
         * has a plane to offer; a *translation* in a plane is the commonest 3D move there is, and
         * without it sliding an object across a floor takes two drags along two arms and lands
         * wherever the second one stopped. The 2D gizmo has had its `Both` centre handle for the
         * same reason since ED-401 -- this is that handle, once there are three planes to choose.
         */
        XY,

        /** @brief The plane spanned by the Y and Z arms. Translate only. */
        YZ,

        /** @brief The plane spanned by the Z and X arms. Translate only. */
        ZX
    };

    /** @brief Returns true when @p axis names a plane rather than a single arm. */
    [[nodiscard]] constexpr bool isGizmoPlane(GizmoAxis3D axis)
    {
        return axis == GizmoAxis3D::XY || axis == GizmoAxis3D::YZ || axis == GizmoAxis3D::ZX;
    }

    /** @brief Returns the stable name of @p axis, for logs and tests. */
    [[nodiscard]] const char* toString(GizmoAxis3D axis);

    /**
     * @brief Where the 3D translate gizmo is, in the world and on the screen.
     *
     * Both, because the two halves need different ones: a drag is solved in the world, where the
     * axis is a line the cursor ray is measured against, while a *grab* is decided on the screen,
     * where "near the arm" means near in pixels however far away the entity is.
     */
    struct TranslateGizmo3DLayout
    {
        /** @brief The entity's world position: where the three arms meet. */
        StudioVector3 origin;

        /** @brief Unit world directions of the X, Y and Z arms, in that order. */
        std::array<StudioVector3, 3> axes{StudioVector3{1.0f, 0.0f, 0.0f}, StudioVector3{0.0f, 1.0f, 0.0f},
                                          StudioVector3{0.0f, 0.0f, 1.0f}};

        /** @brief Length of each arm in world units, chosen so it is a constant size on screen. */
        float armLength = 1.0f;

        /** @brief The origin in viewport pixels. */
        StudioVector2 screenOrigin;

        /** @brief Each arm's far end in viewport pixels. */
        std::array<StudioVector2, 3> screenTips{};

        /** @brief Whether each arm could be projected at all: an arm behind the eye cannot. */
        std::array<bool, 3> armVisible{true, true, true};

        /** @brief How far from an arm, in pixels, still counts as grabbing it. */
        float grabTolerance = 8.0f;

        /**
         * @brief One plane handle: a square offset from the origin along two of the three arms.
         *
         * Offset rather than cornered at the origin, and that is the whole of why the arms and the
         * planes do not fight each other for a press: the square starts a quarter of the way out,
         * so every pixel of an arm's own length is still the arm's.
         */
        struct Plane
        {
            /** @brief The axis *not* in the plane, which is the plane's normal. */
            StudioVector3 normal;

            /** @brief The two in-plane arm directions, for constraining and snapping a drag. */
            StudioVector3 u;
            StudioVector3 v;

            /** @brief The square's four world corners, in order round it. */
            std::array<StudioVector3, 4> corners{};

            /** @brief The same four in viewport pixels. */
            std::array<StudioVector2, 4> screenCorners{};

            /** @brief False when any corner is behind the eye: a half-projected quad is not a quad. */
            bool visible = false;
        };

        /**
         * @brief The XY, YZ and ZX handles, in that order.
         *
         * `plan.md` STUDIO-12001. Three rather than one, because which plane a user wants is the
         * question they are answering by reaching for one -- a single "screen plane" handle would
         * slide an object along whatever the camera happened to be looking at, which is not a
         * direction anything in the scene is laid out along.
         */
        std::array<Plane, 3> planes{};
    };

    /** @brief How far along an arm the plane handles begin, as a fraction of its length. */
    inline constexpr float kGizmo3DPlaneInner = 0.28f;

    /** @brief How far along an arm the plane handles end, as a fraction of its length. */
    inline constexpr float kGizmo3DPlaneOuter = 0.58f;

    /** @brief The on-screen length the arms are sized to. Matches the 2D gizmo's, so both feel alike. */
    inline constexpr float kGizmo3DScreenLength = 90.0f;

    /**
     * @brief Returns the layout for @p entityId, or nothing when it has no transform.
     *
     * @param space World points the arms along the world axes; Local points them along the
     *        entity's own, which is what a user placing something inside a rotated rig wants.
     * @param pivotWorld Where to put the manipulator, when that is not the entity's own position
     *        -- the shared pivot of a multi-selection. Where a gizmo is *drawn* is where it must be
     *        grabbed, so this belongs to the layout rather than to the drawing.
     */
    [[nodiscard]] std::optional<TranslateGizmo3DLayout> computeTranslateGizmo3DLayout(
        const SceneDocument& scene, const StudioCamera3D& camera, const Uuid& entityId,
        GizmoSpace space = GizmoSpace::World,
        const std::optional<StudioVector3>& pivotWorld = std::nullopt);

    /**
     * @brief Returns which arm @p screenPoint is over, or None.
     *
     * Nearest arm wins where two overlap on screen, which they do exactly when one is pointing
     * almost at the camera -- the case where a user cannot tell them apart either, and where
     * picking the first in some fixed order would feel arbitrary.
     */
    [[nodiscard]] GizmoAxis3D hitTestTranslateGizmo3D(const TranslateGizmo3DLayout& layout,
                                                      const StudioVector2& screenPoint);

    /**
     * @brief Returns the segments that draw @p layout, with @p active drawn highlighted.
     *
     * In the wireframe's own currency, so the 3D viewport draws its gizmo through exactly the path
     * it draws everything else through.
     */
    [[nodiscard]] std::vector<WireSegment> buildTranslateGizmo3DSegments(
        const TranslateGizmo3DLayout& layout, GizmoAxis3D active = GizmoAxis3D::None);

    /**
     * @brief Converts a world-space delta into the local delta an entity's position needs.
     *
     * The 3D form of `worldDeltaToLocal`. An entity's `position` is relative to its parent, so
     * dragging a child of a rotated or scaled parent one world unit is not a one-unit change to
     * what is stored -- the classic gizmo bug that works on roots and drifts on children.
     */
    [[nodiscard]] StudioVector3 worldDeltaToLocal3D(const SceneDocument& scene, const Uuid& entityId,
                                                    const StudioVector3& worldDelta);

    /**
     * @brief Returns the parameter along @p axis nearest to @p ray, or nothing when they are parallel.
     *
     * The whole of the drag's mathematics: an axis drag asks where along this line the user is
     * pointing, and a line and a ray that never converge have no answer -- which is exactly the
     * case of an arm pointing straight at the camera, where a pixel of cursor movement would
     * otherwise fling the entity across the level.
     */
    [[nodiscard]] std::optional<float> closestPointOnAxis(const WorldRay& ray, const StudioVector3& origin,
                                                          const StudioVector3& axis);

    /**
     * @brief Returns where @p ray meets the plane through @p origin with normal @p normal.
     *
     * The plane counterpart of `closestPointOnAxis`, and the whole of a plane drag's mathematics:
     * a plane drag asks where on this surface the user is pointing. Nothing when the ray is
     * parallel to the plane -- the case of a plane seen exactly edge-on, where a pixel of cursor
     * movement would otherwise fling the entity across the level, which is the same reason the axis
     * form returns nothing for an arm pointing at the camera.
     *
     * Also nothing when the meeting point is behind the eye: a ray pointed away from a plane still
     * meets it, mathematically, at a negative distance, and taking that answer would drag an object
     * to a mirror image of where the cursor is.
     */
    [[nodiscard]] std::optional<StudioVector3> intersectRayWithPlane(const WorldRay& ray,
                                                                     const StudioVector3& origin,
                                                                     const StudioVector3& normal);

    /**
     * @brief Where the 3D rotate gizmo is: three rings, sampled and projected.
     *
     * Sampled rather than described, because a circle in the world is an *ellipse* on screen and
     * the hit-test has to answer in pixels. A polyline of samples is one representation both the
     * drawing and the grab can use, so what a user sees is exactly what they can grab.
     */
    struct RotateGizmo3DLayout
    {
        /** @brief The entity's world position: the centre of all three rings. */
        StudioVector3 origin;

        /** @brief Unit world normals of the X, Y and Z rings, in that order. */
        std::array<StudioVector3, 3> axes{StudioVector3{1.0f, 0.0f, 0.0f}, StudioVector3{0.0f, 1.0f, 0.0f},
                                          StudioVector3{0.0f, 0.0f, 1.0f}};

        /** @brief Ring radius in world units, chosen so it is a constant size on screen. */
        float radius = 1.0f;

        /**
         * @brief Each ring's projected samples, in viewport pixels.
         *
         * Empty for a ring seen edge-on: it would project to a line through the centre, overlap
         * the other two and have no plane a drag could measure an angle in.
         */
        std::array<std::vector<StudioVector2>, 3> rings;

        /** @brief How far from a ring, in pixels, still counts as grabbing it. */
        float grabTolerance = 8.0f;
    };

    /** @brief How many points each ring is sampled at. Enough that the polyline reads as a circle. */
    inline constexpr int kRotateGizmo3DSamples = 48;

    /** @brief Returns the rotate layout for @p entityId, or nothing when it has no transform. */
    [[nodiscard]] std::optional<RotateGizmo3DLayout> computeRotateGizmo3DLayout(
        const SceneDocument& scene, const StudioCamera3D& camera, const Uuid& entityId,
        GizmoSpace space = GizmoSpace::World,
        const std::optional<StudioVector3>& pivotWorld = std::nullopt);

    /** @brief Returns which ring @p screenPoint is over, or None. Nearest ring wins. */
    [[nodiscard]] GizmoAxis3D hitTestRotateGizmo3D(const RotateGizmo3DLayout& layout,
                                                   const StudioVector2& screenPoint);

    /** @brief Returns the segments that draw @p layout, with @p active drawn highlighted. */
    [[nodiscard]] std::vector<WireSegment> buildRotateGizmo3DSegments(
        const RotateGizmo3DLayout& layout, GizmoAxis3D active = GizmoAxis3D::None);

    /** @brief Returns the rotation of @p radians about the unit axis @p axis. */
    [[nodiscard]] StudioQuaternion quaternionFromAxisAngle(const StudioVector3& axis, float radians);

    /**
     * @brief One in-progress 3D rotate drag.
     *
     * Turns in **world** space and stores the result in the parent's frame, exactly as the 2D
     * rotate gizmo does: the cursor is describing a world angle, and a child of a rotated parent
     * that applied that angle locally would turn by a rotated fraction of it.
     */
    class RotateGizmo3DDrag
    {
    public:
        [[nodiscard]] bool isActive() const { return axis_ != GizmoAxis3D::None; }
        [[nodiscard]] const Uuid& getEntityId() const { return entityId_; }
        [[nodiscard]] GizmoAxis3D getAxis() const { return axis_; }

        /** @brief Starts a drag on the ring under @p cursor. Returns true when one was grabbed. */
        bool begin(const SceneDocument& scene, const StudioCamera3D& camera,
                   const RotateGizmo3DLayout& layout, const Uuid& entityId,
                   const StudioVector2& cursor);

        /**
         * @brief Returns the entity's new *local* rotation, or nothing when it has not turned.
         *
         * The angle is measured from the press rather than accumulated frame to frame, and the
         * delta is wrapped into (-pi, pi] so dragging across the seam does not spin the entity.
         */
        [[nodiscard]] std::optional<StudioQuaternion> update(const SceneDocument& scene,
                                                             const StudioCamera3D& camera,
                                                             const StudioVector2& cursor,
                                                             const GizmoSnap& snap);

        /**
         * @brief Returns how far the drag has turned, in radians, or nothing when it has not.
         *
         * The *gesture*, as `TranslateGizmo3DDrag::getWorldDelta` is: what a whole selection turns
         * by, measured once, so twenty entities cannot each solve the cursor ray against their own
         * ring and drift apart.
         */
        [[nodiscard]] std::optional<float> getDeltaAngle(const StudioCamera3D& camera,
                                                         const StudioVector2& cursor,
                                                         const GizmoSnap& snap) const;

        /** @brief Returns the grabbed ring's world normal: the axis a selection turns about. */
        [[nodiscard]] const StudioVector3& getNormal() const { return normal_; }

        void end() { axis_ = GizmoAxis3D::None; }

    private:
        Uuid entityId_;
        GizmoAxis3D axis_ = GizmoAxis3D::None;

        StudioVector3 normal_;
        StudioVector3 origin_;

        /** @brief The in-plane basis the angle is measured in, fixed at the press. */
        StudioVector3 planeX_;
        StudioVector3 planeY_;

        float startAngle_ = 0.0f;

        /** @brief The entity's world rotation at the press: what the turn is applied on top of. */
        StudioQuaternion startWorld_;
    };

    /**
     * @brief Where the 3D scale gizmo is: three arms ending in handles, and one at the centre.
     *
     * The arms are **always the entity's own axes**, for the reason the 2D scale gizmo has no space
     * toggle either: a non-uniform scale in world space needs a shear, which a
     * position/rotation/scale transform cannot express, so a "world scale" would have to quietly do
     * something else in the one place a user is entitled to exact numbers.
     *
     * The edge-on case is what makes this a different shape from the other two. A ring seen edge-on
     * is dropped, because a rotation needs a plane to measure an angle in and an edge-on ring has
     * none; an arm pointing at the camera refuses a translate, because there is no line to slide
     * along. A scale needs neither: it is a ratio of screen distances, and the screen always has
     * one. So an arm here is never dropped and never refuses -- it **shortens to a floor** so its
     * handle stays clear of the centre one, and fades, which is the honest report that it has
     * little room left to say anything precise with.
     */
    struct ScaleGizmo3DLayout
    {
        /** @brief The entity's world position: where the three arms meet. */
        StudioVector3 origin;

        /** @brief Unit world directions of the entity's own X, Y and Z axes, in that order. */
        std::array<StudioVector3, 3> axes{StudioVector3{1.0f, 0.0f, 0.0f}, StudioVector3{0.0f, 1.0f, 0.0f},
                                          StudioVector3{0.0f, 0.0f, 1.0f}};

        /** @brief Length of each arm in world units, chosen so it is a constant size on screen. */
        float armLength = 1.0f;

        /** @brief The origin in viewport pixels. */
        StudioVector2 screenOrigin;

        /** @brief Where each arm's handle is drawn, in viewport pixels. */
        std::array<StudioVector2, 3> screenHandles{};

        /**
         * @brief Whether each arm has a direction on screen at all.
         *
         * False only for an axis pointing exactly through the eye, where the arm projects onto its
         * own origin and there is no direction to drag along -- not a foreshortened arm, which is
         * shortened and faded but still usable. Reaching it takes an exact alignment and a hair of
         * orbiting leaves it; the centre handle covers those pixels meanwhile.
         */
        std::array<bool, 3> armVisible{true, true, true};

        /**
         * @brief How much of its full screen length each arm has left, from 0 to 1.
         *
         * Drives the fade. It is the arm's own foreshortening rather than an angle to the camera,
         * because that is the quantity the user is affected by: a short arm is a coarse control.
         */
        std::array<float, 3> armFade{1.0f, 1.0f, 1.0f};

        /** @brief Half-extent of the square at the end of each arm. */
        float handleExtent = 6.0f;

        /** @brief Half-extent of the centre square, which scales all three axes together. */
        float centerExtent = 10.0f;

        /** @brief How far from an arm, in pixels, still counts as grabbing it. */
        float grabTolerance = 7.0f;
    };

    /**
     * @brief The shortest an arm is drawn, in pixels, however edge-on its axis is.
     *
     * Comfortably outside the centre handle, because the whole point of the floor is that the two
     * stay separately grabbable: an arm allowed to collapse onto the centre square is an axis the
     * user can see and cannot reach.
     */
    inline constexpr float kScaleGizmo3DMinimumArmPixels = 30.0f;

    /**
     * @brief Returns the scale layout for @p entityId, or nothing when it has no transform.
     *
     * Takes no `GizmoSpace`: scale is always local, so the arms always follow the entity's own
     * rotation.
     */
    [[nodiscard]] std::optional<ScaleGizmo3DLayout> computeScaleGizmo3DLayout(
        const SceneDocument& scene, const StudioCamera3D& camera, const Uuid& entityId,
        const std::optional<StudioVector3>& pivotWorld = std::nullopt);

    /**
     * @brief Returns which handle @p screenPoint is over, or None.
     *
     * The centre wins where it overlaps an arm, exactly as in 2D: it is the smaller target, it is
     * what a user aiming at the middle of the gizmo means, and the arms stay reachable along the
     * whole of the rest of their length.
     */
    [[nodiscard]] GizmoAxis3D hitTestScaleGizmo3D(const ScaleGizmo3DLayout& layout,
                                                  const StudioVector2& screenPoint);

    /** @brief Returns the segments that draw @p layout, with @p active drawn highlighted. */
    [[nodiscard]] std::vector<WireSegment> buildScaleGizmo3DSegments(
        const ScaleGizmo3DLayout& layout, GizmoAxis3D active = GizmoAxis3D::None);

    /**
     * @brief One in-progress 3D scale drag.
     *
     * Solved entirely on the screen, unlike the other two. Scale is unitless, so the only
     * zoom-independent measure of it is a *ratio* of screen distances -- drag a handle to twice the
     * distance it was grabbed at and the entity doubles, at any camera and any depth. That is also
     * what makes the foreshortened case survivable: a ratio along a short arm is still a ratio.
     */
    class ScaleGizmo3DDrag
    {
    public:
        [[nodiscard]] bool isActive() const { return axis_ != GizmoAxis3D::None; }
        [[nodiscard]] const Uuid& getEntityId() const { return entityId_; }
        [[nodiscard]] GizmoAxis3D getAxis() const { return axis_; }

        /**
         * @brief Starts a drag on the handle under @p cursor.
         *
         * @return False when nothing was grabbed, when the entity has no transform, or when the
         *         press landed too near the origin for a ratio to mean anything -- every factor
         *         below is a division by how far out the grab was.
         */
        bool begin(const SceneDocument& scene, const ScaleGizmo3DLayout& layout, const Uuid& entityId,
                   const StudioVector2& cursor);

        /** @brief Returns the entity's new *local* scale, or nothing when it has not changed. */
        [[nodiscard]] std::optional<StudioVector3> update(const ScaleGizmo3DLayout& layout,
                                                           const StudioVector2& cursor,
                                                           const GizmoSnap& snap) const;

        /**
         * @brief Returns the factor the drag describes, for a caller applying it to several things.
         *
         * The *gesture*, as `TranslateGizmo3DDrag::getWorldDelta` is: one quantity, so a selection
         * of twenty cannot disagree about how far the cursor went.
         */
        [[nodiscard]] float getFactor(const ScaleGizmo3DLayout& layout, const StudioVector2& cursor,
                                      const GizmoSnap& snap) const;

        void end() { axis_ = GizmoAxis3D::None; }

    private:
        Uuid entityId_;
        GizmoAxis3D axis_ = GizmoAxis3D::None;

        StudioVector3 startLocalScale_{1.0f, 1.0f, 1.0f};

        /** @brief The grabbed arm's unit screen direction, fixed at the press. */
        StudioVector2 direction_{1.0f, 0.0f};

        /** @brief How far out the grab was, along that direction. Every factor divides by it. */
        float grabDistance_ = 1.0f;
    };

    /**
     * @brief Returns the shared pivot for @p entityIds: the average of their world positions.
     *
     * The 3D form of `computeSelectionPivot`, and the same choice for the same reasons: the average
     * rather than the first entity's position, because a pivot that jumps as the selection order
     * changes is one a user cannot predict; and rather than the centre of the bounding box, because
     * that moves when an entity is merely *rotated*, with nothing having been asked to move.
     *
     * Returns nothing when no selected entity has a transform.
     */
    [[nodiscard]] std::optional<StudioVector3> computeSelectionPivot3D(const SceneDocument& scene,
                                                                        const std::vector<Uuid>& entityIds);

    /**
     * @brief The selection-wide half of a 3D drag.
     *
     * Runs *beside* the single-entity drags rather than instead of them, exactly as
     * `MultiTransformDrag` runs beside the 2D ones: those compute the gesture -- how far along the
     * axis, through what angle, by what factor -- and this turns one gesture into the edits a whole
     * selection needs. One quantity, many entities, so twenty of them cannot disagree about what
     * the cursor did.
     *
     * Rotate and scale carry their members *around* the pivot as well as changing them, which is
     * the difference between turning an arrangement and spinning each of its parts in place.
     */
    class MultiTransform3D
    {
    public:
        /**
         * @brief Captures @p entityIds, as they are now, about @p pivotWorld.
         *
         * Only the selection's *roots*: a child carried by a selected parent would otherwise be
         * transformed twice, once by its parent and once on its own account.
         *
         * @return False when nothing in the selection can be transformed.
         */
        bool begin(const SceneDocument& scene, const std::vector<Uuid>& entityIds,
                   const StudioVector3& pivotWorld);

        [[nodiscard]] bool isActive() const { return !entries_.empty(); }
        [[nodiscard]] std::size_t getEntityCount() const { return entries_.size(); }

        /** @brief Returns the pivot the gesture is measured about. */
        [[nodiscard]] const StudioVector3& getPivot() const { return pivot_; }

        /** @brief Returns the edits that move every captured entity by @p worldDelta. */
        [[nodiscard]] std::vector<EntityTransformEdit> translate(const SceneDocument& scene,
                                                                 const StudioVector3& worldDelta) const;

        /** @brief Returns the edits for a turn of @p radians about @p axis through the pivot. */
        [[nodiscard]] std::vector<EntityTransformEdit> rotate(const SceneDocument& scene,
                                                              const StudioVector3& axis,
                                                              float radians) const;

        /**
         * @brief Returns the edits for scaling by @p factor about the pivot, in the frame @p axes.
         *
         * The frame is the gizmo's own -- the arms the user actually grabbed -- because the offsets
         * from the pivot have to grow along the same directions the sizes do. Each member's own
         * scale then takes the factor axis for axis, which is exact when it shares that frame and
         * the closest a position/rotation/scale transform can come when it does not: the exact
         * answer there is a shear, which such a transform cannot hold.
         */
        [[nodiscard]] std::vector<EntityTransformEdit> scale(const SceneDocument& scene,
                                                             const std::array<StudioVector3, 3>& axes,
                                                             const StudioVector3& factor) const;

        void end() { entries_.clear(); }

    private:
        /** @brief One selected root, as it was when the drag began. */
        struct Entry
        {
            Uuid entityId;

            /** @brief Where the drag started, so every frame is measured from the press. */
            StudioVector3 startWorldPosition;
            StudioVector3 startLocal;
            StudioQuaternion startWorldRotation;
            StudioQuaternion inverseParentRotation;
            StudioVector3 startLocalScale{1.0f, 1.0f, 1.0f};
        };

        std::vector<Entry> entries_;
        StudioVector3 pivot_;
    };

    /**
     * @brief One in-progress 3D translate drag.
     *
     * Holds where the drag began, in world and local space, so every update is computed from the
     * press rather than from the previous frame: accumulating frame-to-frame deltas drifts, and
     * the drift is worst exactly when the pointer is moving fastest.
     */
    class TranslateGizmo3DDrag
    {
    public:
        [[nodiscard]] bool isActive() const { return axis_ != GizmoAxis3D::None; }
        [[nodiscard]] const Uuid& getEntityId() const { return entityId_; }
        [[nodiscard]] GizmoAxis3D getAxis() const { return axis_; }

        /**
         * @brief Starts a drag on the arm under @p cursor.
         *
         * @return True when an arm was grabbed, in which case the press belongs to the gizmo and
         *         must not also reselect whatever is behind it.
         */
        bool begin(const SceneDocument& scene, const StudioCamera3D& camera,
                   const TranslateGizmo3DLayout& layout, const Uuid& entityId,
                   const StudioVector2& cursor);

        /**
         * @brief Returns the entity's new *local* position, or nothing when it has not moved.
         *
         * Nothing rather than the unchanged value, so a drag that has not moved pushes no command
         * -- an undo entry restoring a value the entity already had costs the user an undo to
         * reach a change they can see.
         */
        [[nodiscard]] std::optional<StudioVector3> update(const SceneDocument& scene,
                                                          const StudioCamera3D& camera,
                                                          const StudioVector2& cursor,
                                                          const GizmoSnap& snap);

        /**
         * @brief Returns how far the world has moved since the press, or nothing when it has not.
         *
         * The *gesture*, which is what a multi-selection needs: update() answers "where does this
         * one entity go", and twenty entities asking that separately would each solve the ray
         * against their own position and drift apart.
         */
        [[nodiscard]] std::optional<StudioVector3> getWorldDelta(const StudioCamera3D& camera,
                                                                 const StudioVector2& cursor,
                                                                 const GizmoSnap& snap) const;

        void end() { axis_ = GizmoAxis3D::None; }

    private:
        Uuid entityId_;
        GizmoAxis3D axis_ = GizmoAxis3D::None;

        /** @brief The grabbed arm's world direction, fixed at the press. Unused for a plane. */
        StudioVector3 direction_;

        /** @brief The grabbed plane's normal and its two in-plane axes. Unused for an arm. */
        StudioVector3 normal_;
        StudioVector3 planeU_;
        StudioVector3 planeV_;

        StudioVector3 startWorld_;
        StudioVector3 startLocal_;

        /** @brief Where along the axis the press pointed, so the entity does not jump to the cursor. */
        float startParameter_ = 0.0f;

        /** @brief Where on the plane the press pointed, for the same reason. */
        StudioVector3 startHit_;
    };
}
