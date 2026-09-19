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

#include <set>

#include <cmath>

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/StudioCamera2D.hpp"
#include "CNA/Studio/Scene/StudioIcons.hpp"
#include "CNA/Studio/Scene/GameCamera.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"
#include "CNA/Studio/Scene/SpriteAnimation.hpp"
#include "CNA/Studio/Scene/TransformGizmos.hpp"

using namespace CNA::Studio;

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
        StudioEntity entity{Uuid::generate(), std::move(name)};
        StudioComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
        transform.setProperty("position", PropertyValue{StudioVector3{x, y, 0.0f}});
        transform.setProperty("scale", PropertyValue{StudioVector3{scale, scale, 1.0f}});
        entity.addComponent(std::move(transform));
        return scene.addEntity(std::move(entity));
    }

    /** @brief Gives @p entityId a sprite of the given size, via its source rectangle. */
    void addSprite(SceneDocument& scene, const ComponentRegistry& registry, const Uuid& entityId,
                   int width, int height, float layerDepth = 0.5f)
    {
        StudioComponent sprite{BuiltinComponentIds::kSpriteRenderer};
        sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
        sprite.setProperty("sourceRectangle", PropertyValue{StudioRectangle{0, 0, width, height}});
        sprite.setProperty("layerDepth", PropertyValue{layerDepth});
        scene.findEntityForEdit(entityId)->addComponent(std::move(sprite));
    }

    /** @brief Adds a component of @p typeId, populated with its declared defaults. */
    void addComponent(SceneDocument& scene, const ComponentRegistry& registry, const Uuid& entityId,
                      const char* typeId)
    {
        StudioComponent component{typeId};
        component.applyDefaults(*registry.find(typeId));
        scene.findEntityForEdit(entityId)->addComponent(std::move(component));
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
        scene.findEntityForEdit(entityId)->findComponent(BuiltinComponentIds::kTransform)
            ->setProperty("rotation", PropertyValue{quaternionFromZRotation(radians)});
    }

    /** @brief A size provider that reports nothing, exercising the unknown-size fallback. */
    const SpriteSizeProvider kNoSizes = [](const Uuid&) { return StudioVector2{}; };
}

CNA_STUDIO_TEST(WorldTransformComposesThroughTheParentChain)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 100.0f, 50.0f, 2.0f);
    const Uuid child = addEntity(scene, registry, "Child", 10.0f, 0.0f);
    scene.reparentEntity(child, parent);

    const std::optional<WorldTransform> world = computeWorldTransform(scene, child);
    CNA_STUDIO_EXPECT(world.has_value());

    // The child's local offset is scaled by the parent before being added: 100 + 10*2.
    CNA_STUDIO_EXPECT(nearlyEqual(world->position.x, 120.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(world->position.y, 50.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(world->scale.x, 2.0f));
}

CNA_STUDIO_TEST(WorldTransformAppliesParentRotation)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    scene.findEntityForEdit(parent)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{quaternionFromZRotation(3.14159265f * 0.5f)});

    const Uuid child = addEntity(scene, registry, "Child", 10.0f, 0.0f);
    scene.reparentEntity(child, parent);

    const std::optional<WorldTransform> world = computeWorldTransform(scene, child);
    CNA_STUDIO_EXPECT(world.has_value());

    // A quarter turn about Z takes the local +X offset onto world +Y.
    CNA_STUDIO_EXPECT(nearlyEqual(world->position.x, 0.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(world->position.y, 10.0f, 0.01f));
}

CNA_STUDIO_TEST(QuaternionZRotationRoundTrips)
{
    for (float angle : {0.0f, 0.5f, 1.5f, -2.0f, 3.0f})
    {
        CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(quaternionFromZRotation(angle)), angle, 0.001f));
    }
}

CNA_STUDIO_TEST(EntityBoundsUseTheSourceRectangleAndOrigin)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = addEntity(scene, registry, "Sprite", 100.0f, 200.0f);
    addSprite(scene, registry, id, 32, 16);

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_STUDIO_EXPECT(bounds.has_value());
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->min.x, 100.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.x, 132.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.y, 216.0f));

    // The origin shifts the sprite, exactly as SpriteBatch::Draw's origin parameter does.
    scene.findEntityForEdit(id)->findComponent(BuiltinComponentIds::kSpriteRenderer)
        ->setProperty("origin", PropertyValue{StudioVector2{16.0f, 8.0f}});

    const std::optional<WorldBounds2D> centred = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_STUDIO_EXPECT(nearlyEqual(centred->min.x, 84.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(centred->max.x, 116.0f));
}

CNA_STUDIO_TEST(EntityBoundsFallBackWhenTheTextureSizeIsUnknown)
{
    // A sprite whose texture failed to import must still be clickable, or the entity cannot be
    // selected and therefore cannot be fixed.
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = addEntity(scene, registry, "Broken", 0.0f, 0.0f);
    StudioComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{Uuid::generate()}});
    scene.findEntityForEdit(id)->addComponent(std::move(sprite));

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_STUDIO_EXPECT(bounds.has_value());
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.x - bounds->min.x, kUnknownSpriteExtent));
}

CNA_STUDIO_TEST(EntityBoundsUseTheProviderWhenNoSourceRectangleIsSet)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid textureId = Uuid::generate();
    const Uuid id = addEntity(scene, registry, "Sprite", 0.0f, 0.0f);
    StudioComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{textureId}});
    scene.findEntityForEdit(id)->addComponent(std::move(sprite));

    const SpriteSizeProvider provider = [textureId](const Uuid& asset) {
        return asset == textureId ? StudioVector2{48.0f, 24.0f} : StudioVector2{};
    };

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, provider);
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.x, 48.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.y, 24.0f));
}

CNA_STUDIO_TEST(RotatedSpriteBoundsCoverTheRotatedCorners)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = addEntity(scene, registry, "Sprite", 0.0f, 0.0f);
    addSprite(scene, registry, id, 100, 10);
    scene.findEntityForEdit(id)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{quaternionFromZRotation(3.14159265f * 0.5f)});

    // Rotated a quarter turn, a 100x10 sprite occupies a 10x100 box. Taking the AABB before the
    // rotation instead would leave most of the sprite unclickable.
    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, id, kNoSizes);
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.x - bounds->min.x, 10.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.y - bounds->min.y, 100.0f, 0.01f));
}

CNA_STUDIO_TEST(HierarchyBoundsCoverDescendants)
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
    CNA_STUDIO_EXPECT(bounds.has_value());
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->min.x, 0.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(bounds->max.x, 510.0f));
}

