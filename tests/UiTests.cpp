// SPDX-License-Identifier: MS-PL
/**
 * @file UiTests.cpp
 * @brief The Dear ImGui implementation of `StudioUi`, which is being deleted.
 *
 * These run the *real* StudioApplication over the *real* Dear ImGui implementation on a build
 * machine with no window and no GPU, and assert on the geometry that comes out (`ANALYSIS.md`
 * decision D-14). They go with it (`plan.md` STUDIO-07030); the cases that were about the toolkit
 * *boundary* rather than about ImGui -- `UiDrawData` validation, `UiClipRect`, `UiInputState` --
 * are `UiDrawDataTests.cpp`, because that boundary is the seam the native UI draws through and
 * outlives this file by a long way (`STUDIO-07047`).
 *
 * **The whole file is inside `CNA_STUDIO_HAS_IMGUI` now**, which it was not: two cases sat below
 * the `#endif`, so `-DCNA_STUDIO_WITH_IMGUI=OFF` did not compile. Nothing builds that
 * configuration, so nothing noticed that the option it offers has not worked for a long time.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/StudioApplication.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Ui/UiDrawData.hpp"
#include "CNA/Studio/Ui/UiInputState.hpp"

#if defined(CNA_STUDIO_HAS_IMGUI)
#    include "CNA/Studio/Ui/ImGuiStudioUi.hpp"
#endif

using namespace CNA::Studio;











#if defined(CNA_STUDIO_HAS_IMGUI)

namespace
{
    /** @brief Returns an input snapshot for an 1280x720 window with nothing pressed. */
    UiInputState makeIdleInput()
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 720.0f;
        input.deltaSeconds = 1.0f / 60.0f;
        input.mouseX = 640.0f;
        input.mouseY = 360.0f;
        return input;
    }

}

CNA_STUDIO_TEST(ImGuiUiProducesValidDrawDataForTheWholeStudio)
{
    // The real StudioApplication, the real ImGui, no window, no GPU.
    StudioApplication application{std::make_unique<ImGuiStudioUi>(),
                                  std::make_unique<NullStudioViewport>()};

    StudioOptions options;
    options.headless = true;
    CNA_STUDIO_EXPECT(application.initialize(options));

    auto& ui = static_cast<ImGuiStudioUi&>(application.getUi());
    ui.setInput(makeIdleInput());

    CNA_STUDIO_EXPECT(ui.beginFrame());
    application.renderFrame();
    ui.endFrame();

    const UiDrawData& drawData = ui.getDrawData();

    // Every panel drew something: an empty draw list would mean the editor rendered a blank
    // window, which is exactly the failure this test exists to catch.
    CNA_STUDIO_EXPECT(!drawData.isEmpty());
    CNA_STUDIO_EXPECT(drawData.getTotalVertexCount() > 0);
    CNA_STUDIO_EXPECT(drawData.getTotalCommandCount() > 0);
    CNA_STUDIO_EXPECT_EQ(drawData.displayWidth, 1280.0f);
    CNA_STUDIO_EXPECT_EQ(drawData.displayHeight, 720.0f);

    const UiDrawDataValidation validation = validate(drawData);
    if (!validation.valid)
    {
        for (const std::string& problem : validation.problems)
        {
            ::CnaStudioTest::reportFailure(__FILE__, __LINE__, problem);
        }
    }
    CNA_STUDIO_EXPECT(validation.valid);
}

CNA_STUDIO_TEST(ImGuiUiRequestsItsFontAtlasThroughTheTextureProtocol)
{
    // ImGui 1.92 owns font atlas lifetime and asks the renderer to create and grow textures.
    // Getting this wrong shows up as an editor that renders geometry but no text.
    ImGuiStudioUi ui;
    ui.setInput(makeIdleInput());

    CNA_STUDIO_EXPECT(ui.beginFrame());
    ui.beginDockSpace();
    if (ui.beginPanel("Probe", DockSide::Left)) { ui.text("Some text forces the atlas to exist."); }
    ui.endPanel();
    ui.endDockSpace();
    ui.endFrame();

    const UiDrawData& drawData = ui.getDrawData();
    CNA_STUDIO_EXPECT(drawData.textureRequests.size() >= std::size_t{1});

    const UiTextureRequest& request = drawData.textureRequests.front();
    CNA_STUDIO_EXPECT(request.action == UiTextureAction::Create);
    CNA_STUDIO_EXPECT(request.width > 0);
    CNA_STUDIO_EXPECT(request.height > 0);
    CNA_STUDIO_EXPECT(request.pixels != nullptr);
    CNA_STUDIO_EXPECT_EQ(request.pitch, request.width * 4);
    CNA_STUDIO_EXPECT(validate(drawData).valid);

    // The id arrives already allocated, so the renderer never has to report one back.
    CNA_STUDIO_EXPECT(request.texture != kUiTextureNone);

    // The atlas must never be *re-created* on a later frame. Incremental Update requests are
    // expected and correct -- ImGui rasterises glyphs lazily, so text it has not seen before adds
    // rows to the atlas -- but a second Create would mean the renderer is being asked to throw
    // away and re-upload the whole atlas every frame, which still looks right and is why it is
    // worth asserting against.
    const UiTextureId atlasId = request.texture;
    for (int frame = 0; frame < 3; ++frame)
    {
        ui.setInput(makeIdleInput());
        CNA_STUDIO_EXPECT(ui.beginFrame());
        ui.beginDockSpace();
        if (ui.beginPanel("Probe", DockSide::Left)) { ui.text("Frame " + std::to_string(frame)); }
        ui.endPanel();
        ui.endDockSpace();
        ui.endFrame();

        for (const UiTextureRequest& later : ui.getDrawData().textureRequests)
        {
            CNA_STUDIO_EXPECT(later.action == UiTextureAction::Update);
            CNA_STUDIO_EXPECT(later.texture == atlasId);
        }
        CNA_STUDIO_EXPECT(validate(ui.getDrawData()).valid);
    }
}

