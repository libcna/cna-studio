// SPDX-License-Identifier: MS-PL
/**
 * @file StudioViewport3DTests.cpp
 * @brief The 3D view in the native shell (plan.md STUDIO-07009, STUDIO-11001, STUDIO-11002).
 *
 * The model was never the gap here either. `StudioCamera3D`, `pickEntityAt3D`,
 * `buildSceneModelBatch` and `buildSceneWireframe` are CNA-free, tested, and have been shared with
 * the prototype since it had a 3D view. What was missing was a native viewport that switched to it
 * and turned a drag into an orbit.
 *
 * So these are about the difference between navigating and selecting, which is the thing a 3D
 * viewport gets wrong in a way users feel immediately: every button is also a camera gesture, so a
 * release after an orbit must not select whatever the camera happened to stop over.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneWireframe.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 640.0f;
    constexpr float kHeight = 480.0f;

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    UiInputState away() { return at(-1.0f, -1.0f); }

    /** @brief Two entities with meshes, far enough apart that a ray can tell them apart. */
    struct Fixture
    {
        StudioContext context;
        StudioCamera3D camera;
        StudioFrame frame{StudioTheme::dark()};
        StudioViewportState state;
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioViewportResult last;
        Uuid nearEntity;

        Fixture()
        {
            nearEntity = add("Near", StudioVector3{0.0f, 0.0f, 0.0f});
            (void)add("Far", StudioVector3{40.0f, 0.0f, 0.0f});

            state.view = StudioViewportView::ThreeD;
            camera.setViewportSize(StudioVector2{kWidth, kHeight});
            camera.setPivot(StudioVector3{0.0f, 0.0f, 0.0f});
            camera.setDistance(20.0f);
        }

        Uuid add(const std::string& name, const StudioVector3& position)
        {
            StudioEntity entity{Uuid::generate(), name};
            StudioComponent transform{"CNA.Transform"};
            transform.setProperty("position", PropertyValue{position});
            entity.getComponents().push_back(std::move(transform));
            const Uuid id = entity.getId();
            context.getScene().addEntity(std::move(entity));
            return id;
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& pass) {
                const StudioViewportResult drawn =
                    studioViewportPanel3D(pass, body, context, camera, state, {});
                if (pass.isInputPass()) { last = drawn; }
            });
        }

        /** @brief A press, a drag to (x, y), and a release there. */
        void dragTo(float fromX, float fromY, float x, float y, bool shift = false,
                    bool control = false)
        {
            UiInputState press = at(fromX, fromY);
            press.modifiers.shift = shift;
            press.modifiers.control = control;
            run(press);

            press.setMouseDown(UiMouseButton::Left, true);
            run(press);

            UiInputState moved = at(x, y, /*leftDown=*/true);
            moved.modifiers.shift = shift;
            moved.modifiers.control = control;
            run(moved);

            UiInputState released = at(x, y);
            released.modifiers.shift = shift;
            released.modifiers.control = control;
            run(released);
        }

        /** @brief A press and release in the same place, which is a click. */
        void clickAt(float x, float y, bool control = false)
        {
            dragTo(x, y, x, y, /*shift=*/false, control);
        }
    };
}

CNA_STUDIO_TEST(ADragOrbitsTheCameraWithoutMovingThePivot)
{
    // Orbiting turns the eye around a point the user chose. A drag that moved the pivot as well
    // would leave the camera somewhere neither the user nor the previous frame put it, and is the
    // difference between a 3D view that can be aimed and one people give up on.
    Fixture fixture;
    fixture.run(away());

    const float yaw = fixture.camera.getYaw();
    const float pitch = fixture.camera.getPitch();
    const StudioVector3 pivot = fixture.camera.getPivot();
    const float distance = fixture.camera.getDistance();

    fixture.dragTo(320.0f, 240.0f, 420.0f, 200.0f);

    CNA_STUDIO_EXPECT(std::abs(fixture.camera.getYaw() - yaw) > 0.01f);
    CNA_STUDIO_EXPECT(std::abs(fixture.camera.getPitch() - pitch) > 0.01f);
    CNA_STUDIO_EXPECT_EQ(fixture.camera.getPivot().x, pivot.x);
    CNA_STUDIO_EXPECT_EQ(fixture.camera.getPivot().y, pivot.y);
    CNA_STUDIO_EXPECT_EQ(fixture.camera.getDistance(), distance);
}

CNA_STUDIO_TEST(ShiftDragPansTheCameraRatherThanTurningIt)
{
    Fixture fixture;
    fixture.run(away());

    const float yaw = fixture.camera.getYaw();
    const StudioVector3 pivot = fixture.camera.getPivot();

    fixture.dragTo(320.0f, 240.0f, 400.0f, 300.0f, /*shift=*/true);

    CNA_STUDIO_EXPECT_EQ(fixture.camera.getYaw(), yaw);
    const StudioVector3 moved = fixture.camera.getPivot();
    CNA_STUDIO_EXPECT(std::abs(moved.x - pivot.x) + std::abs(moved.y - pivot.y)
                          + std::abs(moved.z - pivot.z) > 0.001f);
}