CNA_STUDIO_TEST(CameraRoundTripsBetweenWorldAndScreen)
{
    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    camera.setCenter(StudioVector2{100.0f, 50.0f});
    camera.setZoom(2.0f);

    // The camera centre sits at the middle of the viewport by definition.
    const StudioVector2 centreOnScreen = camera.worldToScreen(StudioVector2{100.0f, 50.0f});
    CNA_STUDIO_EXPECT(nearlyEqual(centreOnScreen.x, 400.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(centreOnScreen.y, 300.0f));

    for (const StudioVector2& world : {StudioVector2{0.0f, 0.0f}, StudioVector2{-250.0f, 375.0f},
                                       StudioVector2{1000.0f, -1000.0f}})
    {
        const StudioVector2 back = camera.screenToWorld(camera.worldToScreen(world));
        CNA_STUDIO_EXPECT(nearlyEqual(back.x, world.x, 0.01f));
        CNA_STUDIO_EXPECT(nearlyEqual(back.y, world.y, 0.01f));
    }
}

CNA_STUDIO_TEST(CameraPanTracksTheCursorAtAnyZoom)
{
    for (float zoom : {0.25f, 1.0f, 4.0f})
    {
        StudioCamera2D camera;
        camera.setViewportSize(StudioVector2{800.0f, 600.0f});
        camera.setZoom(zoom);

        const StudioVector2 grabScreen{200.0f, 150.0f};
        const StudioVector2 grabWorld = camera.screenToWorld(grabScreen);

        const StudioVector2 delta{60.0f, -25.0f};
        camera.panByScreenDelta(delta);

        // The world point grabbed must end up under the cursor's new position, or a drag drifts.
        const StudioVector2 nowAt = camera.worldToScreen(grabWorld);
        CNA_STUDIO_EXPECT(nearlyEqual(nowAt.x, grabScreen.x + delta.x, 0.01f));
        CNA_STUDIO_EXPECT(nearlyEqual(nowAt.y, grabScreen.y + delta.y, 0.01f));
    }
}

CNA_STUDIO_TEST(CameraZoomKeepsTheAnchorPointFixed)
{
    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    camera.setCenter(StudioVector2{10.0f, 20.0f});

    const StudioVector2 anchor{620.0f, 110.0f};
    const StudioVector2 anchorWorld = camera.screenToWorld(anchor);

    camera.zoomAt(anchor, 2.5f);

    // Wheel-zoom must keep what is under the pointer under the pointer; zooming about the view
    // centre instead makes the user chase their target across the screen.
    const StudioVector2 after = camera.worldToScreen(anchorWorld);
    CNA_STUDIO_EXPECT(nearlyEqual(after.x, anchor.x, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(after.y, anchor.y, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(camera.getZoom(), 2.5f));
}

CNA_STUDIO_TEST(CameraZoomStaysWithinItsLimits)
{
    StudioCamera2D camera;
    camera.setZoom(1000.0f);
    CNA_STUDIO_EXPECT(nearlyEqual(camera.getZoom(), StudioCamera2D::kMaxZoom));

    camera.setZoom(0.0f);
    CNA_STUDIO_EXPECT(nearlyEqual(camera.getZoom(), StudioCamera2D::kMinZoom));

    // Even when clamping bites, the anchor must not jump -- that is why zoomAt re-derives the
    // anchor's position after setZoom rather than computing the new centre algebraically.
    camera.setZoom(StudioCamera2D::kMaxZoom);
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    const StudioVector2 anchor{100.0f, 100.0f};
    const StudioVector2 anchorWorld = camera.screenToWorld(anchor);
    camera.zoomAt(anchor, 10.0f);
    const StudioVector2 after = camera.worldToScreen(anchorWorld);
    CNA_STUDIO_EXPECT(nearlyEqual(after.x, anchor.x, 0.01f));
}

CNA_STUDIO_TEST(CameraFramesBoundsWithinTheViewport)
{
    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    WorldBounds2D bounds;
    bounds.min = StudioVector2{-100.0f, -50.0f};
    bounds.max = StudioVector2{300.0f, 150.0f};
    camera.frame(bounds, 0.1f);

    CNA_STUDIO_EXPECT(nearlyEqual(camera.getCenter().x, 100.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(camera.getCenter().y, 50.0f));

    // Everything asked for must actually be on screen, with the margin respected.
    const StudioVector2 topLeft = camera.worldToScreen(bounds.min);
    const StudioVector2 bottomRight = camera.worldToScreen(bounds.max);
    CNA_STUDIO_EXPECT(topLeft.x >= 0.0f && topLeft.y >= 0.0f);
    CNA_STUDIO_EXPECT(bottomRight.x <= 800.0f && bottomRight.y <= 600.0f);
}

CNA_STUDIO_TEST(PickingSelectsTheEntityUnderTheCursor)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid left = addEntity(scene, registry, "Left", 0.0f, 0.0f);
    addSprite(scene, registry, left, 50, 50);
    const Uuid right = addEntity(scene, registry, "Right", 200.0f, 0.0f);
    addSprite(scene, registry, right, 50, 50);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    camera.setCenter(StudioVector2{100.0f, 25.0f});

    CNA_STUDIO_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(StudioVector2{25.0f, 25.0f}),
                                   kNoSizes).entityId == left);
    CNA_STUDIO_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(StudioVector2{225.0f, 25.0f}),
                                   kNoSizes).entityId == right);

    // Empty space selects nothing rather than the nearest thing.
    CNA_STUDIO_EXPECT(!pickEntityAt(scene, camera, camera.worldToScreen(StudioVector2{120.0f, 25.0f}),
                                    kNoSizes).entityId.isValid());
}

CNA_STUDIO_TEST(PickingPrefersTheFrontmostSprite)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid back = addEntity(scene, registry, "Back", 0.0f, 0.0f);
    addSprite(scene, registry, back, 100, 100, 0.9f);
    const Uuid front = addEntity(scene, registry, "Front", 0.0f, 0.0f);
    addSprite(scene, registry, front, 100, 100, 0.1f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    camera.setCenter(StudioVector2{50.0f, 50.0f});

    // XNA's convention: 0 is front, 1 is back.
    CNA_STUDIO_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(StudioVector2{50.0f, 50.0f}),
                                   kNoSizes).entityId == front);
}

CNA_STUDIO_TEST(PickingIgnoresDisabledEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid hidden = addEntity(scene, registry, "Hidden", 0.0f, 0.0f);
    addSprite(scene, registry, hidden, 100, 100, 0.1f);
    scene.findEntityForEdit(hidden)->setEnabled(false);

    const Uuid visible = addEntity(scene, registry, "Visible", 0.0f, 0.0f);
    addSprite(scene, registry, visible, 100, 100, 0.9f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    camera.setCenter(StudioVector2{50.0f, 50.0f});

    // The disabled entity is in front, so picking it would be the natural bug.
    CNA_STUDIO_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(StudioVector2{50.0f, 50.0f}),
                                   kNoSizes).entityId == visible);
}

CNA_STUDIO_TEST(PickingHonoursParentTransforms)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 1000.0f, 500.0f, 2.0f);
    const Uuid child = addEntity(scene, registry, "Child", 10.0f, 0.0f);
    addSprite(scene, registry, child, 20, 20);
    scene.reparentEntity(child, parent);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    camera.setCenter(StudioVector2{1030.0f, 520.0f});

    // The child sits at 1000 + 10*2 = 1020, scaled 2x so 40 units wide.
    CNA_STUDIO_EXPECT(pickEntityAt(scene, camera, camera.worldToScreen(StudioVector2{1040.0f, 520.0f}),
                                   kNoSizes).entityId == child);
    CNA_STUDIO_EXPECT(!pickEntityAt(scene, camera, camera.worldToScreen(StudioVector2{1010.0f, 520.0f}),
                                    kNoSizes).entityId.isValid());
}

// ---------------------------------------------------------------------------------------------
// Translate gizmo
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(GizmoLayoutSitsOnTheEntityAndKeepsItsScreenSize)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 300.0f, 120.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});
    camera.setCenter(StudioVector2{300.0f, 120.0f});

    const std::optional<TranslateGizmoLayout> layout = computeTranslateGizmoLayout(scene, camera, id);
    CNA_STUDIO_EXPECT(layout.has_value());
    CNA_STUDIO_EXPECT(nearlyEqual(layout->origin.x, 400.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(layout->origin.y, 300.0f));

    // Handles are sized in screen pixels, so the gizmo stays grabbable at any zoom. A gizmo that
    // shrinks as you zoom out is one you cannot grab exactly when you most need to.
    camera.setZoom(0.1f);
    const std::optional<TranslateGizmoLayout> zoomedOut = computeTranslateGizmoLayout(scene, camera, id);
    CNA_STUDIO_EXPECT(nearlyEqual(zoomedOut->axisLength, layout->axisLength));
    CNA_STUDIO_EXPECT(nearlyEqual(zoomedOut->centerExtent, layout->centerExtent));
}