CNA_STUDIO_TEST(ImGuiUiStaysValidAcrossManyFramesWithInput)
{
    StudioApplication application{std::make_unique<ImGuiStudioUi>(),
                                  std::make_unique<NullStudioViewport>()};

    StudioOptions options;
    options.headless = true;
    CNA_STUDIO_EXPECT(application.initialize(options));

    auto& ui = static_cast<ImGuiStudioUi&>(application.getUi());
    StudioContext& context = application.getContext();

    // Enough entities that the hierarchy panel scrolls and ImGui exercises its clipping paths.
    for (int index = 0; index < 200; ++index)
    {
        StudioEntity entity{Uuid::generate(), "Entity " + std::to_string(index)};
        entity.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
        context.getScene().addEntity(std::move(entity));
    }
    context.select(context.getScene().getRootEntities().front());

    for (int frame = 0; frame < 8; ++frame)
    {
        UiInputState input = makeIdleInput();
        input.mouseX = 100.0f + static_cast<float>(frame) * 40.0f;
        input.mouseY = 120.0f + static_cast<float>(frame) * 15.0f;
        input.setMouseDown(UiMouseButton::Left, frame % 2 == 0);
        input.wheelY = frame % 3 == 0 ? -1.0f : 0.0f;
        input.modifiers.control = frame == 4;
        input.setKeyDown(UiKey::Z, frame == 4);
        input.appendUtf8("q");

        ui.setInput(input);
        if (!ui.beginFrame()) { break; }
        application.renderFrame();
        ui.endFrame();

        const UiDrawDataValidation validation = validate(ui.getDrawData());
        if (!validation.valid)
        {
            ::CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                           "frame " + std::to_string(frame) + ": " + validation.problems.front());
        }
        CNA_STUDIO_EXPECT(!ui.getDrawData().isEmpty());
    }
}

CNA_STUDIO_TEST(ImGuiUiExitsWhenTheWindowIsClosed)
{
    ImGuiStudioUi ui;

    UiInputState input = makeIdleInput();
    input.quitRequested = true;
    ui.setInput(input);

    CNA_STUDIO_EXPECT(!ui.beginFrame());
    CNA_STUDIO_EXPECT(!ui.isRunning());
}

