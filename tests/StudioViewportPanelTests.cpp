// SPDX-License-Identifier: MS-PL
/**
 * @file StudioViewportPanelTests.cpp
 * @brief Navigating and selecting in the viewport (plan.md STUDIO-07009).
 *
 * None of this needs a graphics device, which is the point of keeping it apart from the scene
 * rendering: a camera is arithmetic and picking is a ray against bounds, so both can be driven at
 * the same layer the user drives them — with a pointer, over a rectangle — in a headless test.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cmath>
#include <string>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 800.0f;
    constexpr float kHeight = 600.0f;

    UiInputState at(float x, float y)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.deltaSeconds = 1.0f / 60.0f;
        return input;
    }

    /** @brief A scene with two sprites at known world positions. */
    struct Fixture
    {
        StudioContext context;
        StudioCamera2D camera;
        StudioViewportState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioViewportResult last;

        Uuid left;
        Uuid right;

        Fixture()
        {
            left = addSprite("Left", -100.0f, 0.0f);
            right = addSprite("Right", 100.0f, 0.0f);
            camera.setViewportSize(StudioVector2{kWidth, kHeight});
        }

        Uuid addSprite(const std::string& name, float x, float y)
        {
            StudioEntity entity{Uuid::generate(), name};

            StudioComponent transform{BuiltinComponentIds::kTransform};
            transform.setProperty("position", PropertyValue{StudioVector3{x, y, 0.0f}});
            transform.setProperty("scale", PropertyValue{StudioVector3{1.0f, 1.0f, 1.0f}});
            entity.addComponent(std::move(transform));

            StudioComponent sprite{BuiltinComponentIds::kSpriteRenderer};
            entity.addComponent(std::move(sprite));

            return context.getScene().addEntity(std::move(entity));
        }

        /** @brief Every sprite is 64x64, so picking has something to hit. */
        [[nodiscard]] SpriteSizeProvider sizes() const
        {
            return [](const Uuid&) { return StudioVector2{64.0f, 64.0f}; };
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioViewportResult drawn =
                    studioViewportPanel(f, body, context, camera, state, sizes());
                if (f.isInputPass()) { last = drawn; }
            });
        }

        void settle(float x = 5.0f, float y = 5.0f) { run(at(x, y)); }

        /** @brief Where an entity's centre lands on screen right now. */
        [[nodiscard]] StudioVector2 screenOf(const Uuid& id)
        {
            const StudioEntity* entity = context.getScene().findEntity(id);
            const StudioVector3 position =
                entity->findComponent(BuiltinComponentIds::kTransform)
                    ->getProperty("position").get<StudioVector3>();
            return camera.worldToScreen(StudioVector2{position.x, position.y});
        }

        void click(float x, float y, bool additive = false)
        {
            run(at(x, y));

            UiInputState down = at(x, y);
            down.setMouseDown(UiMouseButton::Left, true);
            down.modifiers.control = additive;
            run(down);

            UiInputState up = at(x, y);
            up.modifiers.control = additive;
            run(up);
        }
    };
}

CNA_STUDIO_TEST(TheCameraIsToldTheSizeItIsDrawingInto)
{
    // A docked panel changes size whenever anything else does, and a camera projecting for the
    // wrong extent puts every entity somewhere the user did not click.
    Fixture fixture;
    fixture.body = UiRect{200.0f, 100.0f, 400.0f, 300.0f};
    fixture.settle(210.0f, 110.0f);

    CNA_STUDIO_EXPECT_EQ(fixture.camera.getViewportSize().x, 400.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.camera.getViewportSize().y, 300.0f);
}