CNA_STUDIO_TEST(GizmoHitTestDistinguishesItsHandles)
{
    TranslateGizmoLayout layout;
    layout.origin = StudioVector2{100.0f, 100.0f};

    CNA_STUDIO_EXPECT(hitTestTranslateGizmo(layout, StudioVector2{100.0f, 100.0f}) == GizmoHandle::Both);
    CNA_STUDIO_EXPECT(hitTestTranslateGizmo(layout, StudioVector2{150.0f, 100.0f}) == GizmoHandle::XAxis);
    CNA_STUDIO_EXPECT(hitTestTranslateGizmo(layout, StudioVector2{100.0f, 150.0f}) == GizmoHandle::YAxis);
    CNA_STUDIO_EXPECT(hitTestTranslateGizmo(layout, StudioVector2{300.0f, 300.0f}) == GizmoHandle::None);

    // Past the tip is a miss: the arms are segments, not infinite lines.
    CNA_STUDIO_EXPECT(hitTestTranslateGizmo(layout, StudioVector2{100.0f + layout.axisLength + 30.0f, 100.0f})
                      == GizmoHandle::None);

    // Slightly off an arm still counts, within the grab tolerance.
    CNA_STUDIO_EXPECT(hitTestTranslateGizmo(layout, StudioVector2{150.0f, 104.0f}) == GizmoHandle::XAxis);
}

CNA_STUDIO_TEST(GizmoDragConstrainsToTheGrabbedAxis)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, camera, id, GizmoHandle::XAxis,
                                 camera.worldToScreen(StudioVector2{0.0f, 0.0f})));
    CNA_STUDIO_EXPECT(drag.isActive());

    // A diagonal cursor move on the X arm must move only in X.
    const std::optional<StudioVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(StudioVector2{50.0f, 80.0f}));
    CNA_STUDIO_EXPECT(moved.has_value());
    CNA_STUDIO_EXPECT(nearlyEqual(moved->x, 50.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->y, 0.0f));

    drag.end();
    CNA_STUDIO_EXPECT(!drag.isActive());
    CNA_STUDIO_EXPECT(!drag.update(scene, camera, StudioVector2{0.0f, 0.0f}).has_value());
}

CNA_STUDIO_TEST(GizmoDragMeasuresFromTheGrabPointNotTheEntityOrigin)
{
    // Grabbing an arm away from its root must not teleport the entity to the cursor: the offset
    // between cursor and entity has to survive the whole drag.
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 10.0f, 20.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, camera, id, GizmoHandle::Both,
                                 camera.worldToScreen(StudioVector2{60.0f, 20.0f})));

    const std::optional<StudioVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(StudioVector2{70.0f, 25.0f}));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->x, 20.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->y, 25.0f));
}

CNA_STUDIO_TEST(GizmoDragDoesNotDriftOverManyUpdates)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    drag.begin(scene, camera, id, GizmoHandle::Both, camera.worldToScreen(StudioVector2{0.0f, 0.0f}));

    // Every update is measured from the grab point, so wandering around and returning must land
    // exactly back at the start. An implementation that accumulated frame deltas would not.
    for (int step = 0; step < 200; ++step)
    {
        const float t = static_cast<float>(step);
        // The result is deliberately dropped: what is being exercised is that these updates leave
        // no residue behind, which the final one below proves.
        (void)drag.update(scene, camera, camera.worldToScreen(StudioVector2{t * 3.0f, -t * 1.5f}));
    }

    const std::optional<StudioVector3> back =
        drag.update(scene, camera, camera.worldToScreen(StudioVector2{0.0f, 0.0f}));
    CNA_STUDIO_EXPECT(nearlyEqual(back->x, 0.0f, 0.001f));
    CNA_STUDIO_EXPECT(nearlyEqual(back->y, 0.0f, 0.001f));
}

CNA_STUDIO_TEST(GizmoDragHonoursAScaledParent)
{
    // The position property is local. Dragging a child of a 2x parent by 100 world units must
    // change its stored position by 50, or the gizmo drifts on every child.
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f, 2.0f);
    const Uuid child = addEntity(scene, registry, "Child", 0.0f, 0.0f);
    scene.reparentEntity(child, parent);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    drag.begin(scene, camera, child, GizmoHandle::Both, camera.worldToScreen(StudioVector2{0.0f, 0.0f}));

    const std::optional<StudioVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(StudioVector2{100.0f, 0.0f}));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->x, 50.0f));
}

CNA_STUDIO_TEST(GizmoDragHonoursARotatedParent)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    scene.findEntityForEdit(parent)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{quaternionFromZRotation(3.14159265f * 0.5f)});

    const Uuid child = addEntity(scene, registry, "Child", 0.0f, 0.0f);
    scene.reparentEntity(child, parent);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    drag.begin(scene, camera, child, GizmoHandle::Both, camera.worldToScreen(StudioVector2{0.0f, 0.0f}));

    // Under a parent rotated a quarter turn, moving the child along world +Y is a change along its
    // own local -X... or +X depending on handedness; either way the magnitude is what is checked,
    // and the axis the drag did *not* move along must stay put.
    const std::optional<StudioVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(StudioVector2{0.0f, 30.0f}));
    CNA_STUDIO_EXPECT(nearlyEqual(std::fabs(moved->x), 30.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->y, 0.0f, 0.01f));
}

// ---------------------------------------------------------------------------------------------
// Local space, rotate gizmo and scale gizmo (ED-401)
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(LocalSpaceArmsFollowTheEntityRotation)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);
    setZRotation(scene, id, kQuarterTurn);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    // World space ignores the entity's rotation entirely: the arms are the screen's own axes.
    const auto world = computeTranslateGizmoLayout(scene, camera, id, GizmoSpace::World);
    CNA_STUDIO_EXPECT(nearlyEqual(world->xAxis.x, 1.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(world->xAxis.y, 0.0f));

    // Local space turns them with it: a quarter turn puts the X arm straight down the screen.
    const auto local = computeTranslateGizmoLayout(scene, camera, id, GizmoSpace::Local);
    CNA_STUDIO_EXPECT(nearlyEqual(local->xAxis.x, 0.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(local->xAxis.y, 1.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(local->yAxis.x, -1.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(local->yAxis.y, 0.0f));

    // The tips are still the arm length away, so the gizmo is the same size in both spaces.
    CNA_STUDIO_EXPECT(nearlyEqual(std::hypot(local->getXTip().x - local->origin.x,
                                             local->getXTip().y - local->origin.y),
                                  local->axisLength));
}

CNA_STUDIO_TEST(ALocalSpaceDragProjectsOntoTheRotatedArm)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);
    setZRotation(scene, id, kQuarterTurn);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, camera, id, GizmoHandle::XAxis,
                                 camera.worldToScreen(StudioVector2{0.0f, 0.0f}), GizmoSpace::Local));

    // The entity's own X points along world +Y after a quarter turn, so a diagonal cursor move
    // must move it only in world Y -- and, being a root entity, that is its stored position too.
    const std::optional<StudioVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(StudioVector2{50.0f, 80.0f}));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->x, 0.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->y, 80.0f, 0.01f));
}

