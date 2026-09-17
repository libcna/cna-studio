// SPDX-License-Identifier: MS-PL
/**
 * @file StudioAudioPreviewTests.cpp
 * @brief Hearing a clip from the native Details panel.
 *
 * `plan.md` STUDIO-07044 — one of the Inspector sections `STUDIO-07041` found with no native
 * answer, and therefore one of the things that stop Dear ImGui being deleted.
 *
 * The prototype offers a preview in two places: on an entity carrying an audio source, with that
 * source's own volume, pan and pitch, and on a selected sound asset, with neutral ones. Both are
 * here, and one difference is deliberate: the prototype draws **one preview per entity**, found
 * with `findComponent`, while `CNA.AudioSource` is declared `unique = false`. An entity with two
 * sources on it has two clips and the prototype can only ever hear the first. The native panel
 * draws a preview per *source*, and `EachAudioSourceHearsItsOwnClip` is that difference.
 *
 * Driven through `runStudioFrame` over a rectangle this file chooses, rather than through a docked
 * shell: the preview sits below seven property rows, and a panel sized by the dock layout would
 * put it under the fold, where a test would be asserting about a scrollbar.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"
#include "CNA/Studio/Viewport/StudioAudio.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 900.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    float metricOf(const StudioTheme& theme, StudioMetric metric)
    {
        return static_cast<float>(theme.metric(metric));
    }

    /**
     * @brief The panel, a context, a recording audio device, and where the rows land.
     *
     * The rectangle is deliberately taller than the content: `studioBeginScroll` takes a
     * scrollbar's width out of the viewport the moment the content overflows, which would move
     * every control this file clicks by the thickness of a bar nobody is testing.
     */
    struct Fixture
    {
        StudioContext context;
        NullStudioAudio audio;
        StudioFrame frame{StudioTheme::dark()};

        /**
         * @brief The real typefaces, because a frame without an atlas draws every glyph as a
         *        rectangle.
         *
         * `StudioShell` installs one; a bare frame does not, and text then measures and paints as
         * solid blocks. That would leave `ThePreviewButtonsAreActuallyVisible` counting the
         * antialiasing on two icons and calling it a file name -- which is the exact shape of
         * mistake that guard exists to refuse.
         */
        StudioFontAtlas fonts;

        UiRect bounds{0.0f, 0.0f, 520.0f, 880.0f};
        StudioDetailsResult last;

        /** @brief Unset to run the panel the way a build with no audio device does. */
        bool withAudio = true;

        /** @brief A scratch project root, so the assets these tests add really exist. */
        std::filesystem::path projectRoot;

        Fixture()
        {
            frame.setFontAtlas(&fonts);

            projectRoot = std::filesystem::temp_directory_path()
                        / ("cna-studio-audio-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(projectRoot, code);
            std::filesystem::create_directories(projectRoot, code);
            context.getAssets().setProjectRoot(projectRoot.generic_string());
        }

        ~Fixture()
        {
            std::error_code code;
            std::filesystem::remove_all(projectRoot, code);
        }

        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;

        static int& counter() { static int value = 0; return value; }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                StudioDetailsServices services;
                if (withAudio) { services.audio = &audio; }
                const StudioDetailsResult result =
                    studioDetailsPanel(f, bounds, context, services);
                if (f.isInputPass()) { last = result; }
            });
        }

        void settle() { run(at(-1.0f, -1.0f)); }

        /** @brief Clicks, starting un-pressed so the router sees a real press. */
        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        /** @brief The panel's content rectangle: what the panel insets `bounds` to. */
        [[nodiscard]] UiRect content() const
        {
            return bounds.inset(UiEdges{metricOf(frame.theme(), StudioMetric::SpacingSmall)});
        }

        /** @brief The rectangle of row @p index, laid out exactly as the panel lays them. */
        [[nodiscard]] UiRect row(std::size_t index) const
        {
            const float height = std::max(metricOf(frame.theme(), StudioMetric::ControlHeight),
                                          metricOf(frame.theme(), StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(frame.theme(), StudioMetric::SpacingXSmall);
            const UiRect area = content();
            return UiRect{area.x, area.y + static_cast<float>(index) * (height + spacing),
                          area.width, height};
        }

        /** @brief Where the control column of row @p index starts. */
        [[nodiscard]] float controlLeft() const
        {
            const UiRect area = content();
            return area.x + std::round(area.width * 0.38f)
                   + metricOf(frame.theme(), StudioMetric::SpacingSmall);
        }

        /** @brief Clicks the Play button of the preview on row @p index. */
        void clickPlay(std::size_t index)
        {
            const UiRect box = row(index);
            const float buttonWidth = std::max(metricOf(frame.theme(), StudioMetric::ControlHeight),
                                               metricOf(frame.theme(), StudioMetric::MinimumHitTarget));
            click(controlLeft() + buttonWidth * 0.5f, box.centerY());
        }

        /** @brief Clicks the Stop button of the preview on row @p index. */
        void clickStop(std::size_t index)
        {
            const UiRect box = row(index);
            const float buttonWidth = std::max(metricOf(frame.theme(), StudioMetric::ControlHeight),
                                               metricOf(frame.theme(), StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(frame.theme(), StudioMetric::SpacingXSmall);
            click(controlLeft() + buttonWidth + spacing + buttonWidth * 0.5f, box.centerY());
        }
    };

    /**
     * @brief Adds a sound asset, with a real file behind it, and returns its id.
     *
     * The file matters: a tracked asset whose source is gone is a *different* state, and the
     * inspector says so and offers to relink it (STUDIO-09013). A fixture that left every asset
     * missing would be testing the preview against the one case where there is nothing to play.
     */
    Uuid addSound(StudioContext& context, const std::string& path)
    {
        AssetRecord record;
        record.id = Uuid::generate();
        record.sourcePath = path;
        record.type = AssetType::SoundEffect;
        record.importerId = AssetDatabase::defaultImporterFor(record.type);
        // Written *before* the record is added, because adding one asks the filesystem whether
        // its file is there (STUDIO-30012) -- and an asset tracked before its file exists is a
        // missing asset, which is a different state with a different inspector.
        if (!context.getAssets().getProjectRoot().empty())
        {
            const std::filesystem::path file{context.getAssets().resolvePath(path)};
            std::error_code code;
            std::filesystem::create_directories(file.parent_path(), code);
            std::ofstream stream{file, std::ios::binary};
            stream << "not really a wav";
        }

        const Uuid id = record.id;
        (void)context.getAssets().add(std::move(record));
        return id;
    }

    /** @brief An audio source component pointed at @p clip. */
    StudioComponent audioSource(const Uuid& clip, float volume, float pitch, float pan)
    {
        StudioComponent source{BuiltinComponentIds::kAudioSource};
        source.setProperty("clip", PropertyValue{PropertyValue::AssetReference{clip}});
        source.setProperty("volume", PropertyValue{volume});
        source.setProperty("pitch", PropertyValue{pitch});
        source.setProperty("pan", PropertyValue{pan});
        return source;
    }

    /** @brief How many rows a component occupies, header and preview excluded. */
    std::size_t propertyRows(const StudioContext& context, const StudioComponent& component)
    {
        const ComponentDescriptor* descriptor =
            context.getComponentRegistry().find(component.getTypeId());
        return descriptor != nullptr ? descriptor->properties.size()
                                     : component.getProperties().size();
    }

    /**
     * @brief The row index of the preview belonging to audio source number @p which.
     *
     * Derived from the descriptor rather than written down as a number, so adding a property to
     * `CNA.AudioSource` moves these tests' clicks with it instead of silently making them miss.
     */
    std::size_t previewRow(const StudioContext& context, const Uuid& entityId, std::size_t which)
    {
        const StudioEntity* entity = context.getScene().findEntity(entityId);
        if (entity == nullptr) { return 0; }

        std::size_t row = 3;  // name, enabled, the gap
        std::size_t seen = 0;
        for (const StudioComponent& component : entity->getComponents())
        {
            const std::size_t properties = propertyRows(context, component);
            const bool isSource = component.getTypeId() == BuiltinComponentIds::kAudioSource;
            if (isSource && seen++ == which) { return row + 1 + properties; }
            row += 1 + properties + (isSource ? 1u : 0u);
        }
        return 0;
    }

    /** @brief An entity with a transform and one audio source, selected. */
    struct SourceFixture : Fixture
    {
        Uuid entity;
        Uuid clip;

        SourceFixture()
        {
            clip = addSound(context, "Assets/Audio/bounce.wav");

            StudioEntity subject{Uuid::generate(), "Speaker"};
            subject.getComponents().push_back(StudioComponent{"CNA.Transform"});
            subject.getComponents().push_back(audioSource(clip, 0.25f, 0.5f, -0.75f));

            entity = subject.getId();
            context.getScene().addEntity(std::move(subject));
            context.select(entity);
        }
    };
}

CNA_STUDIO_TEST(AnAudioSourcePlaysItsClipWithItsOwnVolumePanAndPitch)
{
    // The whole point of previewing from the component rather than from the file: a preview at some
    // other level is a preview of a different sound, and "why is this quiet in game" is exactly the
    // question a preview exists to answer.
    SourceFixture fixture;
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.controls, std::size_t{1});
    CNA_STUDIO_EXPECT(fixture.audio.getRequests().empty());

    fixture.clickPlay(previewRow(fixture.context, fixture.entity, 0));

    CNA_STUDIO_EXPECT_EQ(fixture.audio.getRequests().size(), std::size_t{1});
    if (fixture.audio.getRequests().empty()) { return; }

    const NullStudioAudio::Request& request = fixture.audio.getRequests().front();
    CNA_STUDIO_EXPECT(request.assetId == fixture.clip);
    CNA_STUDIO_EXPECT_EQ(request.volume, 0.25f);
    CNA_STUDIO_EXPECT_EQ(request.pitch, 0.5f);
    CNA_STUDIO_EXPECT_EQ(request.pan, -0.75f);

    // And the panel says what happened, so the binder can put it in the Output Log: a clip that
    // will not load and a clip of silence sound identical.
    CNA_STUDIO_EXPECT(fixture.last.audio.played);
    CNA_STUDIO_EXPECT(fixture.last.audio.started);
    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.clip, std::string{"Assets/Audio/bounce.wav"});
}