CNA_STUDIO_TEST(TheWheelZoomsAboutThePointerRatherThanTheCentre)
{
    // Zooming about the centre makes a user chase the thing they were looking at across the
    // screen, which is the difference between a viewport that feels direct and one that fights.
    Fixture fixture;
    fixture.settle(100.0f, 100.0f);

    const float zoomBefore = fixture.camera.getZoom();
    const StudioVector2 worldUnderPointer =
        fixture.camera.screenToWorld(StudioVector2{100.0f, 100.0f});

    UiInputState wheel = at(100.0f, 100.0f);
    wheel.wheelY = 1.0f;
    fixture.run(wheel);

    CNA_STUDIO_EXPECT(fixture.camera.getZoom() > zoomBefore);
    CNA_STUDIO_EXPECT(fixture.last.cameraChanged);

    // The same world point is still under the pointer, which is what "about the pointer" means.
    const StudioVector2 after = fixture.camera.screenToWorld(StudioVector2{100.0f, 100.0f});
    CNA_STUDIO_EXPECT(std::abs(after.x - worldUnderPointer.x) < 0.5f);
    CNA_STUDIO_EXPECT(std::abs(after.y - worldUnderPointer.y) < 0.5f);
}

CNA_STUDIO_TEST(DraggingWithTheMiddleOrRightButtonPans)
{
    // Both, because a trackpad has no middle button and a viewport a laptop cannot pan is a
    // viewport half the users cannot use.
    for (const UiMouseButton button : {UiMouseButton::Middle, UiMouseButton::Right})
    {
        Fixture fixture;
        fixture.settle(400.0f, 300.0f);
        const StudioVector2 before = fixture.camera.getCenter();

        UiInputState down = at(400.0f, 300.0f);
        down.setMouseDown(button, true);
        fixture.run(down);

        UiInputState moved = at(460.0f, 330.0f);
        moved.setMouseDown(button, true);
        fixture.run(moved);

        CNA_STUDIO_EXPECT(fixture.camera.getCenter() != before);
        CNA_STUDIO_EXPECT(fixture.last.cameraChanged);
    }
}

CNA_STUDIO_TEST(APanThatStartedOutsideTheViewportDoesNotMoveIt)
{
    // The pointer crosses the viewport during every drag of anything else in the editor -- a
    // splitter, a dock tab -- and a camera that jumped whenever one passed over would be unusable.
    Fixture fixture;
    fixture.body = UiRect{400.0f, 0.0f, 400.0f, kHeight};
    fixture.settle(10.0f, 10.0f);
    const StudioVector2 before = fixture.camera.getCenter();

    UiInputState down = at(10.0f, 10.0f);
    down.setMouseDown(UiMouseButton::Middle, true);
    fixture.run(down);

    UiInputState across = at(600.0f, 300.0f);
    across.setMouseDown(UiMouseButton::Middle, true);
    fixture.run(across);

    CNA_STUDIO_EXPECT(fixture.camera.getCenter() == before);
}

CNA_STUDIO_TEST(ClickingASpriteSelectsIt)
{
    Fixture fixture;
    fixture.settle();

    const StudioVector2 target = fixture.screenOf(fixture.right);
    fixture.click(target.x, target.y);

    CNA_STUDIO_EXPECT(fixture.last.selectionChanged);
    CNA_STUDIO_EXPECT(fixture.last.picked == fixture.right);
    CNA_STUDIO_EXPECT(fixture.context.isSelected(fixture.right));
    CNA_STUDIO_EXPECT(!fixture.context.isSelected(fixture.left));
}

CNA_STUDIO_TEST(ClickingEmptySpaceClearsTheSelection)
{
    // How a user deselects without reaching for the keyboard.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.settle();

    const StudioVector2 far = fixture.camera.worldToScreen(StudioVector2{4000.0f, 4000.0f});
    fixture.click(std::min(far.x, kWidth - 2.0f), std::min(far.y, kHeight - 2.0f));

    CNA_STUDIO_EXPECT(fixture.context.getSelection().empty());
}