CNA_STUDIO_TEST(RotateGizmoHitTestGrabsTheRingAndNotItsInterior)
{
    RotateGizmoLayout layout;
    layout.origin = StudioVector2{100.0f, 100.0f};

    CNA_STUDIO_EXPECT(hitTestRotateGizmo(layout, layout.getPointAt(0.0f)) == GizmoHandle::ZAxis);
    CNA_STUDIO_EXPECT(hitTestRotateGizmo(layout, layout.getPointAt(2.0f)) == GizmoHandle::ZAxis);

    // Inside the ring is where the entity is. Grabbing there would make the sprite itself
    // unclickable whenever the rotate gizmo is up.
    CNA_STUDIO_EXPECT(hitTestRotateGizmo(layout, layout.origin) == GizmoHandle::None);
    CNA_STUDIO_EXPECT(hitTestRotateGizmo(layout, StudioVector2{100.0f, 130.0f}) == GizmoHandle::None);

    // And well outside it is a miss too -- the band is a band, not a half-plane.
    CNA_STUDIO_EXPECT(hitTestRotateGizmo(layout, StudioVector2{100.0f, 300.0f}) == GizmoHandle::None);
}

CNA_STUDIO_TEST(ARotateDragTurnsByTheAngleTheCursorSwept)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, id);
    CNA_STUDIO_EXPECT(layout.has_value());

    RotateGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, *layout, id, layout->getPointAt(0.0f)));

    const std::optional<StudioQuaternion> turned = drag.update(*layout, layout->getPointAt(kQuarterTurn));
    CNA_STUDIO_EXPECT(turned.has_value());
    CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(*turned), kQuarterTurn, 0.001f));

    // Sweeping back to where it started restores the original rotation exactly: every update is
    // measured from the press, so a long drag leaves no residue.
    const std::optional<StudioQuaternion> back = drag.update(*layout, layout->getPointAt(0.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(*back), 0.0f, 0.001f));
}

CNA_STUDIO_TEST(ARotateDragCrossingTheAngleSeamDoesNotSpin)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, id);

    RotateGizmoDrag drag;
    // Just below +pi, dragged just past it. atan2 wraps to -pi there, so an implementation that
    // subtracted raw angles would report nearly a full turn backwards.
    const float almostPi = 3.14159265f - 0.05f;
    CNA_STUDIO_EXPECT(drag.begin(scene, *layout, id, layout->getPointAt(almostPi)));

    const std::optional<StudioQuaternion> turned =
        drag.update(*layout, layout->getPointAt(-almostPi));
    CNA_STUDIO_EXPECT(nearlyEqual(std::fabs(zRotationOf(*turned)), 0.1f, 0.01f));
}

CNA_STUDIO_TEST(ARotateDragOnAChildTurnsItByTheSameWorldAngle)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid parent = addEntity(scene, registry, "Parent", 0.0f, 0.0f);
    setZRotation(scene, parent, kQuarterTurn);

    const Uuid child = addEntity(scene, registry, "Child", 0.0f, 0.0f);
    scene.reparentEntity(child, parent);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, child);

    RotateGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, *layout, child, layout->getPointAt(0.0f)));

    const std::optional<StudioQuaternion> turned = drag.update(*layout, layout->getPointAt(0.5f));
    CNA_STUDIO_EXPECT(turned.has_value());

    // The stored value is local, so it holds the turn alone -- the parent's quarter is not in it.
    CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(*turned), 0.5f, 0.001f));

    // And the entity really did end up half a radian round in the world, which is what the user
    // was pointing at. Getting this wrong gives a child that lags or races its own cursor.
    scene.findEntityForEdit(child)->findComponent(BuiltinComponentIds::kTransform)
        ->setProperty("rotation", PropertyValue{*turned});
    CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(computeWorldTransform(scene, child)->rotation),
                                  kQuarterTurn + 0.5f, 0.001f));
}

CNA_STUDIO_TEST(ScaleGizmoHitTestFindsItsHandles)
{
    ScaleGizmoLayout layout;
    layout.origin = StudioVector2{100.0f, 100.0f};

    CNA_STUDIO_EXPECT(hitTestScaleGizmo(layout, StudioVector2{100.0f, 100.0f}) == GizmoHandle::Both);
    CNA_STUDIO_EXPECT(hitTestScaleGizmo(layout, layout.getXTip()) == GizmoHandle::XAxis);
    CNA_STUDIO_EXPECT(hitTestScaleGizmo(layout, layout.getYTip()) == GizmoHandle::YAxis);
    CNA_STUDIO_EXPECT(hitTestScaleGizmo(layout, StudioVector2{140.0f, 100.0f}) == GizmoHandle::XAxis);
    CNA_STUDIO_EXPECT(hitTestScaleGizmo(layout, StudioVector2{300.0f, 300.0f}) == GizmoHandle::None);

    // The end square is a real target, so a press a few pixels off the arm's line but plainly on
    // its handle still counts -- that is what the square is drawn for.
    CNA_STUDIO_EXPECT(hitTestScaleGizmo(layout, StudioVector2{layout.getXTip().x, 105.0f})
                      == GizmoHandle::XAxis);
}

CNA_STUDIO_TEST(AScaleDragIsARatioOfHowFarTheHandleMoved)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);
    CNA_STUDIO_EXPECT(layout.has_value());

    ScaleGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, *layout, id, GizmoHandle::XAxis, layout->getXTip()));

    // Twice as far out is twice the scale, and the axis not grabbed is untouched.
    const StudioVector2 doubled{layout->origin.x + (layout->getXTip().x - layout->origin.x) * 2.0f,
                                layout->origin.y};
    const std::optional<StudioVector3> scaled = drag.update(*layout, doubled);
    CNA_STUDIO_EXPECT(nearlyEqual(scaled->x, 2.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(scaled->y, 1.0f));

    // Dragging through the origin flips the entity rather than sticking at zero: negative scale is
    // a legitimate edit and XNA's own SpriteBatch honours it.
    const StudioVector2 through{layout->origin.x - (layout->getXTip().x - layout->origin.x),
                                layout->origin.y};
    CNA_STUDIO_EXPECT(nearlyEqual(drag.update(*layout, through)->x, -1.0f));
}

CNA_STUDIO_TEST(TheUniformScaleHandleScalesBothAxesTogether)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f, 3.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);

    ScaleGizmoDrag drag;
    const StudioVector2 grab{layout->origin.x + 8.0f, layout->origin.y};
    CNA_STUDIO_EXPECT(drag.begin(scene, *layout, id, GizmoHandle::Both, grab));

    // Half the distance, half the scale -- and it multiplies what was already there rather than
    // replacing it, which is why an entity already at 3 lands on 1.5 rather than on 0.5.
    const std::optional<StudioVector3> scaled =
        drag.update(*layout, StudioVector2{layout->origin.x + 4.0f, layout->origin.y});
    CNA_STUDIO_EXPECT(nearlyEqual(scaled->x, 1.5f));
    CNA_STUDIO_EXPECT(nearlyEqual(scaled->y, 1.5f));
}

CNA_STUDIO_TEST(AScaleGrabAtTheOriginIsRefused)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);

    // Every factor is a division by how far out the grab was, so a grab on the pivot would scale
    // by infinity. Refusing lets the press fall through to whatever is underneath instead.
    ScaleGizmoDrag drag;
    CNA_STUDIO_EXPECT(!drag.begin(scene, *layout, id, GizmoHandle::Both, layout->origin));
    CNA_STUDIO_EXPECT(!drag.isActive());
}

