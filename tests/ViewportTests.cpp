// SPDX-License-Identifier: MS-PL
/**
 * @file ViewportTests.cpp
 * @brief Tests for world transforms, the editor camera and picking.
 *
 * All of this is CNA-free by design (see SceneTransform.hpp), which is what lets the geometry the
 * viewport, the gizmo and the picker all depend on be verified with no window and no GPU. A bug
 * here would show up as "clicking selects the wrong thing", which is miserable to debug through a
 * running editor and trivial to catch at this level.
 */

#include "TestHarness.hpp"

#include <cmath>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/EditorCamera2D.hpp"
#include "CNA/Editor/Scene/EditorIcons.hpp"
#include "CNA/Editor/Scene/GameCamera.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"
#include "CNA/Editor/Scene/TransformGizmos.hpp"

using namespace CNA::Editor;

namespace
{
    ComponentRegistry makeRegistry()
    {
        ComponentRegistry registry;
        registerBuiltinComponents(registry);
        return registry;
    }

    /** @brief Adds an entity with a Transform at (@p x, @p y) and returns its id. */
    Uuid addEntity(SceneDocument& scene, const ComponentRegistry& registry, std::string name,
                   float x, float y, float scale = 1.0f)
    {
        EditorEntity entity{Uuid::generate(), std::move(name)};
        EditorComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
        transform.setProperty("position", PropertyValue{EditorVector3{x, y, 0.0f}});
        transform.setProperty("scale", PropertyValue{EditorVector3{scale, scale, 1.0f}});
        entity.addComponent(std::move(transform));
        return scene.addEntity(std::move(entity));
    }

    /** @brief Gives @p entityId a sprite of the given size, via its source rectangle. */
    void addSprite(SceneDocument& scene, const ComponentRegistry& registry, const Uuid& entityId,
                   int width, int height, float layerDepth = 0.5f)
    {
        EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
        sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
        sprite.setProperty("sourceRectangle", PropertyValue{EditorRectangle{0, 0, width, height}});
        sprite.setProperty("layerDepth", PropertyValue{layerDepth});
        scene.findEntity(entityId)->addComponent(std::move(sprite));
    }

    /** @brief Adds a component of @p typeId, populated with its declared defaults. */
    void addComponent(SceneDocument& scene, const ComponentRegistry& registry, const Uuid& entityId,
                      const char* typeId)
    {
        EditorComponent component{typeId};
        component.applyDefaults(*registry.find(typeId));
        scene.findEntity(entityId)->addComponent(std::move(component));
    }

    bool nearlyEqual(float a, float b, float tolerance = 0.001f)
    {
        return std::fabs(a - b) <= tolerance;
    }

    /** @brief A quarter turn about Z, in radians: the rotation every local-space test uses. */
    constexpr float kQuarterTurn = 3.14159265f * 0.5f;

    /** @brief Sets @p entityId's local rotation to @p radians about Z. */
    void setZRotation(SceneDocument& scene, const Uuid& entityId, float radians)
    {
        scene.findEntity(entityId)->findComponent(BuiltinComponentIds::kTransform)
            ->setProperty("rotation", PropertyValue{quaternionFromZRotation(radians)});
    }

    /** @brief A size provider that reports nothing, exercising the unknown-size fallback. */
    const SpriteSizeProvider kNoSizes = [](const Uuid&) { return EditorVector2{}; };
}

CNA_EDITOR_TEST(WorldTransformComposesThroughTheParentChain)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 100.0f, 50.0f, 2.0f);
    const Uuid child = addEntity(scene, registry, "Child", 10.0f, 0.0f);
    scene.reparentEntity(child, parent);

    const std::optional<WorldTransform> world = computeWorldTransform(scene, child);
    CNA_EDITOR_EXPECT(world.has_value());

    // The child's local offset is scaled by the parent before being added: 100 + 10*2.
    CNA_EDITOR_EXPECT(nearlyEqual(world->position.x, 120.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(world->position.y, 50.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(world->scale.x, 2.0f));
}

CNA_EDITOR_TEST(WorldTransformAppliesParentRotation)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    scene.findEntity(parent)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{quaternionFromZRotation(3.14159265f * 0.5f)});

    const Uuid child = addEntity(scene, registry, "Child", 10.0f, 0.0f);
    scene.reparentEntity(child, parent);

    const std::optional<WorldTransform> world = computeWorldTransform(scene, child);
    CNA_EDITOR_EXPECT(world.has_value());

    // A quarter turn about Z takes the local +X offset onto world +Y.
    CNA_EDITOR_EXPECT(nearlyEqual(world->position.x, 0.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(world->position.y, 10.0f, 0.01f));
}

CNA_EDITOR_TEST(QuaternionZRotationRoundTrips)
{
    for (float angle : {0.0f, 0.5f, 1.5f, -2.0f, 3.0f})
    {
        CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(quaternionFromZRotation(angle)), angle, 0.001f));
    }
}

CNA_EDITOR_TEST(EntityBoundsUseTheSourceRectangleAndOrigin)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = addEntity(scene, registry, "Sprite", 100.0f, 200.0f);
    addSprite(scene, registry, id, 32, 16);

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_EDITOR_EXPECT(bounds.has_value());
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->min.x, 100.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.x, 132.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.y, 216.0f));

    // The origin shifts the sprite, exactly as SpriteBatch::Draw's origin parameter does.
    scene.findEntity(id)->findComponent(BuiltinComponentIds::kSpriteRenderer)
        ->setProperty("origin", PropertyValue{EditorVector2{16.0f, 8.0f}});

    const std::optional<WorldBounds2D> centred = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_EDITOR_EXPECT(nearlyEqual(centred->min.x, 84.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(centred->max.x, 116.0f));
}

CNA_EDITOR_TEST(EntityBoundsFallBackWhenTheTextureSizeIsUnknown)
{
    // A sprite whose texture failed to import must still be clickable, or the entity cannot be
    // selected and therefore cannot be fixed.
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = addEntity(scene, registry, "Broken", 0.0f, 0.0f);
    EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{Uuid::generate()}});
    scene.findEntity(id)->addComponent(std::move(sprite));

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_EDITOR_EXPECT(bounds.has_value());
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.x - bounds->min.x, kUnknownSpriteExtent));
}

CNA_EDITOR_TEST(EntityBoundsUseTheProviderWhenNoSourceRectangleIsSet)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid textureId = Uuid::generate();
    const Uuid id = addEntity(scene, registry, "Sprite", 0.0f, 0.0f);
    EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{textureId}});
    scene.findEntity(id)->addComponent(std::move(sprite));

    const SpriteSizeProvider provider = [textureId](const Uuid& asset) {
        return asset == textureId ? EditorVector2{48.0f, 24.0f} : EditorVector2{};
    };

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, provider);
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.x, 48.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.y, 24.0f));
}

CNA_EDITOR_TEST(RotatedSpriteBoundsCoverTheRotatedCorners)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = addEntity(scene, registry, "Sprite", 0.0f, 0.0f);
    addSprite(scene, registry, id, 100, 10);
    scene.findEntity(id)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{quaternionFromZRotation(3.14159265f * 0.5f)});

    // Rotated a quarter turn, a 100x10 sprite occupies a 10x100 box. Taking the AABB before the
    // rotation instead would leave most of the sprite unclickable.
    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.x - bounds->min.x, 10.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.y - bounds->min.y, 100.0f, 0.01f));
}