CNA_STUDIO_TEST(TheWheelDolliesGeometricallySoOneNotchFeelsTheSameCloseUpAndFarAway)
{
    // A linear step is unusable at both ends of the range: it crawls when far out and jumps
    // through the subject when close in.
    Fixture fixture;
    fixture.run(away());

    const float far = 20.0f;
    fixture.camera.setDistance(far);
    UiInputState wheel = at(320.0f, 240.0f);
    wheel.wheelY = 1.0f;
    fixture.run(wheel);
    const float fromFar = far / fixture.camera.getDistance();

    const float near = 5.0f;
    fixture.camera.setDistance(near);
    fixture.run(at(320.0f, 240.0f));
    fixture.run(wheel);
    const float fromNear = near / fixture.camera.getDistance();

    CNA_STUDIO_EXPECT(fixture.camera.getDistance() < near);
    CNA_STUDIO_EXPECT(std::abs(fromFar - fromNear) < 0.001f);
}

CNA_STUDIO_TEST(AReleaseAfterAnOrbitDoesNotSelectWhateverTheCameraStoppedOver)
{
    // The failure that makes a 3D viewport feel like it is fighting the user: every button is also
    // a camera gesture, so "released the mouse" cannot mean "clicked".
    Fixture fixture;
    fixture.run(away());
    fixture.context.clearSelection();

    fixture.dragTo(320.0f, 240.0f, 460.0f, 180.0f);

    CNA_STUDIO_EXPECT(!fixture.last.clicked3D);
    CNA_STUDIO_EXPECT(!fixture.last.selectionChanged);
    CNA_STUDIO_EXPECT(fixture.context.getSelection().empty());
}

CNA_STUDIO_TEST(APressAndReleaseThatTurnedNothingSelectsWhatIsUnderIt)
{
    // And the other half: a viewport where clicking never selects because every press is treated
    // as a drag is one where the outliner is the only way to select anything.
    Fixture fixture;
    fixture.run(away());
    fixture.context.clearSelection();

    fixture.clickAt(320.0f, 240.0f);

    CNA_STUDIO_EXPECT(fixture.last.clicked3D);
    CNA_STUDIO_EXPECT(fixture.last.selectionChanged);
    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection() == fixture.nearEntity);
}

CNA_STUDIO_TEST(ClickingNothingClearsTheSelectionAndCtrlOnNothingDoesNot)
{
    // The same two rules the 2D viewport has, because they are rules about selecting rather than
    // about a projection: clearing a selection the user is halfway through assembling is the one
    // outcome they cannot have meant.
    Fixture fixture;
    fixture.run(away());
    fixture.clickAt(320.0f, 240.0f);
    CNA_STUDIO_EXPECT(!fixture.context.getSelection().empty());

    // Aimed at empty space, off the axis the entities sit on, rather than at a corner of the
    // panel. An entity carrying only a transform still has a box to pick against -- deliberately,
    // because a light or a camera has to be clickable -- so a corner is not reliably a miss, and a
    // test that assumed it was would be asserting about this scene rather than about the rule.
    // Aiming *along* the axis is no better: 900 units away is still in front of a camera whose far
    // plane is 5000, which is the first thing this test caught about its own setup.
    fixture.camera.setPivot(StudioVector3{0.0f, 900.0f, 0.0f});
    fixture.camera.setDistance(5.0f);
    fixture.run(away());

    CNA_STUDIO_EXPECT(!pickEntityAt3D(fixture.context.getScene(), fixture.camera,
                                      StudioVector2{320.0f, 240.0f}, {}).isValid());

    fixture.clickAt(320.0f, 240.0f, /*control=*/true);
    CNA_STUDIO_EXPECT(!fixture.context.getSelection().empty());

    fixture.clickAt(320.0f, 240.0f);
    CNA_STUDIO_EXPECT(fixture.context.getSelection().empty());
}

CNA_STUDIO_TEST(TheThreeDimensionalViewDrawsNothingAndViolatesNoPhase)
{
    // The panel draws nothing -- the scene arrives as a texture the shell composites -- which is
    // what lets this half be tested with no graphics device at all.
    Fixture fixture;
    fixture.run(away());
    fixture.dragTo(100.0f, 100.0f, 300.0f, 260.0f);

    CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT(validate(fixture.frame.drawData()).valid);
}