CNA_STUDIO_TEST(ScaleGizmoArmsAreAlwaysLocal)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);
    setZRotation(scene, id, kQuarterTurn);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    // There is no space parameter to get wrong: a non-uniform scale in world space is not
    // representable in a position/rotation/scale transform, so the arms are the axes the stored
    // numbers actually belong to and nothing else.
    const auto layout = computeScaleGizmoLayout(scene, camera, id);
    CNA_STUDIO_EXPECT(nearlyEqual(layout->xAxis.x, 0.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(layout->xAxis.y, 1.0f));
}

CNA_STUDIO_TEST(SnappingRoundsTheResultRatherThanTheMovement)
{
    CNA_STUDIO_EXPECT(nearlyEqual(snapTo(13.7f, 10.0f), 10.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(snapTo(16.0f, 10.0f), 20.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(snapTo(-13.0f, 10.0f), -10.0f));

    // A step of zero means "do not snap", which is what an unmodified drag passes.
    CNA_STUDIO_EXPECT(nearlyEqual(snapTo(13.7f, 0.0f), 13.7f));

    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 3.0f, 7.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    TranslateGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, camera, id, GizmoHandle::Both,
                                 camera.worldToScreen(StudioVector2{3.0f, 7.0f})));

    GizmoSnap snap;
    snap.translate = 10.0f;

    // An entity that started at (3, 7) and was dragged by (9, 6) lands on (10, 10) -- not on
    // (13, 17), which is where snapping the *movement* would have put it. A grid is a set of
    // places things sit, not a set of distances they travel.
    const std::optional<StudioVector3> moved =
        drag.update(scene, camera, camera.worldToScreen(StudioVector2{12.0f, 13.0f}), snap);
    CNA_STUDIO_EXPECT(nearlyEqual(moved->x, 10.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(moved->y, 10.0f));
}

CNA_STUDIO_TEST(ASnappedRotationTurnsByWholeStepsFromWhereItStarted)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    // Deliberately not on a step: what a snapped rotation must preserve is the *turn*, so an
    // entity at 7 degrees turned by a snapped quarter lands on 97, not straightened to 90.
    const float start = 7.0f * 3.14159265f / 180.0f;
    setZRotation(scene, id, start);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeRotateGizmoLayout(scene, camera, id);

    RotateGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, *layout, id, layout->getPointAt(0.0f)));

    GizmoSnap snap;
    snap.rotate = kDefaultRotationSnap;

    // Swept 20 degrees, which snaps to 15.
    const float swept = 20.0f * 3.14159265f / 180.0f;
    CNA_STUDIO_EXPECT(nearlyEqual(drag.getDeltaAngle(*layout, layout->getPointAt(swept), snap),
                                  kDefaultRotationSnap, 0.001f));

    const std::optional<StudioQuaternion> turned =
        drag.update(*layout, layout->getPointAt(swept), snap);
    CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(*turned), start + kDefaultRotationSnap, 0.001f));
}

CNA_STUDIO_TEST(ASnappedScaleLandsOnRoundNumbers)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Target", 0.0f, 0.0f);

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    const auto layout = computeScaleGizmoLayout(scene, camera, id);

    ScaleGizmoDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, *layout, id, GizmoHandle::XAxis, layout->getXTip()));

    GizmoSnap snap;
    snap.scale = kDefaultScaleSnap;

    // 1.93 of the way out, which is 1.9 once snapped -- what a user wants from a snapped scale is
    // an entity at a round number, not one at an arbitrary number times a round factor.
    const StudioVector2 cursor{layout->origin.x + (layout->getXTip().x - layout->origin.x) * 1.93f,
                               layout->origin.y};
    CNA_STUDIO_EXPECT(nearlyEqual(drag.update(*layout, cursor, snap)->x, 1.9f, 0.0001f));

    // And without the modifier it is exactly where the cursor is.
    CNA_STUDIO_EXPECT(nearlyEqual(drag.update(*layout, cursor)->x, 1.93f, 0.0001f));
}

CNA_STUDIO_TEST(ASelectionsPivotIsTheAverageOfItsEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", 0.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 40.0f);

    const std::optional<StudioVector2> pivot = computeSelectionPivot(scene, {a, b});
    CNA_STUDIO_EXPECT(pivot.has_value());
    CNA_STUDIO_EXPECT(nearlyEqual(pivot->x, 50.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(pivot->y, 20.0f));

    // Nothing selected, or nothing with a transform, has no pivot at all.
    CNA_STUDIO_EXPECT(!computeSelectionPivot(scene, {}).has_value());
}

CNA_STUDIO_TEST(ASelectionsRootsExcludeDescendantsOfOtherSelectedEntities)
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
    CNA_STUDIO_EXPECT_EQ(roots.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(roots[0] == parent);
    CNA_STUDIO_EXPECT(roots[1] == loner);

    // A child selected without its parent is a root of the selection, whatever the hierarchy says.
    const std::vector<Uuid> orphaned = findSelectionRoots(scene, {grandchild});
    CNA_STUDIO_EXPECT_EQ(orphaned.size(), std::size_t{1});
}

CNA_STUDIO_TEST(AMultiDragMovesEveryEntityByTheSameWorldDelta)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", 0.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 40.0f);

    MultiTransformDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, {a, b}, *computeSelectionPivot(scene, {a, b})));
    CNA_STUDIO_EXPECT_EQ(drag.getEntityCount(), std::size_t{2});

    const std::vector<EntityTransformEdit> edits = drag.translate(scene, StudioVector2{25.0f, -5.0f});
    CNA_STUDIO_EXPECT_EQ(edits.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(nearlyEqual(edits[0].position->x, 25.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[0].position->y, -5.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[1].position->x, 125.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[1].position->y, 35.0f));

    // Nothing else is touched: a move is a move.
    CNA_STUDIO_EXPECT(!edits[0].rotation.has_value());
    CNA_STUDIO_EXPECT(!edits[0].scale.has_value());
}

CNA_STUDIO_TEST(AMultiRotationCarriesEntitiesAroundThePivotAndTurnsThem)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", -100.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 0.0f);

    MultiTransformDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, {a, b}, *computeSelectionPivot(scene, {a, b})));

    // A quarter turn about the origin between them: each swaps its X offset for a Y one, and each
    // is turned by the same quarter. Rotating a group in place instead would leave them side by
    // side, which is not what rotating an arrangement means.
    const std::vector<EntityTransformEdit> edits = drag.rotate(scene, kQuarterTurn);

    CNA_STUDIO_EXPECT(nearlyEqual(edits[0].position->x, 0.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[0].position->y, -100.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[1].position->x, 0.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[1].position->y, 100.0f, 0.01f));
    CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(*edits[0].rotation), kQuarterTurn, 0.001f));
}

CNA_STUDIO_TEST(AMultiScaleSpreadsEntitiesAwayFromThePivot)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid a = addEntity(scene, registry, "A", -100.0f, 0.0f);
    const Uuid b = addEntity(scene, registry, "B", 100.0f, 0.0f);

    MultiTransformDrag drag;
    CNA_STUDIO_EXPECT(drag.begin(scene, {a, b}, *computeSelectionPivot(scene, {a, b})));

    const std::vector<EntityTransformEdit> edits = drag.scale(scene, StudioVector2{2.0f, 2.0f});

    // Both the entities and the distances between them: a group scaled up with everything left in
    // place would simply overlap itself.
    CNA_STUDIO_EXPECT(nearlyEqual(edits[0].position->x, -200.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[1].position->x, 200.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[0].scale->x, 2.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(edits[0].scale->y, 2.0f));
}

