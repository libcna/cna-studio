// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/EditorCamera2D.hpp
 * @brief The 2D editor camera, and the world/screen conversions everything else depends on.
 *
 * This is the editor's *own* camera — never an entity in the scene, never serialised into the
 * scene file (ANALYSIS.md decision D-07). Where the user is looking is a property of the user, not
 * of the level.
 *
 * It lives in `cna-editor-scene` rather than in the viewport module on purpose: picking, framing,
 * grid spacing and gizmo hit-testing all need the same conversions, and keeping them CNA-free
 * means all of that is unit-testable with no window and no GPU.
 *
 * **Coordinate conventions.** World Y points *down*, matching XNA's `SpriteBatch` and every 2D
 * sprite coordinate a game will already be using; picking a different convention for the editor
 * would mean every value the inspector shows disagreed with the game. Screen coordinates are
 * pixels within the viewport panel, origin at its top-left.
 */

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    /**
     * @brief An orthographic 2D camera over a viewport of a given pixel size.
     *
     * Zoom is expressed as *pixels per world unit*: 1 means one world unit is one pixel, 2 means
     * the view is magnified twice. That is the quantity the grid, the gizmo handle sizes and the
     * scroll-wheel step all actually want, and deriving it from an "orthographic height" every
     * time invites disagreement about which is authoritative.
     */
    class EditorCamera2D
    {
    public:
        /** @brief The zoom limits. Beyond these, float precision starts to show in the grid. */
        static constexpr float kMinZoom = 0.02f;
        static constexpr float kMaxZoom = 64.0f;

        /** @brief Returns the world-space point at the centre of the view. */
        [[nodiscard]] const EditorVector2& getCenter() const { return center_; }
        void setCenter(const EditorVector2& center) { center_ = center; }

        /** @brief Returns the zoom in pixels per world unit. */
        [[nodiscard]] float getZoom() const { return zoom_; }

        /** @brief Sets the zoom, clamped to [kMinZoom, kMaxZoom]. */
        void setZoom(float zoom);

        /** @brief Returns the viewport size in pixels. */
        [[nodiscard]] const EditorVector2& getViewportSize() const { return viewportSize_; }
        void setViewportSize(const EditorVector2& size) { viewportSize_ = size; }

        /** @brief Converts a world point to viewport pixels. */
        [[nodiscard]] EditorVector2 worldToScreen(const EditorVector2& world) const;

        /** @brief Converts viewport pixels to a world point. */
        [[nodiscard]] EditorVector2 screenToWorld(const EditorVector2& screen) const;

        /** @brief Returns the world-space rectangle currently visible. */
        [[nodiscard]] WorldBounds2D getVisibleBounds() const;

        /**
         * @brief Pans by a screen-space delta, in pixels.
         *
         * Taking pixels rather than world units is what makes a drag track the cursor exactly at
         * any zoom — converting on the caller's side is the classic way to get drift.
         */
        void panByScreenDelta(const EditorVector2& screenDelta);

        /**
         * @brief Multiplies the zoom by @p factor, keeping @p screenAnchor over the same world point.
         *
         * Anchoring at the cursor is what makes wheel-zoom feel right: the thing under the pointer
         * stays under the pointer. Zooming about the view centre instead makes the user chase their
         * target across the screen.
         */
        void zoomAt(const EditorVector2& screenAnchor, float factor);

        /**
         * @brief Moves and zooms so that @p bounds fills the view with a margin.
         *
         * @param bounds World-space rectangle to frame. An empty rectangle only recentres.
         * @param marginFraction Fraction of the viewport left as padding, per side.
         */
        void frame(const WorldBounds2D& bounds, float marginFraction = 0.1f);

    private:
        EditorVector2 center_;
        EditorVector2 viewportSize_{1280.0f, 720.0f};
        float zoom_ = 1.0f;
    };

    /** @brief What a pick at a screen point found. */
    struct ScenePickResult
    {
        /** @brief The entity hit, or the nil Uuid when nothing was under the point. */
        Uuid entityId;

        /** @brief The world point tested. */
        EditorVector2 worldPoint;
    };

    /**
     * @brief Returns the topmost entity whose bounds contain @p screenPoint.
     *
     * "Topmost" means the smallest `SpriteRenderer.layerDepth`, matching XNA's convention that 0 is
     * front and 1 is back; ties break towards the entity later in the document, which is the one
     * drawn last and therefore the one visually on top.
     *
     * Ray casting against bounds rather than GPU picking, deliberately: it needs no render target
     * and no read-back, works headless, and is fast enough until scenes get large. See plan.md
     * ED-320 for when to revisit.
     */
    [[nodiscard]] ScenePickResult pickEntityAt(const SceneDocument& scene,
                                               const EditorCamera2D& camera,
                                               const EditorVector2& screenPoint,
                                               const SpriteSizeProvider& sizeProvider);

    /**
     * @brief Returns a world-space grid spacing that keeps lines about @p targetPixels apart.
     *
     * Here rather than in the renderer because two things need the same answer: the grid the user
     * *sees* and the grid a snapped drag lands on. Two implementations of "how far apart are the
     * lines" would be two chances for a snap to put an entity somewhere no line is drawn.
     *
     * A fixed world spacing is unusable -- zoomed out it becomes a solid block, zoomed in it
     * vanishes. Stepping through 1-2-5 decades is the standard answer and keeps the spacing a round
     * number, which matters because the user reads coordinates off it.
     */
    [[nodiscard]] float chooseGridSpacing(float zoom, float targetPixels);

    /** @brief The screen spacing the editor's grid and its snapping are both built around. */
    inline constexpr float kGridTargetPixels = 90.0f;
}
