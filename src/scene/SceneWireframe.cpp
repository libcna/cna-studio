// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Scene/SceneWireframe.hpp"

#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "CNA/Studio/Core/StudioMatrix.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/StudioCamera2D.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/SceneLighting.hpp"
#include "CNA/Studio/Scene/SceneModels.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns the signed distance from the eye to @p point along the view direction. */
        float depthOf(const StudioCamera3D& camera, const StudioVector3& point)
        {
            return dot(subtract(point, camera.getEye()), camera.getForward());
        }

        /** @brief Returns the point @p fraction of the way from @p from to @p to. */
        StudioVector3 interpolate(const StudioVector3& from, const StudioVector3& to, float fraction)
        {
            return add(from, scale(subtract(to, from), fraction));
        }

        /** @brief Appends the twelve edges of @p bounds to @p out, clipped and projected. */
        std::size_t appendBox(std::vector<WireSegment>& out, const StudioCamera3D& camera,
                              const WorldBounds3D& bounds, const StudioColor& color, float thickness)
        {
            const StudioVector3 corners[8] = {
                {bounds.min.x, bounds.min.y, bounds.min.z}, {bounds.max.x, bounds.min.y, bounds.min.z},
                {bounds.max.x, bounds.min.y, bounds.max.z}, {bounds.min.x, bounds.min.y, bounds.max.z},
                {bounds.min.x, bounds.max.y, bounds.min.z}, {bounds.max.x, bounds.max.y, bounds.min.z},
                {bounds.max.x, bounds.max.y, bounds.max.z}, {bounds.min.x, bounds.max.y, bounds.max.z}};

            // Bottom face, top face, then the four uprights.
            static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                                 {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

            std::size_t drawn = 0;
            for (const auto& edge : edges)
            {
                const std::optional<std::pair<StudioVector2, StudioVector2>> projected =
                    projectSegment(camera, corners[edge[0]], corners[edge[1]]);
                if (!projected) { continue; }
                out.push_back(WireSegment{projected->first, projected->second, color, thickness});
                ++drawn;
            }
            return drawn;
        }

        /**
         * @brief Appends three rings describing the smallest sphere that contains @p bounds.
         *
         * The sphere `BoundingSphere::CreateFromBoundingBox` builds -- centred on the box and
         * reaching its furthest corner -- because that is the sphere a CNA game gets when it turns
         * a bounding box into a bounding sphere, and drawing any other one would be showing the
         * user a volume their collision test does not use.
         *
         * Three rings, about the three world axes, and this is the one place the reasoning differs
         * from `appendLightVisualisation`, which draws exactly one and says why. A light's ring is
         * a *boundary the user aims*, and two of three rings collapsing edge-on in the editor's
         * opening view would leave two lines through the middle of the badge. A sphere is a volume
         * being inspected: the collapsed rings are its silhouette from that angle, which is the
         * truth about a sphere and is legible rather than confusing.
         *
         * @return How many segments were appended, at most @p budget.
         */
        std::size_t appendBoundingSphere(std::vector<WireSegment>& out, const StudioCamera3D& camera,
                                         const WorldBounds3D& bounds, const StudioColor& color,
                                         float thickness, std::size_t budget)
        {
            if (budget == 0) { return 0; }

            const StudioVector3 center = bounds.getCenter();
            const float radius = length(subtract(bounds.max, center));
            if (!(radius > 0.0f)) { return 0; }

            // Twenty-four a ring rather than the light's thirty-two: three rings at that rate is
            // ninety-six segments for one entity, and the overlay can be on for a whole scene.
            constexpr std::size_t kRingSamples = 24;
            constexpr float kTwoPi = 6.28318530717958647692f;

            std::size_t drawn = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                StudioVector3 previous;
                for (std::size_t i = 0; i <= kRingSamples; ++i)
                {
                    const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(kRingSamples);
                    const float u = std::cos(angle) * radius;
                    const float v = std::sin(angle) * radius;

                    StudioVector3 point = center;
                    if (axis == 0) { point.x += u; point.y += v; }
                    else if (axis == 1) { point.x += u; point.z += v; }
                    else { point.y += u; point.z += v; }

                    if (i > 0)
                    {
                        if (drawn >= budget) { return drawn; }
                        if (const auto projected = projectSegment(camera, previous, point))
                        {
                            out.push_back(
                                WireSegment{projected->first, projected->second, color, thickness});
                            ++drawn;
                        }
                    }
                    previous = point;
                }
            }

            return drawn;
        }

        /**
         * @brief Returns how many world units one viewport pixel spans at @p point's depth.
         *
         * The same arithmetic `TransformGizmos3D` uses to size a gizmo arm, and here for the same
         * reason: a light's arrow has to be legible in a scene laid out in hundreds of units and in
         * one laid out in single figures, and only a screen-space size is both.
         */
        float worldUnitsPerPixelNear(const StudioCamera3D& camera, const StudioVector3& point)
        {
            const StudioVector2 viewport = camera.getViewportSize();
            if (viewport.y <= 0.0f) { return 1.0f; }

            if (camera.getProjection() == CameraProjection::Orthographic)
            {
                return camera.getOrthographicHeight() / viewport.y;
            }

            const float depth = dot(subtract(point, camera.getEye()), camera.getForward());
            const float usable = std::max(depth, camera.getNearPlane());
            return 2.0f * usable * std::tan(camera.getFieldOfView() * 0.5f) / viewport.y;
        }

        /** @brief Returns two unit vectors spanning the plane whose normal is @p normal. */
        std::pair<StudioVector3, StudioVector3> makePlaneBasisForLight(const StudioVector3& normal)
        {
            const StudioVector3 seed = std::abs(normal.x) < 0.9f ? StudioVector3{1.0f, 0.0f, 0.0f}
                                                                 : StudioVector3{0.0f, 1.0f, 0.0f};
            const StudioVector3 planeX = normalize(cross(seed, normal));
            return {planeX, normalize(cross(normal, planeX))};
        }

    }

    std::size_t appendLightVisualisation(std::vector<WireSegment>& segments,
                                        const StudioCamera3D& camera, const SceneLight& light,
                                        const StudioColor& color, std::size_t budget)
    {
        if (budget == 0) { return 0; }

        std::size_t drawn = 0;
        const auto append = [&](const StudioVector3& from, const StudioVector3& to)
        {
            if (drawn >= budget) { return; }
            if (const auto projected = projectSegment(camera, from, to))
            {
                segments.push_back(WireSegment{projected->first, projected->second, color, 1.0f});
                ++drawn;
            }
        };

        // A directional light has no position that matters, so what is worth drawing is the one
        // thing a user can change and cannot otherwise see: which way it points. The line starts at
        // the entity, because that is where the badge and the gizmo are.
        //
        // Sized in *pixels* and converted to world units at the light's own depth, exactly as the
        // 3D manipulators size their arms. A fixed world length cannot work: this editor's scenes
        // are laid out in pixel-like units running to the hundreds, and the same constant that
        // reads well in a scene measured in metres is invisible in one measured in sprites.
        constexpr float kDirectionPixels = 70.0f;
        const float kDirectionLength = kDirectionPixels * worldUnitsPerPixelNear(camera, light.position);
        const StudioVector3 tip = add(light.position, scale(light.direction, kDirectionLength));
        append(light.position, tip);

        // A small arrowhead, so the line reads as an arrow rather than as an edge of something.
        const auto [armX, armY] = makePlaneBasisForLight(light.direction);
        const float kHead = kDirectionLength * 0.18f;
        const StudioVector3 back = add(light.position, scale(light.direction, kDirectionLength - kHead));
        append(tip, add(back, scale(armX, kHead * 0.5f)));
        append(tip, add(back, scale(armX, -kHead * 0.5f)));
        append(tip, add(back, scale(armY, kHead * 0.5f)));
        append(tip, add(back, scale(armY, -kHead * 0.5f)));

        // Only a light that *has* a range gets a circle. A directional light reaches everything,
        // and a ring around one would be a boundary the user could move that means nothing.
        if (light.kind == SceneLightKind::Directional || light.range <= 0.0f) { return drawn; }

        // One ring in the scene's own plane rather than three about the three axes. Three would
        // describe the sphere more completely and would also put two rings edge-on in the view
        // this editor opens in, where they collapse to lines through the middle of the badge.
        constexpr std::size_t kRingSamples = 32;
        StudioVector3 previous{light.position.x + light.range, light.position.y, light.position.z};
        for (std::size_t i = 1; i <= kRingSamples; ++i)
        {
            const float angle = 2.0f * 3.14159265358979323846f * static_cast<float>(i)
                                / static_cast<float>(kRingSamples);
            const StudioVector3 point{light.position.x + std::cos(angle) * light.range,
                                      light.position.y + std::sin(angle) * light.range,
                                      light.position.z};
            append(previous, point);
            previous = point;
        }

        return drawn;
    }

    /**
     * @brief Draws @p mesh's silhouette as seen from @p camera: its outline, not its wireframe.
     *
     * `plan.md` STUDIO-11007. An edge is on the silhouette when the two triangles sharing it face
     * opposite ways -- one towards the camera and one away -- or when only one triangle claims it
     * at all, which is the rim of an open shell. Everything else is interior detail that an
     * outline is not about.
     *
     * The distinction matters because recolouring every edge is what this replaces, and on
     * anything denser than a crate that does not read as a selection: it reads as the object
     * turning into a solid block of the selection colour. An outline stays an outline however many
     * triangles are behind it, and it costs *fewer* segments the denser the mesh gets relative to
     * drawing all of them.
     *
     * @return How many segments were appended.
     */
    std::size_t appendMeshSilhouette(std::vector<WireSegment>& segments, const StudioCamera3D& camera,
                                     const MeshData& mesh, const StudioMatrix& world,
                                     const StudioColor& color, float thickness, std::size_t budget,
                                     bool& outTruncated)
    {
        if (budget == 0)
        {
            outTruncated = true;
            return 0;
        }

        // Under an orthographic projection every triangle is seen along the same direction; under
        // a perspective one it is seen from a point. Using the eye for both would put the
        // silhouette in the wrong place on an orthographic view, which is the view somebody lining
        // geometry up is most likely to be in.
        const bool orthographic = camera.getProjection() == CameraProjection::Orthographic;
        const StudioVector3 eye = camera.getEye();
        const StudioVector3 forward = camera.getForward();

        /** @brief How the two triangles sharing one edge face, as far as we have seen. */
        struct EdgeFacing
        {
            std::uint32_t from = 0;
            std::uint32_t to = 0;
            int faces = 0;
            int frontFaces = 0;
        };

        std::size_t drawn = 0;
        for (const MeshPart& part : mesh.parts)
        {
            // Per part, for the reason `appendMeshEdges` clears its set per part: indices are
            // part-local, so a key built from them is only unique within one.
            std::unordered_map<std::uint64_t, EdgeFacing> edges;

            for (std::size_t triangle = 0; triangle + 2 < part.indices.size(); triangle += 3)
            {
                const std::uint32_t corner[3] = {part.indices[triangle], part.indices[triangle + 1],
                                                 part.indices[triangle + 2]};
                if (corner[0] >= part.vertices.size() || corner[1] >= part.vertices.size()
                    || corner[2] >= part.vertices.size())
                {
                    continue;
                }

                const StudioVector3 a = transformPosition(world, part.vertices[corner[0]].position);
                const StudioVector3 b = transformPosition(world, part.vertices[corner[1]].position);
                const StudioVector3 c = transformPosition(world, part.vertices[corner[2]].position);

                // The importer guarantees counter-clockwise seen from outside, and reverses the
                // winding to keep that through its Y mirror -- so this cross product points out of
                // the surface and `dot` against the view direction says which way the face turns.
                const StudioVector3 normal = cross(subtract(b, a), subtract(c, a));
                const StudioVector3 view =
                    orthographic
                        ? forward
                        : subtract(StudioVector3{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f,
                                                 (a.z + b.z + c.z) / 3.0f},
                                   eye);
                const bool front = dot(normal, view) < 0.0f;

                for (int edge = 0; edge < 3; ++edge)
                {
                    const std::uint32_t from = corner[edge];
                    const std::uint32_t to = corner[(edge + 1) % 3];
                    const std::uint64_t key = (static_cast<std::uint64_t>(std::min(from, to)) << 32)
                                              | static_cast<std::uint64_t>(std::max(from, to));

                    EdgeFacing& facing = edges[key];
                    if (facing.faces == 0)
                    {
                        facing.from = from;
                        facing.to = to;
                    }
                    ++facing.faces;
                    facing.frontFaces += front ? 1 : 0;
                }
            }

            for (const auto& [key, facing] : edges)
            {
                (void)key;

                // One triangle means an open rim, which is part of the outline. Two that disagree
                // is the silhouette proper. Two that agree is interior detail, and three or more
                // is a mesh whose topology is not a surface -- left out rather than guessed at.
                const bool boundary = facing.faces == 1;
                const bool crossing = facing.faces == 2 && facing.frontFaces == 1;
                if (!boundary && !crossing) { continue; }

                if (drawn >= budget)
                {
                    outTruncated = true;
                    return drawn;
                }

                const std::optional<std::pair<StudioVector2, StudioVector2>> projected =
                    projectSegment(camera, transformPosition(world, part.vertices[facing.from].position),
                                   transformPosition(world, part.vertices[facing.to].position));
                if (!projected) { continue; }

                segments.push_back(WireSegment{projected->first, projected->second, color, thickness});
                ++drawn;
            }
        }

        return drawn;
    }

    std::size_t appendMeshEdges(std::vector<WireSegment>& segments, const StudioCamera3D& camera,
                                const MeshData& mesh, const StudioMatrix& world,
                                const StudioColor& color, float thickness, std::size_t budget,
                                bool& outTruncated)
    {
        if (budget == 0)
        {
            outTruncated = true;
            return 0;
        }

        // Three edges per triangle before deduplication. A closed mesh shares almost every edge
        // between two faces, so the real count is nearer half that -- but sizing the stride off
        // the optimistic figure would blow the budget on exactly the open, shell-like models that
        // share fewest edges.
        const std::size_t triangles = mesh.getTriangleCount();
        const std::size_t stride = triangles * 3 > budget ? (triangles * 3 + budget - 1) / budget : 1;
        if (stride > 1) { outTruncated = true; }

        std::size_t drawn = 0;
        for (const MeshPart& part : mesh.parts)
        {
            // Per part rather than per mesh: indices are part-local, so a key built from them is
            // only unique within one. Clearing per part costs nothing and is what makes the key
            // correct.
            std::unordered_set<std::uint64_t> seen;

            for (std::size_t triangle = 0; triangle + 2 < part.indices.size(); triangle += 3 * stride)
            {
                const std::uint32_t corner[3] = {part.indices[triangle], part.indices[triangle + 1],
                                                 part.indices[triangle + 2]};
                if (corner[0] >= part.vertices.size() || corner[1] >= part.vertices.size()
                    || corner[2] >= part.vertices.size())
                {
                    continue;
                }

                for (int edge = 0; edge < 3; ++edge)
                {
                    const std::uint32_t from = corner[edge];
                    const std::uint32_t to = corner[(edge + 1) % 3];

                    // Ordered low-to-high, so that the same edge reached from either of the two
                    // triangles that share it produces the same key.
                    const std::uint64_t key = (static_cast<std::uint64_t>(std::min(from, to)) << 32)
                                              | static_cast<std::uint64_t>(std::max(from, to));
                    if (!seen.insert(key).second) { continue; }

                    if (drawn >= budget)
                    {
                        outTruncated = true;
                        return drawn;
                    }

                    const std::optional<std::pair<StudioVector2, StudioVector2>> projected =
                        projectSegment(camera,
                                       transformPosition(world, part.vertices[from].position),
                                       transformPosition(world, part.vertices[to].position));
                    if (!projected) { continue; }

                    segments.push_back(
                        WireSegment{projected->first, projected->second, color, thickness});
                    ++drawn;
                }
            }
        }

        return drawn;
    }

    std::optional<std::pair<StudioVector2, StudioVector2>> projectSegment(const StudioCamera3D& camera,
                                                                          const StudioVector3& from,
                                                                          const StudioVector3& to)
    {
        // A hair in front of the near plane, not on it: a point exactly on the plane divides by a
        // w of zero, and the resulting coordinate is an infinity that draws a line to nowhere.
        const float nearDistance = camera.getNearClipDistance() + 1e-4f;

        float fromDepth = depthOf(camera, from);
        float toDepth = depthOf(camera, to);

        if (fromDepth < nearDistance && toDepth < nearDistance) { return std::nullopt; }

        StudioVector3 clippedFrom = from;
        StudioVector3 clippedTo = to;

        if (fromDepth < nearDistance)
        {
            clippedFrom = interpolate(from, to, (nearDistance - fromDepth) / (toDepth - fromDepth));
        }
        else if (toDepth < nearDistance)
        {
            clippedTo = interpolate(to, from, (nearDistance - toDepth) / (fromDepth - toDepth));
        }

        const std::optional<StudioVector2> screenFrom = camera.worldToScreen(clippedFrom);
        const std::optional<StudioVector2> screenTo = camera.worldToScreen(clippedTo);
        if (!screenFrom || !screenTo) { return std::nullopt; }

        return std::make_pair(*screenFrom, *screenTo);
    }

    std::vector<WireSegment> buildIconBadge(StudioIconKind kind, const StudioVector2& screenPoint,
                                            const StudioColor& color)
    {
        std::vector<WireSegment> segments;
        if (kind == StudioIconKind::None) { return segments; }

        const float extent = kStudioIconExtent;
        const auto at = [&screenPoint](float x, float y) {
            return StudioVector2{screenPoint.x + x, screenPoint.y + y};
        };
        const auto line = [&segments, &color](const StudioVector2& from, const StudioVector2& to) {
            segments.push_back(WireSegment{from, to, color, 1.0f});
        };
        const auto box = [&line, &at](float halfWidth, float halfHeight) {
            line(at(-halfWidth, -halfHeight), at(halfWidth, -halfHeight));
            line(at(halfWidth, -halfHeight), at(halfWidth, halfHeight));
            line(at(halfWidth, halfHeight), at(-halfWidth, halfHeight));
            line(at(-halfWidth, halfHeight), at(-halfWidth, -halfHeight));
        };

        switch (kind)
        {
            case StudioIconKind::Camera:
                // A body and the lens cone beside it: the silhouette everything from a film camera
                // to a viewport widget uses, and recognisable at thirteen pixels.
                box(extent * 0.6f, extent * 0.5f);
                line(at(extent * 0.6f, -extent * 0.5f), at(extent, -extent * 0.85f));
                line(at(extent, -extent * 0.85f), at(extent, extent * 0.85f));
                line(at(extent, extent * 0.85f), at(extent * 0.6f, extent * 0.5f));
                break;

            case StudioIconKind::Light:
                // A point with rays. Four is enough to read as a light and few enough that a scene
                // full of them is still a scene rather than a haystack.
                box(extent * 0.35f, extent * 0.35f);
                line(at(0.0f, -extent * 0.55f), at(0.0f, -extent));
                line(at(0.0f, extent * 0.55f), at(0.0f, extent));
                line(at(-extent * 0.55f, 0.0f), at(-extent, 0.0f));
                line(at(extent * 0.55f, 0.0f), at(extent, 0.0f));
                break;

            case StudioIconKind::AudioSource:
                // A cone opening to the right, with one wavefront in front of it.
                line(at(-extent * 0.7f, -extent * 0.35f), at(-extent * 0.7f, extent * 0.35f));
                line(at(-extent * 0.7f, -extent * 0.35f), at(0.0f, -extent * 0.8f));
                line(at(-extent * 0.7f, extent * 0.35f), at(0.0f, extent * 0.8f));
                line(at(0.0f, -extent * 0.8f), at(0.0f, extent * 0.8f));
                line(at(extent * 0.5f, -extent * 0.5f), at(extent * 0.5f, extent * 0.5f));
                break;

            case StudioIconKind::Model:
                // A cube drawn flat: a wireframe box in *screen* space, which reads as "a mesh
                // belongs here" without pretending to be the mesh, since ED-402 has not landed.
                box(extent * 0.75f, extent * 0.75f);
                line(at(-extent * 0.75f, -extent * 0.75f), at(-extent * 0.35f, -extent));
                line(at(extent * 0.75f, -extent * 0.75f), at(extent, -extent));
                line(at(-extent * 0.35f, -extent), at(extent, -extent));
                line(at(extent, -extent), at(extent, extent * 0.35f));
                line(at(extent, extent * 0.35f), at(extent * 0.75f, extent * 0.75f));
                break;

            case StudioIconKind::Empty:
            {
                // A cross rather than an outline: a marker is a *place*, and a shape with an
                // interior reads as an object occupying space. The diagonals make it legible
                // against the grid, whose lines are axis-aligned -- an upright cross on a grid
                // line disappears into it.
                const float arm = extent * 0.8f;
                line(at(-arm, -arm), at(arm, arm));
                line(at(-arm, arm), at(arm, -arm));
                break;
            }

            case StudioIconKind::None: break;
        }

        return segments;
    }

    const char* toString(BoundsDisplay display)
    {
        switch (display)
        {
            case BoundsDisplay::None: return "Off";
            case BoundsDisplay::Selected: return "Selected";
            case BoundsDisplay::All: return "All";
        }
        return "Off";
    }

    const char* toString(GridPlane plane)
    {
        switch (plane)
        {
            case GridPlane::SceneXY: return "Scene Plane";
            case GridPlane::Ground: return "Ground Plane";
        }
        return "Scene Plane";
    }

    std::vector<WireSegment> buildSceneGrid(const StudioCamera3D& camera, const WireframeOptions& options)
    {
        std::vector<WireSegment> segments;
        if (options.gridHalfExtent <= 0) { return segments; }

        float spacing = options.gridSpacing;
        if (spacing <= 0.0f)
        {
            // The same decade stepping the 2D grid uses, fed the pixels-per-world-unit the camera
            // achieves at its pivot. One answer to "how far apart are the lines", so a 2D and a 3D
            // view of one scene do not disagree about what a grid square means.
            const float height = camera.getOrthographicHeight();
            const float pixelsPerUnit =
                height > 0.0f ? camera.getViewportSize().y / height : 1.0f;
            spacing = chooseGridSpacing(pixelsPerUnit, kGridTargetPixels);
        }
        if (spacing <= 0.0f) { return segments; }

        // The two axes that lie *in* the plane. X is in both, so only the second one differs, and
        // the loop below never mentions a plane again.
        const bool ground = options.gridPlane == GridPlane::Ground;
        const auto pointAt = [ground](float u, float v) {
            return ground ? StudioVector3{u, 0.0f, v} : StudioVector3{u, v, 0.0f};
        };

        // Centred on the pivot and snapped to the spacing, so flying across a level does not drag
        // the grid's origin along and turn the lines into a shimmering mess.
        const float centerU = std::round(camera.getPivot().x / spacing) * spacing;
        const float centerV =
            std::round((ground ? camera.getPivot().z : camera.getPivot().y) / spacing) * spacing;

        const int extent = options.gridHalfExtent;
        const float half = static_cast<float>(extent) * spacing;

        // One piece is the old behaviour exactly: a whole line, one colour, no fade.
        const int pieces = std::max(1, options.gridFadeSteps);
        const float fadeStart = std::clamp(options.gridFadeStart, 0.0f, 1.0f);

        /**
         * @brief How strongly to draw a point at (@p offsetU, @p offsetV) from the grid's centre.
         *
         * Radial, so the square of lines reads as a disc that dissolves rather than a plate with a
         * bright edge. Full strength inside `gridFadeStart` of the radius and zero at the rim,
         * linear between -- a curve would be a parameter nobody has a reason to choose.
         */
        const auto fadeAt = [&](float offsetU, float offsetV) {
            if (fadeStart <= 0.0f || half <= 0.0f) { return 1.0f; }

            const float distance = std::sqrt(offsetU * offsetU + offsetV * offsetV);
            const float inner = fadeStart * half;
            if (distance <= inner) { return 1.0f; }
            if (distance >= half) { return 0.0f; }
            return 1.0f - (distance - inner) / (half - inner);
        };

        const auto withFade = [](StudioColor color, float fade) {
            color.a = static_cast<std::uint8_t>(
                std::lround(static_cast<float>(color.a) * std::clamp(fade, 0.0f, 1.0f)));
            return color;
        };

        for (int step = -extent; step <= extent; ++step)
        {
            const float offset = static_cast<float>(step) * spacing;
            const float u = centerU + offset;
            const float v = centerV + offset;

            // The world axes win over the grid, and every tenth line over an ordinary one. Without
            // that a user cannot tell where the origin is, which is the one landmark a 3D view has.
            const bool uIsAxis = std::abs(u) < spacing * 0.5f;
            const bool vIsAxis = std::abs(v) < spacing * 0.5f;
            const bool isMajor = (step % 10) == 0;

            // Named for the axis each line *runs along*, which is the axis it is when it passes
            // through the origin: down the middle of the ground plane that is Z, and of the
            // scene's own plane, Y.
            const StudioColor inPlaneAxis = ground ? WireColors::kAxisZ : WireColors::kAxisY;

            const StudioColor alongV =
                uIsAxis ? inPlaneAxis : (isMajor ? WireColors::kGridMajor : WireColors::kGrid);
            const StudioColor alongU =
                vIsAxis ? WireColors::kAxisX : (isMajor ? WireColors::kGridMajor : WireColors::kGrid);

            // Each line is cut into pieces so it can fade along its length: a `WireSegment` carries
            // one colour, so a single segment running from the centre to the rim can only be one
            // strength the whole way (`plan.md` STUDIO-11005).
            for (int piece = 0; piece < pieces; ++piece)
            {
                const float t0 = static_cast<float>(piece) / static_cast<float>(pieces);
                const float t1 = static_cast<float>(piece + 1) / static_cast<float>(pieces);

                const float v0 = centerV - half + t0 * (2.0f * half);
                const float v1 = centerV - half + t1 * (2.0f * half);
                const float u0 = centerU - half + t0 * (2.0f * half);
                const float u1 = centerU - half + t1 * (2.0f * half);

                // Measured at the piece's midpoint, which is what makes the strength vary along the
                // line rather than only between lines.
                const float fadeAlongV = fadeAt(u - centerU, (v0 + v1) * 0.5f - centerV);
                const float fadeAlongU = fadeAt((u0 + u1) * 0.5f - centerU, v - centerV);

                if (fadeAlongV > 0.0f)
                {
                    const std::optional<std::pair<StudioVector2, StudioVector2>> lineAlongV =
                        projectSegment(camera, pointAt(u, v0), pointAt(u, v1));
                    if (lineAlongV)
                    {
                        segments.push_back(WireSegment{lineAlongV->first, lineAlongV->second,
                                                       withFade(alongV, fadeAlongV),
                                                       uIsAxis ? 2.0f : 1.0f});
                    }
                }

                if (fadeAlongU > 0.0f)
                {
                    const std::optional<std::pair<StudioVector2, StudioVector2>> lineAlongU =
                        projectSegment(camera, pointAt(u0, v), pointAt(u1, v));
                    if (lineAlongU)
                    {
                        segments.push_back(WireSegment{lineAlongU->first, lineAlongU->second,
                                                       withFade(alongU, fadeAlongU),
                                                       vIsAxis ? 2.0f : 1.0f});
                    }
                }
            }
        }

        return segments;
    }

    WireframeResult buildSceneWireframe(const SceneDocument& scene, const StudioCamera3D& camera,
                                        const std::vector<Uuid>& selection,
                                        const SpriteSizeProvider& sizeProvider,
                                        const WireframeOptions& options)
    {
        WireframeResult result;

        if (options.drawGrid) { result.segments = buildSceneGrid(camera, options); }

        if (!options.drawEntityBounds) { return result; }

        // Collected once for the whole frame rather than read per entity: `collectSceneLights`
        // already walks the scene, and doing it inside the loop would make it a walk per entity.
        // Keyed by entity so the loop can ask "is this one of them" without a second search order.
        std::unordered_map<Uuid, SceneLight> lights;
        if (options.drawLightGizmos)
        {
            for (const SceneLight& light : collectSceneLights(scene))
            {
                lights.emplace(light.entityId, light);
            }
        }

        for (const StudioEntity& entity : scene.getEntities())
        {
            if (result.segments.size() >= options.maxSegments)
            {
                // Said out loud rather than stopped quietly: a wireframe that ran out of room
                // looks exactly like a scene missing half its entities.
                result.truncated = true;
                break;
            }

            if (!entity.isEnabled()) { continue; }

            const std::optional<WorldBounds3D> bounds =
                computeEntityBounds3D(scene, entity.getId(), sizeProvider, options.meshProvider);
            if (!bounds) { continue; }

            const bool selected =
                std::find(selection.begin(), selection.end(), entity.getId()) != selection.end();
            const StudioColor color = selected ? WireColors::kSelected : WireColors::kEntity;

            const auto remaining = [&] {
                return options.maxSegments > result.segments.size()
                           ? options.maxSegments - result.segments.size()
                           : std::size_t{0};
            };

            // The bounds overlay: the volume the editor measures with, over whatever the entity is
            // otherwise drawn as (`plan.md` STUDIO-11008). `drawBox` is false on the path that
            // already draws exactly this box in the entity's own colour -- a second box on top of
            // the first in a second colour says nothing the first did not.
            const bool overlay = options.boundsOverlay == BoundsDisplay::All
                                 || (options.boundsOverlay == BoundsDisplay::Selected && selected);
            const auto appendOverlay = [&](bool drawBox) {
                if (!overlay) { return std::size_t{0}; }
                std::size_t drawn = 0;
                if (drawBox)
                {
                    drawn += appendBox(result.segments, camera, *bounds, WireColors::kBounds, 1.0f);
                }
                if (options.drawBoundingSpheres)
                {
                    drawn += appendBoundingSphere(result.segments, camera, *bounds,
                                                  WireColors::kBounds, 1.0f, remaining());
                }
                return drawn;
            };

            // A model that has actually been imported is drawn as itself. This is the first thing
            // in the 3D view that is neither a box nor a badge, and the whole point of ED-405
            // coming before ED-402: until there was a mesh to draw, every entity here was a
            // rectangle with a label on it.
            // A selected model is outlined even when nothing else draws edges: in the shaded mode
            // the outline is the *only* thing marking it, and a selection a user cannot see is one
            // they lose track of (`plan.md` STUDIO-11007).
            const bool wantsEdges = options.drawMeshEdges || (selected && options.drawSelectionOutline);

            if (const MeshData* mesh =
                    wantsEdges ? findEntityMesh(entity, options.meshProvider) : nullptr;
                mesh != nullptr && !mesh->isEmpty())
            {
                const std::optional<WorldTransform> world =
                    computeWorldTransform(scene, entity.getId());
                if (world)
                {
                    const StudioMatrix matrix = toWorldMatrix(*world);
                    std::size_t drawn = 0;

                    // The mesh's own edges first, so the outline goes over them rather than under.
                    if (options.drawMeshEdges)
                    {
                        drawn += appendMeshEdges(result.segments, camera, *mesh, matrix, color,
                                                 selected ? 2.0f : 1.0f, remaining(),
                                                 result.truncated);
                    }

                    if (selected && options.drawSelectionOutline)
                    {
                        drawn += appendMeshSilhouette(result.segments, camera, *mesh, matrix,
                                                      WireColors::kSelected, 2.0f, remaining(),
                                                      result.truncated);
                    }

                    // A model is the case the overlay exists for: it is drawn at whatever size its
                    // mesh is, and until STUDIO-11008 it was measured as an eight-unit box at its
                    // origin. The box is drawn here because the mesh is not a box.
                    drawn += appendOverlay(true);

                    if (drawn > 0) { ++result.entitiesDrawn; }
                    continue;
                }
            }

            // A light gets its aim drawn as well as its badge (ED-404). Before the badge, so the
            // badge is the thing on top where the two overlap -- the badge is what a user clicks.
            if (const auto found = lights.find(entity.getId()); found != lights.end())
            {
                const std::size_t budget = options.maxSegments > result.segments.size()
                                               ? options.maxSegments - result.segments.size()
                                               : 0;
                appendLightVisualisation(result.segments, camera, found->second,
                                         selected ? WireColors::kSelected : WireColors::kLight,
                                         budget);
            }

            // An entity that draws nothing gets a badge rather than a box: a camera and a light
            // both have the same non-existent size, so boxing them says only "something is here",
            // which is the one thing a scene of them makes obvious anyway. A model renderer with
            // no mesh loaded lands here too, which is the honest picture of it.
            const StudioIconKind icon = getStudioIconKind(entity);
            if (icon != StudioIconKind::None)
            {
                const std::optional<StudioVector2> screenPoint =
                    camera.worldToScreen(bounds->getCenter());
                if (!screenPoint) { continue; }

                const std::vector<WireSegment> badge = buildIconBadge(icon, *screenPoint, color);
                result.segments.insert(result.segments.end(), badge.begin(), badge.end());

                // A badge is a fixed size in pixels and the box behind it is not, so the overlay
                // here answers the question a badge cannot: how big is the thing I am clicking.
                const std::size_t overlaid = appendOverlay(true);

                if (!badge.empty() || overlaid > 0) { ++result.entitiesDrawn; }
                continue;
            }

            // The box drawn here *is* the bounds, so the overlay adds no second copy of it -- only
            // the sphere, which is information the box does not carry.
            std::size_t drawn = appendBox(result.segments, camera, *bounds, color,
                                          selected ? 2.0f : 1.0f);
            drawn += appendOverlay(false);
            if (drawn > 0) { ++result.entitiesDrawn; }
        }

        return result;
    }

    std::optional<float> intersectRayWithBounds(const WorldRay& ray, const WorldBounds3D& bounds)
    {
        // The slab test: clip the ray against each pair of parallel faces and keep the overlap.
        float entry = -std::numeric_limits<float>::max();
        float exit = std::numeric_limits<float>::max();

        const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
        const float direction[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
        const float low[3] = {bounds.min.x, bounds.min.y, bounds.min.z};
        const float high[3] = {bounds.max.x, bounds.max.y, bounds.max.z};

        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(direction[axis]) < 1e-8f)
            {
                // Parallel to this pair of faces: a miss unless the ray already lies between them.
                // A flat box -- which is what a sprite is -- is exactly this case on one axis, so
                // getting it wrong makes every sprite unpickable from the side.
                if (origin[axis] < low[axis] || origin[axis] > high[axis]) { return std::nullopt; }
                continue;
            }

            const float inverse = 1.0f / direction[axis];
            float near = (low[axis] - origin[axis]) * inverse;
            float far = (high[axis] - origin[axis]) * inverse;
            if (near > far) { std::swap(near, far); }

            entry = std::max(entry, near);
            exit = std::min(exit, far);
            if (entry > exit) { return std::nullopt; }
        }

        if (exit < 0.0f) { return std::nullopt; }

        // Zero when the ray starts inside the box, which is a hit at the eye rather than a miss.
        return std::max(entry, 0.0f);
    }

    Uuid pickEntityAt3D(const SceneDocument& scene, const StudioCamera3D& camera,
                        const StudioVector2& screenPoint, const SpriteSizeProvider& sizeProvider,
                        const MeshProvider& meshProvider)
    {
        const WorldRay ray = camera.screenToRay(screenPoint);

        Uuid nearestId;
        float nearestDistance = std::numeric_limits<float>::max();

        for (const StudioEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const std::optional<WorldBounds3D> bounds =
                computeEntityBounds3D(scene, entity.getId(), sizeProvider, meshProvider);
            if (!bounds) { continue; }

            const std::optional<float> distance = intersectRayWithBounds(ray, *bounds);
            if (!distance || *distance >= nearestDistance) { continue; }

            nearestDistance = *distance;
            nearestId = entity.getId();
        }

        return nearestId;
    }
}
