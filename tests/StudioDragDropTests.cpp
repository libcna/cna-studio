// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDragDropTests.cpp
 * @brief Typed payloads carried from one widget to another (plan.md STUDIO-03023).
 *
 * The type is the whole point. A target that swallowed anything would let a user drop a texture
 * onto a material slot and see nothing happen, which is indistinguishable from a drag that never
 * worked — so the cases here are as much about what a target *refuses* as about what it takes.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

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
        input.deltaSeconds = 1.0f / 60.0f;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A source button, a target that takes "asset", and a target that takes "entity". */
    struct Fixture
    {
        StudioFrame frame{StudioTheme::dark()};
        UiRect source{20.0f, 20.0f, 120.0f, 28.0f};
        UiRect assetTarget{20.0f, 200.0f, 200.0f, 40.0f};
        UiRect entityTarget{20.0f, 300.0f, 200.0f, 40.0f};

        bool assetHovered = false;
        bool assetRefused = false;
        bool entityRefused = false;
        std::vector<std::string> dropped;

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioWidgetResult held =
                    studioButton(f, f.ids().make("source"), source, "Texture");

                StudioFrame::StudioDragPayload payload;
                payload.type = "asset";
                payload.value = "player.png";
                payload.label = "player.png";
                (void)studioDragSource(f, f.ids().make("source"), held.interaction,
                                       std::move(payload));

                const StudioFrame::StudioDropResult asset =
                    f.acceptDrop(f.ids().make("asset-target"), assetTarget, "asset");
                const StudioFrame::StudioDropResult entity =
                    f.acceptDrop(f.ids().make("entity-target"), entityTarget, "entity");

                if (!f.isInputPass()) { return; }
                assetHovered = asset.hovered;
                assetRefused = asset.refused;
                entityRefused = entity.refused;
                if (asset.dropped) { dropped.push_back(asset.value); }
                if (entity.dropped) { dropped.push_back("WRONG:" + entity.value); }

                studioDrawDragPreview(f);
            });
        }

        void settle() { run(at(400.0f, 400.0f)); }

        /** @brief Presses on the source and moves to (@p x, @p y), which starts a drag. */
        void pickUpAndMoveTo(float x, float y)
        {
            run(at(source.centerX(), source.centerY()));
            run(at(source.centerX(), source.centerY(), /*leftDown=*/true));
            run(at(x, y, /*leftDown=*/true));
        }

        /** @brief Releases where the pointer is. */
        void release(float x, float y) { run(at(x, y)); }
    };
}

CNA_STUDIO_TEST(NothingIsCarriedUntilThePointerHasActuallyMoved)
{
    // The threshold is what keeps a click that wobbled by a pixel from becoming a drag: without
    // it, selecting a row on a trackpad would start carrying it.
    Fixture fixture;
    fixture.settle();

    fixture.run(at(fixture.source.centerX(), fixture.source.centerY()));
    fixture.run(at(fixture.source.centerX(), fixture.source.centerY(), /*leftDown=*/true));
    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());

    fixture.run(at(fixture.source.centerX() + 2.0f, fixture.source.centerY(), /*leftDown=*/true));
    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());

    fixture.run(at(fixture.source.centerX() + 80.0f, fixture.source.centerY(), /*leftDown=*/true));
    CNA_STUDIO_EXPECT(fixture.frame.isDragging());
    CNA_STUDIO_EXPECT_EQ(fixture.frame.dragPayload().type, std::string{"asset"});
}

CNA_STUDIO_TEST(TheThresholdIsMeasuredFromWhereThePressBeganNotFromTheWidgetsCentre)
{
    // Pressing near an edge would otherwise start a drag with the pointer having moved nothing at
    // all, which is a control that runs away from the user.
    Fixture fixture;
    fixture.settle();

    const float edge = fixture.source.left() + 3.0f;
    fixture.run(at(edge, fixture.source.centerY()));
    fixture.run(at(edge, fixture.source.centerY(), /*leftDown=*/true));
    fixture.run(at(edge + 2.0f, fixture.source.centerY(), /*leftDown=*/true));

    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());
}

CNA_STUDIO_TEST(ATargetOfTheRightTypeLightsUpBeforeTheDrop)
{
    // A drag with no feedback is a drag the user has to complete to discover whether it would have
    // worked.
    Fixture fixture;
    fixture.settle();
    fixture.pickUpAndMoveTo(fixture.assetTarget.centerX(), fixture.assetTarget.centerY());

    CNA_STUDIO_EXPECT(fixture.frame.isDragging());
    CNA_STUDIO_EXPECT(fixture.assetHovered);
    CNA_STUDIO_EXPECT(!fixture.assetRefused);
}

