// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/TransformGizmos.hpp
 * @brief The translate, rotate and scale gizmos: geometry, hit-testing and drag state.
 *
 * CNA-free, like the camera and the picker, and for the same reason: the hard parts of a gizmo are
 * arithmetic, not rendering. Where the handles are, which one the cursor is over, and what world
 * delta a drag implies are all questions with exact answers that can be checked in CI. Only the
 * lines and squares need a graphics device, and those are the easy part.
 *
 * Handles are sized in **screen** pixels rather than world units, so a gizmo stays the same size
 * whatever the zoom. A gizmo that shrinks as you zoom out is one you cannot grab exactly when you
 * most need to.
 *
 * All three manipulators share one shape: a layout computed from the scene and the camera, a
 * hit-test against that same layout, and a drag that answers "what should this property be now"
 * without touching the document. Nothing here executes a command -- the panel does that, because
 * only the panel knows whether this is the first edit of a drag (a new undo entry) or a later one
 * (a merge into it).
 */

#include <optional>

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include <vector>

#include "CNA/Editor/Scene/EditorCamera2D.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief Which part of a gizmo the cursor is over. */
    enum class GizmoHandle
    {
        None,
        /** @brief The X arm: dragging acts along the entity's X axis only. */
        XAxis,
        /** @brief The Y arm: dragging acts along the entity's Y axis only. */
        YAxis,
        /** @brief The ring: dragging turns the entity about Z, the only axis 2D editing uses. */
        ZAxis,
        /** @brief The centre handle: dragging acts on both axes at once. */
        Both
    };

    /**
     * @brief Which frame of reference a gizmo's arms point along.
     *
     * The distinction only becomes visible once an entity is rotated: in `World` the arms are the
     * screen's own axes and a drag along X moves the entity right whatever it is doing; in `Local`
     * the arms follow the entity's own rotation, so the same drag moves it *forwards*. Both are
     * needed and neither is a superset -- laying out a level wants world, driving a rotated object
     * along its own axis wants local.
     *
     * Scale deliberately ignores this and is always local. A non-uniform scale applied in world
     * space is not representable in a position/rotation/scale transform at all -- it needs a shear
     * -- so a "world scale" that silently did something else would be a lie in the one place a
     * user is entitled to exact numbers.
     */
    enum class GizmoSpace
    {
        World,
        Local
    };

    /** @brief Returns the display name of @p space. */
    const char* toString(GizmoSpace space);

    /**
     * @brief How much a drag rounds by while the snap modifier is held.
     *
     * Zero on any field means that manipulator does not snap, which is what an unmodified drag
     * passes. Snapping is a *modifier* rather than a mode on purpose: the two are wanted a few
     * seconds apart -- line this up with that, now nudge it -- and a mode would have to be turned
     * off again every time.
     */
    struct GizmoSnap
    {
        /** @brief World units a translate lands on. Normally the visible grid spacing. */
        float translate = 0.0f;

        /** @brief Radians a rotation lands on. */
        float rotate = 0.0f;

        /** @brief Steps a scale factor lands on, e.g. 0.1 for tenths. */
        float scale = 0.0f;

        /** @brief Returns true when nothing at all is snapped. */
        [[nodiscard]] bool isEmpty() const
        {
            return translate <= 0.0f && rotate <= 0.0f && scale <= 0.0f;
        }
    };

    /** @brief Fifteen degrees, in radians: the rotation step every editor snaps to. */
    inline constexpr float kDefaultRotationSnap = 3.14159265358979323846f / 12.0f;

    /** @brief Tenths: fine enough to be useful, coarse enough to be a round number. */
    inline constexpr float kDefaultScaleSnap = 0.1f;

    /** @brief Returns @p value rounded to the nearest multiple of @p step, or unchanged when it is 0. */
    [[nodiscard]] float snapTo(float value, float step);

    /**
     * @brief The translate gizmo's screen-space layout. All values are pixels.
     *
     * The axis directions are stored rather than assumed, because in local space the arms follow
     * the entity's rotation. Storing them keeps the drawing, the hit-test and the drag reading the
     * same two vectors, which is what stops the arms drifting away from what they grab.
     */
    struct TranslateGizmoLayout
    {
        /** @brief The entity's world position, in screen pixels. */
        EditorVector2 origin;

        /** @brief Unit screen-space direction of the X arm. */
        EditorVector2 xAxis{1.0f, 0.0f};

        /** @brief Unit screen-space direction of the Y arm. */
        EditorVector2 yAxis{0.0f, 1.0f};

        /** @brief Length of each axis arm. */
        float axisLength = 72.0f;

        /** @brief Half-extent of the centre square. */
        float centerExtent = 11.0f;

        /** @brief How far from an arm still counts as grabbing it. */
        float grabTolerance = 7.0f;

        /** @brief Returns the far end of the X arm. */
        [[nodiscard]] EditorVector2 getXTip() const
        {
            return EditorVector2{origin.x + xAxis.x * axisLength, origin.y + xAxis.y * axisLength};
        }

        /** @brief Returns the far end of the Y arm. */
        [[nodiscard]] EditorVector2 getYTip() const
        {
            return EditorVector2{origin.x + yAxis.x * axisLength, origin.y + yAxis.y * axisLength};
        }
    };

    /** @brief The rotate gizmo's screen-space layout. All values are pixels. */
    struct RotateGizmoLayout
    {
        /** @brief The entity's world position, in screen pixels. */
        EditorVector2 origin;

        /** @brief Radius of the ring the cursor grabs. */
        float radius = 68.0f;

        /** @brief How far from the ring still counts as grabbing it. */
        float grabTolerance = 8.0f;

        /**
         * @brief The entity's current world rotation about Z, in radians.
         *
         * Drawn as a mark on the ring. Without it the ring is a circle, and a circle cannot show
         * that anything happened -- rotating a symmetrical sprite would give no feedback at all.
         */
        float angle = 0.0f;

        /** @brief Returns the point on the ring at @p radians. */
        [[nodiscard]] EditorVector2 getPointAt(float radians) const;
    };

    /**
     * @brief The scale gizmo's screen-space layout. All values are pixels.
     *
     * Same arms as the translate gizmo, ending in squares rather than arrowheads: the shape says
     * "this end goes in and out" rather than "this points somewhere", which is the whole difference
     * between the two tools.
     */
    struct ScaleGizmoLayout
    {
        EditorVector2 origin;
        EditorVector2 xAxis{1.0f, 0.0f};
        EditorVector2 yAxis{0.0f, 1.0f};

        float axisLength = 64.0f;

        /** @brief Half-extent of the square at the end of each arm. */
        float handleExtent = 6.0f;

        /** @brief Half-extent of the centre square, which scales both axes together. */
        float centerExtent = 11.0f;

        float grabTolerance = 7.0f;

        [[nodiscard]] EditorVector2 getXTip() const
        {
            return EditorVector2{origin.x + xAxis.x * axisLength, origin.y + xAxis.y * axisLength};
        }

        [[nodiscard]] EditorVector2 getYTip() const
        {
            return EditorVector2{origin.x + yAxis.x * axisLength, origin.y + yAxis.y * axisLength};
        }
    };

    /**
     * @brief Returns the translate gizmo layout for @p entityId, or nothing when it has no transform.
     */
    [[nodiscard]] std::optional<TranslateGizmoLayout> computeTranslateGizmoLayout(
        const SceneDocument& scene,
        const EditorCamera2D& camera,
        const Uuid& entityId,
        GizmoSpace space = GizmoSpace::World);

    /** @brief Returns the rotate gizmo layout for @p entityId, or nothing when it has no transform. */
    [[nodiscard]] std::optional<RotateGizmoLayout> computeRotateGizmoLayout(const SceneDocument& scene,
                                                                            const EditorCamera2D& camera,
                                                                            const Uuid& entityId);

    /**
     * @brief Returns the scale gizmo layout for @p entityId, or nothing when it has no transform.
     *
     * Takes no space: scale is always local, so the arms always follow the entity's own rotation.
     */
    [[nodiscard]] std::optional<ScaleGizmoLayout> computeScaleGizmoLayout(const SceneDocument& scene,
                                                                          const EditorCamera2D& camera,
                                                                          const Uuid& entityId);

    /**
     * @brief Returns which handle @p screenPoint is over.
     *
     * The centre square wins over the arms where they overlap: it is the smaller target and the
     * one a user aiming for "move freely" expects, and the arms remain reachable everywhere else
     * along their length.
     */
    [[nodiscard]] GizmoHandle hitTestTranslateGizmo(const TranslateGizmoLayout& layout,
                                                    const EditorVector2& screenPoint);

    /** @brief Returns GizmoHandle::ZAxis when @p screenPoint is on the ring, otherwise None. */
    [[nodiscard]] GizmoHandle hitTestRotateGizmo(const RotateGizmoLayout& layout,
                                                 const EditorVector2& screenPoint);

    /** @brief Returns which handle @p screenPoint is over, centre first for the same reason. */
    [[nodiscard]] GizmoHandle hitTestScaleGizmo(const ScaleGizmoLayout& layout,
                                                const EditorVector2& screenPoint);

    /**
     * @brief Converts a world-space delta into the local delta an entity's position needs.
     *
     * An entity's `position` property is relative to its parent, so dragging a child of a rotated
     * or scaled parent by one world unit is not a one-unit change to its stored position. Getting
     * this wrong makes a gizmo that works perfectly on root entities and drifts on every child.
     */
    [[nodiscard]] EditorVector2 worldDeltaToLocal(const SceneDocument& scene,
                                                  const Uuid& entityId,
                                                  const EditorVector2& worldDelta);

    /**
     * @brief One in-progress translate drag.
     *
     * Records where the drag began, in both screen and local space, so every update computes the
     * offset from the *original* position rather than accumulating frame deltas. Accumulating is
     * the classic source of gizmo drift: rounding error compounds, and the object slowly separates
     * from the cursor over a long drag. Every drag below is built the same way, for the same reason.
     */
    class TranslateGizmoDrag
    {
    public:
        /** @brief Returns true while a drag is in progress. */
        [[nodiscard]] bool isActive() const { return handle_ != GizmoHandle::None; }

        /** @brief Returns the handle being dragged. */
        [[nodiscard]] GizmoHandle getHandle() const { return handle_; }

        /** @brief Returns the entity being moved. */
        [[nodiscard]] const Uuid& getEntityId() const { return entityId_; }

        /**
         * @brief Starts a drag.
         *
         * @param scene The document the entity lives in.
         * @param camera The viewport camera, for the screen-to-world conversion.
         * @param entityId The entity to move.
         * @param handle The grabbed handle.
         * @param screenPoint Where the press landed.
         * @param space Which frame the arms point along; the constraint follows the arms.
         * @return False when @p entityId has no transform to move.
         */
        bool begin(const SceneDocument& scene,
                   const EditorCamera2D& camera,
                   const Uuid& entityId,
                   GizmoHandle handle,
                   const EditorVector2& screenPoint,
                   GizmoSpace space = GizmoSpace::World);

        /**
         * @brief Returns the entity's new local position for a cursor now at @p screenPoint.
         *
         * Axis-constrained by the grabbed handle. Returns std::nullopt when no drag is active.
         */
        [[nodiscard]] std::optional<EditorVector3> update(const SceneDocument& scene,
                                                          const EditorCamera2D& camera,
                                                          const EditorVector2& screenPoint,
                                                          const GizmoSnap& snap = {}) const;

        /**
         * @brief Returns the world-space movement the drag describes, before it is made local.
         *
         * The quantity a *multi-selection* needs: every selected entity moves by the same world
         * delta, and each turns it into its own local units. Exposed rather than recomputed so one
         * drag and many entities cannot disagree about how far the cursor went.
         */
        [[nodiscard]] EditorVector2 getWorldDelta(const EditorCamera2D& camera,
                                                  const EditorVector2& screenPoint,
                                                  const GizmoSnap& snap = {}) const;

        /** @brief Ends the drag. */
        void end() { handle_ = GizmoHandle::None; }

    private:
        GizmoHandle handle_ = GizmoHandle::None;
        Uuid entityId_;
        EditorVector2 grabWorld_;
        EditorVector3 startLocalPosition_;

        /** @brief The world-space direction a single-axis drag is constrained to. */
        EditorVector2 constraintAxis_{1.0f, 0.0f};
    };

    /** @brief One in-progress rotate drag. */
    class RotateGizmoDrag
    {
    public:
        [[nodiscard]] bool isActive() const { return active_; }
        [[nodiscard]] const Uuid& getEntityId() const { return entityId_; }

        /**
         * @brief Starts a drag from the angle @p screenPoint makes with the ring's centre.
         *
         * @return False when the entity has no transform, or when the press landed on the centre
         *         itself -- there is no angle there, and a drag started from one would jump.
         */
        bool begin(const SceneDocument& scene,
                   const RotateGizmoLayout& layout,
                   const Uuid& entityId,
                   const EditorVector2& screenPoint);

        /**
         * @brief Returns the entity's new local rotation for a cursor now at @p screenPoint.
         *
         * The turn is applied in **world** space and then expressed in the entity's parent frame,
         * so a child of a rotated parent follows the cursor rather than turning by some rotated
         * fraction of it.
         */
        [[nodiscard]] std::optional<EditorQuaternion> update(const RotateGizmoLayout& layout,
                                                             const EditorVector2& screenPoint,
                                                             const GizmoSnap& snap = {}) const;

        /** @brief Returns the turn so far in radians, for showing the user a number. */
        [[nodiscard]] float getDeltaAngle(const RotateGizmoLayout& layout,
                                          const EditorVector2& screenPoint,
                                          const GizmoSnap& snap = {}) const;

        /** @brief Ends the drag. */
        void end() { active_ = false; }

    private:
        bool active_ = false;
        Uuid entityId_;
        float startPointerAngle_ = 0.0f;
        EditorQuaternion startWorldRotation_;
        EditorQuaternion inverseParentRotation_;
    };

    /** @brief One in-progress scale drag. */
    class ScaleGizmoDrag
    {
    public:
        [[nodiscard]] bool isActive() const { return handle_ != GizmoHandle::None; }
        [[nodiscard]] GizmoHandle getHandle() const { return handle_; }
        [[nodiscard]] const Uuid& getEntityId() const { return entityId_; }

        /**
         * @brief Starts a drag.
         *
         * @return False when the entity has no transform, or when the press landed too close to
         *         the origin for a ratio to mean anything -- the factor is a division by how far
         *         out the grab was, and a grab at the centre would scale by infinity.
         */
        bool begin(const SceneDocument& scene,
                   const ScaleGizmoLayout& layout,
                   const Uuid& entityId,
                   GizmoHandle handle,
                   const EditorVector2& screenPoint);

        /**
         * @brief Returns the entity's new local scale for a cursor now at @p screenPoint.
         *
         * Measured as a ratio in **screen** space, which is exactly right for a unitless quantity:
         * dragging a handle to twice its distance doubles the scale at any zoom.
         */
        [[nodiscard]] std::optional<EditorVector3> update(const ScaleGizmoLayout& layout,
                                                          const EditorVector2& screenPoint,
                                                          const GizmoSnap& snap = {}) const;

        /** @brief Returns the factor the drag describes, for a caller applying it to several things. */
        [[nodiscard]] float getFactor(const ScaleGizmoLayout& layout,
                                      const EditorVector2& screenPoint,
                                      const GizmoSnap& snap = {}) const;

        /** @brief Ends the drag. */
        void end() { handle_ = GizmoHandle::None; }

    private:
        GizmoHandle handle_ = GizmoHandle::None;
        Uuid entityId_;
        EditorVector3 startLocalScale_{1.0f, 1.0f, 1.0f};

        /** @brief How far the grab was from the origin, along the handle's own direction. */
        float grabDistance_ = 1.0f;
    };

    /**
     * @brief Moves @p layout's origin onto @p pivotWorld.
     *
     * How a gizmo ends up on a *shared* pivot: the layout is computed for the primary selection as
     * usual, then placed. Everything else about it -- arm directions, handle sizes, the hit-test --
     * is unchanged, which is what keeps one code path for one entity and for twenty.
     */
    template <typename Layout>
    void placeGizmoAt(Layout& layout, const EditorCamera2D& camera, const EditorVector2& pivotWorld)
    {
        layout.origin = camera.worldToScreen(pivotWorld);
    }

    /**
     * @brief Returns the shared pivot for @p entityIds: the average of their world positions.
     *
     * What a gizmo on a multi-selection sits at. The average rather than the first entity's
     * position, because a pivot that jumps as the selection order changes is one a user cannot
     * predict; and rather than the centre of the bounding box, because that moves when an entity is
     * *rotated* without anything having been asked to move.
     *
     * Returns nothing when no selected entity has a transform.
     */
    [[nodiscard]] std::optional<EditorVector2> computeSelectionPivot(const SceneDocument& scene,
                                                                     const std::vector<Uuid>& entityIds);

    /**
     * @brief Returns the entities in @p entityIds that no other selected entity is an ancestor of.
     *
     * A child moves when its parent does. Applying a drag to both would move it twice -- once by
     * the parent's transform and once by its own -- which is why every editor transforms the roots
     * of a selection rather than all of it.
     */
    [[nodiscard]] std::vector<Uuid> findSelectionRoots(const SceneDocument& scene,
                                                       const std::vector<Uuid>& entityIds);

    /**
     * @brief Turns one gesture into the edits it implies for a whole selection.
     *
     * Captures each root's starting transform at `begin()` and answers every later question from
     * that, so a long drag cannot drift and an undo returns to exactly where the drag started --
     * the same rule the single-entity drags follow, applied to a set.
     */
    class MultiTransformDrag
    {
    public:
        /**
         * @brief Captures the starting state of @p entityIds around @p pivotWorld.
         * @return False when nothing in the selection can be transformed.
         */
        bool begin(const SceneDocument& scene,
                   const std::vector<Uuid>& entityIds,
                   const EditorVector2& pivotWorld);

        [[nodiscard]] bool isActive() const { return !entries_.empty(); }

        /** @brief Returns the pivot the gesture is measured about. */
        [[nodiscard]] const EditorVector2& getPivot() const { return pivot_; }

        /** @brief Returns how many entities the drag will move. */
        [[nodiscard]] std::size_t getEntityCount() const { return entries_.size(); }

        /** @brief Returns the edits for a translation of @p worldDelta. */
        [[nodiscard]] std::vector<EntityTransformEdit> translate(const SceneDocument& scene,
                                                                 const EditorVector2& worldDelta) const;

        /**
         * @brief Returns the edits for a turn of @p radians about the pivot.
         *
         * Both halves: an entity away from the pivot is carried around it as well as turned, which
         * is what makes rotating a group behave like rotating one object rather than like spinning
         * each of them in place.
         */
        [[nodiscard]] std::vector<EntityTransformEdit> rotate(const SceneDocument& scene,
                                                              float radians) const;

        /**
         * @brief Returns the edits for scaling by @p factor about the pivot.
         *
         * Again both halves: distances from the pivot scale with the entities, or a group scaled up
         * would overlap itself.
         */
        [[nodiscard]] std::vector<EntityTransformEdit> scale(const SceneDocument& scene,
                                                             const EditorVector2& factor) const;

        void end() { entries_.clear(); }

    private:
        /** @brief One selected root, as it was when the drag began. */
        struct Entry
        {
            Uuid entityId;
            EditorVector2 startWorldPosition;
            EditorVector3 startLocalPosition;
            EditorQuaternion startWorldRotation;
            EditorQuaternion inverseParentRotation;
            EditorVector3 startLocalScale{1.0f, 1.0f, 1.0f};
        };

        std::vector<Entry> entries_;
        EditorVector2 pivot_;
    };
}