CNA_EDITOR_TEST(HierarchyBoundsCoverDescendants)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    addSprite(scene, registry, parent, 10, 10);

    const Uuid child = addEntity(scene, registry, "Child", 500.0f, 0.0f);
    addSprite(scene, registry, child, 10, 10);
    scene.reparentEntity(child, parent);

    // Framing a parent whose children spread across the level should show the children.
    const std::optional<WorldBounds2D> bounds = computeHierarchyBounds2D(scene, parent, kNoSizes);
    CNA_EDITOR_EXPECT(bounds.has_value());
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->min.x, 0.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(bounds->max.x, 510.0f));
}

CNA_EDITOR_TEST(CameraRoundTripsBetweenWorldAndScreen)
{
    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.setCenter(EditorVector2{100.0f, 50.0f});
    camera.setZoom(2.0f);

    // The camera centre sits at the middle of the viewport by definition.
    const EditorVector2 centreOnScreen = camera.worldToScreen(EditorVector2{100.0f, 50.0f});
    CNA_EDITOR_EXPECT(nearlyEqual(centreOnScreen.x, 400.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(centreOnScreen.y, 300.0f));

    for (const EditorVector2& world : {EditorVector2{0.0f, 0.0f}, EditorVector2{-250.0f, 375.0f},
                                       EditorVector2{1000.0f, -1000.0f}})
    {
        const EditorVector2 back = camera.screenToWorld(camera.worldToScreen(world));
        CNA_EDITOR_EXPECT(nearlyEqual(back.x, world.x, 0.01f));
        CNA_EDITOR_EXPECT(nearlyEqual(back.y, world.y, 0.01f));
    }
}

CNA_EDITOR_TEST(CameraPanTracksTheCursorAtAnyZoom)
{
    for (float zoom : {0.25f, 1.0f, 4.0f})
    {
        EditorCamera2D camera;
        camera.setViewportSize(EditorVector2{800.0f, 600.0f});
        camera.setZoom(zoom);

        const EditorVector2 grabScreen{200.0f, 150.0f};
        const EditorVector2 grabWorld = camera.screenToWorld(grabScreen);

        const EditorVector2 delta{60.0f, -25.0f};
        camera.panByScreenDelta(delta);

        // The world point grabbed must end up under the cursor's new position, or a drag drifts.
        const EditorVector2 nowAt = camera.worldToScreen(grabWorld);
        CNA_EDITOR_EXPECT(nearlyEqual(nowAt.x, grabScreen.x + delta.x, 0.01f));
        CNA_EDITOR_EXPECT(nearlyEqual(nowAt.y, grabScreen.y + delta.y, 0.01f));
    }
}

CNA_EDITOR_TEST(CameraZoomKeepsTheAnchorPointFixed)
{
    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.setCenter(EditorVector2{10.0f, 20.0f});

    const EditorVector2 anchor{620.0f, 110.0f};
    const EditorVector2 anchorWorld = camera.screenToWorld(anchor);

    camera.zoomAt(anchor, 2.5f);

    // Wheel-zoom must keep what is under the pointer under the pointer; zooming about the view
    // centre instead makes the user chase their target across the screen.
    const EditorVector2 after = camera.worldToScreen(anchorWorld);
    CNA_EDITOR_EXPECT(nearlyEqual(after.x, anchor.x, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(after.y, anchor.y, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(camera.getZoom(), 2.5f));
}

CNA_EDITOR_TEST(CameraZoomStaysWithinItsLimits)
{
    EditorCamera2D camera;
    camera.setZoom(1000.0f);
    CNA_EDITOR_EXPECT(nearlyEqual(camera.getZoom(), EditorCamera2D::kMaxZoom));

    camera.setZoom(0.0f);
    CNA_EDITOR_EXPECT(nearlyEqual(camera.getZoom(), EditorCamera2D::kMinZoom));

    // Even when clamping bites, the anchor must not jump -- that is why zoomAt re-derives the
    // anchor's position after setZoom rather than computing the new centre algebraically.
    camera.setZoom(EditorCamera2D::kMaxZoom);
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    const EditorVector2 anchor{100.0f, 100.0f};
    const EditorVector2 anchorWorld = camera.screenToWorld(anchor);
    camera.zoomAt(anchor, 10.0f);
    const EditorVector2 after = camera.worldToScreen(anchorWorld);
    CNA_EDITOR_EXPECT(nearlyEqual(after.x, anchor.x, 0.01f));
}

CNA_EDITOR_TEST(CameraFramesBoundsWithinTheViewport)
{
    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    WorldBounds2D bounds;
    bounds.min = EditorVector2{-100.0f, -50.0f};
    bounds.max = EditorVector2{300.0f, 150.0f};
    camera.frame(bounds, 0.1f);

    CNA_EDITOR_EXPECT(nearlyEqual(camera.getCenter().x, 100.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(camera.getCenter().y, 50.0f));

    // Everything asked for must actually be on screen, with the margin respected.
    const EditorVector2 topLeft = camera.worldToScreen(bounds.min);
    const EditorVector2 bottomRight = camera.worldToScreen(bounds.max);
    CNA_EDITOR_EXPECT(topLeft.x >= 0.0f && topLeft.y >= 0.0f);
    CNA_EDITOR_EXPECT(bottomRight.x <= 800.0f && bottomRight.y <= 600.0f);
}

CNA_EDITOR_TEST(PickingSelectsTheEntityUnderTheCursor)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid left = addEntity(scene, registry, "Left", 0.0f, 0.0f);
    addSprite(scene, registry, left, 50, 50);
    const Uuid right = addEntity(scene, registry, "Right", 200.0f, 0.0f);
    addSprite(scene, registry, right, 50, 50);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.setCenter(EditorVector2{100.0f, 25.0f});

    CNA_EDITOR_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(EditorVector2{25.0f, 25.0f}),
                                   kNoSizes).entityId == left);
    CNA_EDITOR_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(EditorVector2{225.0f, 25.0f}),
                                   kNoSizes).entityId == right);

    // Empty space selects nothing rather than the nearest thing.
    CNA_EDITOR_EXPECT(!pickEntityAt(scene, camera, camera.worldToScreen(EditorVector2{120.0f, 25.0f}),
                                    kNoSizes).entityId.isValid());
}

CNA_EDITOR_TEST(PickingPrefersTheFrontmostSprite)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid back = addEntity(scene, registry, "Back", 0.0f, 0.0f);
    addSprite(scene, registry, back, 100, 100, 0.9f);
    const Uuid front = addEntity(scene, registry, "Front", 0.0f, 0.0f);
    addSprite(scene, registry, front, 100, 100, 0.1f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.setCenter(EditorVector2{50.0f, 50.0f});

    // XNA's convention: 0 is front, 1 is back.
    CNA_EDITOR_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(EditorVector2{50.0f, 50.0f}),
                                   kNoSizes).entityId == front);
}

