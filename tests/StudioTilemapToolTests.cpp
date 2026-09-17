// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTilemapToolTests.cpp
 * @brief Painting tiles in the native viewport (plan.md STUDIO-07003, STUDIO-07009).
 *
 * The migration inventory listed the tilemap tool, the tile index and the tools themselves as the
 * prototype's only toolbar controls with no native answer. The model was never the gap: the grid,
 * `PaintTilesCommand` and its stroke merging have been shared and tested since the prototype had
 * them. What was missing was a viewport that armed a tool and turned a press into a cell.
 *
 * So the cases here are about the difference between a tool and a mode. A press under a tool means
 * something other than "select", which is the whole reason a tool exists — and getting that wrong
 * is not a cosmetic failure: it moves the inspector out from under the user on every stroke, or
 * paints into whatever they last clicked.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/Tilemap.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 600.0f;
    constexpr float kHeight = 400.0f;

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

    /** @brief A scene with one 4x3 tilemap at the origin, selected, and a camera looking at it. */
    struct Fixture
    {
        StudioContext context;
        StudioCamera2D camera;
        StudioCamera3D camera3D;
        StudioFrame frame{StudioTheme::dark()};
        StudioViewportState state;
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioViewportResult last;
        Uuid tilemap;

        // A click spans frames, and the result reports the frame an edit landed in rather than the
        // click: a paint lands on the press and a fill on the release. So the flags are collected
        // across every input pass, which is also the stronger way to ask that something never
        // happened -- `last` alone would only be asking about the release.
        bool paintedAny = false;
        bool toolChangedAny = false;
        bool selectionChangedAny = false;

        Fixture()
        {
            StudioEntity entity{Uuid::generate(), "Ground"};

            StudioComponent transform{"CNA.Transform"};
            transform.setProperty("position", PropertyValue{StudioVector3{0.0f, 0.0f, 0.0f}});
            entity.getComponents().push_back(std::move(transform));

            StudioComponent map{BuiltinComponentIds::kTilemap};
            map.setProperty(TilemapKeys::kColumns, PropertyValue{std::int64_t{4}});
            map.setProperty(TilemapKeys::kRows, PropertyValue{std::int64_t{3}});
            map.setProperty(TilemapKeys::kTileWidth, PropertyValue{std::int64_t{32}});
            map.setProperty(TilemapKeys::kTileHeight, PropertyValue{std::int64_t{32}});
            entity.getComponents().push_back(std::move(map));

            tilemap = entity.getId();
            context.getScene().addEntity(std::move(entity));
            context.select(tilemap);

            camera.setViewportSize(StudioVector2{kWidth, kHeight});
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& pass) {
                const StudioViewportResult drawn =
                    studioViewportPanel(pass, body, context, camera, state, {});
                studioViewportToolOverlay(pass, body, state);
                if (pass.isInputPass())
                {
                    last = drawn;
                    paintedAny = paintedAny || drawn.tilesPainted;
                    toolChangedAny = toolChangedAny || drawn.toolChanged;
                    selectionChangedAny = selectionChangedAny || drawn.selectionChanged;
                }
            });
        }

        /** @brief Where tile (x, y) lands on screen, from the camera the panel uses. */
        [[nodiscard]] StudioVector2 screenOf(int x, int y)
        {
            const StudioVector2 world{static_cast<float>(x) * 32.0f + 16.0f,
                                      static_cast<float>(y) * 32.0f + 16.0f};
            const StudioVector2 local = camera.worldToScreen(world);
            return StudioVector2{body.x + local.x, body.y + local.y};
        }

        void pressAt(int x, int y)
        {
            const StudioVector2 point = screenOf(x, y);
            run(at(point.x, point.y));
            run(at(point.x, point.y, /*leftDown=*/true));
        }

        void releaseAt(int x, int y)
        {
            const StudioVector2 point = screenOf(x, y);
            run(at(point.x, point.y));
        }

        void clickAtTile(int x, int y)
        {
            pressAt(x, y);
            releaseAt(x, y);
        }

        [[nodiscard]] TilemapGrid grid()
        {
            const StudioEntity* entity = context.getScene().findEntity(tilemap);
            const StudioComponent* map = entity->findComponent(BuiltinComponentIds::kTilemap);
            return readTilemapGrid(*map, context.getComponentRegistry().find(
                                             BuiltinComponentIds::kTilemap));
        }

        [[nodiscard]] std::size_t undoDepth()
        {
            std::size_t depth = 0;
            while (context.getHistory().canUndo())
            {
                context.getHistory().undo();
                ++depth;
            }
            return depth;
        }
    };
}

