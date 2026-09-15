// SPDX-License-Identifier: MS-PL
/**
 * @file StudioSceneSettingsTests.cpp
 * @brief The Details panel with nothing selected (plan.md STUDIO-07023).
 *
 * Found by looking rather than by a list. `docs/MIGRATION-INVENTORY.md` accounts for panels, and
 * the Inspector is ported — what it structurally could not see is that the prototype's Inspector
 * shows something else entirely when *nothing* is selected: the scene's environment, an editable
 * grid snap, and the project's layer names. The native Details panel showed a sentence asking the
 * user to select something, and three settings had no native home at all.
 *
 * They are settings that belong to no entity, so the cases worth having are the ones that decide
 * whether they are settings at all: does a change reach the document, does it go through the
 * history, and is the one layer every entity defaults to protected from being removed.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <cmath>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 460.0f;
    constexpr float kHeight = 420.0f;

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

    /** @brief A project on disk, so `hasProject` is true and the settings have something to edit. */
    class Scratch
    {
    public:
        explicit Scratch(const std::string& name)
        {
            root_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-scene-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(root_, code);
            std::filesystem::create_directories(root_, code);

            std::ofstream project{root_ / "Game.cnaproject", std::ios::binary};
            project << R"({"formatVersion":1,"name":"Scened","kind":"CnaNative",)"
                       R"("assetDirectory":"Assets"})";
        }

        ~Scratch()
        {
            std::error_code code;
            std::filesystem::remove_all(root_, code);
        }

        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;

        [[nodiscard]] std::string file() const
        {
            return (root_ / "Game.cnaproject").generic_string();
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path root_;
    };

    struct Fixture
    {
        Scratch scratch;
        StudioContext context;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioDetailsResult last;

        explicit Fixture(const std::string& name) : scratch(name)
        {
            (void)context.openProject(scratch.file());
            context.clearSelection();
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& pass) {
                const StudioDetailsResult drawn = studioDetailsPanel(pass, body, context);
                if (pass.isInputPass()) { last = drawn; }
            });
        }

        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        void type(const std::vector<char16_t>& characters)
        {
            UiInputState input = away();
            input.characters = characters;
            run(input);
        }

        void press(UiKey key)
        {
            UiInputState input = away();
            input.setKeyDown(key, true);
            run(input);
            run(away());
        }

        /** @brief The nth row's rectangle, from the same arithmetic the panel lays out with. */
        [[nodiscard]] UiRect row(int index) const
        {
            const float padding = static_cast<float>(frame.theme().metric(StudioMetric::SpacingSmall));
            const float height = std::max(
                static_cast<float>(frame.theme().metric(StudioMetric::ControlHeight)),
                static_cast<float>(frame.theme().metric(StudioMetric::MinimumHitTarget)));
            const float spacing = static_cast<float>(frame.theme().metric(StudioMetric::SpacingXSmall));

            return UiRect{body.x + padding,
                          body.y + padding + static_cast<float>(index) * (height + spacing),
                          body.width - padding * 2.0f, height};
        }

        [[nodiscard]] SceneEnvironment environment() const
        {
            return context.getScene().getEnvironment();
        }

        [[nodiscard]] std::vector<std::string> layers() const
        {
            return context.getProject().getLayers();
        }
    };
}

CNA_STUDIO_TEST(WithNothingSelectedTheDetailsPanelOffersTheScenesOwnSettings)
{
    // Rather than a sentence. A setting that belongs to no entity has to live somewhere, and the
    // inspector standing idle is where the prototype put it.
    Fixture fixture{"idle"};
    fixture.run(away());

    CNA_STUDIO_EXPECT(fixture.last.rowsDrawn > 5);
    CNA_STUDIO_EXPECT(fixture.frame.interactionCount() > 0);
    CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT(validate(fixture.frame.drawData()).valid);
}

CNA_STUDIO_TEST(WithNoProjectThereIsNothingToSetAndThePanelSaysSo)
{
    // The other empty. "No project is open" and "select an entity" call for different next actions,
    // and a panel offering a grid snap for a project that is not there would be offering to edit
    // nothing.
    StudioContext context;
    StudioFrame frame{StudioTheme::dark()};
    StudioDetailsResult result;

    runStudioFrame(frame, away(), [&](StudioFrame& pass) {
        result = studioDetailsPanel(pass, UiRect{0.0f, 0.0f, kWidth, kHeight}, context);
    });

    CNA_STUDIO_EXPECT_EQ(result.rowsDrawn, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(frame.interactionCount(), std::size_t{0});
}

CNA_STUDIO_TEST(TheGridSnapIsEditableAndGoesThroughTheHistory)
{
    // It was readable-only natively: the viewport read `project.getGridSnap()` and nothing could
    // change it, so a user who wanted to snap to half a unit had to edit the project file.
    Fixture fixture{"gridsnap"};
    fixture.run(away());

    const UiRect field = fixture.row(1);
    fixture.click(field.right() - 40.0f, field.centerY());
    fixture.type({u'0', u'.', u'5'});
    fixture.press(UiKey::Enter);

    CNA_STUDIO_EXPECT(std::abs(fixture.context.getProject().getGridSnap() - 0.5f) < 0.001f);
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());

    // Once, not twice. Every numeric field in Studio is a caller that normalises what it stores --
    // "00.5" typed, "0.5" written back -- and an edit that landed twice would leave one undo that
    // does nothing in front of the one that does.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT(std::abs(fixture.context.getProject().getGridSnap()) < 0.001f);
    CNA_STUDIO_EXPECT(!fixture.context.getHistory().canUndo());


}

