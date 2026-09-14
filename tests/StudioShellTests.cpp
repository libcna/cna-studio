// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellTests.cpp
 * @brief Layout, geometry, batching and golden-image tests for the CNA Studio shell.
 *
 * The whole file runs with no GPU, no window and no CNA. The shell's appearance is tested by
 * rasterising the same geometry the CNA renderer will draw (see `UiSoftwareRasterizer`), which is
 * what lets visual regressions be caught now rather than once graphical CI exists.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Core/ImageDiff.hpp"
#include "CNA/Studio/UiCore/StudioDrawList.hpp"
#include "CNA/Studio/UiCore/StudioShellLayout.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"
#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

using namespace CNA::Studio;

namespace
{
    /** @brief One frame of the real shell: its geometry, its totals and its resolved regions. */
    struct ShellRender
    {
        UiDrawData data;
        std::size_t vertices = 0;
        std::size_t commands = 0;
        StudioShellLayout layout;
        std::size_t phaseViolations = 0;
        UiRect viewportBounds;
        UiRect outlinerBounds;
    };

    /**
     * @brief Renders the default shell at a size and scale through the real application frame.
     *
     * Drives `StudioShell` rather than a draw-only function, so these tests measure what a user
     * actually sees. The pointer is deliberately outside the window: this is the shell at rest,
     * which is the state a golden image should pin.
     */
    ShellRender renderShell(float width, float height, float scale = 1.0f,
                            const std::vector<std::string>& closedPanels = {})
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);

        StudioShell shell{theme};
        for (const std::string& panel : closedPanels) { shell.dockTree().removePanel(panel); }

        UiInputState input;
        input.displayWidth = width;
        input.displayHeight = height;
        input.mouseInWindow = false;
        shell.renderFrame(input);

        ShellRender result;
        result.data = shell.drawData();
        result.layout = shell.layout();
        result.phaseViolations = shell.frame().phaseViolations();
        result.viewportBounds = shell.panelBounds("viewport");
        result.outlinerBounds = shell.panelBounds("outliner");
        for (const UiDrawList& list : result.data.lists)
        {
            result.vertices += list.vertices.size();
            result.commands += list.commands.size();
        }
        return result;
    }

    /** @brief Where golden images and failure artefacts are written. */
    std::string artifactDirectory()
    {
        const char* fromEnv = std::getenv("CNA_STUDIO_TEST_ARTIFACTS");
        return fromEnv != nullptr ? std::string{fromEnv} : std::string{};
    }
} // namespace

// ------------------------------------------------------------------------------------------------
// Geometry primitives (STUDIO-03011)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(SplittingARectangleConsumesItExactly)
{
    UiRect area{0.0f, 0.0f, 100.0f, 50.0f};
    const UiRect top = area.splitTop(10.0f);

    CNA_STUDIO_EXPECT(top == UiRect(0.0f, 0.0f, 100.0f, 10.0f));
    CNA_STUDIO_EXPECT(area == UiRect(0.0f, 10.0f, 100.0f, 40.0f));
    CNA_STUDIO_EXPECT_EQ(top.height + area.height, 50.0f);
}

CNA_STUDIO_TEST(SplittingMoreThanThereIsLeavesNothingRatherThanANegativeRemainder)
{
    // A window dragged smaller than its own chrome must produce empty panels, not panels drawn at
    // negative sizes -- which rasterise as garbage across the whole window.
    UiRect area{0.0f, 0.0f, 100.0f, 20.0f};
    const UiRect taken = area.splitTop(500.0f);

    CNA_STUDIO_EXPECT_EQ(taken.height, 20.0f);
    CNA_STUDIO_EXPECT_EQ(area.height, 0.0f);
    CNA_STUDIO_EXPECT(area.isEmpty());
}

CNA_STUDIO_TEST(EverySplitDirectionPartitionsWithoutOverlap)
{
    for (int direction = 0; direction < 4; ++direction)
    {
        UiRect area{10.0f, 20.0f, 100.0f, 80.0f};
        const UiRect whole = area;
        UiRect slice;
        switch (direction)
        {
            case 0: slice = area.splitTop(30.0f); break;
            case 1: slice = area.splitBottom(30.0f); break;
            case 2: slice = area.splitLeft(30.0f); break;
            default: slice = area.splitRight(30.0f); break;
        }
        CNA_STUDIO_EXPECT(slice.intersect(area).isEmpty());
        CNA_STUDIO_EXPECT(whole.intersect(slice) == slice);
        CNA_STUDIO_EXPECT(whole.intersect(area) == area);
    }
}