CNA_EDITOR_TEST(PickingIgnoresDisabledEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid hidden = addEntity(scene, registry, "Hidden", 0.0f, 0.0f);
    addSprite(scene, registry, hidden, 100, 100, 0.1f);
    scene.findEntity(hidden)->setEnabled(false);

    const Uuid visible = addEntity(scene, registry, "Visible", 0.0f, 0.0f);
    addSprite(scene, registry, visible, 100, 100, 0.9f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.setCenter(EditorVector2{50.0f, 50.0f});

    // The disabled entity is in front, so picking it would be the natural bug.
    CNA_EDITOR_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(EditorVector2{50.0f, 50.0f}),
                                   kNoSizes).entityId == visible);
}

CNA_EDITOR_TEST(PickingHonoursParentTransforms)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 1000.0f, 500.0f, 2.0f);
    const Uuid child = addEntity(scene, registry, "Child", 10.0f, 0.0f);
    addSprite(scene, registry, child, 20, 20);
    scene.reparentEntity(child, parent);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.setCenter(EditorVector2{1030.0f, 520.0f});

    // The child sits at 1000 + 10*2 = 1020, scaled 2x so 40 units wide.
    CNA_EDITOR_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(EditorVector2{1040.0f, 520.0f}),
                                   kNoSizes).entityId == child);
    CNA_EDITOR_EXPECT(!pickEntityAt(scene, camera, camera.worldToScreen(EditorVector2{1010.0f, 520.0f}),
                                    kNoSizes).entityId.isValid());
}

// ---------------------------------------------------------------------------------------------
// Translate gizmo
// ---------------------------------------------------------------------------------------------

CNA_EDITOR_TEST(GizmoLayoutSitsOnTheEntityAndKeepsItsScreenSize)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 300.0f, 120.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});
    camera.setCenter(EditorVector2{300.0f, 120.0f});

    const std::optional<TranslateGizmoLayout> layout = computeTranslateGizmoLayout(scene, camera, id);
    CNA_EDITOR_EXPECT(layout.has_value());
    CNA_EDITOR_EXPECT(nearlyEqual(layout->origin.x, 400.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(layout->origin.y, 300.0f));

    // Handles are sized in screen pixels, so the gizmo stays grabbable at any zoom. A gizmo that
    // shrinks as you zoom out is one you cannot grab exactly when you most need to.
    camera.setZoom(0.1f);
    const std::optional<TranslateGizmoLayout> zoomedOut = computeTranslateGizmoLayout(scene, camera, id);
    CNA_EDITOR_EXPECT(nearlyEqual(zoomedOut->axisLength, layout->axisLength));
    CNA_EDITOR_EXPECT(nearlyEqual(zoomedOut->centerExtent, layout->centerExtent));
}

CNA_EDITOR_TEST(GizmoHitTestDistinguishesItsHandles)
{
    TranslateGizmoLayout layout;
    layout.origin = EditorVector2{100.0f, 100.0f};

    CNA_EDITOR_EXPECT(hitTestTranslateGizmo(layout, EditorVector2{100.0f, 100.0f}) == GizmoHandle::Both);
    CNA_EDITOR_EXPECT(hitTestTranslateGizmo(layout, EditorVector2{150.0f, 100.0f}) == GizmoHandle::XAxis);
    CNA_EDITOR_EXPECT(hitTestTranslateGizmo(layout, EditorVector2{100.0f, 150.0f}) == GizmoHandle::YAxis);
    CNA_EDITOR_EXPECT(hitTestTranslateGizmo(layout, EditorVector2{300.0f, 300.0f}) == GizmoHandle::None);

    // Past the tip is a miss: the arms are segments, not infinite lines.
    CNA_EDITOR_EXPECT(hitTestTranslateGizmo(layout, EditorVector2{100.0f + layout.axisLength + 30.0f, 100.0f})
                      == GizmoHandle::None);

    // Slightly off an arm still counts, within the grab tolerance.
    CNA_EDITOR_EXPECT(hitTestTranslateGizmo(layout, EditorVector2{150.0f, 104.0f}) == GizmoHandle::XAxis);
}

CNA_EDITOR_TEST(GizmoDragConstrainsToTheGrabbedAxis)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, camera, id, GizmoHandle::XAxis,
                                 camera.worldToScreen(EditorVector2{0.0f, 0.0f})));
    CNA_EDITOR_EXPECT(drag.isActive());

    // A diagonal cursor move on the X arm must move only in X.
    const std::optional<EditorVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(EditorVector2{50.0f, 80.0f}));
    CNA_EDITOR_EXPECT(moved.has_value());
    CNA_EDITOR_EXPECT(nearlyEqual(moved->x, 50.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->y, 0.0f));

    drag.end();
    CNA_EDITOR_EXPECT(!drag.isActive());
    CNA_EDITOR_EXPECT(!drag.update(scene, camera, EditorVector2{0.0f, 0.0f}).has_value());
}

CNA_EDITOR_TEST(GizmoDragMeasuresFromTheGrabPointNotTheEntityOrigin)
{
    // Grabbing an arm away from its root must not teleport the entity to the cursor: the offset
    // between cursor and entity has to survive the whole drag.
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 10.0f, 20.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, camera, id, GizmoHandle::Both,
                                 camera.worldToScreen(EditorVector2{60.0f, 20.0f})));

    const std::optional<EditorVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(EditorVector2{70.0f, 25.0f}));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->x, 20.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->y, 25.0f));
}

CNA_EDITOR_TEST(GizmoDragDoesNotDriftOverManyUpdates)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    drag.begin(scene, camera, id, GizmoHandle::Both, camera.worldToScreen(EditorVector2{0.0f, 0.0f}));

    // Every update is measured from the grab point, so wandering around and returning must land
    // exactly back at the start. An implementation that accumulated frame deltas would not.
    for (int step = 0; step < 200; ++step)
    {
        const float t = static_cast<float>(step);
        // The result is deliberately dropped: what is being exercised is that these updates leave
        // no residue behind, which the final one below proves.
        (void)drag.update(scene, camera, camera.worldToScreen(EditorVector2{t * 3.0f, -t * 1.5f}));
    }

    const std::optional<EditorVector3> back =
        drag.update(scene, camera, camera.worldToScreen(EditorVector2{0.0f, 0.0f}));
    CNA_EDITOR_EXPECT(nearlyEqual(back->x, 0.0f, 0.001f));
    CNA_EDITOR_EXPECT(nearlyEqual(back->y, 0.0f, 0.001f));
}

CNA_EDITOR_TEST(GizmoDragHonoursAScaledParent)
{
    // The position property is local. Dragging a child of a 2x parent by 100 world units must
    // change its stored position by 50, or the gizmo drifts on every child.
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f, 2.0f);
    const Uuid child = addEntity(scene, registry, "Child", 0.0f, 0.0f);
    scene.reparentEntity(child, parent);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    drag.begin(scene, camera, child, GizmoHandle::Both, camera.worldToScreen(EditorVector2{0.0f, 0.0f}));

    const std::optional<EditorVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(EditorVector2{100.0f, 0.0f}));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->x, 50.0f));
}