CNA_STUDIO_TEST(ATargetOfTheWrongTypeSaysSoRatherThanIgnoringIt)
{
    Fixture fixture;
    fixture.settle();
    fixture.pickUpAndMoveTo(fixture.entityTarget.centerX(), fixture.entityTarget.centerY());

    CNA_STUDIO_EXPECT(fixture.entityRefused);
    CNA_STUDIO_EXPECT(!fixture.assetHovered);

    fixture.release(fixture.entityTarget.centerX(), fixture.entityTarget.centerY());
    CNA_STUDIO_EXPECT(fixture.dropped.empty());
    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());
}

CNA_STUDIO_TEST(ReleasingOverAMatchingTargetDeliversThePayload)
{
    Fixture fixture;
    fixture.settle();
    fixture.pickUpAndMoveTo(fixture.assetTarget.centerX(), fixture.assetTarget.centerY());
    fixture.release(fixture.assetTarget.centerX(), fixture.assetTarget.centerY());

    CNA_STUDIO_EXPECT_EQ(fixture.dropped.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(fixture.dropped.front(), std::string{"player.png"});
    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());
}

CNA_STUDIO_TEST(ReleasingOverNothingCancelsRatherThanDelivering)
{
    Fixture fixture;
    fixture.settle();
    fixture.pickUpAndMoveTo(500.0f, 100.0f);
    CNA_STUDIO_EXPECT(fixture.frame.isDragging());

    fixture.release(500.0f, 100.0f);

    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());
    CNA_STUDIO_EXPECT(fixture.dropped.empty());
}

CNA_STUDIO_TEST(EscapeAbandonsADragInFlight)
{
    Fixture fixture;
    fixture.settle();
    fixture.pickUpAndMoveTo(fixture.assetTarget.centerX(), fixture.assetTarget.centerY());
    CNA_STUDIO_EXPECT(fixture.frame.isDragging());

    UiInputState escape = at(fixture.assetTarget.centerX(), fixture.assetTarget.centerY(),
                             /*leftDown=*/true);
    escape.setKeyDown(UiKey::Escape, true);
    fixture.run(escape);

    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());

    // And releasing afterwards delivers nothing: the payload is gone, not merely hidden.
    fixture.release(fixture.assetTarget.centerX(), fixture.assetTarget.centerY());
    CNA_STUDIO_EXPECT(fixture.dropped.empty());
}

CNA_STUDIO_TEST(OnlyOnePayloadIsCarriedAtATime)
{
    // Two at once is a state with no correct drop, so it is unreachable rather than handled.
    Fixture fixture;
    fixture.settle();
    fixture.pickUpAndMoveTo(300.0f, 300.0f);
    CNA_STUDIO_EXPECT(fixture.frame.isDragging());

    StudioFrame::StudioDragPayload second;
    second.type = "entity";
    second.value = "Player";
    CNA_STUDIO_EXPECT(!fixture.frame.beginDrag(fixture.frame.ids().make("other"),
                                                std::move(second)));
    CNA_STUDIO_EXPECT_EQ(fixture.frame.dragPayload().type, std::string{"asset"});
}

CNA_STUDIO_TEST(ADragWithNoTypeIsRefused)
{
    // An untyped payload can be dropped anywhere, which is the same as having no types at all.
    Fixture fixture;
    fixture.settle();

    StudioFrame::StudioDragPayload untyped;
    untyped.value = "something";
    CNA_STUDIO_EXPECT(!fixture.frame.beginDrag(fixture.frame.ids().make("x"), std::move(untyped)));
    CNA_STUDIO_EXPECT(!fixture.frame.isDragging());
}

CNA_STUDIO_TEST(TheCarriedLabelIsDrawnAndStaysOnScreen)
{
    // It follows the pointer, so at the far corner it has to flip to the other side of it rather
    // than running off the window -- and it must not cover the target being aimed at.
    Fixture fixture;
    fixture.settle();

    const UiRect middle = fixture.frame.dragPreviewBounds(120.0f, 20.0f);
    CNA_STUDIO_EXPECT(middle.left() >= 0.0f);

    fixture.pickUpAndMoveTo(kWidth - 4.0f, kHeight - 4.0f);
    const UiRect corner = fixture.frame.dragPreviewBounds(120.0f, 20.0f);

    CNA_STUDIO_EXPECT(corner.right() <= kWidth);
    CNA_STUDIO_EXPECT(corner.bottom() <= kHeight);
    CNA_STUDIO_EXPECT(corner.left() >= 0.0f);
    CNA_STUDIO_EXPECT(corner.top() >= 0.0f);
}