CNA_STUDIO_TEST(CtrlClickingEmptySpaceLeavesTheSelectionAlone)
{
    // A missed Ctrl-click that wiped a careful multi-selection would be unforgivable.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.context.toggleSelection(fixture.right);
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.context.getSelection().size(), std::size_t{2});

    const StudioVector2 far = fixture.camera.worldToScreen(StudioVector2{4000.0f, 4000.0f});
    fixture.click(std::min(far.x, kWidth - 2.0f), std::min(far.y, kHeight - 2.0f),
                  /*additive=*/true);

    CNA_STUDIO_EXPECT_EQ(fixture.context.getSelection().size(), std::size_t{2});
}

CNA_STUDIO_TEST(CtrlClickingASecondSpriteAddsItToTheSelection)
{
    Fixture fixture;
    fixture.settle();

    fixture.click(fixture.screenOf(fixture.left).x, fixture.screenOf(fixture.left).y);
    CNA_STUDIO_EXPECT_EQ(fixture.context.getSelection().size(), std::size_t{1});

    const StudioVector2 second = fixture.screenOf(fixture.right);
    fixture.click(second.x, second.y, /*additive=*/true);

    CNA_STUDIO_EXPECT_EQ(fixture.context.getSelection().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(fixture.context.isSelected(fixture.left));
    CNA_STUDIO_EXPECT(fixture.context.isSelected(fixture.right));
}

CNA_STUDIO_TEST(ThePointersWorldPositionIsReportedForWhateverWantsIt)
{
    Fixture fixture;
    fixture.settle(0.0f, 0.0f);

    // The centre of the viewport is the camera's centre, whatever the zoom.
    fixture.run(at(kWidth * 0.5f, kHeight * 0.5f));

    CNA_STUDIO_EXPECT(fixture.last.pointerInside);
    CNA_STUDIO_EXPECT(std::abs(fixture.last.pointerWorld.x - fixture.camera.getCenter().x) < 0.5f);
    CNA_STUDIO_EXPECT(std::abs(fixture.last.pointerWorld.y - fixture.camera.getCenter().y) < 0.5f);
}

CNA_STUDIO_TEST(AnEmptyViewportRectangleDoesNothingRatherThanDividingByIt)
{
    Fixture fixture;
    fixture.body = UiRect{0.0f, 0.0f, 0.0f, 0.0f};
    fixture.settle();

    CNA_STUDIO_EXPECT(!fixture.last.cameraChanged);
    CNA_STUDIO_EXPECT(!fixture.last.selectionChanged);
    CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
}

// ---------------------------------------------------------------------------------------------
// The manipulators
// ---------------------------------------------------------------------------------------------

namespace
{
    /** @brief The world position an entity's transform holds. */
    StudioVector3 positionOf(const StudioContext& context, const Uuid& id)
    {
        const StudioEntity* entity = context.getScene().findEntity(id);
        return entity->findComponent(BuiltinComponentIds::kTransform)
            ->getProperty("position").get<StudioVector3>();
    }
}

CNA_STUDIO_TEST(PressingATranslateArmStartsADragRatherThanSelecting)
{
    // The press that grabs a manipulator must not also pick: a user aiming at an arm that happens
    // to lie over another sprite would select that sprite and lose the thing they were moving.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.settle();

    const auto layout = computeTranslateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                    fixture.left);
    CNA_STUDIO_EXPECT(layout.has_value());

    const StudioVector2 arm = layout->getXTip();
    UiInputState down = at(arm.x, arm.y);
    down.setMouseDown(UiMouseButton::Left, true);
    fixture.run(at(arm.x, arm.y));
    fixture.run(down);

    CNA_STUDIO_EXPECT(fixture.state.dragging());
    CNA_STUDIO_EXPECT(!fixture.last.selectionChanged);
}