CNA_EDITOR_TEST(GizmoDragHonoursARotatedParent)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    scene.findEntity(parent)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{quaternionFromZRotation(3.14159265f * 0.5f)});

    const Uuid child = addEntity(scene, registry, "Child", 0.0f, 0.0f);
    scene.reparentEntity(child, parent);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    drag.begin(scene, camera, child, GizmoHandle::Both, camera.worldToScreen(EditorVector2{0.0f, 0.0f}));

    // Under a parent rotated a quarter turn, moving the child along world +Y is a change along its
    // own local -X... or +X depending on handedness; either way the magnitude is what is checked,
    // and the axis the drag did *not* move along must stay put.
    const std::optional<EditorVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(EditorVector2{0.0f, 30.0f}));
    CNA_EDITOR_EXPECT(nearlyEqual(std::fabs(moved->x), 30.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->y, 0.0f, 0.01f));
}

// ---------------------------------------------------------------------------------------------
// Local space, rotate gizmo and scale gizmo (ED-401)
// ---------------------------------------------------------------------------------------------

CNA_EDITOR_TEST(LocalSpaceArmsFollowTheEntityRotation)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);
    setZRotation(scene, id, kQuarterTurn);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    // World space ignores the entity's rotation entirely: the arms are the screen's own axes.
    const auto world = computeTranslateGizmoLayout(scene, camera, id, GizmoSpace::World);
    CNA_EDITOR_EXPECT(nearlyEqual(world->xAxis.x, 1.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(world->xAxis.y, 0.0f));

    // Local space turns them with it: a quarter turn puts the X arm straight down the screen.
    const auto local = computeTranslateGizmoLayout(scene, camera, id, GizmoSpace::Local);
    CNA_EDITOR_EXPECT(nearlyEqual(local->xAxis.x, 0.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(local->xAxis.y, 1.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(local->yAxis.x, -1.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(local->yAxis.y, 0.0f));

    // The tips are still the arm length away, so the gizmo is the same size in both spaces.
    CNA_EDITOR_EXPECT(nearlyEqual(std::hypot(local->getXTip().x - local->origin.x,
                                             local->getXTip().y - local->origin.y),
                                  local->axisLength));
}

CNA_EDITOR_TEST(ALocalSpaceDragProjectsOntoTheRotatedArm)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);
    setZRotation(scene, id, kQuarterTurn);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, camera, id, GizmoHandle::XAxis,
                                 camera.worldToScreen(EditorVector2{0.0f, 0.0f}), GizmoSpace::Local));

    // The entity's own X points along world +Y after a quarter turn, so a diagonal cursor move
    // must move it only in world Y -- and, being a root entity, that is its stored position too.
    const std::optional<EditorVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(EditorVector2{50.0f, 80.0f}));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->x, 0.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->y, 80.0f, 0.01f));
}

CNA_EDITOR_TEST(RotateGizmoHitTestGrabsTheRingAndNotItsInterior)
{
    RotateGizmoLayout layout;
    layout.origin = EditorVector2{100.0f, 100.0f};

    CNA_EDITOR_EXPECT(hitTestRotateGizmo(layout, layout.getPointAt(0.0f)) == GizmoHandle::ZAxis);
    CNA_EDITOR_EXPECT(hitTestRotateGizmo(layout, layout.getPointAt(2.0f)) == GizmoHandle::ZAxis);

    // Inside the ring is where the entity is. Grabbing there would make the sprite itself
    // unclickable whenever the rotate gizmo is up.
    CNA_EDITOR_EXPECT(hitTestRotateGizmo(layout, layout.origin) == GizmoHandle::None);
    CNA_EDITOR_EXPECT(hitTestRotateGizmo(layout, EditorVector2{100.0f, 130.0f}) == GizmoHandle::None);

    // And well outside it is a miss too -- the band is a band, not a half-plane.
    CNA_EDITOR_EXPECT(hitTestRotateGizmo(layout, EditorVector2{100.0f, 300.0f}) == GizmoHandle::None);
}

CNA_EDITOR_TEST(ARotateDragTurnsByTheAngleTheCursorSwept)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, id);
    CNA_EDITOR_EXPECT(layout.has_value());

    RotateGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, id, layout->getPointAt(0.0f)));

    const std::optional<EditorQuaternion> turned = drag.update(*layout, layout->getPointAt(kQuarterTurn));
    CNA_EDITOR_EXPECT(turned.has_value());
    CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(*turned), kQuarterTurn, 0.001f));

    // Sweeping back to where it started restores the original rotation exactly: every update is
    // measured from the press, so a long drag leaves no residue.
    const std::optional<EditorQuaternion> back = drag.update(*layout, layout->getPointAt(0.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(*back), 0.0f, 0.001f));
}

CNA_EDITOR_TEST(ARotateDragCrossingTheAngleSeamDoesNotSpin)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, id);

    RotateGizmoDrag drag;
    // Just below +pi, dragged just past it. atan2 wraps to -pi there, so an implementation that
    // subtracted raw angles would report nearly a full turn backwards.
    const float almostPi = 3.14159265f - 0.05f;
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, id, layout->getPointAt(almostPi)));

    const std::optional<EditorQuaternion> turned =
        drag.update(*layout, layout->getPointAt(-almostPi));
    CNA_EDITOR_EXPECT(nearlyEqual(std::fabs(zRotationOf(*turned)), 0.1f, 0.01f));
}

CNA_EDITOR_TEST(ARotateDragOnAChildTurnsItByTheSameWorldAngle)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    setZRotation(scene, parent, kQuarterTurn);

    const Uuid child = addEntity(scene, registry, "Child", 0.0f, 0.0f);
    scene.reparentEntity(child, parent);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, child);

    RotateGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, child, layout->getPointAt(0.0f)));

    const std::optional<EditorQuaternion> turned = drag.update(*layout, layout->getPointAt(0.5f));
    CNA_EDITOR_EXPECT(turned.has_value());

    // The stored value is local, so it holds the turn alone -- the parent's quarter is not in it.
    CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(*turned), 0.5f, 0.001f));

    // And the entity really did end up half a radian round in the world, which is what the user
    // was pointing at. Getting this wrong gives a child that lags or races its own cursor.
    scene.findEntity(child)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{*turned});
    CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(computeWorldTransform(scene, child)->rotation),
                                  kQuarterTurn + 0.5f, 0.001f));
}

CNA_EDITOR_TEST(ScaleGizmoHitTestFindsItsHandles)
{
    ScaleGizmoLayout layout;
    layout.origin = EditorVector2{100.0f, 100.0f};

    CNA_EDITOR_EXPECT(hitTestScaleGizmo(layout, EditorVector2{100.0f, 100.0f}) == GizmoHandle::Both);
    CNA_EDITOR_EXPECT(hitTestScaleGizmo(layout, layout.getXTip()) == GizmoHandle::XAxis);
    CNA_EDITOR_EXPECT(hitTestScaleGizmo(layout, layout.getYTip()) == GizmoHandle::YAxis);
    CNA_EDITOR_EXPECT(hitTestScaleGizmo(layout, EditorVector2{140.0f, 100.0f}) == GizmoHandle::XAxis);
    CNA_EDITOR_EXPECT(hitTestScaleGizmo(layout, EditorVector2{300.0f, 300.0f}) == GizmoHandle::None);

    // The end square is a real target, so a press a few pixels off the arm's line but plainly on
    // its handle still counts -- that is what the square is drawn for.
    CNA_EDITOR_EXPECT(hitTestScaleGizmo(layout, EditorVector2{layout.getXTip().x, 105.0f})
                      == GizmoHandle::XAxis);
}