CNA_STUDIO_TEST(TurningFogOnGoesThroughTheHistoryAndRevealsItsSettings)
{
    // The fog's colour and range only appear where they do something, on the same rule as the
    // prototype's grid-plane menu item: a control that changes nothing visible is a bug report
    // waiting to be filed.
    Fixture fixture{"fog"};
    fixture.run(away());
    const std::size_t before = fixture.last.rowsDrawn;

    CNA_STUDIO_EXPECT(!fixture.environment().fogEnabled);

    // Row 5 is Fog: project, grid snap, a gap, the heading, ambient, fog.
    const UiRect checkbox = fixture.row(5);
    fixture.click(checkbox.x + checkbox.width * 0.45f, checkbox.centerY());

    CNA_STUDIO_EXPECT(fixture.environment().fogEnabled);
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());

    fixture.run(away());
    CNA_STUDIO_EXPECT(fixture.last.rowsDrawn > before);

    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT(!fixture.environment().fogEnabled);
}

CNA_STUDIO_TEST(AmbientLightIsEditedAsFourChannelsAndUndoesAsOne)
{
    Fixture fixture{"ambient"};
    fixture.run(away());

    const StudioColor before = fixture.environment().ambientColor;

    // The red channel: the first field after the swatch, in the control half of the ambient row.
    const UiRect ambient = fixture.row(4);
    const float controlLeft = ambient.x + std::round(ambient.width * 0.38f) + 8.0f;
    fixture.click(controlLeft + 60.0f, ambient.centerY());
    fixture.type({u'2', u'0', u'0'});
    fixture.press(UiKey::Enter);

    const StudioColor after = fixture.environment().ambientColor;
    CNA_STUDIO_EXPECT(!(after == before));
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());

    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT(fixture.environment().ambientColor == before);
}

CNA_STUDIO_TEST(ALayerCanBeAddedAndTheChangeIsUndoable)
{
    // The project's layer *names*, which is a different question from the Layers panel's "what is
    // on each" -- one is the list and the other is its contents, and a user who wants a new layer
    // has nowhere else to go.
    Fixture fixture{"addlayer"};
    fixture.run(away());

    const std::size_t before = fixture.layers().size();
    CNA_STUDIO_EXPECT(before >= 1);

    // Project, grid snap, a gap, the environment heading, ambient, fog, a gap, the layers
    // heading, then one row per layer -- so Add Layer is the row after the last of them.
    const UiRect add = fixture.row(static_cast<int>(8 + before));
    fixture.click(add.x + 30.0f, add.centerY());

    CNA_STUDIO_EXPECT_EQ(fixture.layers().size(), before + 1);
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());

    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(fixture.layers().size(), before);
}

CNA_STUDIO_TEST(TheDefaultLayerCannotBeRemoved)
{
    // Every entity that has not chosen one is on it, so removing it would leave the scene pointing
    // at a layer the project no longer declares.
    Fixture fixture{"removelayer"};
    fixture.run(away());

    const std::size_t before = fixture.layers().size();
    const UiRect first = fixture.row(8);
    fixture.click(first.right() - 8.0f, first.centerY());

    CNA_STUDIO_EXPECT_EQ(fixture.layers().size(), before);
    CNA_STUDIO_EXPECT(!fixture.context.getHistory().canUndo());
}

CNA_STUDIO_TEST(TheSceneSettingsSurviveAPanelTooSmallToShowThem)
{
    // The Details panel is the one most likely to be dragged narrow, because it sits in a side
    // group -- and a row laid out at a negative width is where a division by zero would be.
    for (const UiRect& body : {UiRect{0.0f, 0.0f, 0.0f, 0.0f},
                               UiRect{0.0f, 0.0f, 24.0f, 12.0f},
                               UiRect{0.0f, 0.0f, 60.0f, 400.0f}})
    {
        Fixture fixture{"tiny"};
        fixture.body = body;
        fixture.run(away());

        CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
        CNA_STUDIO_EXPECT(validate(fixture.frame.drawData()).valid);
    }
}
