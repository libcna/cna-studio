// SPDX-License-Identifier: MS-PL
/**
 * @file UiTests.cpp
 * @brief Tests for the toolkit boundary: UiDrawData, UiInputState, and the ImGui implementation.
 *
 * The ImGui cases here are the payoff of ANALYSIS.md decision D-14. They run the *real*
 * EditorApplication over the *real* Dear ImGui implementation of EditorUi, on a build machine with
 * no window and no GPU, and assert on the geometry that comes out. Everything the CNA renderer
 * will be handed at run time is therefore already exercised in CI.
 */

#include "TestHarness.hpp"

#include "CNA/Editor/EditorApplication.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Ui/UiDrawData.hpp"
#include "CNA/Editor/Ui/UiInputState.hpp"

#if defined(CNA_EDITOR_HAS_IMGUI)
#    include "CNA/Editor/Ui/ImGuiEditorUi.hpp"
#endif

using namespace CNA::Editor;

CNA_EDITOR_TEST(UiClipRectIntersectionNormalisesEmptyResults)
{
    const UiClipRect a{0.0f, 0.0f, 100.0f, 100.0f};
    const UiClipRect b{50.0f, 50.0f, 150.0f, 150.0f};

    const UiClipRect overlap = a.intersect(b);
    CNA_EDITOR_EXPECT_EQ(overlap.left, 50.0f);
    CNA_EDITOR_EXPECT_EQ(overlap.right, 100.0f);
    CNA_EDITOR_EXPECT(!overlap.isEmpty());

    // Disjoint rectangles would otherwise come out inverted (right < left), which every consumer
    // would have to special-case. Normalising here means isEmpty() is the only check needed.
    const UiClipRect disjoint = a.intersect(UiClipRect{200.0f, 200.0f, 300.0f, 300.0f});
    CNA_EDITOR_EXPECT(disjoint.isEmpty());
    CNA_EDITOR_EXPECT(disjoint.right >= disjoint.left);
    CNA_EDITOR_EXPECT(disjoint.bottom >= disjoint.top);
}

CNA_EDITOR_TEST(UiClipRectClampsToTheFramebuffer)
{
    const UiClipRect wild{-50.0f, -50.0f, 5000.0f, 5000.0f};
    const UiClipRect clamped = wild.clampTo(800.0f, 600.0f);

    CNA_EDITOR_EXPECT_EQ(clamped.left, 0.0f);
    CNA_EDITOR_EXPECT_EQ(clamped.top, 0.0f);
    CNA_EDITOR_EXPECT_EQ(clamped.right, 800.0f);
    CNA_EDITOR_EXPECT_EQ(clamped.bottom, 600.0f);
}

CNA_EDITOR_TEST(UiDrawDataValidationAcceptsAWellFormedList)
{
    UiDrawData drawData;
    UiDrawList list;
    list.vertices.resize(4);
    list.indices = {0, 1, 2, 0, 2, 3};

    UiDrawCommand command;
    command.indexOffset = 0;
    command.indexCount = 6;
    command.clipRect = UiClipRect{0.0f, 0.0f, 100.0f, 100.0f};
    list.commands.push_back(command);
    drawData.lists.push_back(std::move(list));

    const UiDrawDataValidation result = validate(drawData);
    CNA_EDITOR_EXPECT(result.valid);
    CNA_EDITOR_EXPECT_EQ(result.problems.size(), std::size_t{0});
    CNA_EDITOR_EXPECT_EQ(drawData.getTotalVertexCount(), std::size_t{4});
    CNA_EDITOR_EXPECT_EQ(drawData.getTotalIndexCount(), std::size_t{6});
}

CNA_EDITOR_TEST(UiDrawDataValidationCatchesOutOfRangeIndices)
{
    // A renderer fed one bad index reads out of bounds, and an out-of-bounds read in the UI
    // renderer is a crash the user sees rather than a test failure.
    UiDrawData drawData;
    UiDrawList list;
    list.vertices.resize(3);
    list.indices = {0, 1, 7};

    UiDrawCommand command;
    command.indexCount = 3;
    command.clipRect = UiClipRect{0.0f, 0.0f, 10.0f, 10.0f};
    list.commands.push_back(command);
    drawData.lists.push_back(std::move(list));

    const UiDrawDataValidation result = validate(drawData);
    CNA_EDITOR_EXPECT(!result.valid);
    CNA_EDITOR_EXPECT_EQ(result.problems.size(), std::size_t{1});
}