CNA_EDITOR_TEST(AScaleDragIsARatioOfHowFarTheHandleMoved)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);
    CNA_EDITOR_EXPECT(layout.has_value());

    ScaleGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, id, GizmoHandle::XAxis, layout->getXTip()));

    // Twice as far out is twice the scale, and the axis not grabbed is untouched.
    const EditorVector2 doubled{layout->origin.x + (layout->getXTip().x - layout->origin.x) * 2.0f,
                                layout->origin.y};
    const std::optional<EditorVector3> scaled = drag.update(*layout, doubled);
    CNA_EDITOR_EXPECT(nearlyEqual(scaled->x, 2.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(scaled->y, 1.0f));

    // Dragging through the origin flips the entity rather than sticking at zero: negative scale is
    // a legitimate edit and XNA's own SpriteBatch honours it.
    const EditorVector2 through{layout->origin.x - (layout->getXTip().x - layout->origin.x),
                                layout->origin.y};
    CNA_EDITOR_EXPECT(nearlyEqual(drag.update(*layout, through)->x, -1.0f));
}

CNA_EDITOR_TEST(TheUniformScaleHandleScalesBothAxesTogether)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f, 3.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);

    ScaleGizmoDrag drag;
    const EditorVector2 grab{layout->origin.x + 8.0f, layout->origin.y};
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, id, GizmoHandle::Both, grab));

    // Half the distance, half the scale -- and it multiplies what was already there rather than
    // replacing it, which is why an entity already at 3 lands on 1.5 rather than on 0.5.
    const std::optional<EditorVector3> scaled =
        drag.update(*layout, EditorVector2{layout->origin.x + 4.0f, layout->origin.y});
    CNA_EDITOR_EXPECT(nearlyEqual(scaled->x, 1.5f));
    CNA_EDITOR_EXPECT(nearlyEqual(scaled->y, 1.5f));
}

CNA_EDITOR_TEST(AScaleGrabAtTheOriginIsRefused)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);

    // Every factor is a division by how far out the grab was, so a grab on the pivot would scale
    // by infinity. Refusing lets the press fall through to whatever is underneath instead.
    ScaleGizmoDrag drag;
    CNA_EDITOR_EXPECT(!drag.begin(scene, *layout, id, GizmoHandle::Both, layout->origin));
    CNA_EDITOR_EXPECT(!drag.isActive());
}

CNA_EDITOR_TEST(ScaleGizmoArmsAreAlwaysLocal)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);
    setZRotation(scene, id, kQuarterTurn);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    // There is no space parameter to get wrong: a non-uniform scale in world space is not
    // representable in a position/rotation/scale transform, so the arms are the axes the stored
    // numbers actually belong to and nothing else.
    const auto layout = computeScaleGizmoLayout(scene, camera, id);
    CNA_EDITOR_EXPECT(nearlyEqual(layout->xAxis.x, 0.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(layout->xAxis.y, 1.0f));
}

CNA_EDITOR_TEST(SnappingRoundsTheResultRatherThanTheMovement)
{
    CNA_EDITOR_EXPECT(nearlyEqual(snapTo(13.7f, 10.0f), 10.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(snapTo(16.0f, 10.0f), 20.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(snapTo(-13.0f, 10.0f), -10.0f));

    // A step of zero means "do not snap", which is what an unmodified drag passes.
    CNA_EDITOR_EXPECT(nearlyEqual(snapTo(13.7f, 0.0f), 13.7f));

    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 3.0f, 7.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, camera, id, GizmoHandle::Both,
                                 camera.worldToScreen(EditorVector2{3.0f, 7.0f})));

    GizmoSnap snap;
    snap.translate = 10.0f;

    // An entity that started at (3, 7) and was dragged by (9, 6) lands on (10, 10) -- not on
    // (13, 17), which is where snapping the *movement* would have put it. A grid is a set of
    // places things sit, not a set of distances they travel.
    const std::optional<EditorVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(EditorVector2{12.0f, 13.0f}), snap);
    CNA_EDITOR_EXPECT(nearlyEqual(moved->x, 10.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(moved->y, 10.0f));
}

CNA_EDITOR_TEST(ASnappedRotationTurnsByWholeStepsFromWhereItStarted)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    // Deliberately not on a step: what a snapped rotation must preserve is the *turn*, so an
    // entity at 7 degrees turned by a snapped quarter lands on 97, not straightened to 90.
    const float start = 7.0f * 3.14159265f / 180.0f;
    setZRotation(scene, id, start);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, id);

    RotateGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, id, layout->getPointAt(0.0f)));

    GizmoSnap snap;
    snap.rotate = kDefaultRotationSnap;

    // Swept 20 degrees, which snaps to 15.
    const float swept = 20.0f * 3.14159265f / 180.0f;
    CNA_EDITOR_EXPECT(nearlyEqual(drag.getDeltaAngle(*layout, layout->getPointAt(swept), snap),
                                  kDefaultRotationSnap, 0.001f));

    const std::optional<EditorQuaternion> turned =
        drag.update(*layout, layout->getPointAt(swept), snap);
    CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(*turned), start + kDefaultRotationSnap, 0.001f));
}

CNA_EDITOR_TEST(ASnappedScaleLandsOnRoundNumbers)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);

    ScaleGizmoDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, *layout, id, GizmoHandle::XAxis, layout->getXTip()));

    GizmoSnap snap;
    snap.scale = kDefaultScaleSnap;

    // 1.93 of the way out, which is 1.9 once snapped -- what a user wants from a snapped scale is
    // an entity at a round number, not one at an arbitrary number times a round factor.
    const EditorVector2 cursor{layout->origin.x + (layout->getXTip().x - layout->origin.x) * 1.93f,
                               layout->origin.y};
    CNA_EDITOR_EXPECT(nearlyEqual(drag.update(*layout, cursor, snap)->x, 1.9f, 0.0001f));

    // And without the modifier it is exactly where the cursor is.
    CNA_EDITOR_EXPECT(nearlyEqual(drag.update(*layout, cursor)->x, 1.93f, 0.0001f));
}

