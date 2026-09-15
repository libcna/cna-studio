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
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
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
                    studioViewportPanel(f, body, context, camera, sizes());
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