CNA_STUDIO_TEST(EveryGizmoLayoutRefusesAnEntityWithNoTransform)
{
    SceneDocument scene;
    const Uuid id = scene.addEntity(StudioEntity{Uuid::generate(), "Bare"});

    StudioCamera2D camera;
    camera.setViewportSize(StudioVector2{800.0f, 600.0f});

    CNA_STUDIO_EXPECT(!computeTranslateGizmoLayout(scene, camera, id).has_value());
    CNA_STUDIO_EXPECT(!computeRotateGizmoLayout(scene, camera, id).has_value());
    CNA_STUDIO_EXPECT(!computeScaleGizmoLayout(scene, camera, id).has_value());
}

CNA_STUDIO_TEST(WorldDeltaToLocalIsIdentityForRootEntities)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    const Uuid id = addEntity(scene, registry, "Root", 0.0f, 0.0f);

    const StudioVector2 delta = worldDeltaToLocal(scene, id, StudioVector2{12.0f, -34.0f});
    CNA_STUDIO_EXPECT(nearlyEqual(delta.x, 12.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(delta.y, -34.0f));
}

// ---------------------------------------------------------------------------------------------
// The game's own camera
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheGameViewComesFromThePrimaryCamera)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid camera = addEntity(scene, registry, "Main Camera", 400.0f, 300.0f);
    addComponent(scene, registry, camera, BuiltinComponentIds::kCamera);
    scene.findEntityForEdit(camera)->findComponent(BuiltinComponentIds::kCamera)
        ->setProperty("orthographicSize", PropertyValue{600.0f});

    const GameView view = computeGameView(scene, StudioVector2{1280.0f, 720.0f});
    CNA_STUDIO_EXPECT(view.hasCamera());
    CNA_STUDIO_EXPECT(view.cameraId == camera);

    // Centred on the camera entity, and zoomed so the orthographic size is the visible *height*.
    // Reading it as a width would show the right amount of world on a square window and the wrong
    // amount on every other one.
    CNA_STUDIO_EXPECT(nearlyEqual(view.camera.getCenter().x, 400.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(view.camera.getCenter().y, 300.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(view.camera.getZoom(), 720.0f / 600.0f));

    // A wider window at the same height shows more world sideways rather than stretching what was
    // already there.
    const GameView wider = computeGameView(scene, StudioVector2{1920.0f, 720.0f});
    CNA_STUDIO_EXPECT(nearlyEqual(wider.camera.getZoom(), view.camera.getZoom()));
}

CNA_STUDIO_TEST(TheGameViewClearsToTheCamerasOwnColour)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid camera = addEntity(scene, registry, "Main Camera", 0.0f, 0.0f);
    addComponent(scene, registry, camera, BuiltinComponentIds::kCamera);
    scene.findEntityForEdit(camera)->findComponent(BuiltinComponentIds::kCamera)
        ->setProperty("clearColor", PropertyValue{StudioColor{10, 20, 30, 255}});

    const GameView view = computeGameView(scene, StudioVector2{800.0f, 600.0f});
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(view.clearColor.r), 10);
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(view.clearColor.g), 20);
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(view.clearColor.b), 30);
}

CNA_STUDIO_TEST(TheGameViewIgnoresDisabledAndNonPrimaryCameras)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid disabled = addEntity(scene, registry, "Cutscene Camera", 900.0f, 900.0f);
    addComponent(scene, registry, disabled, BuiltinComponentIds::kCamera);
    scene.findEntityForEdit(disabled)->setEnabled(false);

    const Uuid secondary = addEntity(scene, registry, "Minimap Camera", 700.0f, 700.0f);
    addComponent(scene, registry, secondary, BuiltinComponentIds::kCamera);
    scene.findEntityForEdit(secondary)->findComponent(BuiltinComponentIds::kCamera)
        ->setProperty("isPrimary", PropertyValue{false});

    const Uuid primary = addEntity(scene, registry, "Main Camera", 100.0f, 100.0f);
    addComponent(scene, registry, primary, BuiltinComponentIds::kCamera);

    // A disabled entity is not in the game at all, so its camera is not either; and the one that
    // claims to be primary wins over one that does not, whatever the document order.
    const GameView view = computeGameView(scene, StudioVector2{800.0f, 600.0f});
    CNA_STUDIO_EXPECT(view.cameraId == primary);
}

CNA_STUDIO_TEST(ASceneWithNoCameraStillGetsAView)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    addEntity(scene, registry, "Player", 50.0f, 50.0f);

    // A player that refused to draw would be unable to show the very scene the user is trying to
    // fix. Drawing from the origin at 1:1 is wrong in a way they can see and act on.
    const GameView view = computeGameView(scene, StudioVector2{800.0f, 600.0f});
    CNA_STUDIO_EXPECT(!view.hasCamera());
    CNA_STUDIO_EXPECT(nearlyEqual(view.camera.getZoom(), 1.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(view.camera.getCenter().x, 0.0f));
}

CNA_STUDIO_TEST(TheGameViewFollowsACameraParentedToSomethingElse)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid rig = addEntity(scene, registry, "Camera Rig", 500.0f, 0.0f);
    const Uuid camera = addEntity(scene, registry, "Main Camera", 20.0f, 30.0f);
    addComponent(scene, registry, camera, BuiltinComponentIds::kCamera);
    scene.reparentEntity(camera, rig);

    // The world transform, not the local position: a camera on a rig is the ordinary way to move
    // one, and reading its local offset would leave the view sitting at the origin.
    const GameView view = computeGameView(scene, StudioVector2{800.0f, 600.0f});
    CNA_STUDIO_EXPECT(nearlyEqual(view.camera.getCenter().x, 520.0f));
    CNA_STUDIO_EXPECT(nearlyEqual(view.camera.getCenter().y, 30.0f));
}

CNA_STUDIO_TEST(CamerasAndLightsGetIconsAndSpritesDoNot)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Camera", 0.0f, 0.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    const Uuid lightId = addEntity(scene, registry, "Light", 50.0f, 0.0f);
    addComponent(scene, registry, lightId, BuiltinComponentIds::kLight);

    const Uuid spriteId = addEntity(scene, registry, "Sprite", 100.0f, 0.0f);
    addSprite(scene, registry, spriteId, 32, 32);

    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(cameraId)) == StudioIconKind::Camera);
    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(lightId)) == StudioIconKind::Light);

    // A sprite is already visible and already clickable, so an icon would be noise.
    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(spriteId)) == StudioIconKind::None);
}

/**
 * An entity that draws nothing is a place, and gets a marker (`plan.md` STUDIO-11009).
 *
 * A spawn point, a trigger volume or a grouping node has a transform and nothing else. Before this
 * it classified as `None`, so the only thing that showed it was the small bounds box every entity
 * gets -- which reads as a tiny piece of geometry rather than as a position, and a level with ten
 * spawn points read as ten identical cubes. Asserted on the classification rather than on the
 * drawing because the same answer feeds both the 2D icon pass and the 3D badge.
 */