CNA_STUDIO_TEST(PaintingWritesTheBrushIntoTheCellUnderTheCursor)
{
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::PaintTiles;
    fixture.state.paintTile = 7;
    fixture.run(away());

    fixture.clickAtTile(1, 1);

    CNA_STUDIO_EXPECT_EQ(fixture.grid().at(1, 1), std::int64_t{7});
    CNA_STUDIO_EXPECT(fixture.paintedAny);
}

CNA_STUDIO_TEST(APressUnderATileToolDoesNotAlsoChangeTheSelection)
{
    // The failure this prevents is the one that makes the tool unusable rather than merely wrong:
    // the tilemap being painted into has to *stay* selected for the next cell to land, and a press
    // that also selected would move the inspector out from under the user on every stroke.
    Fixture fixture;

    StudioEntity other{Uuid::generate(), "Elsewhere"};
    StudioComponent transform{"CNA.Transform"};
    transform.setProperty("position", PropertyValue{StudioVector3{16.0f, 16.0f, 0.0f}});
    other.getComponents().push_back(std::move(transform));
    const Uuid otherId = other.getId();
    fixture.context.getScene().addEntity(std::move(other));

    fixture.state.tool = StudioViewportTool::PaintTiles;
    fixture.run(away());
    fixture.clickAtTile(0, 0);

    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection() == fixture.tilemap);
    CNA_STUDIO_EXPECT(!(fixture.context.getPrimarySelection() == otherId));
    CNA_STUDIO_EXPECT(!fixture.selectionChangedAny);
}

CNA_STUDIO_TEST(ADragAcrossSeveralCellsIsOneUndoEntry)
{
    // Forty tiles and forty Ctrl+Zs is a tool nobody uses twice. The first cell of a stroke opens
    // an entry and every later one merges into it.
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::PaintTiles;
    fixture.state.paintTile = 3;
    fixture.run(away());

    fixture.pressAt(0, 0);
    for (int x = 1; x < 4; ++x)
    {
        const StudioVector2 point = fixture.screenOf(x, 0);
        fixture.run(at(point.x, point.y, /*leftDown=*/true));
    }
    fixture.releaseAt(3, 0);

    const TilemapGrid painted = fixture.grid();
    for (int x = 0; x < 4; ++x)
    {
        CNA_STUDIO_EXPECT_EQ(painted.at(x, 0), std::int64_t{3});
    }
    CNA_STUDIO_EXPECT_EQ(fixture.undoDepth(), std::size_t{1});
}

CNA_STUDIO_TEST(TwoStrokesAreTwoUndoEntries)
{
    // Or undoing would jump back past a stroke the user had finished and accepted.
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::PaintTiles;
    fixture.state.paintTile = 1;
    fixture.run(away());

    fixture.clickAtTile(0, 0);
    fixture.clickAtTile(2, 2);

    CNA_STUDIO_EXPECT_EQ(fixture.undoDepth(), std::size_t{2});
}

CNA_STUDIO_TEST(TheEraserClearsTheCellRatherThanWritingZero)
{
    // Tile 0 is the first tile in every sheet anyone draws, so "empty" cannot be zero -- and an
    // eraser that wrote it would paint with the first tile instead.
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::PaintTiles;
    fixture.state.paintTile = 5;
    fixture.run(away());
    fixture.clickAtTile(1, 1);
    CNA_STUDIO_EXPECT_EQ(fixture.grid().at(1, 1), std::int64_t{5});

    fixture.state.tool = StudioViewportTool::EraseTiles;
    fixture.clickAtTile(1, 1);
    CNA_STUDIO_EXPECT_EQ(fixture.grid().at(1, 1), kEmptyTile);
}

