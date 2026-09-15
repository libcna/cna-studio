// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioViewportPanel.hpp
 * @brief Navigating and selecting in the native shell's viewport.
 *
 * `plan.md` STUDIO-07009.
 *
 * ### The panel does not draw the scene
 *
 * The scene arrives as a texture the shell composites (`STUDIO-04012`); this is everything *else* a
 * viewport is — the camera the pointer moves, and what a click in it selects. Keeping the two apart
 * is what lets this half be tested with no graphics device at all: navigation and picking are
 * arithmetic over a camera and a document, and neither needs a pixel.
 *
 * ### Gestures are the ones the prototype's viewport used
 *
 * Wheel zooms about the pointer rather than about the centre, because zooming about the centre
 * makes a user chase the thing they were looking at. Middle drag pans; so does right drag, because
 * a trackpad has no middle button. Left click selects, and a click that hits nothing clears the
 * selection — which is how a user deselects without a keyboard.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/StudioCamera2D.hpp"
#include "CNA/Studio/Scene/Tilemap.hpp"
#include "CNA/Studio/Scene/TransformGizmos.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include "CNA/Studio/Viewport/StudioViewport.hpp"

namespace CNA::Studio
{
    class StudioContext;

    /**
     * @brief The viewport's own retained state: which manipulator is showing, and any drag in it.
     *
     * Held by the caller rather than by the frame's widget store because a gizmo drag is *editor*
     * state, not widget state: it survives the panel being scrolled, re-docked or momentarily
     * hidden, and it is the thing an undo has to be able to reason about.
     */
    /**
     * @brief What a press in the native viewport means.
     *
     * Named apart from the prototype's `StudioTool`, which lives in the Dear ImGui panel headers
     * that `STUDIO-07030` deletes — and which shares this namespace. Two types of one name in
     * `CNA::Studio` is the ODR violation `STUDIO-02039` exists to refuse: it compiles, it links,
     * and it corrupts memory at run time hundreds of tests away from the cause.
     */
    enum class StudioViewportTool
    {
        /** @brief Pick entities and drag the gizmo. The default. */
        Select,
        /** @brief Set the tile under the cursor on the selected tilemap. */
        PaintTiles,
        /** @brief Clear the tile under the cursor. */
        EraseTiles,
        /** @brief Take the tile under the cursor as the brush, then go back to painting. */
        PickTile,
        /** @brief Fill the rectangle a drag encloses, applied on release. */
        FillTiles
    };

    /** @brief The display name of @p tool, e.g. `"Paint Tiles"`. */
    [[nodiscard]] std::string_view studioViewportToolName(StudioViewportTool tool);

    /** @brief Whether @p tool writes into a tilemap rather than selecting. */
    [[nodiscard]] bool studioViewportToolPaints(StudioViewportTool tool);

    struct StudioViewportState
    {
        /**
         * @brief What a press in the viewport means.
         *
         * Kept apart from @ref mode on purpose, exactly as the prototype keeps them: a gizmo mode
         * picks *which manipulator* acts on the selection, and a tool decides whether a press
         * manipulates at all. A single enum spanning both would make "paint tiles with the rotate
         * gizmo" expressible, which is not a thing.
         */
        StudioViewportTool tool = StudioViewportTool::Select;

        /** @brief The tile the paint and fill tools write. */
        std::int64_t paintTile = 0;

        /**
         * @brief Distinguishes one paint stroke from the next in the undo stack's merge key.
         *
         * Without it a drag across forty tiles and the drag after it would merge into one entry,
         * and undoing would jump back past a stroke the user had finished and accepted.
         */
        std::uint64_t paintStroke = 0;

        /** @brief Whether this stroke has already pushed a command, so the next cell merges in. */
        bool paintStrokeHasEdited = false;

        /** @brief Where a fill drag began, while one is in flight. */
        std::optional<TileCoordinate> fillStart;

        /** @brief Which manipulator the selection shows. */
        GizmoMode mode = GizmoMode::Translate;

        /** @brief Whether the translate gizmo's arms follow the world axes or the entity's own. */
        GizmoSpace space = GizmoSpace::World;

        TranslateGizmoDrag translate;
        RotateGizmoDrag rotate;
        ScaleGizmoDrag scale;

