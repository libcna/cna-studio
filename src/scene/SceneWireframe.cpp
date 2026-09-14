// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SceneWireframe.hpp"

#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "CNA/Editor/Core/EditorMatrix.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/EditorCamera2D.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Scene/SceneLighting.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns the signed distance from the eye to @p point along the view direction. */
        float depthOf(const EditorCamera3D& camera, const EditorVector3& point)
        {
            return dot(subtract(point, camera.getEye()), camera.getForward());
        }

        /** @brief Returns the point @p fraction of the way from @p from to @p to. */
        EditorVector3 interpolate(const EditorVector3& from, const EditorVector3& to, float fraction)
        {
            return add(from, scale(subtract(to, from), fraction));
        }

        /** @brief Appends the twelve edges of @p bounds to @p out, clipped and projected. */
        std::size_t appendBox(std::vector<WireSegment>& out, const EditorCamera3D& camera,
                              const WorldBounds3D& bounds, const EditorColor& color, float thickness)
        {
            const EditorVector3 corners[8] = {
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
                const std::optional<std::pair<EditorVector2, EditorVector2>> projected =
                    projectSegment(camera, corners[edge[0]], corners[edge[1]]);
                if (!projected) { continue; }
                out.push_back(WireSegment{projected->first, projected->second, color, thickness});
                ++drawn;
            }
            return drawn;
        }

        /**
         * @brief Returns the mesh a @p entity's `ModelRenderer` names, or nullptr.
         *
         * Every step is a real "no": no component, no asset reference, no provider, nothing
         * imported yet. All of them mean the same thing to the caller -- draw the badge instead --
         * so they are one return value rather than four.
         */
        const MeshData* findEntityMesh(const EditorEntity& entity, const MeshProvider& provider)
        {
            if (!provider) { return nullptr; }

            const EditorComponent* renderer = entity.findComponent(BuiltinComponentIds::kModelRenderer);
            if (renderer == nullptr) { return nullptr; }

            const Uuid modelId =
                renderer->getProperty("model").get<PropertyValue::AssetReference>().id;
            if (!modelId.isValid()) { return nullptr; }

            return provider(modelId);
        }

        /**
         * @brief Returns how many world units one viewport pixel spans at @p point's depth.
         *
         * The same arithmetic `TransformGizmos3D` uses to size a gizmo arm, and here for the same
         * reason: a light's arrow has to be legible in a scene laid out in hundreds of units and in
         * one laid out in single figures, and only a screen-space size is both.
         */
        float worldUnitsPerPixelNear(const EditorCamera3D& camera, const EditorVector3& point)
        {
            const EditorVector2 viewport = camera.getViewportSize();
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
        std::pair<EditorVector3, EditorVector3> makePlaneBasisForLight(const EditorVector3& normal)
        {
            const EditorVector3 seed = std::abs(normal.x) < 0.9f ? EditorVector3{1.0f, 0.0f, 0.0f}
                                                                 : EditorVector3{0.0f, 1.0f, 0.0f};
            const EditorVector3 planeX = normalize(cross(seed, normal));
            return {planeX, normalize(cross(normal, planeX))};
        }

        /** @brief Composes @p transform into the matrix that takes model space to world space. */
        EditorMatrix toWorldMatrix(const WorldTransform& transform)
        {
            // Scale, then rotate, then translate -- the order every transform in this editor
            // composes in, and the one `computeWorldTransform` itself assumes when it accumulates
            // a hierarchy. Any other order here would place a rotated child somewhere the gizmo
            // that moved it does not agree with.
            return multiply(multiply(createScale(transform.scale),
                                     createFromQuaternion(transform.rotation)),
                            createTranslation(transform.position));
        }
    }

    std::size_t appendLightVisualisation(std::vector<WireSegment>& segments,
                                        const EditorCamera3D& camera, const SceneLight& light,
                                        const EditorColor& color, std::size_t budget)
    {
        if (budget == 0) { return 0; }

        std::size_t drawn = 0;
        const auto append = [&](const EditorVector3& from, const EditorVector3& to)
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
        const EditorVector3 tip = add(light.position, scale(light.direction, kDirectionLength));
        append(light.position, tip);

        // A small arrowhead, so the line reads as an arrow rather than as an edge of something.
        const auto [armX, armY] = makePlaneBasisForLight(light.direction);
        const float kHead = kDirectionLength * 0.18f;
        const EditorVector3 back = add(light.position, scale(light.direction, kDirectionLength - kHead));
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
        EditorVector3 previous{light.position.x + light.range, light.position.y, light.position.z};
        for (std::size_t i = 1; i <= kRingSamples; ++i)
        {
            const float angle = 2.0f * 3.14159265358979323846f * static_cast<float>(i)
                                / static_cast<float>(kRingSamples);
            const EditorVector3 point{light.position.x + std::cos(angle) * light.range,
                                      light.position.y + std::sin(angle) * light.range,
                                      light.position.z};
            append(previous, point);
            previous = point;
        }

        return drawn;
    }

    std::size_t appendMeshEdges(std::vector<WireSegment>& segments, const EditorCamera3D& camera,
                                const MeshData& mesh, const EditorMatrix& world,
                                const EditorColor& color, float thickness, std::size_t budget,
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

                    const std::optional<std::pair<EditorVector2, EditorVector2>> projected =
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

    std::optional<std::pair<EditorVector2, EditorVector2>> projectSegment(const EditorCamera3D& camera,
                                                                          const EditorVector3& from,
                                                                          const EditorVector3& to)
    {
        // A hair in front of the near plane, not on it: a point exactly on the plane divides by a
        // w of zero, and the resulting coordinate is an infinity that draws a line to nowhere.
        const float nearDistance = camera.getNearClipDistance() + 1e-4f;

        float fromDepth = depthOf(camera, from);
        float toDepth = depthOf(camera, to);

        if (fromDepth < nearDistance && toDepth < nearDistance) { return std::nullopt; }

        EditorVector3 clippedFrom = from;
        EditorVector3 clippedTo = to;

        if (fromDepth < nearDistance)
        {
            clippedFrom = interpolate(from, to, (nearDistance - fromDepth) / (toDepth - fromDepth));
        }
        else if (toDepth < nearDistance)
        {
            clippedTo = interpolate(to, from, (nearDistance - toDepth) / (fromDepth - toDepth));
        }

        const std::optional<EditorVector2> screenFrom = camera.worldToScreen(clippedFrom);
        const std::optional<EditorVector2> screenTo = camera.worldToScreen(clippedTo);
        if (!screenFrom || !screenTo) { return std::nullopt; }

        return std::make_pair(*screenFrom, *screenTo);
    }

    std::vector<WireSegment> buildIconBadge(EditorIconKind kind, const EditorVector2& screenPoint,
                                            const EditorColor& color)
    {
        std::vector<WireSegment> segments;
        if (kind == EditorIconKind::None) { return segments; }

        const float extent = kEditorIconExtent;
        const auto at = [&screenPoint](float x, float y) {
            return EditorVector2{screenPoint.x + x, screenPoint.y + y};
        };
        const auto line = [&segments, &color](const EditorVector2& from, const EditorVector2& to) {
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
            case EditorIconKind::Camera:
                // A body and the lens cone beside it: the silhouette everything from a film camera
                // to a viewport widget uses, and recognisable at thirteen pixels.
                box(extent * 0.6f, extent * 0.5f);
                line(at(extent * 0.6f, -extent * 0.5f), at(extent, -extent * 0.85f));
                line(at(extent, -extent * 0.85f), at(extent, extent * 0.85f));
                line(at(extent, extent * 0.85f), at(extent * 0.6f, extent * 0.5f));
                break;

            case EditorIconKind::Light:
                // A point with rays. Four is enough to read as a light and few enough that a scene
                // full of them is still a scene rather than a haystack.
                box(extent * 0.35f, extent * 0.35f);
                line(at(0.0f, -extent * 0.55f), at(0.0f, -extent));
                line(at(0.0f, extent * 0.55f), at(0.0f, extent));
                line(at(-extent * 0.55f, 0.0f), at(-extent, 0.0f));
                line(at(extent * 0.55f, 0.0f), at(extent, 0.0f));
                break;

            case EditorIconKind::AudioSource:
                // A cone opening to the right, with one wavefront in front of it.
                line(at(-extent * 0.7f, -extent * 0.35f), at(-extent * 0.7f, extent * 0.35f));
                line(at(-extent * 0.7f, -extent * 0.35f), at(0.0f, -extent * 0.8f));
                line(at(-extent * 0.7f, extent * 0.35f), at(0.0f, extent * 0.8f));
                line(at(0.0f, -extent * 0.8f), at(0.0f, extent * 0.8f));
                line(at(extent * 0.5f, -extent * 0.5f), at(extent * 0.5f, extent * 0.5f));
                break;

            case EditorIconKind::Model:
                // A cube drawn flat: a wireframe box in *screen* space, which reads as "a mesh
                // belongs here" without pretending to be the mesh, since ED-402 has not landed.
                box(extent * 0.75f, extent * 0.75f);
                line(at(-extent * 0.75f, -extent * 0.75f), at(-extent * 0.35f, -extent));
                line(at(extent * 0.75f, -extent * 0.75f), at(extent, -extent));
                line(at(-extent * 0.35f, -extent), at(extent, -extent));
                line(at(extent, -extent), at(extent, extent * 0.35f));
                line(at(extent, extent * 0.35f), at(extent * 0.75f, extent * 0.75f));
                break;

            case EditorIconKind::None: break;
        }

        return segments;
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

    std::vector<WireSegment> buildSceneGrid(const EditorCamera3D& camera, const WireframeOptions& options)
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
            return ground ? EditorVector3{u, 0.0f, v} : EditorVector3{u, v, 0.0f};
        };

        // Centred on the pivot and snapped to the spacing, so flying across a level does not drag
        // the grid's origin along and turn the lines into a shimmering mess.
        const float centerU = std::round(camera.getPivot().x / spacing) * spacing;
        const float centerV =
            std::round((ground ? camera.getPivot().z : camera.getPivot().y) / spacing) * spacing;

        const int extent = options.gridHalfExtent;
        const float half = static_cast<float>(extent) * spacing;

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
            const EditorColor inPlaneAxis = ground ? WireColors::kAxisZ : WireColors::kAxisY;

            const EditorColor alongV =
                uIsAxis ? inPlaneAxis : (isMajor ? WireColors::kGridMajor : WireColors::kGrid);
            const EditorColor alongU =
                vIsAxis ? WireColors::kAxisX : (isMajor ? WireColors::kGridMajor : WireColors::kGrid);

            const std::optional<std::pair<EditorVector2, EditorVector2>> lineAlongV =
                projectSegment(camera, pointAt(u, centerV - half), pointAt(u, centerV + half));
            if (lineAlongV)
            {
                segments.push_back(WireSegment{lineAlongV->first, lineAlongV->second, alongV,
                                               uIsAxis ? 2.0f : 1.0f});
            }

            const std::optional<std::pair<EditorVector2, EditorVector2>> lineAlongU =
                projectSegment(camera, pointAt(centerU - half, v), pointAt(centerU + half, v));
            if (lineAlongU)
            {
                segments.push_back(WireSegment{lineAlongU->first, lineAlongU->second, alongU,
                                               vIsAxis ? 2.0f : 1.0f});
            }
        }

        return segments;
    }

    WireframeResult buildSceneWireframe(const SceneDocument& scene, const EditorCamera3D& camera,
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

        for (const EditorEntity& entity : scene.getEntities())
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
                computeEntityBounds3D(scene, entity.getId(), sizeProvider);
            if (!bounds) { continue; }

            const bool selected =
                std::find(selection.begin(), selection.end(), entity.getId()) != selection.end();
            const EditorColor color = selected ? WireColors::kSelected : WireColors::kEntity;

            // A model that has actually been imported is drawn as itself. This is the first thing
            // in the 3D view that is neither a box nor a badge, and the whole point of ED-405
            // coming before ED-402: until there was a mesh to draw, every entity here was a
            // rectangle with a label on it.
            if (const MeshData* mesh = findEntityMesh(entity, options.meshProvider);
                mesh != nullptr && !mesh->isEmpty())
            {
                const std::optional<WorldTransform> world =
                    computeWorldTransform(scene, entity.getId());
                if (world)
                {
                    const std::size_t budget = options.maxSegments > result.segments.size()
                                                   ? options.maxSegments - result.segments.size()
                                                   : 0;
                    const std::size_t drawn =
                        appendMeshEdges(result.segments, camera, *mesh, toWorldMatrix(*world), color,
                                        selected ? 2.0f : 1.0f, budget, result.truncated);
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
            const EditorIconKind icon = getEditorIconKind(entity);
            if (icon != EditorIconKind::None)
            {
                const std::optional<EditorVector2> screenPoint =
                    camera.worldToScreen(bounds->getCenter());
                if (!screenPoint) { continue; }

                const std::vector<WireSegment> badge = buildIconBadge(icon, *screenPoint, color);
                result.segments.insert(result.segments.end(), badge.begin(), badge.end());
                if (!badge.empty()) { ++result.entitiesDrawn; }
                continue;
            }

            const std::size_t drawn = appendBox(result.segments, camera, *bounds, color,
                                                selected ? 2.0f : 1.0f);
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

    Uuid pickEntityAt3D(const SceneDocument& scene, const EditorCamera3D& camera,
                        const EditorVector2& screenPoint, const SpriteSizeProvider& sizeProvider)
    {
        const WorldRay ray = camera.screenToRay(screenPoint);

        Uuid nearestId;
        float nearestDistance = std::numeric_limits<float>::max();

        for (const EditorEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const std::optional<WorldBounds3D> bounds =
                computeEntityBounds3D(scene, entity.getId(), sizeProvider);
            if (!bounds) { continue; }

            const std::optional<float> distance = intersectRayWithBounds(ray, *bounds);
            if (!distance || *distance >= nearestDistance) { continue; }

            nearestDistance = *distance;
            nearestId = entity.getId();
        }

        return nearestId;
    }
}