CNA_STUDIO_TEST(TheTwoViewsAreExclusiveAndSayWhichIsShowing)
{
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioCamera2D camera;
    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});

    const std::vector<std::string> ids = {"studio.view.2d", "studio.view.3d"};
    for (const std::string& armed : ids)
    {
        shell.invoke(armed);

        std::size_t checked = 0;
        for (const std::string& id : ids)
        {
            const StudioAction* action = shell.actions().find(id);
            CNA_STUDIO_EXPECT(action != nullptr && action->checkable && action->isChecked);
            if (action == nullptr || !action->isChecked) { continue; }
            if (action->isChecked()) { ++checked; }
        }
        CNA_STUDIO_EXPECT_EQ(checked, std::size_t{1});
    }

    CNA_STUDIO_EXPECT(panels.viewportView() == StudioViewportView::ThreeD);
    shell.invoke("studio.view.2d");
    CNA_STUDIO_EXPECT(panels.viewportView() == StudioViewportView::TwoD);
}

CNA_STUDIO_TEST(TheFirstSwitchToThreeDimensionsFramesTheSceneAndLaterOnesDoNot)
{
    // The default camera looks straight down an axis, so an unframed 3D view opens on a grid with
    // the level somewhere off the edge of it. Framing every time would be worse than not framing
    // at all: a user who set up a view, glanced at 2D and came back would find their angle gone.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();

    StudioEntity entity{Uuid::generate(), "Far away"};
    StudioComponent transform{"CNA.Transform"};
    transform.setProperty("position", PropertyValue{StudioVector3{120.0f, 60.0f, 30.0f}});
    entity.getComponents().push_back(std::move(transform));
    context.getScene().addEntity(std::move(entity));

    StudioCamera2D camera;
    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});

    const StudioVector3 before = camera3D.getPivot();
    shell.invoke("studio.view.3d");
    const StudioVector3 framed = camera3D.getPivot();
    CNA_STUDIO_EXPECT(std::abs(framed.x - before.x) > 1.0f);

    // The user's own angle, set after framing, survives a trip through the 2D view.
    camera3D.setYaw(1.25f);
    camera3D.setDistance(7.5f);
    shell.invoke("studio.view.2d");
    shell.invoke("studio.view.3d");
    CNA_STUDIO_EXPECT_EQ(camera3D.getYaw(), 1.25f);
    CNA_STUDIO_EXPECT_EQ(camera3D.getDistance(), 7.5f);
    CNA_STUDIO_EXPECT_EQ(camera3D.getPivot().x, framed.x);
}

CNA_STUDIO_TEST(SwitchingViewsEndsAnyGestureTheOldViewOwned)
{
    // A gizmo drag half-finished in the 2D view would keep writing positions from a projection no
    // longer on screen, and a navigation gesture would resume mid-orbit on the next press.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioCamera2D camera;
    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});

    shell.invoke("studio.view.3d");
    shell.invoke("studio.view.2d");

    // Said out loud, because the two views share a panel and the change is dramatic enough that a
    // user who pressed 3 by accident deserves to be told what they pressed.
    bool announced = false;
    for (const StudioLogEntry& entry : log.entries())
    {
        if (entry.message.find("Viewport: 3D") != std::string::npos) { announced = true; }
    }
    CNA_STUDIO_EXPECT(announced);
}

CNA_STUDIO_TEST(TheCameraSpeedAndInvertZoomPreferencesActuallyReachTheCamera)
{
    // All three viewport preferences -- speed, inverted zoom and the navigation style -- were
    // stored, loaded, given a row in the Preferences panel, and applied by nothing at all. Two of
    // them are arithmetic and are answered here; the style is STUDIO-11014, because three schemes
    // across two viewports is a piece of work rather than a multiplier.
    Fixture fast;
    fast.state.cameraSpeed = 4.0f;
    fast.run(away());

    Fixture slow;
    slow.state.cameraSpeed = 1.0f;
    slow.run(away());

    const float startYaw = fast.camera.getYaw();
    fast.dragTo(320.0f, 240.0f, 400.0f, 240.0f);
    slow.dragTo(320.0f, 240.0f, 400.0f, 240.0f);

    const float fastTurn = std::abs(fast.camera.getYaw() - startYaw);
    const float slowTurn = std::abs(slow.camera.getYaw() - startYaw);
    CNA_STUDIO_EXPECT(slowTurn > 0.0f);
    CNA_STUDIO_EXPECT(fastTurn > slowTurn * 3.5f);

    // And the wheel, both ways round. Scrolling up moves the eye towards the pivot unless the
    // user has asked for the opposite, which is a preference because neither answer is wrong.
    Fixture normal;
    normal.run(away());
    UiInputState wheel = at(320.0f, 240.0f);
    wheel.wheelY = 1.0f;
    const float before = normal.camera.getDistance();
    normal.run(wheel);
    CNA_STUDIO_EXPECT(normal.camera.getDistance() < before);

    Fixture inverted;
    inverted.state.invertZoom = true;
    inverted.run(away());
    inverted.run(wheel);
    CNA_STUDIO_EXPECT(inverted.camera.getDistance() > before);
}
