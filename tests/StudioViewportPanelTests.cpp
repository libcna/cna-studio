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

#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
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
        StudioCamera3D camera3D;
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

    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});

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

CNA_STUDIO_TEST(DraggingAMultiSelectionMovesEveryEntityByTheSameAmount)
{
    // And by the *same* amount: a group drag that moved the grabbed entity further than the rest
    // would tear the arrangement apart, which is the one thing a user selects a group to preserve.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.context.toggleSelection(fixture.right);
    fixture.settle();

    const StudioVector3 leftBefore = positionOf(fixture.context, fixture.left);
    const StudioVector3 rightBefore = positionOf(fixture.context, fixture.right);

    // The manipulator sits at the average of the two, which is where the renderer draws it.
    const auto pivot =
        computeSelectionPivot(fixture.context.getScene(), fixture.context.getSelection());
    CNA_STUDIO_EXPECT(pivot.has_value());

    auto layout = computeTranslateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                              fixture.context.getSelection().back());
    placeGizmoAt(*layout, fixture.camera, *pivot);

    UiInputState down = at(layout->origin.x, layout->origin.y);
    down.setMouseDown(UiMouseButton::Left, true);
    fixture.run(at(layout->origin.x, layout->origin.y));
    fixture.run(down);
    CNA_STUDIO_EXPECT(fixture.state.dragging());

    UiInputState moved = at(layout->origin.x + 40.0f, layout->origin.y + 20.0f);
    moved.setMouseDown(UiMouseButton::Left, true);
    fixture.run(moved);
    fixture.run(at(layout->origin.x + 40.0f, layout->origin.y + 20.0f));

    const StudioVector3 leftAfter = positionOf(fixture.context, fixture.left);
    const StudioVector3 rightAfter = positionOf(fixture.context, fixture.right);

    CNA_STUDIO_EXPECT(std::abs(leftAfter.x - leftBefore.x) > 1.0f);
    CNA_STUDIO_EXPECT(std::abs((leftAfter.x - leftBefore.x) - (rightAfter.x - rightBefore.x)) < 0.01f);
    CNA_STUDIO_EXPECT(std::abs((leftAfter.y - leftBefore.y) - (rightAfter.y - rightBefore.y)) < 0.01f);

    // One undo entry for the whole group and the whole gesture.
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{1});
    fixture.context.getHistory().undo();
    CNA_STUDIO_EXPECT(std::abs(positionOf(fixture.context, fixture.left).x - leftBefore.x) < 0.01f);
    CNA_STUDIO_EXPECT(std::abs(positionOf(fixture.context, fixture.right).x - rightBefore.x) < 0.01f);
}

CNA_STUDIO_TEST(TwoGroupDragsAreTwoUndoEntriesRatherThanOne)
{
    // They share a merge key shape, so without something distinguishing them the second would
    // merge into the first -- and one Ctrl+Z would jump back past a gesture already finished.
    Fixture fixture;
    fixture.context.select(fixture.left);
    fixture.context.toggleSelection(fixture.right);
    fixture.settle();

    for (int pass = 0; pass < 2; ++pass)
    {
        const auto pivot =
            computeSelectionPivot(fixture.context.getScene(), fixture.context.getSelection());
        auto layout = computeTranslateGizmoLayout(fixture.context.getScene(), fixture.camera,
                                                  fixture.context.getSelection().back());
        placeGizmoAt(*layout, fixture.camera, *pivot);

        UiInputState down = at(layout->origin.x, layout->origin.y);
        down.setMouseDown(UiMouseButton::Left, true);
        fixture.run(at(layout->origin.x, layout->origin.y));
        fixture.run(down);

        UiInputState moved = at(layout->origin.x + 30.0f, layout->origin.y);
        moved.setMouseDown(UiMouseButton::Left, true);
        fixture.run(moved);
        fixture.run(at(layout->origin.x + 30.0f, layout->origin.y));
    }

    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{2});
}

CNA_STUDIO_TEST(FocusSelectedMovesTheCameraOntoWhatIsSelected)
{
    Fixture fixture;
    fixture.camera.setCenter(StudioVector2{5000.0f, 5000.0f});
    fixture.context.select(fixture.right);

    CNA_STUDIO_EXPECT(studioFrameSelection(fixture.context, fixture.camera, fixture.sizes()));

    // On the sprite's own extent, not on its transform position: a sprite is anchored at its
    // top-left corner by default, so a 64x64 one at (100, 0) occupies (100, 0)..(164, 64) and
    // framing it centres on the middle of *that*. Asserting the transform position instead is how
    // this test was wrong the first time.
    const StudioVector2 centre = fixture.camera.getCenter();
    CNA_STUDIO_EXPECT(centre.x >= 100.0f && centre.x <= 164.0f);
    CNA_STUDIO_EXPECT(centre.y >= 0.0f && centre.y <= 64.0f);

    // And the whole sprite is inside the visible bounds, which is what "framed" has to mean.
    const WorldBounds2D visible = fixture.camera.getVisibleBounds();
    CNA_STUDIO_EXPECT(visible.min.x <= 100.0f && visible.max.x >= 164.0f);
    CNA_STUDIO_EXPECT(visible.min.y <= 0.0f && visible.max.y >= 64.0f);
}