CNA_STUDIO_TEST(DraggingATranslateArmMovesTheEntityAlongThatAxisOnly)
{
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.settle();

    const StudioVector3 before = positionOf(fixture.context, fixture.left);
    const auto layout = computeTranslateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                    fixture.left);
    const StudioVector2 arm = layout->getXTip();

    UiInputState down = at(arm.x, arm.y);
    down.setMouseDown(UiMouseButton::Left, true);
    fixture.run(at(arm.x, arm.y));
    fixture.run(down);

    // Diagonally, so a gizmo that ignored its constraint would move on both axes.
    UiInputState moved = at(arm.x + 60.0f, arm.y + 60.0f);
    moved.setMouseDown(UiMouseButton::Left, true);
    fixture.run(moved);

    const StudioVector3 after = positionOf(fixture.context, fixture.left);
    CNA_STUDIO_EXPECT(fixture.last.transformed);
    CNA_STUDIO_EXPECT(std::abs(after.x - before.x) > 1.0f);
    CNA_STUDIO_EXPECT(std::abs(after.y - before.y) < 0.01f);
}

CNA_STUDIO_TEST(AWholeDragIsOneUndoEntryRatherThanOnePerFrame)
{
    // Sixty entries a second is an undo stack a user cannot use: pressing Ctrl+Z would rewind the
    // drag frame by frame, and nobody counts frames.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{0});

    const auto layout = computeTranslateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                    fixture.left);
    const StudioVector2 grab = layout->origin;

    UiInputState down = at(grab.x, grab.y);
    down.setMouseDown(UiMouseButton::Left, true);
    fixture.run(at(grab.x, grab.y));
    fixture.run(down);

    for (int step = 1; step <= 6; ++step)
    {
        UiInputState moved = at(grab.x + static_cast<float>(step) * 8.0f, grab.y);
        moved.setMouseDown(UiMouseButton::Left, true);
        fixture.run(moved);
    }
    fixture.run(at(grab.x + 48.0f, grab.y));

    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{1});

    // And undoing that one entry puts it back where it started, not part-way through the drag.
    const StudioVector3 moved = positionOf(fixture.context, fixture.left);
    fixture.context.getHistory().undo();
    const StudioVector3 restored = positionOf(fixture.context, fixture.left);

    CNA_STUDIO_EXPECT(std::abs(moved.x - restored.x) > 1.0f);
    CNA_STUDIO_EXPECT(std::abs(restored.x - (-100.0f)) < 0.01f);
}

CNA_STUDIO_TEST(ReleasingAnywhereEndsTheDrag)
{
    // A drag that only ended when the release landed back inside the viewport would leave the
    // gizmo stuck to the cursor the moment somebody let go over a panel.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.settle();

    const auto layout = computeTranslateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                    fixture.left);
    UiInputState down = at(layout->origin.x, layout->origin.y);
    down.setMouseDown(UiMouseButton::Left, true);
    fixture.run(at(layout->origin.x, layout->origin.y));
    fixture.run(down);
    CNA_STUDIO_EXPECT(fixture.state.dragging());

    // Released far outside the body.
    fixture.body = UiRect{0.0f, 0.0f, 100.0f, 100.0f};
    fixture.run(at(700.0f, 500.0f));

    CNA_STUDIO_EXPECT(!fixture.state.dragging());
}

CNA_STUDIO_TEST(WithNoManipulatorShowingAPressPicksAsItAlwaysDid)
{
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.state.mode = GizmoMode::None;
    fixture.settle();

    const StudioVector2 target = fixture.screenOf(fixture.right);
    fixture.click(target.x, target.y);

    CNA_STUDIO_EXPECT(!fixture.state.dragging());
    CNA_STUDIO_EXPECT(fixture.context.isSelected(fixture.right));
}