CNA_STUDIO_TEST(EachAudioSourceHearsItsOwnClip)
{
    // `CNA.AudioSource` is declared unique = false, and the prototype's preview calls
    // findComponent -- so on an entity with two sources it can only ever play the first, with the
    // first's settings, whichever one the user was looking at.
    SourceFixture fixture;
    const Uuid second = addSound(fixture.context, "Assets/Audio/thud.wav");
    fixture.context.getScene().findEntity(fixture.entity)
        ->getComponents().push_back(audioSource(second, 1.0f, 0.0f, 0.5f));

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.controls, std::size_t{2});

    fixture.clickPlay(previewRow(fixture.context, fixture.entity, 1));

    CNA_STUDIO_EXPECT_EQ(fixture.audio.getRequests().size(), std::size_t{1});
    if (fixture.audio.getRequests().empty()) { return; }
    CNA_STUDIO_EXPECT(fixture.audio.getRequests().front().assetId == second);
    CNA_STUDIO_EXPECT_EQ(fixture.audio.getRequests().front().pan, 0.5f);
}

CNA_STUDIO_TEST(StopStopsThePreviewAndIsOfferedOnlyWhileSomethingIsAudible)
{
    SourceFixture fixture;
    fixture.settle();

    const std::size_t row = previewRow(fixture.context, fixture.entity, 0);

    // Nothing playing: Stop is disabled, and pressing it changes nothing rather than reporting a
    // stop that stopped nothing.
    fixture.clickStop(row);
    CNA_STUDIO_EXPECT(!fixture.last.audio.stopped);

    fixture.clickPlay(row);
    CNA_STUDIO_EXPECT(fixture.audio.isPlaying());

    fixture.clickStop(row);
    CNA_STUDIO_EXPECT(fixture.last.audio.stopped);
    CNA_STUDIO_EXPECT(!fixture.audio.isPlaying());
}