CNA_STUDIO_TEST(FocusSelectedWithNothingSelectedSaysSoRatherThanJumping)
{
    Fixture fixture;
    fixture.camera.setCenter(StudioVector2{7.0f, 9.0f});

    CNA_STUDIO_EXPECT(!studioFrameSelection(fixture.context, fixture.camera, fixture.sizes()));
    CNA_STUDIO_EXPECT_EQ(fixture.camera.getCenter().x, 7.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.camera.getCenter().y, 9.0f);
}

CNA_STUDIO_TEST(AnEntityWithNoGeometryIsStillFramedByItsPosition)
{
    // A camera or an empty grouping node has a position but nothing to draw, and a key that looks
    // like it does nothing is worse than one that works modestly.
    Fixture fixture;

    StudioEntity marker{Uuid::generate(), "Spawn"};
    StudioComponent transform{BuiltinComponentIds::kTransform};
    transform.setProperty("position", PropertyValue{StudioVector3{-400.0f, 250.0f, 0.0f}});
    marker.addComponent(std::move(transform));
    const Uuid id = fixture.context.getScene().addEntity(std::move(marker));

    fixture.context.select(id);
    CNA_STUDIO_EXPECT(studioFrameSelection(fixture.context, fixture.camera, fixture.sizes()));

    CNA_STUDIO_EXPECT(std::abs(fixture.camera.getCenter().x + 400.0f) < 1.0f);
    CNA_STUDIO_EXPECT(std::abs(fixture.camera.getCenter().y - 250.0f) < 1.0f);
}

CNA_STUDIO_TEST(TheTransformShortcutsReachTheGizmoThroughTheRegistry)
{
    // W, E and R are declared on the actions and dispatched by the shell; binding the actions is
    // what makes them arrive. Driven as keystrokes, because "the chord is declared" and "the chord
    // works" are different claims.
    StudioContext context;
    StudioLog log;
    StudioShell shell;
    shell.resetLayout();

    StudioCamera2D camera;

    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});

    UiInputState input;
    input.displayWidth = 1280.0f;
    input.displayHeight = 720.0f;
    shell.renderFrame(input);

    const auto press = [&](UiKey key) {
        UiInputState down = input;
        down.setKeyDown(key, true);
        shell.renderFrame(down);
        shell.renderFrame(input);
    };

    press(UiKey::E);
    CNA_STUDIO_EXPECT(shell.actions().isChecked("studio.view.rotate"));

    press(UiKey::R);
    CNA_STUDIO_EXPECT(shell.actions().isChecked("studio.view.scale"));

    press(UiKey::W);
    CNA_STUDIO_EXPECT(shell.actions().isChecked("studio.view.translate"));
}

CNA_STUDIO_TEST(FIsRefusedUntilSomethingIsSelected)
{
    // Drawn greyed out rather than drawn available and doing nothing, which is the difference
    // between a menu that explains itself and one that appears broken.
    StudioContext context;
    StudioLog log;
    StudioShell shell;
    shell.resetLayout();

    StudioCamera2D camera;

    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});
    shell.renderFrame(UiInputState{});

    CNA_STUDIO_EXPECT(!shell.actions().isEnabled("studio.view.focusSelected"));

    StudioEntity entity{Uuid::generate(), "Thing"};
    entity.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
    const Uuid id = context.getScene().addEntity(std::move(entity));
    context.select(id);

    CNA_STUDIO_EXPECT(shell.actions().isEnabled("studio.view.focusSelected"));
}

// ------------------------------------------------------------------------------------------------
// Dropping an asset into the view (STUDIO-09008)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AnAssetDroppedIntoTheViewLandsWhereItWasLetGo)
{
    // At the pointer rather than at the origin. In a scene with anything in it the origin is under
    // something else, and an editor that put every dropped thing there would make the gesture a
    // step towards moving it rather than a way of placing it.
    Fixture fixture;

    const Uuid texture = Uuid::generate();
    {
        AssetRecord record;
        record.id = texture;
        record.sourcePath = "Assets/hero.png";
        record.type = AssetType::Texture2D;
        CNA_STUDIO_EXPECT(fixture.context.getAssets().add(std::move(record)));
    }

    fixture.settle();

    StudioFrame::StudioDragPayload payload;
    payload.type = std::string{kStudioAssetDragType};
    payload.value = texture.toString();
    payload.label = "hero.png";

    // A point well away from the view's centre, so "landed where it was let go" is distinguishable
    // from "landed at the origin".
    const float x = kWidth * 0.25f;
    const float y = kHeight * 0.75f;

    UiInputState down = at(x, y);
    down.setMouseDown(UiMouseButton::Left, true);
    fixture.run(down);
    CNA_STUDIO_EXPECT(fixture.frame.beginDrag(fixture.frame.ids().make("source"), payload));

    fixture.run(down);
    fixture.run(at(x, y));

    CNA_STUDIO_EXPECT(fixture.last.assetDropped.isValid());
    CNA_STUDIO_EXPECT_EQ(fixture.last.assetDropped.toString(), texture.toString());

    // The world point under the pointer, which is what the camera says and not what the test
    // recomputes a second way.
    const StudioVector2 expected =
        fixture.camera.screenToWorld(StudioVector2{x - fixture.body.left(),
                                                   y - fixture.body.top()});
    CNA_STUDIO_EXPECT_EQ(fixture.last.assetDropPosition.x, expected.x);
    CNA_STUDIO_EXPECT_EQ(fixture.last.assetDropPosition.y, expected.y);

    // Reported rather than acted on: the viewport does not know what an asset becomes, and nothing
    // reached the scene from here.
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{0});
}