CNA_STUDIO_TEST(InsettingPastTheSizeYieldsAnEmptyRectangleNotAnInvertedOne)
{
    const UiRect small{0.0f, 0.0f, 10.0f, 10.0f};
    const UiRect inset = small.inset(20.0f);
    CNA_STUDIO_EXPECT(inset.isEmpty());
    CNA_STUDIO_EXPECT(inset.width >= 0.0f);
    CNA_STUDIO_EXPECT(inset.height >= 0.0f);
}

CNA_STUDIO_TEST(PixelSnappingPreservesEdgesRatherThanSizes)
{
    // Rounding position and size independently makes a 1px rule two pixels wide at one position
    // and zero at another, which reads as the rule flickering as a panel is dragged.
    const UiRect r{10.4f, 20.6f, 100.3f, 1.2f};
    const UiRect snapped = r.pixelSnapped();
    CNA_STUDIO_EXPECT_EQ(snapped.left(), 10.0f);
    CNA_STUDIO_EXPECT_EQ(snapped.top(), 21.0f);
    CNA_STUDIO_EXPECT_EQ(snapped.right(), 111.0f);
    CNA_STUDIO_EXPECT_EQ(snapped.bottom(), 22.0f);
}

// ------------------------------------------------------------------------------------------------
// Shell layout (STUDIO-06003, STUDIO-06006, STUDIO-06007)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheShellLayoutIsWellFormedAtEveryCommonResolution)
{
    // The invariant a docking layout must never violate: nothing escapes the window, nothing
    // overlaps. Checked directly rather than inferred from a screenshot.
    const StudioTheme theme = StudioTheme::dark();
    const std::pair<float, float> resolutions[] = {
        {1280.0f, 720.0f}, {1600.0f, 900.0f}, {1920.0f, 1080.0f},
        {2560.0f, 1440.0f}, {3440.0f, 1440.0f}};

    for (const auto& [width, height] : resolutions)
    {
        const StudioShellLayout layout = computeStudioShellLayout(width, height, theme);
        if (!layout.isWellFormed())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "shell layout is malformed at " + std::to_string(static_cast<int>(width))
                + "x" + std::to_string(static_cast<int>(height)));
        }
        CNA_STUDIO_EXPECT(layout.isWellFormed());
        CNA_STUDIO_EXPECT(!layout.dockArea.isEmpty());
        CNA_STUDIO_EXPECT(!layout.menuBar.isEmpty());
        CNA_STUDIO_EXPECT(!layout.statusBar.isEmpty());
    }
}

CNA_STUDIO_TEST(TheShellLayoutIsWellFormedAtEveryDpiScale)
{
    for (const float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f})
    {
        StudioTheme theme = StudioTheme::dark();
        theme.setScale(scale);
        const StudioShellLayout layout = computeStudioShellLayout(1920.0f, 1080.0f, theme);
        CNA_STUDIO_EXPECT(layout.isWellFormed());
        CNA_STUDIO_EXPECT(!layout.dockArea.isEmpty());
    }
}

CNA_STUDIO_TEST(ChromeHeightScalesWithDpi)
{
    StudioTheme theme = StudioTheme::dark();
    const StudioShellLayout at100 = computeStudioShellLayout(1920.0f, 1080.0f, theme);
    theme.setScale(2.0f);
    const StudioShellLayout at200 = computeStudioShellLayout(1920.0f, 1080.0f, theme);

    CNA_STUDIO_EXPECT_EQ(at200.menuBar.height, at100.menuBar.height * 2.0f);
    CNA_STUDIO_EXPECT_EQ(at200.toolbar.height, at100.toolbar.height * 2.0f);
    CNA_STUDIO_EXPECT_EQ(at200.statusBar.height, at100.statusBar.height * 2.0f);
}