CNA_STUDIO_TEST(AnAudioSourceWithNoClipSaysSoRatherThanOfferingSilence)
{
    Fixture fixture;
    StudioEntity subject{Uuid::generate(), "Speaker"};
    subject.getComponents().push_back(audioSource(Uuid{}, 1.0f, 0.0f, 0.0f));
    const Uuid entity = subject.getId();
    fixture.context.getScene().addEntity(std::move(subject));
    fixture.context.select(entity);

    fixture.settle();

    // The control is still drawn -- a preview that disappears when there is no clip is one the
    // user cannot tell from a feature that does not exist.
    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.controls, std::size_t{1});

    fixture.clickPlay(previewRow(fixture.context, entity, 0));
    CNA_STUDIO_EXPECT(fixture.audio.getRequests().empty());
    CNA_STUDIO_EXPECT(!fixture.last.audio.played);
}

CNA_STUDIO_TEST(AClipWhoseAssetIsGoneIsRefusedRatherThanPlayed)
{
    // The scene outlives the asset database's knowledge of a file. A reference to an asset that is
    // no longer in the project is a broken reference, not a clip that plays nothing.
    SourceFixture fixture;
    CNA_STUDIO_EXPECT(fixture.context.getAssets().removeRecord(fixture.clip));

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.controls, std::size_t{1});

    fixture.clickPlay(previewRow(fixture.context, fixture.entity, 0));
    CNA_STUDIO_EXPECT(fixture.audio.getRequests().empty());
}

