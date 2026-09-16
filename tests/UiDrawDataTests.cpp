// SPDX-License-Identifier: MS-PL
/**
 * @file UiDrawDataTests.cpp
 * @brief The toolkit boundary: `UiDrawData`, `UiClipRect` and `UiInputState`.
 *
 * `plan.md` STUDIO-07047. These were in `UiTests.cpp` beside the Dear ImGui cases, and they are
 * not about Dear ImGui at all: `UiDrawData` is the seam *every* Studio UI draws through — the
 * native shell, the software rasteriser that makes headless golden images possible, and both CNA
 * render backends — and its validation is what turns "the renderer drew nothing" into a failure
 * naming the malformed index run.
 *
 * Separated so that deleting the prototype deletes a file rather than most of one.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Ui/UiDrawData.hpp"
#include "CNA/Studio/Ui/UiInputState.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

CNA_STUDIO_TEST(UiClipRectIntersectionNormalisesEmptyResults)
{
    const UiClipRect a{0.0f, 0.0f, 100.0f, 100.0f};
    const UiClipRect b{50.0f, 50.0f, 150.0f, 150.0f};

    const UiClipRect overlap = a.intersect(b);
    CNA_STUDIO_EXPECT_EQ(overlap.left, 50.0f);
    CNA_STUDIO_EXPECT_EQ(overlap.right, 100.0f);
    CNA_STUDIO_EXPECT(!overlap.isEmpty());

    // Disjoint rectangles would otherwise come out inverted (right < left), which every consumer
    // would have to special-case. Normalising here means isEmpty() is the only check needed.
    const UiClipRect disjoint = a.intersect(UiClipRect{200.0f, 200.0f, 300.0f, 300.0f});
    CNA_STUDIO_EXPECT(disjoint.isEmpty());
    CNA_STUDIO_EXPECT(disjoint.right >= disjoint.left);
    CNA_STUDIO_EXPECT(disjoint.bottom >= disjoint.top);
}

CNA_STUDIO_TEST(UiClipRectClampsToTheFramebuffer)
{
    const UiClipRect wild{-50.0f, -50.0f, 5000.0f, 5000.0f};
    const UiClipRect clamped = wild.clampTo(800.0f, 600.0f);

    CNA_STUDIO_EXPECT_EQ(clamped.left, 0.0f);
    CNA_STUDIO_EXPECT_EQ(clamped.top, 0.0f);
    CNA_STUDIO_EXPECT_EQ(clamped.right, 800.0f);
    CNA_STUDIO_EXPECT_EQ(clamped.bottom, 600.0f);
}

CNA_STUDIO_TEST(UiDrawDataValidationAcceptsAWellFormedList)
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
    CNA_STUDIO_EXPECT(result.valid);
    CNA_STUDIO_EXPECT_EQ(result.problems.size(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(drawData.getTotalVertexCount(), std::size_t{4});
    CNA_STUDIO_EXPECT_EQ(drawData.getTotalIndexCount(), std::size_t{6});
}

CNA_STUDIO_TEST(UiDrawDataValidationCatchesOutOfRangeIndices)
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
    CNA_STUDIO_EXPECT(!result.valid);
    CNA_STUDIO_EXPECT_EQ(result.problems.size(), std::size_t{1});
}

CNA_STUDIO_TEST(UiDrawDataValidationCatchesIndexRunsPastTheBuffer)
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

    CNA_STUDIO_EXPECT(!validate(drawData).valid);
}

CNA_STUDIO_TEST(UiDrawDataValidationRequiresTriangleLists)
{
    UiDrawData drawData;
    UiDrawList list;
    list.vertices.resize(4);
    list.indices = {0, 1, 2, 3};

    UiDrawCommand command;
    command.indexCount = 4;
    list.commands.push_back(command);
    drawData.lists.push_back(std::move(list));

    CNA_STUDIO_EXPECT(!validate(drawData).valid);
}

CNA_STUDIO_TEST(UiDrawDataValidationHonoursVertexOffset)
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

    CNA_STUDIO_EXPECT(validate(drawData).valid);

    drawData.lists[0].commands[0].vertexOffset = 8;
    CNA_STUDIO_EXPECT(!validate(drawData).valid);
}

CNA_STUDIO_TEST(UiDrawDataValidationChecksTextureRequests)
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
    CNA_STUDIO_EXPECT(validate(drawData).valid);

    // A pitch too small for the region would make the renderer walk off the end of each row.
    drawData.textureRequests[0].pitch = 4;
    CNA_STUDIO_EXPECT(!validate(drawData).valid);

    drawData.textureRequests[0].pitch = 16;
    drawData.textureRequests[0].updateX = 3;
    CNA_STUDIO_EXPECT(!validate(drawData).valid);
}

CNA_STUDIO_TEST(UiInputStateConvertsUtf8ToUtf16IncludingSurrogates)
{
    UiInputState input;
    input.appendUtf8("aZ");
    CNA_STUDIO_EXPECT_EQ(input.characters.size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(input.characters[0]), static_cast<int>(u'a'));

    input.characters.clear();
    input.appendUtf8("\xC4\x8D");  // U+010D, 'c' with caron
    CNA_STUDIO_EXPECT_EQ(input.characters.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(input.characters[0]), 0x010D);

    input.characters.clear();
    input.appendUtf8("\xF0\x9F\x8E\xAE");  // U+1F3AE, above the basic multilingual plane
    // CNA's TextInputEXT delivers UTF-16 code units, so a supplementary code point must arrive
    // here as a surrogate pair, exactly as it would from the real platform layer.
    CNA_STUDIO_EXPECT_EQ(input.characters.size(), std::size_t{2});
    CNA_STUDIO_EXPECT(input.characters[0] >= 0xD800 && input.characters[0] <= 0xDBFF);
    CNA_STUDIO_EXPECT(input.characters[1] >= 0xDC00 && input.characters[1] <= 0xDFFF);
}

CNA_STUDIO_TEST(UiInputStateClearsEventsButKeepsHeldState)
{
    UiInputState input;
    input.setMouseDown(UiMouseButton::Left, true);
    input.setKeyDown(UiKey::Z, true);
    input.wheelY = 3.0f;
    input.appendUtf8("x");

    input.clearEvents();

    // Wheel and characters are events; button and key state are absolute and must survive, or
    // every held button would read as a release the moment input stopped arriving.
    CNA_STUDIO_EXPECT_EQ(input.wheelY, 0.0f);
    CNA_STUDIO_EXPECT_EQ(input.characters.size(), std::size_t{0});
    CNA_STUDIO_EXPECT(input.isMouseDown(UiMouseButton::Left));
    CNA_STUDIO_EXPECT(input.isKeyDown(UiKey::Z));
}