CNA_STUDIO_TEST(TheRotateAndScaleManipulatorsDragTheirOwnProperty)
{
    {
        Fixture fixture;
        fixture.context.select(fixture.left);
        fixture.state.mode = GizmoMode::Rotate;
        fixture.settle();

        const auto layout = computeRotateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                     fixture.left);
        CNA_STUDIO_EXPECT(layout.has_value());

        const StudioVector2 onRing{layout->origin.x + layout->radius, layout->origin.y};
        UiInputState down = at(onRing.x, onRing.y);
        down.setMouseDown(UiMouseButton::Left, true);
        fixture.run(at(onRing.x, onRing.y));
        fixture.run(down);
        CNA_STUDIO_EXPECT(fixture.state.dragging());

        UiInputState turned = at(layout->origin.x, layout->origin.y + layout->radius);
        turned.setMouseDown(UiMouseButton::Left, true);
        fixture.run(turned);

        CNA_STUDIO_EXPECT(fixture.last.transformed);
        CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{1});
    }
    {
        Fixture fixture;
        fixture.context.select(fixture.left);
        fixture.state.mode = GizmoMode::Scale;
        fixture.settle();

        const auto layout = computeScaleGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                    fixture.left);
        CNA_STUDIO_EXPECT(layout.has_value());

        const StudioVector2 handle = layout->getXTip();
        UiInputState down = at(handle.x, handle.y);
        down.setMouseDown(UiMouseButton::Left, true);
        fixture.run(at(handle.x, handle.y));
        fixture.run(down);
        CNA_STUDIO_EXPECT(fixture.state.dragging());

        UiInputState pulled = at(handle.x + 40.0f, handle.y);
        pulled.setMouseDown(UiMouseButton::Left, true);
        fixture.run(pulled);

        CNA_STUDIO_EXPECT(fixture.last.transformed);
    }
}

CNA_STUDIO_TEST(AManipulatorDragDoesNotAlsoPanTheCamera)
{
    // Both are held-button gestures over the same rectangle, and a camera that moved while an
    // entity was being dragged would take the entity with it.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.settle();

    const auto layout = computeTranslateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                    fixture.left);
    UiInputState down = at(layout->origin.x, layout->origin.y);
    down.setMouseDown(UiMouseButton::Left, true);
    down.setMouseDown(UiMouseButton::Middle, true);
    fixture.run(at(layout->origin.x, layout->origin.y));
    fixture.run(down);

    const StudioVector2 centreBefore = fixture.camera.getCenter();

    UiInputState moved = at(layout->origin.x + 50.0f, layout->origin.y + 50.0f);
    moved.setMouseDown(UiMouseButton::Left, true);
    moved.setMouseDown(UiMouseButton::Middle, true);
    fixture.run(moved);

    CNA_STUDIO_EXPECT(fixture.camera.getCenter() == centreBefore);
}

CNA_STUDIO_TEST(TheToolbarsTransformButtonsChooseTheManipulator)
{
    // They have been drawing and doing nothing since the toolbar existed. Driven through the real
    // shell, because "does pressing W change the gizmo" is a question about the wiring rather than
    // about the panel.
    StudioContext context;
    StudioLog log;
    StudioShell shell;
    shell.resetLayout();

    StudioCamera2D camera;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, {});

    UiInputState input;
    input.displayWidth = 1280.0f;
    input.displayHeight = 720.0f;
    shell.renderFrame(input);

    // Translate is the default, and the toolbar says so.
    CNA_STUDIO_EXPECT(shell.actions().isChecked("studio.view.translate"));
    CNA_STUDIO_EXPECT(!shell.actions().isChecked("studio.view.rotate"));

    shell.invoke("studio.view.rotate");
    shell.renderFrame(input);

    CNA_STUDIO_EXPECT(shell.actions().isChecked("studio.view.rotate"));
    CNA_STUDIO_EXPECT(!shell.actions().isChecked("studio.view.translate"));
    CNA_STUDIO_EXPECT(!shell.actions().isChecked("studio.view.scale"));

    shell.invoke("studio.view.scale");
    shell.renderFrame(input);
    CNA_STUDIO_EXPECT(shell.actions().isChecked("studio.view.scale"));
}