CNA_STUDIO_TEST(WithNoAudioInThisBuildThePreviewIsDisabledRatherThanAbsent)
{
    // A dependency-free Studio has no audio device at all. The control is still there and still
    // says why it cannot be used, which is the difference between a build without a feature and a
    // build whose feature is broken.
    SourceFixture fixture;
    fixture.withAudio = false;
    fixture.settle();

    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.controls, std::size_t{1});

    fixture.clickPlay(previewRow(fixture.context, fixture.entity, 0));
    CNA_STUDIO_EXPECT(!fixture.last.audio.played);
    CNA_STUDIO_EXPECT(fixture.audio.getRequests().empty());
}

CNA_STUDIO_TEST(ASelectedSoundAssetCanBeHeardBeforeAnythingUsesIt)
{
    // Hearing a clip is most often wanted right after importing it, when no entity references it
    // yet -- so the asset inspector offers the same preview, with neutral settings: this is the
    // file as imported, with nothing an entity chose applied to it.
    Fixture fixture;
    const Uuid clip = addSound(fixture.context, "Assets/Audio/pickup.wav");
    fixture.context.selectAsset(clip);

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.controls, std::size_t{1});

    // name, path, type, id, then the preview.
    fixture.clickPlay(4);

    CNA_STUDIO_EXPECT_EQ(fixture.audio.getRequests().size(), std::size_t{1});
    if (fixture.audio.getRequests().empty()) { return; }

    const NullStudioAudio::Request& request = fixture.audio.getRequests().front();
    CNA_STUDIO_EXPECT(request.assetId == clip);
    CNA_STUDIO_EXPECT_EQ(request.volume, 1.0f);
    CNA_STUDIO_EXPECT_EQ(request.pitch, 0.0f);
    CNA_STUDIO_EXPECT_EQ(request.pan, 0.0f);
}

CNA_STUDIO_TEST(AnAssetThatIsNotASoundOffersNoPreviewAtAll)
{
    // A texture inspector with a Play button on it is a control that can only ever refuse.
    Fixture fixture;
    AssetRecord record;
    record.id = Uuid::generate();
    record.sourcePath = "Assets/Textures/hero.png";
    record.type = AssetType::Texture2D;
    record.importerId = AssetDatabase::defaultImporterFor(record.type);
    const Uuid texture = record.id;
    (void)fixture.context.getAssets().add(std::move(record));
    fixture.context.selectAsset(texture);

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.audio.controls, std::size_t{0});
}