CNA_STUDIO_TEST(AnEntityThatDrawsNothingIsMarkedAsAPlace)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid spawnId = addEntity(scene, registry, "Spawn Point", 0.0f, 0.0f);
    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(spawnId)) == StudioIconKind::Empty);

    // Everything that puts pixels on the screen keeps its `None`: a marker on top of something the
    // viewport already draws is a second mark on one object, not a way to find an invisible one.
    const Uuid spriteId = addEntity(scene, registry, "Sprite", 50.0f, 0.0f);
    addSprite(scene, registry, spriteId, 32, 32);
    const Uuid animatedId = addEntity(scene, registry, "Animated", 100.0f, 0.0f);
    addComponent(scene, registry, animatedId, BuiltinComponentIds::kSpriteAnimation);
    const Uuid tilemapId = addEntity(scene, registry, "Tilemap", 150.0f, 0.0f);
    addComponent(scene, registry, tilemapId, BuiltinComponentIds::kTilemap);

    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(spriteId)) == StudioIconKind::None);
    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(animatedId)) == StudioIconKind::None);
    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(tilemapId)) == StudioIconKind::None);

    // A camera still reads as a camera: the marker is the fallback for what is left over, so it
    // must not have swallowed the kinds that were already answered above it.
    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 200.0f, 0.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);
    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(cameraId)) == StudioIconKind::Camera);

    // And the marker is collected, so it is drawn in the 2D view and can be clicked there. Only
    // the spawn point and the camera: an icon for each of the three drawing entities would be
    // three icons over three visible objects.
    StudioCamera2D view;
    view.setViewportSize(StudioVector2{800.0f, 600.0f});
    const std::vector<StudioIconPlacement> icons = collectStudioIcons(scene, view);
    CNA_STUDIO_EXPECT_EQ(icons.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(icons.front().entityId == spawnId);
    CNA_STUDIO_EXPECT(icons.front().kind == StudioIconKind::Empty);
}

CNA_STUDIO_TEST(AnEntityWithNoTransformGetsNoIcon)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    StudioEntity entity{Uuid::generate(), "Floating Camera"};
    StudioComponent camera{BuiltinComponentIds::kCamera};
    camera.applyDefaults(*registry.find(BuiltinComponentIds::kCamera));
    entity.addComponent(std::move(camera));
    const Uuid id = scene.addEntity(std::move(entity));

    // No transform means no position to draw at, and therefore nothing to click.
    CNA_STUDIO_EXPECT(getStudioIconKind(*scene.findEntity(id)) == StudioIconKind::None);

    StudioCamera2D view;
    view.setViewportSize(StudioVector2{800.0f, 600.0f});
    CNA_STUDIO_EXPECT_EQ(collectStudioIcons(scene, view).size(), std::size_t{0});
}

CNA_STUDIO_TEST(ClickingACameraIconSelectsIt)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 120.0f, 40.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    StudioCamera2D view;
    view.setViewportSize(StudioVector2{800.0f, 600.0f});

    // A camera has no bounds at all, so before icons existed this click found nothing and the
    // entity was reachable only through the hierarchy panel.
    const StudioVector2 onIcon = view.worldToScreen(StudioVector2{120.0f, 40.0f});
    CNA_STUDIO_EXPECT_EQ(pickEntityAt(scene, view, onIcon, kNoSizes).entityId.toString(),
                         cameraId.toString());

    // And just outside the badge, nothing.
    const StudioVector2 offIcon{onIcon.x + kStudioIconExtent + 4.0f, onIcon.y};
    CNA_STUDIO_EXPECT(!pickEntityAt(scene, view, offIcon, kNoSizes).entityId.isValid());
}

CNA_STUDIO_TEST(AnIconWinsOverASpriteBehindIt)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    // A large sprite covering the origin, and a camera parked on top of it.
    const Uuid backdropId = addEntity(scene, registry, "Backdrop", 0.0f, 0.0f);
    addSprite(scene, registry, backdropId, 400, 400);

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 10.0f, 10.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    StudioCamera2D view;
    view.setViewportSize(StudioVector2{800.0f, 600.0f});

    // Icons are drawn last, so a click on one must select its entity even where the sprite covers
    // the same point -- otherwise a camera over the level art is unselectable exactly where it is.
    const StudioVector2 onIcon = view.worldToScreen(StudioVector2{10.0f, 10.0f});
    CNA_STUDIO_EXPECT_EQ(pickEntityAt(scene, view, onIcon, kNoSizes).entityId.toString(),
                         cameraId.toString());

    // Away from the icon the sprite still wins, so the icon steals only what it covers.
    const StudioVector2 onSprite = view.worldToScreen(StudioVector2{200.0f, 200.0f});
    CNA_STUDIO_EXPECT_EQ(pickEntityAt(scene, view, onSprite, kNoSizes).entityId.toString(),
                         backdropId.toString());
}

CNA_STUDIO_TEST(IconsKeepTheirScreenSizeAtAnyZoom)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 500.0f, 500.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);

    StudioCamera2D view;
    view.setViewportSize(StudioVector2{800.0f, 600.0f});
    view.setCenter(StudioVector2{500.0f, 500.0f});

    for (const float zoom : {0.05f, 1.0f, 16.0f})
    {
        view.setZoom(zoom);

        const std::vector<StudioIconPlacement> icons = collectStudioIcons(scene, view);
        CNA_STUDIO_EXPECT_EQ(icons.size(), std::size_t{1});
        if (icons.empty()) { continue; }

        // The badge is a fixed number of pixels whatever the zoom. One that shrank with the view
        // would vanish exactly when it is the only way left to find the entity.
        const StudioVector2 edge{icons.front().center.x + kStudioIconExtent - 1.0f,
                                 icons.front().center.y};
        CNA_STUDIO_EXPECT_EQ(pickEntityAt(scene, view, edge, kNoSizes).entityId.toString(),
                             cameraId.toString());
    }
}

CNA_STUDIO_TEST(DisabledEntitiesGetNoIcon)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid cameraId = addEntity(scene, registry, "Main Camera", 0.0f, 0.0f);
    addComponent(scene, registry, cameraId, BuiltinComponentIds::kCamera);
    scene.findEntityForEdit(cameraId)->setEnabled(false);

    StudioCamera2D view;
    view.setViewportSize(StudioVector2{800.0f, 600.0f});

    // Matching the sprite pass and the picker: what cannot be clicked is not drawn.
    CNA_STUDIO_EXPECT_EQ(collectStudioIcons(scene, view).size(), std::size_t{0});
    CNA_STUDIO_EXPECT(!pickEntityAt(scene, view, view.worldToScreen(StudioVector2{0.0f, 0.0f}),
                                    kNoSizes).entityId.isValid());
}

CNA_STUDIO_TEST(EulerAnglesRoundTripThroughAQuaternion)
{
    const StudioVector3 cases[] = {
        StudioVector3{0.0f, 0.0f, 0.0f},
        StudioVector3{0.0f, 0.0f, 45.0f},
        StudioVector3{0.0f, 0.0f, -170.0f},
        StudioVector3{30.0f, 0.0f, 0.0f},
        StudioVector3{0.0f, 120.0f, 0.0f},
        StudioVector3{15.0f, -60.0f, 100.0f},
        StudioVector3{-89.0f, 33.0f, -12.0f},
    };

    for (const StudioVector3& degrees : cases)
    {
        const StudioVector3 back = eulerDegreesOf(quaternionFromEulerDegrees(degrees));
        CNA_STUDIO_EXPECT(nearlyEqual(back.x, degrees.x, 0.01f));
        CNA_STUDIO_EXPECT(nearlyEqual(back.y, degrees.y, 0.01f));
        CNA_STUDIO_EXPECT(nearlyEqual(back.z, degrees.z, 0.01f));
    }
}

