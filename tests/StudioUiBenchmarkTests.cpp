// SPDX-License-Identifier: MS-PL
/**
 * @file StudioUiBenchmarkTests.cpp
 * @brief What a frame of UI costs each render backend (plan.md STUDIO-04028).
 *
 * The cost model is a second implementation of the two backends' inner loops, written against the
 * draw data rather than against a device. That is what makes it runnable with no CNA and no GPU,
 * and it is also what makes it capable of being quietly wrong -- so the numbers here are exact
 * rather than approximate, computed by hand from draw data small enough to count.
 *
 * The other half of the check is on the CNA-backed build:
 * `TheCostModelAgreesWithWhatTheBackendsActuallyReport` renders the same draw data through the real
 * `CnaUiRenderer` and `StudioModernUiRenderer` and requires their own counters to match these. A
 * model nobody compared against reality is a second implementation with no tests.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioUiBenchmark.hpp"

#include <cstdint>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A draw list of @p quads quads, drawn by @p commands commands over them. */
    UiDrawList listOf(std::size_t quads, std::size_t commands, UiTextureId texture = 1,
                      float clipRight = 1000.0f)
    {
        UiDrawList list;
        for (std::size_t i = 0; i < quads; ++i)
        {
            const auto base = static_cast<std::uint16_t>(list.vertices.size());
            for (int corner = 0; corner < 4; ++corner) { list.vertices.push_back(UiVertex{}); }
            for (const std::uint16_t offset : {0, 1, 2, 0, 2, 3})
            {
                list.indices.push_back(static_cast<std::uint16_t>(base + offset));
            }
        }

        const std::size_t perCommand = (quads / commands) * 6;
        for (std::size_t i = 0; i < commands; ++i)
        {
            UiDrawCommand command;
            command.indexOffset = static_cast<std::uint32_t>(i * perCommand);
            command.indexCount = static_cast<std::uint32_t>(perCommand);
            command.texture = texture;
            command.clipRect = UiClipRect{0.0f, 0.0f, clipRight, 800.0f};
            list.commands.push_back(command);
        }
        return list;
    }

    /** @brief A frame holding @p list, sized so nothing is clipped away by the display bounds. */
    UiDrawData frameOf(UiDrawList list)
    {
        UiDrawData data;
        data.displayWidth = 1000.0f;
        data.displayHeight = 800.0f;
        data.lists.push_back(std::move(list));
        return data;
    }
}