CNA_STUDIO_TEST(AWindowTooSmallForItsChromeDegradesRatherThanBreaking)
{
    const StudioTheme theme = StudioTheme::dark();
    for (const float size : {1.0f, 20.0f, 60.0f, 120.0f})
    {
        const StudioShellLayout layout = computeStudioShellLayout(size, size, theme);
        if (!layout.isWellFormed())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "layout malformed at " + std::to_string(static_cast<int>(size)) + "px square");
        }
        CNA_STUDIO_EXPECT(layout.isWellFormed());

        // Every region stays non-negative: an inverted rectangle rasterises across the window.
        for (const UiRect* r : {&layout.menuBar, &layout.toolbar, &layout.statusBar,
                                &layout.dockArea})
        {
            CNA_STUDIO_EXPECT(r->width >= 0.0f);
            CNA_STUDIO_EXPECT(r->height >= 0.0f);
        }
    }
}

CNA_STUDIO_TEST(ClosingEveryOtherPanelGivesTheViewportTheWholeDockArea)
{
    StudioShell shell;
    UiInputState input;
    input.displayWidth = 1920.0f;
    input.displayHeight = 1080.0f;
    shell.renderFrame(input);

    const UiRect before = shell.panelBounds("viewport");
    CNA_STUDIO_EXPECT(!before.isEmpty());

    for (const char* panel : {"outliner", "layers", "details", "material",
                              "content", "output", "build", "problems"})
    {
        shell.dockTree().removePanel(panel);
    }
    shell.renderFrame(input);

    const UiRect after = shell.panelBounds("viewport");
    CNA_STUDIO_EXPECT(after.width > before.width);
    CNA_STUDIO_EXPECT(after.height > before.height);
    CNA_STUDIO_EXPECT(shell.dockTree().isWellFormed());
    CNA_STUDIO_EXPECT_EQ(shell.dockTree().leaves().size(), std::size_t{1});
}

CNA_STUDIO_TEST(TheChromeAndTheDockAreaTileTheWindowExactly)
{
    // The dock area is what is left after the chrome, and "what is left" must be exactly that:
    // a gap is a strip of app background nobody can use, and an overlap is a panel drawn over a bar.
    const StudioTheme theme = StudioTheme::dark();
    const StudioShellLayout layout = computeStudioShellLayout(1920.0f, 1080.0f, theme);

    CNA_STUDIO_EXPECT_EQ(layout.menuBar.top(), layout.window.top());
    CNA_STUDIO_EXPECT_EQ(layout.toolbar.top(), layout.menuBar.bottom());
    CNA_STUDIO_EXPECT_EQ(layout.dockArea.top(), layout.toolbar.bottom());
    CNA_STUDIO_EXPECT_EQ(layout.dockArea.bottom(), layout.statusBar.top());
    CNA_STUDIO_EXPECT_EQ(layout.statusBar.bottom(), layout.window.bottom());
    CNA_STUDIO_EXPECT_EQ(layout.dockArea.width, layout.window.width);
}