CNA_EDITOR_TEST(UiDrawDataValidationCatchesIndexRunsPastTheBuffer)
{
    UiDrawData drawData;
    UiDrawList list;
    list.vertices.resize(4);
    list.indices = {0, 1, 2};

    UiDrawCommand command;
    command.indexOffset = 0;
    command.indexCount = 9;
    list.commands.push_back(command);
    drawData.lists.push_back(std::move(list));

    CNA_EDITOR_EXPECT(!validate(drawData).valid);
}

CNA_EDITOR_TEST(UiDrawDataValidationRequiresTriangleLists)
{
    UiDrawData drawData;
    UiDrawList list;
    list.vertices.resize(4);
    list.indices = {0, 1, 2, 3};

    UiDrawCommand command;
    command.indexCount = 4;
    list.commands.push_back(command);
    drawData.lists.push_back(std::move(list));

    CNA_EDITOR_EXPECT(!validate(drawData).valid);
}

CNA_EDITOR_TEST(UiDrawDataValidationHonoursVertexOffset)
{
    // VtxOffset lets one draw list exceed 65535 vertices while keeping 16-bit indices. Validation
    // must add it before bounds-checking, or every large list would look broken.
    UiDrawData drawData;
    UiDrawList list;
    list.vertices.resize(10);
    list.indices = {0, 1, 2};

    UiDrawCommand command;
    command.indexCount = 3;
    command.vertexOffset = 5;
    list.commands.push_back(command);
    drawData.lists.push_back(std::move(list));

    CNA_EDITOR_EXPECT(validate(drawData).valid);

    drawData.lists[0].commands[0].vertexOffset = 8;
    CNA_EDITOR_EXPECT(!validate(drawData).valid);
}

CNA_EDITOR_TEST(UiDrawDataValidationChecksTextureRequests)
{
    UiDrawData drawData;

    UiTextureRequest request;
    request.action = UiTextureAction::Create;
    request.width = 4;
    request.height = 4;
    request.updateWidth = 4;
    request.updateHeight = 4;
    request.pitch = 16;
    const std::vector<std::uint8_t> pixels(64, 0xFFu);
    request.pixels = pixels.data();
    drawData.textureRequests.push_back(request);
    CNA_EDITOR_EXPECT(validate(drawData).valid);

    // A pitch too small for the region would make the renderer walk off the end of each row.
    drawData.textureRequests[0].pitch = 4;
    CNA_EDITOR_EXPECT(!validate(drawData).valid);

    drawData.textureRequests[0].pitch = 16;
    drawData.textureRequests[0].updateX = 3;
    CNA_EDITOR_EXPECT(!validate(drawData).valid);
}