CNA_STUDIO_TEST(TheEulerConventionMatchesTheZRotationTheRendererUses)
{
    // The renderer passes zRotationOf() to SpriteBatch::Draw, so a roll typed into the inspector
    // has to be the angle the sprite actually turns by. Two conventions that disagreed here would
    // make the number in the inspector a decoration.
    for (const float roll : {0.0f, 30.0f, -90.0f, 150.0f})
    {
        const StudioQuaternion rotation = quaternionFromEulerDegrees(StudioVector3{0.0f, 0.0f, roll});

        constexpr float kToDegrees = 180.0f / 3.14159265358979323846f;
        CNA_STUDIO_EXPECT(nearlyEqual(zRotationOf(rotation) * kToDegrees, roll, 0.01f));

        // And it agrees with the dedicated 2D helper, which is the other way a rotation is built.
        const StudioQuaternion viaHelper = quaternionFromZRotation(roll / kToDegrees);
        CNA_STUDIO_EXPECT(nearlyEqual(rotation.z, viaHelper.z, 0.0001f));
        CNA_STUDIO_EXPECT(nearlyEqual(rotation.w, viaHelper.w, 0.0001f));
    }
}

CNA_STUDIO_TEST(EulerExtractionSurvivesGimbalLock)
{
    // At a pole, yaw and roll are not separable: every pair with the same sum (or difference)
    // names the same rotation. Reporting *a* valid answer matters more than which one, but it
    // must still be one that rebuilds the same rotation.
    for (const float pitch : {90.0f, -90.0f})
    {
        const StudioQuaternion original =
            quaternionFromEulerDegrees(StudioVector3{pitch, 40.0f, 25.0f});

        const StudioVector3 extracted = eulerDegreesOf(original);
        CNA_STUDIO_EXPECT(nearlyEqual(extracted.x, pitch, 0.05f));
        CNA_STUDIO_EXPECT(nearlyEqual(extracted.z, 0.0f, 0.001f));

        const StudioQuaternion rebuilt = quaternionFromEulerDegrees(extracted);

        // q and -q are the same rotation, so compare what they do rather than their components.
        const StudioVector3 probe{1.0f, 2.0f, 3.0f};
        const StudioVector3 a = rotate(original, probe);
        const StudioVector3 b = rotate(rebuilt, probe);
        CNA_STUDIO_EXPECT(nearlyEqual(a.x, b.x, 0.01f));
        CNA_STUDIO_EXPECT(nearlyEqual(a.y, b.y, 0.01f));
        CNA_STUDIO_EXPECT(nearlyEqual(a.z, b.z, 0.01f));
    }
}

CNA_STUDIO_TEST(AQuaternionRoundTripsThroughEulerAsTheSameRotation)
{
    // The other direction: start from a quaternion nobody typed and check the angles shown for it
    // rebuild it. This is what the inspector does every frame it is not reusing its cache.
    const StudioQuaternion rotations[] = {
        quaternionFromEulerDegrees(StudioVector3{12.0f, 200.0f, -75.0f}),
        quaternionFromEulerDegrees(StudioVector3{-44.0f, -160.0f, 5.0f}),
        multiply(quaternionFromZRotation(0.7f), quaternionFromEulerDegrees(StudioVector3{20.0f, 10.0f, 0.0f})),
    };

    for (const StudioQuaternion& rotation : rotations)
    {
        const StudioQuaternion rebuilt = quaternionFromEulerDegrees(eulerDegreesOf(rotation));

        const StudioVector3 probe{0.3f, -1.7f, 2.1f};
        const StudioVector3 a = rotate(rotation, probe);
        const StudioVector3 b = rotate(rebuilt, probe);
        CNA_STUDIO_EXPECT(nearlyEqual(a.x, b.x, 0.001f));
        CNA_STUDIO_EXPECT(nearlyEqual(a.y, b.y, 0.001f));
        CNA_STUDIO_EXPECT(nearlyEqual(a.z, b.z, 0.001f));
    }
}

CNA_STUDIO_TEST(AnAnimatedSpriteIsSizedByItsFrameNotItsSheet)
{
    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    SceneDocument scene;
    StudioEntity entity{Uuid::generate(), "Hero"};

    StudioComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
    entity.addComponent(std::move(transform));

    StudioComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteRenderer));
    const Uuid sheetId = Uuid::generate();
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{sheetId}});
    entity.addComponent(std::move(sprite));

    StudioComponent animation{BuiltinComponentIds::kSpriteAnimation};
    animation.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteAnimation));
    animation.setProperty(SpriteAnimationKeys::kFrameWidth, PropertyValue{std::int64_t{32}});
    animation.setProperty(SpriteAnimationKeys::kFrameHeight, PropertyValue{std::int64_t{48}});
    entity.addComponent(std::move(animation));

    const Uuid entityId = scene.addEntity(std::move(entity));

    // The sheet is sixteen frames wide. Without the animation the bounds would be the whole sheet,
    // which is sixteen times too wide to click accurately and would make Frame Selected zoom out
    // to fit a strip nobody is looking at.
    const SpriteSizeProvider sheetSize = [&](const Uuid& id) {
        return id == sheetId ? StudioVector2{512.0f, 48.0f} : StudioVector2{};
    };

    const std::optional<WorldBounds2D> bounds = computeEntityBounds2D(scene, entityId, sheetSize);
    CNA_STUDIO_EXPECT(bounds.has_value());
    if (!bounds) { return; }

    CNA_STUDIO_EXPECT_EQ(bounds->max.x - bounds->min.x, 32.0f);
    CNA_STUDIO_EXPECT_EQ(bounds->max.y - bounds->min.y, 48.0f);
}

// ------------------------------------------------------------------------------------------------
// The input seam (STUDIO-04020)
// ------------------------------------------------------------------------------------------------

#if defined(CNA_STUDIO_HAS_CNA)

#    include "CNA/Studio/Viewport/CnaUiPlatform.hpp"

CNA_STUDIO_TEST(EveryKeyStudioCanAskAboutIsOneTheHostCanReport)
{
    // A key in Studio's vocabulary that the platform never maps is a shortcut that does not fire,
    // with nothing on screen to see and no error anywhere. It is the quietest failure in the input
    // path and the easiest to introduce: adding a key to the enumeration is one edit and mapping it
    // is another, in a different file, in the one module that does not build without a CNA
    // checkout.
    //
    // Two were missing when this was written -- Digit2 and Digit3, the 2D/3D view toggles, which
    // the ImGui path had mapped and this one had not.
    const std::vector<CnaUiPlatformKeyBinding>& bindings = cnaUiPlatformKeyBindings();

    std::set<int> mappedUiKeys;
    std::set<int> mappedHostKeys;
    std::size_t duplicates = 0;

    for (const CnaUiPlatformKeyBinding& binding : bindings)
    {
        if (!mappedUiKeys.insert(static_cast<int>(binding.uiKey)).second)
        {
            ++duplicates;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "UiKey " + std::to_string(static_cast<int>(binding.uiKey))
                + " is mapped more than once; the later mapping silently wins.");
        }
        if (!mappedHostKeys.insert(binding.xnaKey).second)
        {
            ++duplicates;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "host key " + std::to_string(binding.xnaKey)
                + " is bound to two different UiKeys, so one of them fires on the wrong key.");
        }
    }
    CNA_STUDIO_EXPECT_EQ(duplicates, std::size_t{0});

    std::size_t unmapped = 0;
    for (int key = static_cast<int>(UiKey::None) + 1; key < static_cast<int>(UiKey::Count); ++key)
    {
        if (mappedUiKeys.count(key) == 0)
        {
            ++unmapped;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "UiKey " + std::to_string(key) + " has no host mapping, so a shortcut using it "
                "never fires through the native UI. Add it to cnaUiPlatformKeyBindings().");
        }
    }
    CNA_STUDIO_EXPECT_EQ(unmapped, std::size_t{0});

    // And the table is not empty for a trivial reason.
    CNA_STUDIO_EXPECT(bindings.size() >= std::size_t{30});
}

#endif  // CNA_STUDIO_HAS_CNA