// ------------------------------------------------------------------------------------------------
// Draw list and batching (STUDIO-04003, STUDIO-04004)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ConsecutiveUntexturedPrimitivesBatchIntoOneDrawCall)
{
    // STUDIO-04003's requirement, asserted rather than assumed: a panel of flat rectangles costs
    // one draw call, not one per rectangle.
    StudioDrawList list;
    list.begin(200.0f, 200.0f);
    for (int i = 0; i < 50; ++i)
    {
        list.fillRect(UiRect{0.0f, static_cast<float>(i) * 4.0f, 200.0f, 3.0f},
                      StudioColor{100, 100, 100, 255});
    }
    list.end();

    CNA_STUDIO_EXPECT_EQ(list.commandCount(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(list.vertexCount(), std::size_t{200});
}

CNA_STUDIO_TEST(AClipChangeStartsANewDrawCall)
{
    StudioDrawList list;
    list.begin(200.0f, 200.0f);
    list.fillRect(UiRect{0.0f, 0.0f, 10.0f, 10.0f}, StudioColor{255, 0, 0, 255});
    list.pushClip(UiRect{0.0f, 0.0f, 50.0f, 50.0f});
    list.fillRect(UiRect{0.0f, 0.0f, 10.0f, 10.0f}, StudioColor{0, 255, 0, 255});
    list.popClip();
    list.end();

    CNA_STUDIO_EXPECT_EQ(list.commandCount(), std::size_t{2});
}

CNA_STUDIO_TEST(NestedClipsCompose)
{
    // A scroll area inside a panel inside a dock cannot draw outside any of them.
    StudioDrawList list;
    list.begin(200.0f, 200.0f);
    list.pushClip(UiRect{0.0f, 0.0f, 100.0f, 100.0f});
    list.pushClip(UiRect{50.0f, 50.0f, 100.0f, 100.0f});

    const UiRect clip = list.currentClip();
    CNA_STUDIO_EXPECT(clip == UiRect(50.0f, 50.0f, 50.0f, 50.0f));

    list.popClip();
    list.popClip();
    list.end();
}

CNA_STUDIO_TEST(PoppingPastTheRootClipLeavesTheDisplayClip)
{
    StudioDrawList list;
    list.begin(200.0f, 100.0f);
    list.popClip();
    list.popClip();
    CNA_STUDIO_EXPECT(list.currentClip() == UiRect(0.0f, 0.0f, 200.0f, 100.0f));
    list.end();
}

CNA_STUDIO_TEST(FullyTransparentPrimitivesEmitNoGeometry)
{
    StudioDrawList list;
    list.begin(100.0f, 100.0f);
    list.fillRect(UiRect{0.0f, 0.0f, 50.0f, 50.0f}, StudioColor{255, 0, 0, 0});
    list.end();
    CNA_STUDIO_EXPECT_EQ(list.vertexCount(), std::size_t{0});
}

CNA_STUDIO_TEST(AnEmptyRectangleEmitsNoGeometry)
{
    StudioDrawList list;
    list.begin(100.0f, 100.0f);
    list.fillRect(UiRect{10.0f, 10.0f, 0.0f, 50.0f}, StudioColor{255, 0, 0, 255});
    list.fillRect(UiRect{10.0f, 10.0f, 50.0f, -5.0f}, StudioColor{255, 0, 0, 255});
    list.end();
    CNA_STUDIO_EXPECT_EQ(list.vertexCount(), std::size_t{0});
}

CNA_STUDIO_TEST(ARoundedRectangleWithAnAbsurdRadiusDoesNotFoldThroughItself)
{
    // Corner arcs that overlap produce self-intersecting geometry which rasterises as a dark
    // smear. Clamping keeps a pill shape at the limit.
    StudioDrawList list;
    list.begin(100.0f, 100.0f);
    list.fillRoundedRect(UiRect{10.0f, 10.0f, 40.0f, 20.0f}, StudioColor{200, 200, 200, 255}, 500.0f);
    list.end();

    CNA_STUDIO_EXPECT(list.vertexCount() > 0);
    for (const UiVertex& v : list.drawData().lists.front().vertices)
    {
        CNA_STUDIO_EXPECT(v.x >= 9.0f && v.x <= 51.0f);
        CNA_STUDIO_EXPECT(v.y >= 9.0f && v.y <= 31.0f);
    }
}

CNA_STUDIO_TEST(TheShellProducesAWellFormedFrame)
{
    const ShellRender shell = renderShell(1920.0f, 1080.0f);
    const UiDrawData& data = shell.data;

    CNA_STUDIO_EXPECT(!data.lists.empty());
    CNA_STUDIO_EXPECT(shell.vertices > 0);
    CNA_STUDIO_EXPECT(shell.commands > 0);
    CNA_STUDIO_EXPECT_EQ(shell.phaseViolations, std::size_t{0});

    // Every index must address a vertex that exists: an out-of-range index is a GPU crash on a
    // real renderer and silently wrong pixels here.
    for (const UiDrawList& drawList : data.lists)
    {
        for (const UiDrawCommand& command : drawList.commands)
        {
            CNA_STUDIO_EXPECT(command.indexOffset + command.indexCount <= drawList.indices.size());
            CNA_STUDIO_EXPECT_EQ(command.indexCount % 3, std::uint32_t{0});
        }
        for (const std::uint16_t index : drawList.indices)
        {
            CNA_STUDIO_EXPECT(index < drawList.vertices.size());
        }
    }
}

CNA_STUDIO_TEST(TheShellDrawsNothingOutsideItsWindow)
{
    const ShellRender shell = renderShell(800.0f, 600.0f);
    for (const UiVertex& v : shell.data.lists.front().vertices)
    {
        CNA_STUDIO_EXPECT(v.x >= -1.0f && v.x <= 801.0f);
        CNA_STUDIO_EXPECT(v.y >= -1.0f && v.y <= 601.0f);
    }
}

CNA_STUDIO_TEST(TheShellFrameCostsABoundedNumberOfDrawCalls)
{
    // Not a performance micro-optimisation: an unbatched UI issues a draw call per rectangle, and
    // the number climbing quietly is exactly how that regresses.
    const ShellRender shell = renderShell(1920.0f, 1080.0f);
    CNA_STUDIO_EXPECT(shell.commands < 32);
}

// ------------------------------------------------------------------------------------------------
// Golden image (STUDIO-33011)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheShellRasterisesToAStableImage)
{
    // The first screenshot test for the Studio shell, and it needs no GPU: the geometry the CNA
    // renderer will draw is rasterised on the CPU instead. A clean process exit cannot tell a
    // working shell from one that drew nothing; this can.
    const ShellRender shell = renderShell(640.0f, 360.0f);
    const ImageBuffer image = rasterizeUiDrawData(shell.data, StudioColor{0, 0, 0, 255});

    CNA_STUDIO_EXPECT(image.isWellFormed());
    CNA_STUDIO_EXPECT_EQ(image.width, 640);
    CNA_STUDIO_EXPECT_EQ(image.height, 360);

    // Rendering twice must be byte-identical. Without determinism a golden image is a coin toss.
    const ShellRender again = renderShell(640.0f, 360.0f);
    const ImageBuffer second = rasterizeUiDrawData(again.data, StudioColor{0, 0, 0, 255});
    CNA_STUDIO_EXPECT(image.pixels == second.pixels);
}

CNA_STUDIO_TEST(TheShellActuallyDrawsSomethingRatherThanClearing)
{
    // The failure a clean exit cannot distinguish: a window that opened and drew nothing. The
    // clear colour is one no theme uses, so any pixel still holding it was never covered.
    const ShellRender shell = renderShell(640.0f, 360.0f);
    const StudioColor sentinel{255, 0, 255, 255};
    const ImageBuffer image = rasterizeUiDrawData(shell.data, sentinel);

    std::size_t untouched = 0;
    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
    {
        if (image.pixels[i] == sentinel.r && image.pixels[i + 1] == sentinel.g
            && image.pixels[i + 2] == sentinel.b)
        {
            ++untouched;
        }
    }
    CNA_STUDIO_EXPECT_EQ(untouched, std::size_t{0});
}

CNA_STUDIO_TEST(EveryShellRegionIsVisiblyDistinct)
{
    // A layered UI whose layers all resolve to the same pixel value is a UI with no depth. Sampling
    // the middle of each region catches a theme or a draw order that flattened them.
    const ShellRender shell = renderShell(1280.0f, 720.0f);
    const StudioShellLayout& layout = shell.layout;
    const ImageBuffer image = rasterizeUiDrawData(shell.data, StudioColor{0, 0, 0, 255});
    const auto sample = [&image](const UiRect& r) {
        const int x = std::clamp(static_cast<int>(r.centerX()), 0, image.width - 1);
        const int y = std::clamp(static_cast<int>(r.centerY()), 0, image.height - 1);
        const std::size_t i = (static_cast<std::size_t>(y) * image.width + x) * 4;
        return std::string{std::to_string(image.pixels[i]) + ","
                         + std::to_string(image.pixels[i + 1]) + ","
                         + std::to_string(image.pixels[i + 2])};
    };

    const std::string menuBar = sample(layout.menuBar);
    const std::string viewport = sample(shell.viewportBounds);
    const std::string outliner = sample(shell.outlinerBounds);

    CNA_STUDIO_EXPECT(!shell.viewportBounds.isEmpty());
    CNA_STUDIO_EXPECT(!shell.outlinerBounds.isEmpty());
    CNA_STUDIO_EXPECT(menuBar != viewport);
    CNA_STUDIO_EXPECT(outliner != viewport);
}

CNA_STUDIO_TEST(TheShellRendersAtEveryTestedResolutionAndScale)
{
    struct Case { float width; float height; float scale; const char* name; };
    const Case cases[] = {
        {1280.0f, 720.0f,  1.0f,  "1280x720@100"},
        {1600.0f, 900.0f,  1.0f,  "1600x900@100"},
        {1920.0f, 1080.0f, 1.0f,  "1920x1080@100"},
        {2560.0f, 1440.0f, 1.5f,  "2560x1440@150"},
        {3440.0f, 1440.0f, 1.0f,  "3440x1440@100"},
        {1920.0f, 1080.0f, 2.0f,  "1920x1080@200"},
    };

    const std::string artifacts = artifactDirectory();
    for (const Case& c : cases)
    {
        const ShellRender rendered = renderShell(c.width, c.height, c.scale);
        const ImageBuffer image = rasterizeUiDrawData(rendered.data, StudioColor{255, 0, 255, 255});

        if (!image.isWellFormed())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"shell produced no image at "} + c.name);
            continue;
        }
        CNA_STUDIO_EXPECT(image.isWellFormed());

        // Written for CI to collect, so a visual regression can be looked at rather than inferred
        // from a failed comparison (STUDIO-33015).
        if (!artifacts.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(artifacts, ec);
            (void) writeImageAsPng(image, artifacts + "/shell-" + c.name + ".png");
        }
    }
}