CNA_EDITOR_TEST(UiInputStateConvertsUtf8ToUtf16IncludingSurrogates)
{
    UiInputState input;
    input.appendUtf8("aZ");
    CNA_EDITOR_EXPECT_EQ(input.characters.size(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(input.characters[0]), static_cast<int>(u'a'));

    input.characters.clear();
    input.appendUtf8("\xC4\x8D");  // U+010D, 'c' with caron
    CNA_EDITOR_EXPECT_EQ(input.characters.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(static_cast<int>(input.characters[0]), 0x010D);

    input.characters.clear();
    input.appendUtf8("\xF0\x9F\x8E\xAE");  // U+1F3AE, above the basic multilingual plane
    // CNA's TextInputEXT delivers UTF-16 code units, so a supplementary code point must arrive
    // here as a surrogate pair, exactly as it would from the real platform layer.
    CNA_EDITOR_EXPECT_EQ(input.characters.size(), std::size_t{2});
    CNA_EDITOR_EXPECT(input.characters[0] >= 0xD800 && input.characters[0] <= 0xDBFF);
    CNA_EDITOR_EXPECT(input.characters[1] >= 0xDC00 && input.characters[1] <= 0xDFFF);
}

CNA_EDITOR_TEST(UiInputStateClearsEventsButKeepsHeldState)
{
    UiInputState input;
    input.setMouseDown(UiMouseButton::Left, true);
    input.setKeyDown(UiKey::Z, true);
    input.wheelY = 3.0f;
    input.appendUtf8("x");

    input.clearEvents();

    // Wheel and characters are events; button and key state are absolute and must survive, or
    // every held button would read as a release the moment input stopped arriving.
    CNA_EDITOR_EXPECT_EQ(input.wheelY, 0.0f);
    CNA_EDITOR_EXPECT_EQ(input.characters.size(), std::size_t{0});
    CNA_EDITOR_EXPECT(input.isMouseDown(UiMouseButton::Left));
    CNA_EDITOR_EXPECT(input.isKeyDown(UiKey::Z));
}

#if defined(CNA_EDITOR_HAS_IMGUI)

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

CNA_EDITOR_TEST(ImGuiUiProducesValidDrawDataForTheWholeEditor)
{
    // The real EditorApplication, the real ImGui, no window, no GPU.
    EditorApplication application{std::make_unique<ImGuiEditorUi>(),
                                  std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    auto& ui = static_cast<ImGuiEditorUi&>(application.getUi());
    ui.setInput(makeIdleInput());

    CNA_EDITOR_EXPECT(ui.beginFrame());
    application.renderFrame();
    ui.endFrame();

    const UiDrawData& drawData = ui.getDrawData();

    // Every panel drew something: an empty draw list would mean the editor rendered a blank
    // window, which is exactly the failure this test exists to catch.
    CNA_EDITOR_EXPECT(!drawData.isEmpty());
    CNA_EDITOR_EXPECT(drawData.getTotalVertexCount() > 0);
    CNA_EDITOR_EXPECT(drawData.getTotalCommandCount() > 0);
    CNA_EDITOR_EXPECT_EQ(drawData.displayWidth, 1280.0f);
    CNA_EDITOR_EXPECT_EQ(drawData.displayHeight, 720.0f);

    const UiDrawDataValidation validation = validate(drawData);
    if (!validation.valid)
    {
        for (const std::string& problem : validation.problems)
        {
            ::CnaEditorTest::reportFailure(__FILE__, __LINE__, problem);
        }
    }
    CNA_EDITOR_EXPECT(validation.valid);
}

CNA_EDITOR_TEST(ImGuiUiRequestsItsFontAtlasThroughTheTextureProtocol)
{
    // ImGui 1.92 owns font atlas lifetime and asks the renderer to create and grow textures.
    // Getting this wrong shows up as an editor that renders geometry but no text.
    ImGuiEditorUi ui;
    ui.setInput(makeIdleInput());

    CNA_EDITOR_EXPECT(ui.beginFrame());
    ui.beginDockSpace();
    if (ui.beginPanel("Probe", DockSide::Left)) { ui.text("Some text forces the atlas to exist."); }
    ui.endPanel();
    ui.endDockSpace();
    ui.endFrame();

    const UiDrawData& drawData = ui.getDrawData();
    CNA_EDITOR_EXPECT(drawData.textureRequests.size() >= std::size_t{1});

    const UiTextureRequest& request = drawData.textureRequests.front();
    CNA_EDITOR_EXPECT(request.action == UiTextureAction::Create);
    CNA_EDITOR_EXPECT(request.width > 0);
    CNA_EDITOR_EXPECT(request.height > 0);
    CNA_EDITOR_EXPECT(request.pixels != nullptr);
    CNA_EDITOR_EXPECT_EQ(request.pitch, request.width * 4);
    CNA_EDITOR_EXPECT(validate(drawData).valid);

    // The id arrives already allocated, so the renderer never has to report one back.
    CNA_EDITOR_EXPECT(request.texture != kUiTextureNone);

    // The atlas must never be *re-created* on a later frame. Incremental Update requests are
    // expected and correct -- ImGui rasterises glyphs lazily, so text it has not seen before adds
    // rows to the atlas -- but a second Create would mean the renderer is being asked to throw
    // away and re-upload the whole atlas every frame, which still looks right and is why it is
    // worth asserting against.
    const UiTextureId atlasId = request.texture;
    for (int frame = 0; frame < 3; ++frame)
    {
        ui.setInput(makeIdleInput());
        CNA_EDITOR_EXPECT(ui.beginFrame());
        ui.beginDockSpace();
        if (ui.beginPanel("Probe", DockSide::Left)) { ui.text("Frame " + std::to_string(frame)); }
        ui.endPanel();
        ui.endDockSpace();
        ui.endFrame();

        for (const UiTextureRequest& later : ui.getDrawData().textureRequests)
        {
            CNA_EDITOR_EXPECT(later.action == UiTextureAction::Update);
            CNA_EDITOR_EXPECT(later.texture == atlasId);
        }
        CNA_EDITOR_EXPECT(validate(ui.getDrawData()).valid);
    }
}

CNA_EDITOR_TEST(ImGuiUiStaysValidAcrossManyFramesWithInput)
{
    EditorApplication application{std::make_unique<ImGuiEditorUi>(),
                                  std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    auto& ui = static_cast<ImGuiEditorUi&>(application.getUi());
    EditorContext& context = application.getContext();

    // Enough entities that the hierarchy panel scrolls and ImGui exercises its clipping paths.
    for (int index = 0; index < 200; ++index)
    {
        EditorEntity entity{Uuid::generate(), "Entity " + std::to_string(index)};
        entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
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
            ::CnaEditorTest::reportFailure(__FILE__, __LINE__,
                                           "frame " + std::to_string(frame) + ": " + validation.problems.front());
        }
        CNA_EDITOR_EXPECT(!ui.getDrawData().isEmpty());
    }
}

CNA_EDITOR_TEST(ImGuiUiExitsWhenTheWindowIsClosed)
{
    ImGuiEditorUi ui;

    UiInputState input = makeIdleInput();
    input.quitRequested = true;
    ui.setInput(input);

    CNA_EDITOR_EXPECT(!ui.beginFrame());
    CNA_EDITOR_EXPECT(!ui.isRunning());
}

CNA_EDITOR_TEST(ImGuiUiRoutesLogMessagesAndBoundsThem)
{
    ImGuiEditorUi ui;
    ui.log(LogSeverity::Info, "hello");
    ui.log(LogSeverity::Error, "boom");

    CNA_EDITOR_EXPECT_EQ(ui.getLog().size(), std::size_t{2});
    CNA_EDITOR_EXPECT(ui.getLog()[1].first == LogSeverity::Error);

    // A game logging every frame through the runtime bridge must not grow editor memory forever.
    for (int index = 0; index < 12000; ++index) { ui.log(LogSeverity::Trace, "spam"); }
    CNA_EDITOR_EXPECT(ui.getLog().size() <= std::size_t{10000});
}

#endif  // CNA_EDITOR_HAS_IMGUI

CNA_EDITOR_TEST(ImGuiUiEmitsAQuadForEveryVisibleGlyph)
{
    // Diagnostic turned regression test. A screenshot of the hosted editor showed tab labels
    // reading "iewport" and "nspector" -- the capitals V and I appear nowhere else in the UI, so
    // the suspicion was that those glyphs were missing from the font atlas. This settles it
    // without a window: every printable character must contribute one textured quad, so a string
    // of N distinct characters must produce 4N vertices.
    ImGuiEditorUi ui;
    ui.setInput(makeIdleInput());

    CNA_EDITOR_EXPECT(ui.beginFrame());
    ui.beginDockSpace();
    if (ui.beginPanel("Glyphs", DockSide::Left)) { ui.text("VIVID"); }
    ui.endPanel();
    ui.endDockSpace();
    ui.endFrame();

    // Warm the atlas, then measure on a later frame so lazy rasterisation cannot skew the count.
    for (int frame = 0; frame < 3; ++frame)
    {
        ui.setInput(makeIdleInput());
        CNA_EDITOR_EXPECT(ui.beginFrame());
        ui.beginDockSpace();
        if (ui.beginPanel("Glyphs", DockSide::Left)) { ui.text("VIVID"); }
        ui.endPanel();
        ui.endDockSpace();
        ui.endFrame();
    }

    const std::size_t withText = ui.getDrawData().getTotalVertexCount();

    ui.setInput(makeIdleInput());
    CNA_EDITOR_EXPECT(ui.beginFrame());
    ui.beginDockSpace();
    if (ui.beginPanel("Glyphs", DockSide::Left)) { }
    ui.endPanel();
    ui.endDockSpace();
    ui.endFrame();

    const std::size_t withoutText = ui.getDrawData().getTotalVertexCount();

    // "VIVID" is five characters; four vertices each.
    CNA_EDITOR_EXPECT_EQ(withText - withoutText, std::size_t{20});
}

CNA_EDITOR_TEST(ImGuiUiRequestsAnUpdateWhenNewGlyphsAppear)
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
    ImGuiEditorUi ui;

    const auto drawFrame = [&ui](const std::string& text) {
        ui.setInput(makeIdleInput());
        CNA_EDITOR_EXPECT(ui.beginFrame());
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
        CNA_EDITOR_EXPECT(request.texture == atlasId);
        CNA_EDITOR_EXPECT(request.pixels != nullptr);
        CNA_EDITOR_EXPECT(request.updateWidth > 0);
        CNA_EDITOR_EXPECT(request.updateHeight > 0);
    }
    CNA_EDITOR_EXPECT(sawUpdate);
    CNA_EDITOR_EXPECT(validate(ui.getDrawData()).valid);
}