CNA_STUDIO_TEST(TheCostOfAFrameIsCountedExactly)
{
    // Ten quads: 40 vertices, 60 indices, drawn by two commands of five quads each.
    const StudioUiFrameCost cost = studioUiFrameCost(frameOf(listOf(10, 2)));

    CNA_STUDIO_EXPECT_EQ(cost.drawLists, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(cost.vertices, std::size_t{40});
    CNA_STUDIO_EXPECT_EQ(cost.indices, std::size_t{60});
    CNA_STUDIO_EXPECT_EQ(cost.triangles, std::size_t{20});
    CNA_STUDIO_EXPECT_EQ(cost.drawCalls, std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(cost.clippedAway, std::size_t{0});

    // One texture across both commands, so it is bound once; one clip rectangle, set once.
    CNA_STUDIO_EXPECT_EQ(cost.textureChanges, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(cost.clipChanges, std::size_t{1});

    // The modern backend uploads the list once: 40 vertices and 60 indices.
    CNA_STUDIO_EXPECT_EQ(cost.modernSubmittedBytes,
                         40 * kStudioUiSubmittedVertexBytes + 60 * kStudioUiUploadIndexBytes);

    // The classic backend hands the driver the whole vertex array on each of its two calls,
    // because `DrawUserIndexedPrimitives` takes a vertex offset relative to the array it is given.
    CNA_STUDIO_EXPECT_EQ(cost.classicSubmittedBytes,
                         2 * 40 * kStudioUiSubmittedVertexBytes + 60 * kStudioUiUploadIndexBytes);
}

CNA_STUDIO_TEST(TheClassicBackendsCostGrowsWithHowFinelyTheFrameIsBatched)
{
    // The whole argument for the modern renderer, as a number rather than an assumption. The same
    // geometry, drawn by one command and by a hundred: the modern backend uploads it once either
    // way, and the classic one re-copies the vertex array per call.
    const StudioUiFrameCost coarse = studioUiFrameCost(frameOf(listOf(100, 1)));
    const StudioUiFrameCost fine = studioUiFrameCost(frameOf(listOf(100, 100)));

    CNA_STUDIO_EXPECT_EQ(coarse.vertices, fine.vertices);
    CNA_STUDIO_EXPECT_EQ(coarse.indices, fine.indices);
    CNA_STUDIO_EXPECT_EQ(coarse.modernSubmittedBytes, fine.modernSubmittedBytes);

    CNA_STUDIO_EXPECT(fine.classicSubmittedBytes > coarse.classicSubmittedBytes * 50);
    CNA_STUDIO_EXPECT_EQ(fine.drawCalls, std::size_t{100});

    // Exactly a hundred copies of all four hundred vertices, plus the indices once. `vertexOffset`
    // is zero on every command in a list the toolkit has not had to split, which is every list
    // under 65536 vertices -- so "from the base vertex to the end" is "the whole array" in
    // practice, and the classic backend's cost is draw calls times list size rather than anything
    // smaller.
    CNA_STUDIO_EXPECT_EQ(fine.classicSubmittedBytes,
                         100 * 400 * kStudioUiSubmittedVertexBytes + 600 * kStudioUiUploadIndexBytes);
}

CNA_STUDIO_TEST(ASplitListChargesTheClassicBackendFromEachCommandsBaseVertex)
{
    // The other half of that rule, which only a list past 65535 vertices reaches: the array
    // `DrawUserIndexedPrimitives` is handed starts at the command's base vertex, because CNA's
    // vertex offset is relative to the array given rather than to the buffer. Charging the whole
    // list for a split one would overstate the backend on exactly the frames it is worst on, and
    // a number that flatters the case being argued against is worse than no number.
    UiDrawData data = frameOf(listOf(8, 2));
    data.lists.front().commands[1].vertexOffset = 16;   // the second command's quads start here

    const StudioUiFrameCost cost = studioUiFrameCost(data);
    CNA_STUDIO_EXPECT_EQ(cost.classicSubmittedBytes,
                         (32 + 16) * kStudioUiSubmittedVertexBytes + 48 * kStudioUiUploadIndexBytes);
}

CNA_STUDIO_TEST(TheCostModelSkipsExactlyWhatTheBackendsSkip)
{
    // A cost model that counted what was *emitted* rather than what is *submitted* would
    // over-report both backends and quietly change the ratio between them.

    // A list with no commands is skipped whole -- its vertices are never uploaded.
    UiDrawData noCommands = frameOf(listOf(10, 1));
    noCommands.lists.front().commands.clear();
    const StudioUiFrameCost skippedList = studioUiFrameCost(noCommands);
    CNA_STUDIO_EXPECT_EQ(skippedList.drawLists, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(skippedList.vertices, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(skippedList.modernSubmittedBytes, std::size_t{0});

    // A command with no indices is skipped, and is not a draw call.
    UiDrawData emptyCommand = frameOf(listOf(10, 2));
    emptyCommand.lists.front().commands.front().indexCount = 0;
    CNA_STUDIO_EXPECT_EQ(studioUiFrameCost(emptyCommand).drawCalls, std::size_t{1});

    // A command whose clip rectangle selects no pixels is counted as clipped away rather than
    // drawn: a zero-area scissor is rejected outright by some graphics APIs, so both backends
    // `continue` before issuing anything.
    UiDrawData clipped = frameOf(listOf(10, 2));
    clipped.lists.front().commands.front().clipRect = UiClipRect{50.0f, 50.0f, 50.0f, 90.0f};
    const StudioUiFrameCost clippedCost = studioUiFrameCost(clipped);
    CNA_STUDIO_EXPECT_EQ(clippedCost.drawCalls, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clippedCost.clippedAway, std::size_t{1});
}

CNA_STUDIO_TEST(ClipChangesAreCountedOnTheScissorRatherThanTheRectangle)
{
    // Both backends compare the rectangle *in framebuffer pixels*, after truncating the origin
    // and ceiling the extent. Two logical rectangles that differ below a pixel therefore resolve
    // to one scissor and are one change -- and a model that compared UiClipRect would over-count
    // exactly where the UI is densest, which is where the comparison matters.
    UiDrawData data = frameOf(listOf(10, 2));
    data.lists.front().commands[0].clipRect = UiClipRect{0.0f, 0.0f, 100.0f, 100.0f};
    data.lists.front().commands[1].clipRect = UiClipRect{0.2f, 0.1f, 100.2f, 100.1f};
    CNA_STUDIO_EXPECT_EQ(studioUiFrameCost(data).clipChanges, std::size_t{1});

    // A whole pixel apart is two.
    data.lists.front().commands[1].clipRect = UiClipRect{0.0f, 0.0f, 100.0f, 130.0f};
    CNA_STUDIO_EXPECT_EQ(studioUiFrameCost(data).clipChanges, std::size_t{2});
}

CNA_STUDIO_TEST(ATextureBoundTwiceInARowIsBoundOnce)
{
    UiDrawData data = frameOf(listOf(12, 3));
    data.lists.front().commands[0].texture = 1;
    data.lists.front().commands[1].texture = 1;
    data.lists.front().commands[2].texture = 2;
    CNA_STUDIO_EXPECT_EQ(studioUiFrameCost(data).textureChanges, std::size_t{2});

    // And alternating between two is a bind per command, which is what a batcher exists to avoid
    // and what this number is for.
    data.lists.front().commands[1].texture = 2;
    data.lists.front().commands[2].texture = 1;
    CNA_STUDIO_EXPECT_EQ(studioUiFrameCost(data).textureChanges, std::size_t{3});
}

CNA_STUDIO_TEST(AtlasGrowthIsChargedAsThePixelsItUploads)
{
    // Atlas growth is one of the frame shapes STUDIO-04028 names, and it is invisible in the
    // geometry counts: a frame that re-rasterises the font uploads megabytes and draws the same
    // quads. A Create is charged for the whole texture and an Update only for its region, which
    // is the distinction STUDIO-04016 exists to have made.
    UiDrawData data = frameOf(listOf(4, 1));

    UiTextureRequest create;
    create.action = UiTextureAction::Create;
    create.width = 512;
    create.height = 512;
    data.textureRequests.push_back(create);

    UiTextureRequest update;
    update.action = UiTextureAction::Update;
    update.texture = 1;
    update.width = 512;
    update.height = 512;
    update.updateWidth = 64;
    update.updateHeight = 16;
    data.textureRequests.push_back(update);

    const StudioUiFrameCost cost = studioUiFrameCost(data);
    CNA_STUDIO_EXPECT_EQ(cost.texturesCreated, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(cost.texturesUpdated, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(cost.textureBytesUploaded, std::size_t{512 * 512 * 4 + 64 * 16 * 4});
}

CNA_STUDIO_TEST(AccumulatingFramesAddsEveryFieldAndForgetsNone)
{
    // A field added to the cost and forgotten here would read as zero across a whole run, which
    // looks like "this frame shape costs nothing" rather than like a missing line.
    const StudioUiFrameCost one = studioUiFrameCost(frameOf(listOf(10, 2)));

    StudioUiFrameCost total;
    studioUiAccumulateCost(total, one);
    studioUiAccumulateCost(total, one);

    CNA_STUDIO_EXPECT_EQ(total.drawLists, one.drawLists * 2);
    CNA_STUDIO_EXPECT_EQ(total.vertices, one.vertices * 2);
    CNA_STUDIO_EXPECT_EQ(total.indices, one.indices * 2);
    CNA_STUDIO_EXPECT_EQ(total.triangles, one.triangles * 2);
    CNA_STUDIO_EXPECT_EQ(total.drawCalls, one.drawCalls * 2);
    CNA_STUDIO_EXPECT_EQ(total.textureChanges, one.textureChanges * 2);
    CNA_STUDIO_EXPECT_EQ(total.clipChanges, one.clipChanges * 2);
    CNA_STUDIO_EXPECT_EQ(total.classicSubmittedBytes, one.classicSubmittedBytes * 2);
    CNA_STUDIO_EXPECT_EQ(total.modernSubmittedBytes, one.modernSubmittedBytes * 2);
}

CNA_STUDIO_TEST(TheRealShellCostsMoreThroughTheClassicBackendThanTheModernOne)
{
    // The conclusion STUDIO-04027 turns on, asserted on a *real* frame rather than on a fixture:
    // whatever the shell happens to describe, the classic backend's per-draw user arrays cost it
    // more than the modern backend's one upload per list. The threshold is deliberately far below
    // what is measured (17-18x across every scenario in `--ui-benchmark`) so that this fails on a
    // reversal rather than on a change of a few percent.
    StudioTheme theme = StudioTheme::dark();
    StudioShell shell{theme};

    UiInputState input;
    input.displayWidth = 1600.0f;
    input.displayHeight = 900.0f;
    input.mouseInWindow = false;

    shell.renderFrame(input);
    shell.renderFrame(input);

    const StudioUiFrameCost cost = studioUiFrameCost(shell.drawData());
    CNA_STUDIO_EXPECT(cost.drawCalls > 4);
    CNA_STUDIO_EXPECT(cost.vertices > 500);
    CNA_STUDIO_EXPECT(cost.classicSubmittedBytes > cost.modernSubmittedBytes * 4);
}