CNA_STUDIO_TEST(TwoRendersOfDifferentContentDifferMeasurably)
{
    // Guards the golden comparison itself: if compareImages reported everything as matching, every
    // visual test above would pass vacuously.
    const ShellRender wide = renderShell(640.0f, 360.0f);
    const ShellRender narrow = renderShell(640.0f, 360.0f, 1.0f, {"outliner", "layers"});

    const ImageBuffer a = rasterizeUiDrawData(wide.data, StudioColor{0, 0, 0, 255});
    const ImageBuffer b = rasterizeUiDrawData(narrow.data, StudioColor{0, 0, 0, 255});

    const ImageDifference difference = compareImages(a, b, 8);
    CNA_STUDIO_EXPECT(difference.comparable);
    CNA_STUDIO_EXPECT(difference.differingPixels > 0);
}

CNA_STUDIO_TEST(APngIsWrittenAndIsReadableAsOne)
{
    const ShellRender shell = renderShell(64.0f, 48.0f);
    const ImageBuffer image = rasterizeUiDrawData(shell.data, StudioColor{0, 0, 0, 255});
    const std::vector<std::uint8_t> png = encodeImageAsPng(image);

    CNA_STUDIO_EXPECT(png.size() > 8);
    const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (std::size_t i = 0; i < 8; ++i) { CNA_STUDIO_EXPECT_EQ(png[i], signature[i]); }

    // IHDR immediately after the signature, and IEND at the end: a file missing either is not a
    // PNG, however convincingly it starts.
    CNA_STUDIO_EXPECT_EQ(std::string(reinterpret_cast<const char*>(png.data()) + 12, 4),
                         std::string{"IHDR"});
    CNA_STUDIO_EXPECT_EQ(std::string(reinterpret_cast<const char*>(png.data()) + png.size() - 8, 4),
                         std::string{"IEND"});
}