CNA_STUDIO_TEST(TheEyedropperTakesTheTileAndThenGoesBackToPainting)
{
    // An eyedropper that left the user still holding the eyedropper is one they have to put down
    // before they can use what it took.
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::PaintTiles;
    fixture.state.paintTile = 9;
    fixture.run(away());
    fixture.clickAtTile(2, 1);

    fixture.state.paintTile = 0;
    fixture.state.tool = StudioViewportTool::PickTile;
    fixture.clickAtTile(2, 1);

    CNA_STUDIO_EXPECT_EQ(fixture.state.paintTile, std::int64_t{9});
    CNA_STUDIO_EXPECT(fixture.state.tool == StudioViewportTool::PaintTiles);
    CNA_STUDIO_EXPECT(fixture.toolChangedAny);
}

CNA_STUDIO_TEST(TheEyedropperOverAnEmptyCellTakesNothing)
{
    // An empty cell is not a brush. Taking one would leave the user painting nothing and wondering
    // why the tool had stopped working.
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::PickTile;
    fixture.state.paintTile = 4;
    fixture.run(away());

    fixture.clickAtTile(0, 0);

    CNA_STUDIO_EXPECT_EQ(fixture.state.paintTile, std::int64_t{4});
    CNA_STUDIO_EXPECT(fixture.state.tool == StudioViewportTool::PickTile);
}

CNA_STUDIO_TEST(AFillCoversTheDraggedRectangleOnReleaseAsOneEntry)
{
    // On the release, so a drag can be adjusted before it commits -- and as one entry, because a
    // fill is one intention however many cells it covers.
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::FillTiles;
    fixture.state.paintTile = 2;
    fixture.run(away());

    fixture.pressAt(0, 0);
    CNA_STUDIO_EXPECT_EQ(fixture.grid().at(0, 0), kEmptyTile);   // nothing yet

    const StudioVector2 corner = fixture.screenOf(2, 1);
    fixture.run(at(corner.x, corner.y, /*leftDown=*/true));
    fixture.run(at(corner.x, corner.y));

    const TilemapGrid filled = fixture.grid();
    for (int y = 0; y <= 1; ++y)
    {
        for (int x = 0; x <= 2; ++x) { CNA_STUDIO_EXPECT_EQ(filled.at(x, y), std::int64_t{2}); }
    }
    CNA_STUDIO_EXPECT_EQ(filled.at(3, 0), kEmptyTile);
    CNA_STUDIO_EXPECT_EQ(fixture.undoDepth(), std::size_t{1});
}

CNA_STUDIO_TEST(AFillDraggedBackwardsStillFillsTheRectangle)
{
    // Up and to the left is an ordinary way to drag, and a fill that only worked one way would be
    // a tool that works for half its users.
    Fixture fixture;
    fixture.state.tool = StudioViewportTool::FillTiles;
    fixture.state.paintTile = 6;
    fixture.run(away());

    fixture.pressAt(2, 2);
    const StudioVector2 corner = fixture.screenOf(1, 1);
    fixture.run(at(corner.x, corner.y, /*leftDown=*/true));
    fixture.run(at(corner.x, corner.y));

    const TilemapGrid filled = fixture.grid();
    for (int y = 1; y <= 2; ++y)
    {
        for (int x = 1; x <= 2; ++x) { CNA_STUDIO_EXPECT_EQ(filled.at(x, y), std::int64_t{6}); }
    }
    CNA_STUDIO_EXPECT_EQ(filled.at(0, 0), kEmptyTile);
}