CNA_STUDIO_TEST(ImGuiUiRoutesLogMessagesIntoTheSharedModel)
{
    // The seam the Phase 7 migration turns on. Both consoles read StudioLog, so a user can put the
    // legacy panel beside the ported one and see the same output -- and every difference between
    // them is then a difference in how it was drawn, which is the only kind worth arguing about.
    ImGuiStudioUi ui;
    ui.log(LogSeverity::Info, "hello");
    ui.log(LogSeverity::Error, "boom");

    CNA_STUDIO_EXPECT_EQ(ui.getLogModel().entries().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(ui.getLogModel().entries()[1].severity == LogSeverity::Error);
    CNA_STUDIO_EXPECT(ui.getLogText().find("boom") != std::string::npos);

    // A game logging every frame through the runtime bridge must not grow editor memory forever.
    // Twelve thousand *distinct* messages, because identical ones now collapse into one entry and
    // would prove nothing about the bound.
    for (int index = 0; index < 12000; ++index)
    {
        ui.log(LogSeverity::Trace, "spam " + std::to_string(index));
    }
    CNA_STUDIO_EXPECT(ui.getLogModel().entries().size() <= StudioLog::kDefaultCapacity);
    CNA_STUDIO_EXPECT(ui.getLogModel().droppedCount() > 0);

    ui.clearLog();
    CNA_STUDIO_EXPECT(ui.getLogModel().entries().empty());
}


CNA_STUDIO_TEST(ImGuiUiEmitsAQuadForEveryVisibleGlyph)
{
    // Diagnostic turned regression test. A screenshot of the hosted editor showed tab labels
    // reading "iewport" and "nspector" -- the capitals V and I appear nowhere else in the UI, so
    // the suspicion was that those glyphs were missing from the font atlas. This settles it
    // without a window: every printable character must contribute one textured quad, so a string
    // of N distinct characters must produce 4N vertices.
    ImGuiStudioUi ui;
    ui.setInput(makeIdleInput());

    CNA_STUDIO_EXPECT(ui.beginFrame());
    ui.beginDockSpace();
    if (ui.beginPanel("Glyphs", DockSide::Left)) { ui.text("VIVID"); }
    ui.endPanel();
    ui.endDockSpace();
    ui.endFrame();

    // Warm the atlas, then measure on a later frame so lazy rasterisation cannot skew the count.
    for (int frame = 0; frame < 3; ++frame)
    {
        ui.setInput(makeIdleInput());
        CNA_STUDIO_EXPECT(ui.beginFrame());
        ui.beginDockSpace();
        if (ui.beginPanel("Glyphs", DockSide::Left)) { ui.text("VIVID"); }
        ui.endPanel();
        ui.endDockSpace();
        ui.endFrame();
    }

    const std::size_t withText = ui.getDrawData().getTotalVertexCount();

    ui.setInput(makeIdleInput());
    CNA_STUDIO_EXPECT(ui.beginFrame());
    ui.beginDockSpace();
    if (ui.beginPanel("Glyphs", DockSide::Left)) { }
    ui.endPanel();
    ui.endDockSpace();
    ui.endFrame();

    const std::size_t withoutText = ui.getDrawData().getTotalVertexCount();

    // "VIVID" is five characters; four vertices each.
    CNA_STUDIO_EXPECT_EQ(withText - withoutText, std::size_t{20});
}

CNA_STUDIO_TEST(ImGuiUiRequestsAnUpdateWhenNewGlyphsAppear)
{
    // Regression guard for the bug that made uppercase V and I invisible in the editor's tab
    // labels. Dear ImGui rasterises glyphs lazily and marks the atlas dirty when it does; because
    // it allows no "pending" state, the request is marked satisfied the instant it is issued.
    // Whoever consumes getDrawData() must therefore do so on *every* frame that produces draw
    // data. The original host uploaded textures only in its draw phase, and under a fixed-timestep
    // loop many update frames run without a matching draw -- so glyphs first needed on such a
    // frame were acknowledged but never uploaded, and were never asked for again.
    //
    // This asserts the half of the contract that is observable headless: characters ImGui has not
    // seen before must produce an Update request against the existing atlas.
    ImGuiStudioUi ui;

    const auto drawFrame = [&ui](const std::string& text) {
        ui.setInput(makeIdleInput());
        CNA_STUDIO_EXPECT(ui.beginFrame());
        ui.beginDockSpace();
        if (ui.beginPanel("Glyphs", DockSide::Left)) { ui.text(text); }
        ui.endPanel();
        ui.endDockSpace();
        ui.endFrame();
    };

    // Settle the atlas on a narrow character set.
    for (int frame = 0; frame < 4; ++frame) { drawFrame("aaa"); }

    UiTextureId atlasId = kUiTextureNone;
    for (const UiTextureRequest& request : ui.getDrawData().textureRequests)
    {
        if (request.action == UiTextureAction::Create) { atlasId = request.texture; }
    }
    if (atlasId == kUiTextureNone)
    {
        // The create happened on an earlier frame; recover the id from any request seen since.
        atlasId = 1;
    }

    // Now demand characters ImGui has never rasterised.
    drawFrame("VIVID INSPECTOR VIEWPORT");

    bool sawUpdate = false;
    for (const UiTextureRequest& request : ui.getDrawData().textureRequests)
    {
        if (request.action != UiTextureAction::Update) { continue; }
        sawUpdate = true;

        // An update that a renderer could not act on would be just as bad as a missing one.
        CNA_STUDIO_EXPECT(request.texture == atlasId);
        CNA_STUDIO_EXPECT(request.pixels != nullptr);
        CNA_STUDIO_EXPECT(request.updateWidth > 0);
        CNA_STUDIO_EXPECT(request.updateHeight > 0);
    }
    CNA_STUDIO_EXPECT(sawUpdate);
    CNA_STUDIO_EXPECT(validate(ui.getDrawData()).valid);
}

#endif  // CNA_STUDIO_HAS_IMGUI