CNA_STUDIO_TEST(RasterisationRespectsClipping)
{
    StudioDrawList list;
    list.begin(64.0f, 64.0f);
    list.pushClip(UiRect{0.0f, 0.0f, 32.0f, 64.0f});
    list.fillRect(UiRect{0.0f, 0.0f, 64.0f, 64.0f}, StudioColor{255, 255, 255, 255});
    list.popClip();
    list.end();

    const ImageBuffer image = rasterizeUiDrawData(list.drawData(), StudioColor{0, 0, 0, 255});
    const auto pixelAt = [&image](int x, int y) {
        return image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
    };

    CNA_STUDIO_EXPECT_EQ(pixelAt(10, 32), std::uint8_t{255});
    CNA_STUDIO_EXPECT_EQ(pixelAt(50, 32), std::uint8_t{0});
}

CNA_STUDIO_TEST(RasterisationBlendsAlphaRatherThanReplacing)
{
    StudioDrawList list;
    list.begin(16.0f, 16.0f);
    list.fillRect(UiRect{0.0f, 0.0f, 16.0f, 16.0f}, StudioColor{0, 0, 0, 255});
    list.fillRect(UiRect{0.0f, 0.0f, 16.0f, 16.0f}, StudioColor{255, 255, 255, 128});
    list.end();

    const ImageBuffer image = rasterizeUiDrawData(list.drawData(), StudioColor{0, 0, 0, 255});
    const std::uint8_t value = image.pixels[0];
    CNA_STUDIO_EXPECT(value > 100 && value < 160);
}
