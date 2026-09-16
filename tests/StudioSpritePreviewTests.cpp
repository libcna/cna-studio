// SPDX-License-Identifier: MS-PL
/**
 * @file StudioSpritePreviewTests.cpp
 * @brief The sprite animation preview in the native Details panel.
 *
 * `plan.md` STUDIO-07043, the fourth of the five Inspector sections `STUDIO-07041` found with no
 * native answer.
 *
 * The inventory states the requirement as "a preview that does not put the frame it is showing
 * into the document", and that is the case worth the most here: playback is editor state, and a
 * scene that recorded the frame an artist happened to be paused on would carry it into every save
 * and every diff (`ANALYSIS.md` decision D-07).
 *
 * The second is a trap the prototype could not have: the native panel is a *function called twice
 * a frame*, once to route input and once to draw. A clip advanced on both passes runs at double
 * speed, and — worse — the picture drawn is a frame later than the one the transport buttons were
 * read against, so Next steps from a frame nobody saw.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SpriteAnimation.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A texture id the fixture hands out, so a test can find it in the draw data. */
    constexpr UiTextureId kSheetTexture = 4242;

    float metricOf(const StudioTheme& theme, StudioMetric metric)
    {
        return static_cast<float>(theme.metric(metric));
    }

    UiInputState at(float x, float y, bool leftDown = false, float delta = 1.0f / 60.0f)
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 900.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.deltaSeconds = delta;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A component holding a four-frame clip at a round rate. */
    StudioComponent spriteAnimation(const Uuid& sheet, int frameCount, float framesPerSecond)
    {
        StudioComponent animation{BuiltinComponentIds::kSpriteAnimation};
        animation.setProperty(SpriteAnimationKeys::kSheet,
                              PropertyValue{PropertyValue::AssetReference{sheet}});
        animation.setProperty(SpriteAnimationKeys::kFrameWidth, PropertyValue{std::int64_t{32}});
        animation.setProperty(SpriteAnimationKeys::kFrameHeight, PropertyValue{std::int64_t{32}});
        animation.setProperty(SpriteAnimationKeys::kSheetColumns, PropertyValue{std::int64_t{4}});
        animation.setProperty(SpriteAnimationKeys::kFramesPerSecond,
                              PropertyValue{framesPerSecond});
        animation.setProperty(SpriteAnimationKeys::kLoop, PropertyValue{true});

        PropertyValue::ListValue frames;
        for (int index = 0; index < frameCount; ++index)
        {
            frames.items.push_back(PropertyValue{static_cast<std::int64_t>(index)});
        }
        animation.setProperty(SpriteAnimationKeys::kFrames, PropertyValue{std::move(frames)});
        return animation;
    }

    /** @brief The panel over an entity with a four-frame clip on it. */
    struct Fixture
    {
        StudioContext context;
        StudioFrame frame{StudioTheme::dark()};
        StudioFontAtlas fonts;
        UiRect bounds{0.0f, 0.0f, 520.0f, 880.0f};
        StudioDetailsResult last;

        Uuid entity;
        Uuid sheet;

        /** @brief Unset to run the panel the way a build with no device does. */
        bool withThumbnails = true;

        /** @brief How many times the thumbnail seam was asked, to prove it is asked at all. */
        int thumbnailRequests = 0;

        explicit Fixture(int frameCount = 4, float framesPerSecond = 10.0f)
        {
            frame.setFontAtlas(&fonts);

            AssetRecord record;
            record.id = Uuid::generate();
            record.sourcePath = "Assets/Textures/hero-run.png";
            record.type = AssetType::Texture2D;
            record.importerId = AssetDatabase::defaultImporterFor(record.type);
            // The sheet's size as the importer recorded it. The preview reads this rather than
            // asking the renderer, so the frame's texel range is known with no device at all.
            record.importerSettings.set("pixelSize",
                                        PropertyValue{StudioVector2{128.0f, 32.0f}}.toJson());
            sheet = record.id;
            (void)context.getAssets().add(std::move(record));

            StudioEntity subject{Uuid::generate(), "Hero"};
            subject.getComponents().push_back(StudioComponent{"CNA.Transform"});
            subject.getComponents().push_back(spriteAnimation(sheet, frameCount, framesPerSecond));
            entity = subject.getId();
            context.getScene().addEntity(std::move(subject));
            context.select(entity);
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                StudioDetailsServices services;
                if (withThumbnails)
                {
                    services.thumbnail = [this](const Uuid& id) {
                        ++thumbnailRequests;
                        return id == sheet ? kSheetTexture : kUiTextureNone;
                    };
                }
                const StudioDetailsResult result = studioDetailsPanel(f, bounds, context, services);
                if (f.isDrawPass()) { last = result; }
            });
        }

        /** @brief One frame with the pointer away from everything. */
        void settle(float delta = 1.0f / 60.0f) { run(at(-1.0f, -1.0f, false, delta)); }

        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        /** @brief Where the transport row sits: name, enabled, gap, then the components. */
        [[nodiscard]] UiRect transportRow() const
        {
            const StudioEntity* found = context.getScene().findEntity(entity);
            std::size_t row = 3;
            for (const StudioComponent& component : found->getComponents())
            {
                const ComponentDescriptor* descriptor =
                    context.getComponentRegistry().find(component.getTypeId());
                const std::size_t properties =
                    descriptor != nullptr ? descriptor->properties.size()
                                          : component.getProperties().size();
                if (component.getTypeId() == BuiltinComponentIds::kSpriteAnimation)
                {
                    row += 1 + properties;
                    break;
                }
                row += 1 + properties;
            }

            const float height = std::max(metricOf(frame.theme(), StudioMetric::ControlHeight),
                                          metricOf(frame.theme(), StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(frame.theme(), StudioMetric::SpacingXSmall);
            const UiRect area =
                bounds.inset(UiEdges{metricOf(frame.theme(), StudioMetric::SpacingSmall)});
            return UiRect{area.x, area.y + static_cast<float>(row) * (height + spacing), area.width,
                          height};
        }

        [[nodiscard]] float controlLeft() const
        {
            const UiRect area =
                bounds.inset(UiEdges{metricOf(frame.theme(), StudioMetric::SpacingSmall)});
            return area.x + std::round(area.width * 0.38f)
                   + metricOf(frame.theme(), StudioMetric::SpacingSmall);
        }

        /** @brief Clicks transport button @p index: 0 play/pause, 1 previous, 2 next. */
        void clickTransport(int index)
        {
            const float width = std::max(metricOf(frame.theme(), StudioMetric::ControlHeight),
                                         metricOf(frame.theme(), StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(frame.theme(), StudioMetric::SpacingXSmall);
            const UiRect row = transportRow();
            click(controlLeft() + (width + spacing) * static_cast<float>(index) + width * 0.5f,
                  row.centerY());
        }

        /** @brief The clip as the document currently holds it. */
        [[nodiscard]] SpriteAnimationClip clip() const
        {
            const StudioEntity* found = context.getScene().findEntity(entity);
            const StudioComponent* component =
                found->findComponent(BuiltinComponentIds::kSpriteAnimation);
            return readSpriteAnimationClip(
                *component, context.getComponentRegistry().find(BuiltinComponentIds::kSpriteAnimation));
        }

        /** @brief Whether this frame's draw data samples @p texture anywhere. */
        [[nodiscard]] bool drewTexture(UiTextureId texture) const
        {
            for (const UiDrawList& list : frame.drawData().lists)
            {
                for (const UiDrawCommand& command : list.commands)
                {
                    if (command.texture == texture && command.indexCount > 0) { return true; }
                }
            }
            return false;
        }
    };
}

CNA_STUDIO_TEST(ASpriteAnimationGetsAPreviewOfItsOwnClip)
{
    Fixture fixture;
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.animationFrames, std::size_t{4});
    CNA_STUDIO_EXPECT(fixture.last.animation.isActive());
    CNA_STUDIO_EXPECT(fixture.last.animation.entityId == fixture.entity);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(TheSheetsFrameIsSampledFromTheSheetsTexture)
{
    // The seam is asked, and the texture it answers with is actually drawn -- rather than the box
    // being filled with a placeholder while the seam is called and ignored.
    Fixture fixture;
    fixture.settle();

    CNA_STUDIO_EXPECT(fixture.thumbnailRequests > 0);
    CNA_STUDIO_EXPECT(fixture.drewTexture(kSheetTexture));
}

CNA_STUDIO_TEST(WithNoDeviceThePreviewDrawsNoTextureAndStillSaysWhatTheFrameIs)
{
    // A dependency-free Studio has nothing that can turn a PNG into a texture. The preview then
    // draws the frame's box rather than a picture -- and the panel still works, which is the
    // property that lets the headless capture photograph this panel at all.
    Fixture fixture;
    fixture.withThumbnails = false;
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.thumbnailRequests, 0);
    CNA_STUDIO_EXPECT(!fixture.drewTexture(kSheetTexture));

    // And the preview is still live: the transport works and the viewport is still told which
    // frame to draw, because neither needs the picture.
    CNA_STUDIO_EXPECT(fixture.last.animation.isActive());
    CNA_STUDIO_EXPECT_EQ(fixture.last.animationFrames, std::size_t{4});
}

CNA_STUDIO_TEST(PlayingAdvancesTheFrameAndTheViewportIsToldWhichOne)
{
    // Ten frames a second, so a tenth of a second is exactly one frame.
    Fixture fixture{4, 10.0f};
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{0});

    fixture.clickTransport(0);
    fixture.settle(0.1f);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{1});

    fixture.settle(0.1f);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{2});

    // And it loops, because the clip says so.
    fixture.settle(0.1f);
    fixture.settle(0.1f);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{0});
}