CNA_EDITOR_TEST(ASelectionsPivotIsTheAverageOfItsEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", 0.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 40.0f);

    const std::optional<EditorVector2> pivot = computeSelectionPivot(scene, {a, b});
    CNA_EDITOR_EXPECT(pivot.has_value());
    CNA_EDITOR_EXPECT(nearlyEqual(pivot->x, 50.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(pivot->y, 20.0f));

    // Nothing selected, or nothing with a transform, has no pivot at all.
    CNA_EDITOR_EXPECT(!computeSelectionPivot(scene, {}).has_value());
}

CNA_EDITOR_TEST(ASelectionsRootsExcludeDescendantsOfOtherSelectedEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    const Uuid child = addEntity(scene, registry, "Child", 10.0f, 0.0f);
    const Uuid grandchild = addEntity(scene, registry, "Grandchild", 10.0f, 0.0f);
    const Uuid loner = addEntity(scene, registry, "Loner", 500.0f, 0.0f);
    scene.reparentEntity(child, parent);
    scene.reparentEntity(grandchild, child);

    // A child moves when its parent does, so transforming both would move it twice -- once by the
    // parent's transform and once by its own.
    const std::vector<Uuid> roots = findSelectionRoots(scene, {parent, child, grandchild, loner});
    CNA_EDITOR_EXPECT_EQ(roots.size(), std::size_t{2});
    CNA_EDITOR_EXPECT(roots[0] == parent);
    CNA_EDITOR_EXPECT(roots[1] == loner);

    // A child selected without its parent is a root of the selection, whatever the hierarchy says.
    const std::vector<Uuid> orphaned = findSelectionRoots(scene, {grandchild});
    CNA_EDITOR_EXPECT_EQ(orphaned.size(), std::size_t{1});
}

CNA_EDITOR_TEST(AMultiDragMovesEveryEntityByTheSameWorldDelta)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", 0.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 40.0f);

    MultiTransformDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, {a, b}, *computeSelectionPivot(scene, {a, b})));
    CNA_EDITOR_EXPECT_EQ(drag.getEntityCount(), std::size_t{2});

    const std::vector<EntityTransformEdit> edits = drag.translate(scene, EditorVector2{25.0f, -5.0f});
    CNA_EDITOR_EXPECT_EQ(edits.size(), std::size_t{2});
    CNA_EDITOR_EXPECT(nearlyEqual(edits[0].position->x, 25.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[0].position->y, -5.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[1].position->x, 125.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[1].position->y, 35.0f));

    // Nothing else is touched: a move is a move.
    CNA_EDITOR_EXPECT(!edits[0].rotation.has_value());
    CNA_EDITOR_EXPECT(!edits[0].scale.has_value());
}

CNA_EDITOR_TEST(AMultiRotationCarriesEntitiesAroundThePivotAndTurnsThem)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", -100.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 0.0f);

    MultiTransformDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, {a, b}, *computeSelectionPivot(scene, {a, b})));

    // A quarter turn about the origin between them: each swaps its X offset for a Y one, and each
    // is turned by the same quarter. Rotating a group in place instead would leave them side by
    // side, which is not what rotating an arrangement means.
    const std::vector<EntityTransformEdit> edits = drag.rotate(scene, kQuarterTurn);

    CNA_EDITOR_EXPECT(nearlyEqual(edits[0].position->x, 0.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[0].position->y, -100.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[1].position->x, 0.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[1].position->y, 100.0f, 0.01f));
    CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(*edits[0].rotation), kQuarterTurn, 0.001f));
}

CNA_EDITOR_TEST(AMultiScaleSpreadsEntitiesAwayFromThePivot)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", -100.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 0.0f);

    MultiTransformDrag drag;
    CNA_EDITOR_EXPECT(drag.begin(scene, {a, b}, *computeSelectionPivot(scene, {a, b})));

    const std::vector<EntityTransformEdit> edits = drag.scale(scene, EditorVector2{2.0f, 2.0f});

    // Both the entities and the distances between them: a group scaled up with everything left in
    // place would simply overlap itself.
    CNA_EDITOR_EXPECT(nearlyEqual(edits[0].position->x, -200.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[1].position->x, 200.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[0].scale->x, 2.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(edits[0].scale->y, 2.0f));
}

CNA_EDITOR_TEST(EveryGizmoLayoutRefusesAnEntityWithNoTransform)
{
    SceneDocument scene;
    const Uuid id = scene.addEntity(EditorEntity{Uuid::generate(), "Bare"});

    EditorCamera2D camera;
    camera.setViewportSize(EditorVector2{800.0f, 600.0f});

    CNA_EDITOR_EXPECT(!computeTranslateGizmoLayout(scene, camera, id).has_value());
    CNA_EDITOR_EXPECT(!computeRotateGizmoLayout(scene, camera, id).has_value());
    CNA_EDITOR_EXPECT(!computeScaleGizmoLayout(scene, camera, id).has_value());
}

CNA_EDITOR_TEST(WorldDeltaToLocalIsIdentityForRootEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Root", 0.0f, 0.0f);

    const EditorVector2 delta = worldDeltaToLocal(scene, id, EditorVector2{12.0f, -34.0f});
    CNA_EDITOR_EXPECT(nearlyEqual(delta.x, 12.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(delta.y, -34.0f));
}

// ---------------------------------------------------------------------------------------------
// The game's own camera
// ---------------------------------------------------------------------------------------------

CNA_EDITOR_TEST(TheGameViewComesFromThePrimaryCamera)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid camera = addEntity(scene, registry, "Main Camera", 400.0f, 300.0f);
    addComponent(scene, registry, camera, BuiltinComponentIds::kCamera);
    scene.findEntity(camera)->findComponent(BuiltinComponentIds::kCamera)
        ->setProperty("orthographicSize", PropertyValue{600.0f});

    const GameView view = computeGameView(scene, EditorVector2{1280.0f, 720.0f});
    CNA_EDITOR_EXPECT(view.hasCamera());
    CNA_EDITOR_EXPECT(view.cameraId == camera);

    // Centred on the camera entity, and zoomed so the orthographic size is the visible *height*.
    // Reading it as a width would show the right amount of world on a square window and the wrong
    // amount on every other one.
    CNA_EDITOR_EXPECT(nearlyEqual(view.camera.getCenter().x, 400.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(view.camera.getCenter().y, 300.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(view.camera.getZoom(), 720.0f / 600.0f));

    // A wider window at the same height shows more world sideways rather than stretching what was
    // already there.
    const GameView wider = computeGameView(scene, EditorVector2{1920.0f, 720.0f});
    CNA_EDITOR_EXPECT(nearlyEqual(wider.camera.getZoom(), view.camera.getZoom()));
}

CNA_EDITOR_TEST(TheGameViewClearsToTheCamerasOwnColour)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid camera = addEntity(scene, registry, "Main Camera", 0.0f, 0.0f);
    addComponent(scene, registry, camera, BuiltinComponentIds::kCamera);
    scene.findEntity(camera)->findComponent(BuiltinComponentIds::kCamera)
        ->setProperty("clearColor", PropertyValue{EditorColor{10, 20, 30, 255}});

    const GameView view = computeGameView(scene, EditorVector2{800.0f, 600.0f});
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(view.clearColor.r), 10);
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(view.clearColor.g), 20);
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(view.clearColor.b), 30);
}