        /**
         * @brief The same gesture applied to a whole selection.
         *
         * Runs *beside* the three above rather than instead of them: those compute what the
         * gesture is — how far along an axis, through what angle, by what factor — and this turns
         * that one answer into an edit per entity. Two gesture implementations would be two
         * chances for the group and the entity under the cursor to disagree.
         */
        MultiTransformDrag multi;

        /**
         * @brief Distinguishes one multi-drag from the next in the undo stack's merge key.
         *
         * Without it, two consecutive group drags would merge into one undo entry — and undoing
         * would jump back past a gesture the user had already finished and accepted.
         */
        std::uint64_t multiDragId = 0;

        /**
         * @brief Whether this drag has already pushed a command.
         *
         * The first frame of a drag opens an undo entry and every frame after it merges into that
         * one, so the whole gesture is a single Ctrl+Z rather than one per frame at sixty a second.
         */
        bool dragHasEdited = false;

        /** @brief True while any manipulator is being dragged. */
        [[nodiscard]] bool dragging() const
        {
            return translate.isActive() || rotate.isActive() || scale.isActive();
        }

        /** @brief Ends whatever drag is in flight. */
        void endDrag()
        {
            translate.end();
            rotate.end();
            scale.end();
            multi.end();
            dragHasEdited = false;
        }
    };

    /** @brief What the user did in the viewport this frame. */
    /**
     * @brief Draws the armed tool's name, and its tile index where one applies.
     *
     * Over the image in the corner, which is where the prototype puts it — and it is the only
     * thing on the screen that says a press means something other than "select". A tool that
     * armed silently would be a viewport that behaves differently from yesterday with nothing
     * explaining why.
     *
     * Drawn only while a tile tool is armed: an overlay that was always there would be chrome over
     * the thing the viewport exists to show.
     *
     * @param frame The frame.
     * @param bounds The viewport's rectangle.
     * @param state The tool and its brush; the brush is written when the field commits.
     */
    void studioViewportToolOverlay(StudioFrame& frame, const UiRect& bounds,
                                   StudioViewportState& state);

    struct StudioViewportResult
    {
        /** @brief The camera moved, so the scene must be re-rendered. Input pass only. */
        bool cameraChanged = false;

        /** @brief The selection changed. Input pass only. */
        bool selectionChanged = false;

        /** @brief What was picked, or the nil id when the click hit nothing. Input pass only. */
        Uuid picked;

        /** @brief The world point under the pointer, for a status bar or a ruler. */
        StudioVector2 pointerWorld;

        /** @brief Whether the pointer is over the viewport at all. */
        bool pointerInside = false;

        /** @brief A manipulator moved the selection this frame. Input pass only. */
        bool transformed = false;

        /** @brief A tile tool wrote into the tilemap this frame. Input pass only. */
        bool tilesPainted = false;

        /** @brief The tool changed itself, which the eyedropper does. Input pass only. */
        bool toolChanged = false;
    };

    /**
     * @brief Drives the camera and the selection from input over @p bounds.
     *
     * @param frame The frame.
     * @param bounds The viewport panel's body, in window coordinates.
     * @param context The editor. Its scene is picked against; its selection is written.
     * @param camera The editor camera, panned and zoomed in place.
     * @param state Which manipulator is showing, and any drag in progress.
     * @param sizeProvider Resolves a sprite's texel size for picking. An empty provider makes
     *        every sprite pick at its default size, which is what a build with no device can know.
     * @return What happened.
     */
    StudioViewportResult studioViewportPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioCamera2D& camera,
                                             StudioViewportState& state,
                                             const SpriteSizeProvider& sizeProvider = {});

    /**
     * @brief Moves @p camera to frame the current selection.
     *
     * An entity with no drawable geometry — a camera, an empty grouping node — still has a
     * position, and framing it centres on that rather than doing nothing: a key that appears not
     * to work is worse than one that works modestly.
     *
     * @param context The editor, for the scene and the selection.
     * @param camera The camera to move.
     * @param sizeProvider Resolves sprite sizes, so a sprite frames to its extent rather than to
     *        a point.
     * @return False when nothing is selected, or when nothing selected could be located.
     */
    bool studioFrameSelection(const StudioContext& context, StudioCamera2D& camera,
                              const SpriteSizeProvider& sizeProvider = {});
}