CNA_STUDIO_TEST(TimeAdvancesOncePerFrameRatherThanOncePerPass)
{
    // The trap a panel described twice a frame has and an object that owns its panel does not. A
    // clip advanced on the input pass and again on the draw pass runs at exactly double speed,
    // which looks plausible enough that nobody questions the rate -- and leaves the picture one
    // frame ahead of the transport that was just clicked.
    Fixture fixture{8, 10.0f};
    fixture.settle();
    fixture.clickTransport(0);

    // Clicking took three frames of its own, each a sixtieth of a second, so the position is
    // wherever those left it. Measured from here rather than from zero.
    const std::size_t start = fixture.last.animation.position;
    fixture.settle(0.1f);
    const std::size_t after = fixture.last.animation.position;

    CNA_STUDIO_EXPECT_EQ((after + 8 - start) % 8, std::size_t{1});
}

CNA_STUDIO_TEST(SteppingMovesOneFrameAndStopsPlayback)
{
    Fixture fixture{4, 10.0f};
    fixture.settle();

    fixture.clickTransport(2);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{1});

    fixture.clickTransport(2);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{2});

    fixture.clickTransport(1);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{1});

    // Stepping stops playback, so a long settle after it changes nothing. A step that left the
    // clip running would move the frame out from under the user as they examined it.
    fixture.settle(1.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{1});
}