CNA_EDITOR_TEST(TheGameViewIgnoresDisabledAndNonPrimaryCameras)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid disabled = addEntity(scene, registry, "Cutscene Camera", 900.0f, 900.0f);
    addComponent(scene, registry, disabled, BuiltinComponentIds::kCamera);
    scene.findEntity(disabled)->setEnabled(false);

    const Uuid secondary = addEntity(scene, registry, "Minimap Camera", 700.0f, 700.0f);
    addComponent(scene, registry, secondary, BuiltinComponentIds::kCamera);
    scene.findEntity(secondary)->findComponent(BuiltinComponentIds::kCamera)
        ->setProperty("isPrimary", PropertyValue{false});

    const Uuid primary = addEntity(scene, registry, "Main Camera", 100.0f, 100.0f);
    addComponent(scene, registry, primary, BuiltinComponentIds::kCamera);

    // A disabled entity is not in the game at all, so its camera is not either; and the one that
    // claims to be primary wins over one that does not, whatever the document order.
    const GameView view = computeGameView(scene, EditorVector2{800.0f, 600.0f});
    CNA_EDITOR_EXPECT(view.cameraId == primary);
}

CNA_EDITOR_TEST(ASceneWithNoCameraStillGetsAView)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    addEntity(scene, registry, "Player", 50.0f, 50.0f);

    // A player that refused to draw would be unable to show the very scene the user is trying to
    // fix. Drawing from the origin at 1:1 is wrong in a way they can see and act on.
    const GameView view = computeGameView(scene, EditorVector2{800.0f, 600.0f});
    CNA_EDITOR_EXPECT(!view.hasCamera());
    CNA_EDITOR_EXPECT(nearlyEqual(view.camera.getZoom(), 1.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(view.camera.getCenter().x, 0.0f));
}

CNA_EDITOR_TEST(TheGameViewFollowsACameraParentedToSomethingElse)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid rig = addEntity(scene, registry, "Camera Rig", 500.0f, 0.0f);
    const Uuid camera = addEntity(scene, registry, "Main Camera", 20.0f, 30.0f);
    addComponent(scene, registry, camera, BuiltinComponentIds::kCamera);
    scene.reparentEntity(camera, rig);

    // The world transform, not the local position: a camera on a rig is the ordinary way to move
    // one, and reading its local offset would leave the view sitting at the origin.
    const GameView view = computeGameView(scene, EditorVector2{800.0f, 600.0f});
    CNA_EDITOR_EXPECT(nearlyEqual(view.camera.getCenter().x, 520.0f));
    CNA_EDITOR_EXPECT(nearlyEqual(view.camera.getCenter().y, 30.0f));
}

CNA_EDITOR_TEST(CamerasAndLightsGetIconsAndSpritesDoNot)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Camera", 0.0f, 0.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    const Uuid lightId = addEntity(scene, registry, "Light", 50.0f, 0.0f);
    addComponent(scene, registry, lightId, BuiltinComponentIds::kLight);

    const Uuid spriteId = addEntity(scene, registry, "Sprite", 100.0f, 0.0f);
    addSprite(scene, registry, spriteId, 32, 32);

    CNA_EDITOR_EXPECT(getEditorIconKind(*scene.findEntity(cameraId)) == EditorIconKind::Camera);
    CNA_EDITOR_EXPECT(getEditorIconKind(*scene.findEntity(lightId)) == EditorIconKind::Light);

    // A sprite is already visible and already clickable, so an icon would be noise.
    CNA_EDITOR_EXPECT(getEditorIconKind(*scene.findEntity(spriteId)) == EditorIconKind::None);
}

CNA_EDITOR_TEST(AnEntityWithNoTransformGetsNoIcon)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    EditorEntity entity{Uuid::generate(), "Floating Camera"};
    EditorComponent camera{BuiltinComponentIds::kCamera};
    camera.applyDefaults(*registry.find(BuiltinComponentIds::kCamera));
    entity.addComponent(std::move(camera));
    const Uuid id = scene.addEntity(std::move(entity));

    // No transform means no position to draw at, and therefore nothing to click.
    CNA_EDITOR_EXPECT(getEditorIconKind(*scene.findEntity(id)) == EditorIconKind::None);

    EditorCamera2D view;
    view.setViewportSize(EditorVector2{800.0f, 600.0f});
    CNA_EDITOR_EXPECT_EQ(collectEditorIcons(scene, view).size(), std::size_t{0});
}

CNA_EDITOR_TEST(ClickingACameraIconSelectsIt)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 120.0f, 40.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    EditorCamera2D view;
    view.setViewportSize(EditorVector2{800.0f, 600.0f});

    // A camera has no bounds at all, so before icons existed this click found nothing and the
    // entity was reachable only through the hierarchy panel.
    const EditorVector2 onIcon = view.worldToScreen(EditorVector2{120.0f, 40.0f});
    CNA_EDITOR_EXPECT_EQ(pickEntityAt(scene, view, onIcon, kNoSizes).entityId.toString(),
                         cameraId.toString());

    // And just outside the badge, nothing.
    const EditorVector2 offIcon{onIcon.x + kEditorIconExtent + 4.0f, onIcon.y};
    CNA_EDITOR_EXPECT(!pickEntityAt(scene, view, offIcon, kNoSizes).entityId.isValid());
}

CNA_EDITOR_TEST(AnIconWinsOverASpriteBehindIt)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    // A large sprite covering the origin, and a camera parked on top of it.
    const Uuid backdropId = addEntity(scene, registry, "Backdrop", 0.0f, 0.0f);
    addSprite(scene, registry, backdropId, 400, 400);

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 10.0f, 10.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    EditorCamera2D view;
    view.setViewportSize(EditorVector2{800.0f, 600.0f});

    // Icons are drawn last, so a click on one must select its entity even where the sprite covers
    // the same point -- otherwise a camera over the level art is unselectable exactly where it is.
    const EditorVector2 onIcon = view.worldToScreen(EditorVector2{10.0f, 10.0f});
    CNA_EDITOR_EXPECT_EQ(pickEntityAt(scene, view, onIcon, kNoSizes).entityId.toString(),
                         cameraId.toString());

    // Away from the icon the sprite still wins, so the icon steals only what it covers.
    const EditorVector2 onSprite = view.worldToScreen(EditorVector2{200.0f, 200.0f});
    CNA_EDITOR_EXPECT_EQ(pickEntityAt(scene, view, onSprite, kNoSizes).entityId.toString(),
                         backdropId.toString());
}

CNA_EDITOR_TEST(IconsKeepTheirScreenSizeAtAnyZoom)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 500.0f, 500.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    EditorCamera2D view;
    view.setViewportSize(EditorVector2{800.0f, 600.0f});
    view.setCenter(EditorVector2{500.0f, 500.0f});

    for (const float zoom : {0.05f, 1.0f, 16.0f})
    {
        view.setZoom(zoom);

        const std::vector<EditorIconPlacement> icons = collectEditorIcons(scene, view);
        CNA_EDITOR_EXPECT_EQ(icons.size(), std::size_t{1});
        if (icons.empty()) { continue; }

        // The badge is a fixed number of pixels whatever the zoom. One that shrank with the view
        // would vanish exactly when it is the only way left to find the entity.
        const EditorVector2 edge{icons.front().center.x + kEditorIconExtent - 1.0f,
                                 icons.front().center.y};
        CNA_EDITOR_EXPECT_EQ(pickEntityAt(scene, view, edge, kNoSizes).entityId.toString(),
                             cameraId.toString());
    }
}