CNA_STUDIO_TEST(ThePreviewButtonsAndTheClipNameAreActuallyVisibleAndNotPaintedOver)
{
    // STUDIO-35063's lesson, applied rather than rediscovered: three widgets in this project have
    // been described before the surface they sit on, drawn first and painted over by it, and all
    // three worked perfectly while being invisible. Rasterised rather than read off the draw order,
    // because an ordering heuristic over emitted quads is a test of the heuristic -- the first
    // version of the asset inspector's guard passed with the defect deliberately restored.
    //
    // Two bands rather than one, because the row has two things in it that can be lost separately:
    // the buttons, and the sentence that says what would be heard. One band over the whole row
    // would pass on the antialiasing of two icons alone -- measured at 7 distinct colours with the
    // text missing, against 114 for the row as it should be.
    SourceFixture fixture;

    // Two frames with the texture table kept across them: the font atlas is requested on the frame
    // it is rasterised and never again, so a table built from the last frame alone would draw every
    // glyph as a solid rectangle and pass this for the wrong reason.
    UiTextureTable textures;
    fixture.settle();
    textures.apply(fixture.frame.drawData());
    fixture.settle();

    const ImageBuffer image = rasterizeUiDrawData(
        fixture.frame.drawData(), fixture.frame.theme().color(StudioColorRole::AppBackground),
        textures);
    CNA_STUDIO_EXPECT(!image.isEmpty());
    if (image.isEmpty()) { return; }

    const UiRect box = fixture.row(previewRow(fixture.context, fixture.entity, 0));
    const float buttonWidth =
        std::max(metricOf(fixture.frame.theme(), StudioMetric::ControlHeight),
                 metricOf(fixture.frame.theme(), StudioMetric::MinimumHitTarget));
    const float spacing = metricOf(fixture.frame.theme(), StudioMetric::SpacingXSmall);

    /** @brief Distinct colours in a band of the row. */
    const auto coloursIn = [&](float fromX, float toX) {
        const int left = std::max(0, static_cast<int>(fromX));
        const int right = std::min(image.width, static_cast<int>(toX));
        const int top = std::max(0, static_cast<int>(box.y));
        const int bottom = std::min(image.height, static_cast<int>(box.bottom()));
        std::set<std::uint32_t> colours;
        for (int y = top; y < bottom; ++y)
        {
            for (int x = left; x < right; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)
                     + static_cast<std::size_t>(x)) * 4u;
                colours.insert(static_cast<std::uint32_t>(image.pixels[offset]) << 16
                               | static_cast<std::uint32_t>(image.pixels[offset + 1]) << 8
                               | static_cast<std::uint32_t>(image.pixels[offset + 2]));
            }
        }
        return colours.size();
    };

    const float buttonsRight = fixture.controlLeft() + buttonWidth * 2.0f + spacing;
    const std::size_t buttons = coloursIn(fixture.controlLeft(), buttonsRight);
    if (buttons < 4)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
            "the preview's Play and Stop rasterise to " + std::to_string(buttons)
            + " distinct colours, which is a flat fill rather than two buttons with icons on them "
              "(plan.md STUDIO-35063).");
    }
    CNA_STUDIO_EXPECT(buttons >= 4);

    const std::size_t clipName =
        coloursIn(buttonsRight + metricOf(fixture.frame.theme(), StudioMetric::SpacingSmall),
                  box.right());
    if (clipName < 8)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
            "the clip's name beside the preview rasterises to " + std::to_string(clipName)
            + " distinct colours, which is a flat fill rather than text. Either it is not being "
              "drawn or it is being painted over (plan.md STUDIO-35063).");
    }
    CNA_STUDIO_EXPECT(clipName >= 8);
}