CNA_STUDIO_TEST(PreviewingPutsNothingIntoTheDocument)
{
    // The requirement the migration inventory states for this task, asserted against the bytes a
    // save would write rather than against a property nobody thought to check.
    Fixture fixture{4, 10.0f};
    fixture.settle();

    const std::string before = Json::write(fixture.context.getScene().toJson());

    fixture.clickTransport(0);
    for (int tick = 0; tick < 30; ++tick) { fixture.settle(0.1f); }
    fixture.clickTransport(2);
    fixture.clickTransport(1);

    // The preview has certainly moved.
    CNA_STUDIO_EXPECT(fixture.last.animation.isActive());

    const std::string after = Json::write(fixture.context.getScene().toJson());
    CNA_STUDIO_EXPECT_EQ(before, after);

    // And nothing was pushed onto the history either, which is the other way a preview could
    // reach the document: an undo entry per frame would make Ctrl+Z step the animation backwards.
    CNA_STUDIO_EXPECT(!fixture.context.getHistory().canUndo());
}

CNA_STUDIO_TEST(TwoAnimatedComponentsKeepTheirOwnPosition)
{
    // The playback is keyed by the component's identity rather than held once for the panel, so a
    // second clip on the same entity does not share the first one's frame.
    Fixture fixture{4, 10.0f};
    StudioEntity* subject = fixture.context.getScene().findEntity(fixture.entity);
    subject->getComponents().push_back(spriteAnimation(fixture.sheet, 4, 10.0f));

    fixture.settle();
    fixture.clickTransport(2);
    fixture.clickTransport(2);

    // The first component's preview is on frame 2; the second is still on frame 0, and the
    // snapshot the viewport gets is the last one described -- the second.
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{0});

    // Which is only meaningful if the first really did move, so ask it directly by removing the
    // second and looking at what the panel then publishes.
    subject = fixture.context.getScene().findEntity(fixture.entity);
    subject->getComponents().pop_back();
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{2});
}