CNA_EDITOR_TEST(DisabledEntitiesGetNoIcon)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 0.0f, 0.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);
    scene.findEntity(cameraId)->setEnabled(false);

    EditorCamera2D view;
    view.setViewportSize(EditorVector2{800.0f, 600.0f});

    // Matching the sprite pass and the picker: what cannot be clicked is not drawn.
    CNA_EDITOR_EXPECT_EQ(collectEditorIcons(scene, view).size(), std::size_t{0});
    CNA_EDITOR_EXPECT(!pickEntityAt(scene, view, view.worldToScreen(EditorVector2{0.0f, 0.0f}),
                                    kNoSizes).entityId.isValid());
}

CNA_EDITOR_TEST(EulerAnglesRoundTripThroughAQuaternion)
{
    const EditorVector3 cases[] = {
        EditorVector3{0.0f, 0.0f, 0.0f},
        EditorVector3{0.0f, 0.0f, 45.0f},
        EditorVector3{0.0f, 0.0f, -170.0f},
        EditorVector3{30.0f, 0.0f, 0.0f},
        EditorVector3{0.0f, 120.0f, 0.0f},
        EditorVector3{15.0f, -60.0f, 100.0f},
        EditorVector3{-89.0f, 33.0f, -12.0f},
    };

    for (const EditorVector3& degrees : cases)
    {
        const EditorVector3 back = eulerDegreesOf(quaternionFromEulerDegrees(degrees));
        CNA_EDITOR_EXPECT(nearlyEqual(back.x, degrees.x, 0.01f));
        CNA_EDITOR_EXPECT(nearlyEqual(back.y, degrees.y, 0.01f));
        CNA_EDITOR_EXPECT(nearlyEqual(back.z, degrees.z, 0.01f));
    }
}

CNA_EDITOR_TEST(TheEulerConventionMatchesTheZRotationTheRendererUses)
{
    // The renderer passes zRotationOf() to SpriteBatch::Draw, so a roll typed into the inspector
    // has to be the angle the sprite actually turns by. Two conventions that disagreed here would
    // make the number in the inspector a decoration.
    for (const float roll : {0.0f, 30.0f, -90.0f, 150.0f})
    {
        const EditorQuaternion rotation = quaternionFromEulerDegrees(EditorVector3{0.0f, 0.0f, roll});

        constexpr float kToDegrees = 180.0f / 3.14159265358979323846f;
        CNA_EDITOR_EXPECT(nearlyEqual(zRotationOf(rotation) * kToDegrees, roll, 0.01f));

        // And it agrees with the dedicated 2D helper, which is the other way a rotation is built.
        const EditorQuaternion viaHelper = quaternionFromZRotation(roll / kToDegrees);
        CNA_EDITOR_EXPECT(nearlyEqual(rotation.z, viaHelper.z, 0.0001f));
        CNA_EDITOR_EXPECT(nearlyEqual(rotation.w, viaHelper.w, 0.0001f));
    }
}

CNA_EDITOR_TEST(EulerExtractionSurvivesGimbalLock)
{
    // At a pole, yaw and roll are not separable: every pair with the same sum (or difference)
    // names the same rotation. Reporting *a* valid answer matters more than which one, but it
    // must still be one that rebuilds the same rotation.
    for (const float pitch : {90.0f, -90.0f})
    {
        const EditorQuaternion original =
            quaternionFromEulerDegrees(EditorVector3{pitch, 40.0f, 25.0f});

        const EditorVector3 extracted = eulerDegreesOf(original);
        CNA_EDITOR_EXPECT(nearlyEqual(extracted.x, pitch, 0.05f));
        CNA_EDITOR_EXPECT(nearlyEqual(extracted.z, 0.0f, 0.001f));

        const EditorQuaternion rebuilt = quaternionFromEulerDegrees(extracted);

        // q and -q are the same rotation, so compare what they do rather than their components.
        const EditorVector3 probe{1.0f, 2.0f, 3.0f};
        const EditorVector3 a = rotate(original, probe);
        const EditorVector3 b = rotate(rebuilt, probe);
        CNA_EDITOR_EXPECT(nearlyEqual(a.x, b.x, 0.01f));
        CNA_EDITOR_EXPECT(nearlyEqual(a.y, b.y, 0.01f));
        CNA_EDITOR_EXPECT(nearlyEqual(a.z, b.z, 0.01f));
    }
}

CNA_EDITOR_TEST(AQuaternionRoundTripsThroughEulerAsTheSameRotation)
{
    // The other direction: start from a quaternion nobody typed and check the angles shown for it
    // rebuild it. This is what the inspector does every frame it is not reusing its cache.
    const EditorQuaternion rotations[] = {
        quaternionFromEulerDegrees(EditorVector3{12.0f, 200.0f, -75.0f}),
        quaternionFromEulerDegrees(EditorVector3{-44.0f, -160.0f, 5.0f}),
        multiply(quaternionFromZRotation(0.7f), quaternionFromEulerDegrees(EditorVector3{20.0f, 10.0f, 0.0f})),
    };

    for (const EditorQuaternion& rotation : rotations)
    {
        const EditorQuaternion rebuilt = quaternionFromEulerDegrees(eulerDegreesOf(rotation));

        const EditorVector3 probe{0.3f, -1.7f, 2.1f};
        const EditorVector3 a = rotate(rotation, probe);
        const EditorVector3 b = rotate(rebuilt, probe);
        CNA_EDITOR_EXPECT(nearlyEqual(a.x, b.x, 0.001f));
        CNA_EDITOR_EXPECT(nearlyEqual(a.y, b.y, 0.001f));
        CNA_EDITOR_EXPECT(nearlyEqual(a.z, b.z, 0.001f));
    }
}

CNA_EDITOR_TEST(AnAnimatedSpriteIsSizedByItsFrameNotItsSheet)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    SceneDocument scene;
    EditorEntity entity{Uuid::generate(), "Hero"};

    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
    entity.addComponent(std::move(transform));

    EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    const Uuid sheetId = Uuid::generate();
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{sheetId}});
    entity.addComponent(std::move(sprite));

    EditorComponent animation{BuiltinComponentIds::kSpriteAnimation};
    animation.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteAnimation));
    animation.setProperty(SpriteAnimationKeys::kFrameWidth, PropertyValue{std::int64_t{32}});
    animation.setProperty(SpriteAnimationKeys::kFrameHeight, PropertyValue{std::int64_t{48}});
    entity.addComponent(std::move(animation));

    const Uuid entityId = scene.addEntity(std::move(entity));

    // The sheet is sixteen frames wide. Without the animation the bounds would be the whole sheet,
    // which is sixteen times too wide to click accurately and would make Frame Selected zoom out
    // to fit a strip nobody is looking at.
    const SpriteSizeProvider sheetSize = [&](const Uuid& id) {
        return id == sheetId ? EditorVector2{512.0f, 48.0f} : EditorVector2{};
    };

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, entityId, sheetSize);
    CNA_EDITOR_EXPECT(bounds.has_value());
    if (!bounds) { return; }

    CNA_EDITOR_EXPECT_EQ(bounds->max.x - bounds->min.x, 32.0f);
    CNA_EDITOR_EXPECT_EQ(bounds->max.y - bounds->min.y, 48.0f);
}
