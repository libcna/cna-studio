// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/SceneWireframe.hpp
 * @brief What the 3D viewport draws, expressed as screen-space line segments (plan.md ED-400).
 *
 * The same split the gizmos use, and for the same reason: the *decisions* -- where the ground grid
 * goes, how far it extends, which entity gets a box, what is clipped against the near plane -- are
 * CNA-free and unit-tested in CI, and the renderer's job shrinks to calling `drawLine` in a loop.
 * A 3D viewport whose geometry could only be checked by looking at it would be a 3D viewport
 * nobody could check.
 *
 * This is deliberately *not* a 3D renderer. Until ED-402 brings a model pipeline there is no mesh
 * to draw, and a wireframe over the scene's bounds answers the question a 3D camera exists to ask:
 * where is everything, actually, in relation to everything else. Sprites are not drawn as textured
 * quads here because `SpriteBatch` cannot draw an arbitrary quad -- only a rotated rectangle -- and
 * a sprite seen from an angle is a trapezoid.
 */

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "CNA/Editor/Core/MeshData.hpp"
#include "CNA/Editor/Scene/EditorCamera3D.hpp"
#include "CNA/Editor/Scene/SceneLighting.hpp"
#include "CNA/Editor/Scene/EditorIcons.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    // `MeshProvider` comes from MeshData.hpp: it is the seam's own callback, and this module is one
    // of its consumers rather than its owner. The same relationship `SpriteSizeProvider` has with
    // the scene -- this module knows what a scene says, not what is on disk, and asks.

    /** @brief One line to draw, in viewport pixels. */
    struct WireSegment
    {
        EditorVector2 from;
        EditorVector2 to;
        EditorColor color;
        float thickness = 1.0f;
    };

    /** @brief The colours the 3D viewport draws with, kept beside the geometry that uses them. */
    namespace WireColors
    {
        /** @brief The ground grid. Dim enough to read the scene over. */
        inline constexpr EditorColor kGrid{70, 70, 78, 255};

        /** @brief Every tenth line, so distances stay readable when the grid is dense. */
        inline constexpr EditorColor kGridMajor{104, 104, 116, 255};

        /** @brief The world X axis. Red, as every 3D tool since the first one. */
        inline constexpr EditorColor kAxisX{196, 84, 84, 255};

        /** @brief The world Y axis. Green, matching the gizmo's Y arm. */
        inline constexpr EditorColor kAxisY{92, 170, 92, 255};

        /** @brief The world Z axis. Blue, and drawn only by the ground grid, where Z is in-plane. */
        inline constexpr EditorColor kAxisZ{84, 116, 196, 255};

        /** @brief An entity's bounding box. */
        inline constexpr EditorColor kEntity{130, 138, 150, 255};

        /** @brief A selected entity's bounding box. Matches the 2D viewport's selection colour. */
        inline constexpr EditorColor kSelected{255, 190, 60, 255};

        /**
         * @brief A light's direction arrow and range ring (ED-404).
         *
         * The same yellow the 2D viewport's light icon uses, so the arrow reads as belonging to the
         * badge it comes out of rather than as one more piece of scene geometry. Dimmer than the
         * badge, because a ring is a much longer line than an icon and the two at equal weight
         * would make the ring the loudest thing in a lit scene.
         */
        inline constexpr EditorColor kLight{190, 168, 84, 255};
    }

    /**
     * @brief Which plane the 3D grid is drawn on.
     *
     * Not a cosmetic choice: the grid is the only landmark a 3D view has, and it has to lie in the
     * plane the scene is actually laid out in. Everything this editor can place today lives in XY,
     * so that is the default; the moment ED-402 puts a model above a floor, a floor is what a user
     * needs to see it standing on.
     */
    enum class GridPlane
    {
        /**
         * @brief The scene's own XY plane at world Z = 0.
         *
         * Where sprites, tilemaps and the 2D camera's whole world live. On this plane a 3D camera
         * at yaw and pitch zero shows exactly what the 2D viewport shows, which is what makes the
         * two views recognisably the same scene.
         */
        SceneXY,

        /**
         * @brief A floor: the XZ plane at world Y = 0.
         *
         * Right for a scene with height in it, and wrong for a flat one -- an unrotated camera
         * looks along it edge-on and sees a single line where a flat scene's grid would be.
         */
        Ground
    };

    /** @brief Returns the display name of @p plane. */
    [[nodiscard]] const char* toString(GridPlane plane);

    /** @brief What to include in a wireframe. */
    struct WireframeOptions
    {
        /** @brief Draw the grid. */
        bool drawGrid = true;

        /** @brief Which plane to draw it on. */
        GridPlane gridPlane = GridPlane::SceneXY;

        /** @brief Draw a box per entity. */
        bool drawEntityBounds = true;

        /**
         * @brief Draw each light's direction and range (ED-404).
         *
         * On by default, and worth a switch because a scene lit by a dozen lamps is a dozen rings
         * over the geometry they light -- useful while aiming one and noise once they are all
         * aimed.
         */
        bool drawLightGizmos = true;

        /**
         * @brief World units between grid lines, or 0 to choose one from the camera's distance.
         *
         * Choosing rather than fixing, for the reason `chooseGridSpacing` exists: a fixed spacing
         * is a solid block when zoomed out and invisible when zoomed in.
         */
        float gridSpacing = 0.0f;

        /** @brief How many cells the grid extends from its centre, in each direction. */
        int gridHalfExtent = 24;

        /**
         * @brief Ceiling on the segments produced, so a large scene cannot stall a frame.
         *
         * Reached rather than approached silently: `WireframeResult::truncated` says so, and the
         * viewport reports it, because a wireframe that quietly stopped halfway through a scene
         * looks exactly like a scene with half its entities missing.
         */
        std::size_t maxSegments = 20000;

        /**
         * @brief Where a `ModelRenderer`'s geometry comes from, or empty to draw boxes as before.
         *
         * In the options rather than beside `sizeProvider` in the parameter list, which is where
         * its symmetry with that callback would put it. The reason is narrow and worth stating:
         * this field is additive and a parameter would not be, so every existing caller -- and
         * every test that pins the box-drawing behaviour -- keeps compiling and keeps meaning what
         * it meant. Empty is the pre-ED-405 behaviour exactly.
         */
        MeshProvider meshProvider;
    };

    /** @brief The segments to draw, and what had to be left out to produce them. */
    struct WireframeResult
    {
        std::vector<WireSegment> segments;

        /** @brief Entities whose box contributed at least one visible segment. */
        std::size_t entitiesDrawn = 0;

        /** @brief True when `maxSegments` stopped the build before the scene was exhausted. */
        bool truncated = false;
    };

    /**
     * @brief Projects the segment @p from -> @p to, clipping it against the near plane.
     *
     * @return The screen-space endpoints, or std::nullopt when the segment is entirely behind the
     *         camera. A segment with one endpoint behind is *shortened* rather than dropped: a
     *         grid line running under the camera is mostly visible, and dropping it whole leaves a
     *         wedge of missing floor exactly where the user is looking.
     */
    [[nodiscard]] std::optional<std::pair<EditorVector2, EditorVector2>> projectSegment(
        const EditorCamera3D& camera, const EditorVector3& from, const EditorVector3& to);

    /**
     * @brief Appends the lines that show where @p light points and how far it reaches (ED-404).
     *
     * The half of ED-404 that ED-402 did not do. The lighting itself is read by
     * `SceneLighting.hpp`, and a light whose effect can be seen but whose *aim* cannot is one a
     * user has to point by typing Euler angles and re-rendering. So: an arrow along the direction,
     * starting at the entity, and -- for a light that has a range -- one ring at that range.
     *
     * One ring in the scene's own plane rather than three about the three axes. Three describe the
     * sphere more completely and put two of them edge-on in the view this editor opens in, where
     * they collapse into lines through the middle of the badge. A directional light gets no ring at
     * all: it reaches everything, and a boundary the user can drag that means nothing is worse than
     * no boundary.
     *
     * @return How many segments were appended, which is at most @p budget.
     */
    std::size_t appendLightVisualisation(std::vector<WireSegment>& segments,
                                         const EditorCamera3D& camera, const SceneLight& light,
                                         const EditorColor& color, std::size_t budget);

    /**
     * @brief Returns the grid alone: the XY plane at world Z = 0, centred on the camera's pivot.
     *
     * The *scene's* plane, not a ground plane under it. Everything this editor can currently place
     * lives in XY -- sprites, tilemaps, the 2D camera's whole world -- so a grid on XZ would be a
     * floor beneath a scene that has no floor, and an unrotated 3D camera would look along it
     * edge-on and show nothing. On this plane, a 3D camera at yaw and pitch zero shows exactly what
     * the 2D viewport shows, which is what makes the two views recognisably the same scene.
     *
     * `options.gridPlane` chooses: the scene's plane by default, a floor for a scene with height
     * in it. One function either way, because the two differ by which pair of axes is in the plane
     * and nothing else -- a second function would be the same loop twice, free to drift.
     */
    [[nodiscard]] std::vector<WireSegment> buildSceneGrid(const EditorCamera3D& camera,
                                                          const WireframeOptions& options = {});

    /**
     * @brief Returns the screen-space badge for @p kind, centred on @p screenPoint.
     *
     * Drawn in pixels rather than in the world, exactly as the 2D viewport's icons are and for the
     * same reason: a camera has no size, so a badge scaled by distance would vanish at the far end
     * of a level and swallow the screen at the near end. Ten entities that draw nothing are ten
     * identical cubes without this -- and "which of these is the camera" is the first question a
     * 3D view of such a scene is asked.
     */
    [[nodiscard]] std::vector<WireSegment> buildIconBadge(EditorIconKind kind,
                                                          const EditorVector2& screenPoint,
                                                          const EditorColor& color);

    /**
     * @brief Appends @p mesh's triangle edges, placed by @p world, to @p segments.
     *
     * Each edge once rather than once per triangle that owns it: an interior edge is shared by two
     * faces, so drawing them naively doubles both the work and the apparent line weight, and a
     * dense model comes out looking like a solid blob.
     *
     * @param budget The most segments this call may add. When the mesh needs more, triangles are
     *        sampled at a stride so that what appears is the whole shape drawn sparsely rather
     *        than one corner of it drawn completely -- a wireframe that stopped at the budget would
     *        show a model with a bite taken out of it, which reads as broken geometry rather than
     *        as a full view. `outTruncated` is set when that happens.
     * @return The number of segments appended.
     */
    std::size_t appendMeshEdges(std::vector<WireSegment>& segments, const EditorCamera3D& camera,
                                const MeshData& mesh, const EditorMatrix& world,
                                const EditorColor& color, float thickness, std::size_t budget,
                                bool& outTruncated);

    /**
     * @brief Returns everything the 3D viewport draws for @p scene.
     *
     * @param selection Entities drawn in the selection colour, and drawn thicker so a selected box
     *        inside a cluster of others can still be told apart.
     * @param sizeProvider Supplies sprite dimensions, exactly as the 2D picking path does.
     */
    [[nodiscard]] WireframeResult buildSceneWireframe(const SceneDocument& scene,
                                                      const EditorCamera3D& camera,
                                                      const std::vector<Uuid>& selection,
                                                      const SpriteSizeProvider& sizeProvider,
                                                      const WireframeOptions& options = {});

    /**
     * @brief Returns the entity whose box is nearest the eye along the ray through @p screenPoint.
     *
     * The 3D counterpart of `pickEntityAt`, and the same trade: a ray against bounds rather than
     * GPU picking, so it needs no render target, no read-back, and works headless. "Nearest"
     * rather than "topmost", because depth is a real quantity here and layer order is not.
     *
     * @return The entity hit, or the nil Uuid when the ray missed everything.
     */
    [[nodiscard]] Uuid pickEntityAt3D(const SceneDocument& scene, const EditorCamera3D& camera,
                                      const EditorVector2& screenPoint,
                                      const SpriteSizeProvider& sizeProvider);

    /**
     * @brief Returns the distance along @p ray at which it enters @p bounds, if it does.
     *
     * The slab test. Exposed because picking is not its only caller -- framing a click and
     * dropping an asset into a 3D view both need to know where a ray meets a box.
     */
    [[nodiscard]] std::optional<float> intersectRayWithBounds(const WorldRay& ray,
                                                              const WorldBounds3D& bounds);
}