CNA_STUDIO_TEST(PaintingWithNoTilemapSelectedSaysSoOncePerPress)
{
    // A brush over a sprite is a near miss, and sixty lines a second about it is how a console
    // stops being read.
    StudioContext context;
    StudioLog log;
    context.setLogSink([&log](LogSeverity severity, const std::string& message) {
        log.append(severity, message);
    });

    StudioEntity sprite{Uuid::generate(), "Sprite"};
    StudioComponent transform{"CNA.Transform"};
    transform.setProperty("position", PropertyValue{StudioVector3{0.0f, 0.0f, 0.0f}});
    sprite.getComponents().push_back(std::move(transform));
    const Uuid spriteId = sprite.getId();
    context.getScene().addEntity(std::move(sprite));
    context.select(spriteId);

    StudioCamera2D camera;

    camera.setViewportSize(StudioVector2{kWidth, kHeight});
    StudioFrame frame{StudioTheme::dark()};
    StudioViewportState state;
    state.tool = StudioViewportTool::PaintTiles;

    const auto run = [&](const UiInputState& input) {
        runStudioFrame(frame, input, [&](StudioFrame& pass) {
            (void)studioViewportPanel(pass, UiRect{0.0f, 0.0f, kWidth, kHeight}, context, camera,
                                      state, {});
        });
    };

    run(away());
    for (int i = 0; i < 20; ++i) { run(at(300.0f, 200.0f, /*leftDown=*/i > 0)); }

    std::size_t said = 0;
    for (const StudioLogEntry& entry : log.entries())
    {
        if (entry.message.find("Tilemap component") != std::string::npos) { said += entry.repeats; }
    }
    CNA_STUDIO_EXPECT_EQ(said, std::size_t{1});
}

CNA_STUDIO_TEST(TheOverlaySaysWhichToolIsArmedAndOffersABrushWhereOneApplies)
{
    // The only thing on the screen that says a press means something other than "select". A tool
    // that armed silently would be a viewport that behaves differently from yesterday with nothing
    // explaining why.
    Fixture fixture;
    fixture.run(away());
    const std::size_t idle = fixture.frame.interactionCount();

    fixture.state.tool = StudioViewportTool::PaintTiles;
    fixture.run(away());
    const std::size_t painting = fixture.frame.interactionCount();
    CNA_STUDIO_EXPECT(painting > idle);

    // The eraser has no index and the eyedropper sets one rather than reading it, so neither gets
    // a field: a control that does nothing is one the user stops believing.
    fixture.state.tool = StudioViewportTool::EraseTiles;
    fixture.run(away());
    CNA_STUDIO_EXPECT(fixture.frame.interactionCount() < painting);

    CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT(validate(fixture.frame.drawData()).valid);
}

CNA_STUDIO_TEST(TheToolCommandsAreExclusiveAndSayWhichIsArmed)
{
    // A press means one thing, so arming two tools would be arming neither -- and a toolbar that
    // could not say which is on is one a user tests by editing their level.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioCamera2D camera;
    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});

    const std::vector<std::string> ids = {"studio.view.tool.select", "studio.view.tool.paint",
                                          "studio.view.tool.erase", "studio.view.tool.pick",
                                          "studio.view.tool.fill"};

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
}

CNA_STUDIO_TEST(AToolCommandWithNoViewportBehindItIsRefusedRatherThanIgnored)
{
    // What this prevents cost a session to find. Every tool command is *declared* in the registry
    // with no handler, and gains one only when `setViewportServices` binds the viewport -- which
    // needs a graphics device. Invoke one before that and the id is found, the absent handler
    // "runs", and the screen is exactly as it was: a feature that looks broken rather than one
    // that plainly said it was not ready. The shell records the refusal; anything driving it from
    // the outside, `--shell-invoke` above all, has to read that rather than the return of `find`.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioShellPanels panels{shell, context, log};

    CNA_STUDIO_EXPECT(shell.actions().find("studio.view.tool.paint") != nullptr);
    shell.invoke("studio.view.tool.paint");
    CNA_STUDIO_EXPECT_EQ(shell.invokedActions().size(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(shell.refusedActions().size(), std::size_t{1});

    // And bound, it runs and is recorded as having run.
    StudioCamera2D camera;
    StudioCamera3D camera3D;
    panels.setViewportServices(camera, camera3D, {});
    shell.invoke("studio.view.tool.paint");
    CNA_STUDIO_EXPECT_EQ(shell.invokedActions().size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(shell.refusedActions().size(), std::size_t{1});
}