CNA_STUDIO_TEST(TheSnapshotStopsTravellingWhenTheSelectionMovesOff)
{
    // The viewport draws whatever the snapshot names, so a stale one leaves an entity frozen on a
    // preview frame after the user has moved on -- which reads as a scene that has been modified.
    Fixture fixture{4, 10.0f};
    fixture.settle();
    CNA_STUDIO_EXPECT(fixture.last.animation.isActive());

    fixture.context.clearSelection();
    fixture.settle();
    CNA_STUDIO_EXPECT(!fixture.last.animation.isActive());
    CNA_STUDIO_EXPECT_EQ(fixture.last.animationFrames, std::size_t{0});
}

CNA_STUDIO_TEST(AClipWithNoFramesSaysSoAndPublishesNothing)
{
    Fixture fixture{0, 10.0f};
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.animationFrames, std::size_t{0});
    CNA_STUDIO_EXPECT(!fixture.last.animation.isActive());

    // The transport is still drawn, disabled: a preview that vanishes for an empty clip is one a
    // user cannot tell from a feature that is not there.
    fixture.clickTransport(0);
    fixture.settle(1.0f);
    CNA_STUDIO_EXPECT(!fixture.last.animation.isActive());
}

CNA_STUDIO_TEST(ShorteningTheClipWhileItPlaysDoesNotLeaveThePositionPastTheEnd)
{
    // The frame list is editable while the preview runs. A position left past the end would name a
    // frame rectangle that does not exist, and the viewport would be told to draw it.
    Fixture fixture{8, 10.0f};
    fixture.settle();
    fixture.clickTransport(2);
    fixture.clickTransport(2);
    fixture.clickTransport(2);
    CNA_STUDIO_EXPECT_EQ(fixture.last.animation.position, std::size_t{3});

    StudioEntity* subject = fixture.context.getScene().findEntity(fixture.entity);
    StudioComponent* animation = subject->findComponent(BuiltinComponentIds::kSpriteAnimation);
    PropertyValue::ListValue frames;
    frames.items.push_back(PropertyValue{std::int64_t{0}});
    frames.items.push_back(PropertyValue{std::int64_t{1}});
    animation->setProperty(SpriteAnimationKeys::kFrames, PropertyValue{std::move(frames)});

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.clip().getFrameCount(), std::size_t{2});
    CNA_STUDIO_EXPECT(fixture.last.animation.position < std::size_t{2});
}
