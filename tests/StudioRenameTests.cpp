// SPDX-License-Identifier: MS-PL
/**
 * @file StudioRenameTests.cpp
 * @brief Renaming an entity where its name is (plan.md STUDIO-07020, STUDIO-07006).
 *
 * The last row of the inventory's shortcut table that had no native answer, and the only one that
 * needed a widget rather than a binding: the native tree view could draw a label and could not edit
 * one. A rename that opened a dialog, or that sent the user to the Details panel, would be a rename
 * the user has to check afterwards -- did I edit the row I meant? -- and that is the whole reason
 * editors rename in place.
 *
 * What is worth testing is the interaction between the field and the row it replaces. A row that is
 * a text field must stop being a selectable, draggable, droppable row for as long as it is one:
 * clicking to place the caret would reselect, and dragging to select a word would pick the entity
 * up and carry it somewhere.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 900.0f;
    constexpr float kHeight = 500.0f;

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

    UiInputState away() { return at(-1.0f, -1.0f); }

    Uuid add(SceneDocument& scene, const std::string& name, const Uuid& parent = {})
    {
        StudioEntity entity{Uuid::generate(), name};
        entity.setParentId(parent);
        const Uuid id = entity.getId();
        scene.addEntity(std::move(entity));
        return id;
    }

    struct Fixture
    {
        StudioContext context;
        StudioLog log;
        StudioFrame frame{StudioTheme::dark()};
        StudioTreeState state;
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioOutlinerResult last;
        Uuid player;
        Uuid weapon;

        Fixture()
        {
            player = add(context.getScene(), "Player");
            weapon = add(context.getScene(), "Weapon", player);
            context.select(player);
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& pass) {
                const StudioOutlinerResult drawn =
                    studioOutlinerPanel(pass, body, context, state);
                if (pass.isInputPass()) { last = drawn; }
            });
        }

        /** @brief Types @p text into whatever has focus, one frame. */
        void type(const std::string& text)
        {
            UiInputState input = away();
            for (const char character : text)
            {
                input.characters.push_back(static_cast<char16_t>(character));
            }
            run(input);
        }

        void press(UiKey key)
        {
            UiInputState input = away();
            input.setKeyDown(key, true);
            run(input);
            run(away());
        }

        [[nodiscard]] std::string nameOf(const Uuid& id) const
        {
            const StudioEntity* entity = context.getScene().findEntity(id);
            return entity != nullptr ? entity->getName() : std::string{};
        }

        /** @brief The row rectangle for @p id, from the same layout the tree uses. */
        [[nodiscard]] UiRect rowFor(const Uuid& id) const
        {
            const std::vector<StudioTreeRow> rows =
                studioOutlinerRows(context.getScene(), context.getSelection(), state);
            const float height = std::max(
                static_cast<float>(frame.theme().metric(StudioMetric::RowHeight)),
                static_cast<float>(frame.theme().metric(StudioMetric::MinimumHitTarget)));

            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                if (rows[index].id != id.toString()) { continue; }
                return UiRect{body.x, body.y + static_cast<float>(index) * height,
                              body.width, height};
            }
            return UiRect{};
        }
    };
}

CNA_STUDIO_TEST(TypingReplacesTheNameRatherThanAppendingToIt)
{
    // The field opens with everything selected, which is what a rename is for: F2 then a new name,
    // without first having to delete the old one a character at a time.
    Fixture fixture;
    fixture.run(away());

    CNA_STUDIO_EXPECT(studioBeginOutlinerRename(fixture.context.getScene(), fixture.player,
                                                fixture.state));
    fixture.run(away());
    fixture.type("Hero");
    fixture.press(UiKey::Enter);

    CNA_STUDIO_EXPECT_EQ(fixture.nameOf(fixture.player), std::string{"Hero"});
}

CNA_STUDIO_TEST(ARenameGoesThroughTheHistorySoItCanBeUndone)
{
    // Every other edit in the editor is undoable. A rename that was not would be the one change
    // Ctrl+Z does not reach, which is worse than one that cannot be made at all.
    Fixture fixture;
    fixture.run(away());
    (void)studioBeginOutlinerRename(fixture.context.getScene(), fixture.player, fixture.state);
    fixture.run(away());
    fixture.type("Hero");
    fixture.press(UiKey::Enter);

    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(fixture.nameOf(fixture.player), std::string{"Player"});
}

CNA_STUDIO_TEST(EscapeAbandonsTheRenameAndLeavesTheNameAlone)
{
    Fixture fixture;
    fixture.run(away());
    (void)studioBeginOutlinerRename(fixture.context.getScene(), fixture.player, fixture.state);
    fixture.run(away());
    fixture.type("Hero");
    fixture.press(UiKey::Escape);

    CNA_STUDIO_EXPECT_EQ(fixture.nameOf(fixture.player), std::string{"Player"});
    CNA_STUDIO_EXPECT(fixture.state.renaming().empty());
    CNA_STUDIO_EXPECT(!fixture.context.getHistory().canUndo());
}

CNA_STUDIO_TEST(ARenameToTheSameNameIsNotAChange)
{
    // Otherwise F2 followed by Enter -- which is what "no, I did not want to rename it" looks like
    // from the keyboard -- puts an entry in the undo stack that undoes nothing visible.
    Fixture fixture;
    fixture.run(away());
    (void)studioBeginOutlinerRename(fixture.context.getScene(), fixture.player, fixture.state);
    fixture.run(away());
    fixture.press(UiKey::Enter);

    CNA_STUDIO_EXPECT_EQ(fixture.nameOf(fixture.player), std::string{"Player"});
    CNA_STUDIO_EXPECT(!fixture.context.getHistory().canUndo());
}

CNA_STUDIO_TEST(AnEmptyNameIsRefusedRatherThanApplied)
{
    // An entity with no name is a row the outliner draws as a blank line, which the user then has
    // to find by counting.
    Fixture fixture;
    fixture.run(away());
    (void)studioBeginOutlinerRename(fixture.context.getScene(), fixture.player, fixture.state);
    fixture.run(away());
    fixture.type("X");
    fixture.press(UiKey::Backspace);
    fixture.press(UiKey::Enter);

    CNA_STUDIO_EXPECT_EQ(fixture.nameOf(fixture.player), std::string{"Player"});
    CNA_STUDIO_EXPECT(fixture.state.renaming().empty());
}

CNA_STUDIO_TEST(ClickingInsideTheFieldPlacesTheCaretRatherThanReselecting)
{
    // The failure this prevents: a row that is a text field but is still a selectable row. A click
    // to put the caret in the middle of a name would reselect the row -- and a drag to select a
    // word would pick the entity up and start carrying it.
    Fixture fixture;
    fixture.context.select(fixture.weapon);
    fixture.run(away());

    (void)studioBeginOutlinerRename(fixture.context.getScene(), fixture.player, fixture.state);
    fixture.run(away());

    const UiRect row = fixture.rowFor(fixture.player);
    CNA_STUDIO_EXPECT(!row.isEmpty());

    fixture.run(at(row.centerX(), row.centerY()));
    fixture.run(at(row.centerX(), row.centerY(), /*leftDown=*/true));
    fixture.run(at(row.centerX(), row.centerY()));

    // Still editing, and the selection has not moved to the row that is being renamed.
    CNA_STUDIO_EXPECT(fixture.state.renaming() == fixture.player.toString());
    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection() == fixture.weapon);
}

CNA_STUDIO_TEST(ClickingAnotherRowCommitsTheRenameTheWayEveryOtherFieldDoes)
{
    // A user who typed a name and clicked away meant the name. Abandoning it instead would be this
    // one field behaving differently from every other field in Studio.
    Fixture fixture;
    fixture.run(away());
    (void)studioBeginOutlinerRename(fixture.context.getScene(), fixture.player, fixture.state);
    fixture.run(away());
    fixture.type("Hero");

    const UiRect other = fixture.rowFor(fixture.weapon);
    CNA_STUDIO_EXPECT(!other.isEmpty());
    fixture.run(at(other.centerX(), other.centerY()));
    fixture.run(at(other.centerX(), other.centerY(), /*leftDown=*/true));
    fixture.run(at(other.centerX(), other.centerY()));

    CNA_STUDIO_EXPECT_EQ(fixture.nameOf(fixture.player), std::string{"Hero"});
    CNA_STUDIO_EXPECT(fixture.state.renaming().empty());
}

CNA_STUDIO_TEST(RenamingAnEntityThatIsNotThereDoesNothing)
{
    Fixture fixture;
    CNA_STUDIO_EXPECT(!studioBeginOutlinerRename(fixture.context.getScene(), Uuid::generate(),
                                                 fixture.state));
    CNA_STUDIO_EXPECT(fixture.state.renaming().empty());
}

CNA_STUDIO_TEST(F2RenamesTheSelectionAndRaisesTheOutlinerToDoIt)
{
    // Pressing F2 with the outliner behind another tab would otherwise start an edit on a field
    // nobody can see, and swallow the typing that followed.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    (void)bindStudioShellActions(shell, context, log);
    StudioCamera2D camera;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, {});

    const Uuid player = add(context.getScene(), "Player");
    context.select(player);

    const StudioAction* rename = shell.actions().find("studio.edit.rename");
    CNA_STUDIO_EXPECT(rename != nullptr);
    if (rename == nullptr) { return; }

    CNA_STUDIO_EXPECT(rename->isEnabled && rename->isEnabled());
    const StudioShortcut f2{UiKey::F2, UiKeyModifiers{}};
    CNA_STUDIO_EXPECT(rename->shortcut == f2);

    CNA_STUDIO_EXPECT(shell.closePanel("outliner"));
    shell.invoke("studio.edit.rename");

    CNA_STUDIO_EXPECT(shell.isPanelOpen("outliner"));
    CNA_STUDIO_EXPECT(shell.isPanelActive("outliner"));
}

CNA_STUDIO_TEST(RenameIsGreyedOutWithNothingSelected)
{
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    (void)bindStudioShellActions(shell, context, log);
    StudioCamera2D camera;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, {});

    const StudioAction* rename = shell.actions().find("studio.edit.rename");
    CNA_STUDIO_EXPECT(rename != nullptr);
    if (rename != nullptr) { CNA_STUDIO_EXPECT(rename->isEnabled && !rename->isEnabled()); }
}

CNA_STUDIO_TEST(BuildMovedOffF2RatherThanSharingItWithRename)
{
    // F2 is Rename in the prototype and in every file manager, so it is Rename here. Build had it
    // and moved, because two commands on one chord means one of them has quietly stopped working
    // and the user who finds out is the one who pressed it expecting the other.
    StudioShell shell{StudioTheme::dark()};

    const StudioAction* rename = shell.actions().find("studio.edit.rename");
    const StudioAction* build = shell.actions().find("studio.build.build");
    CNA_STUDIO_EXPECT(rename != nullptr && build != nullptr);
    if (rename == nullptr || build == nullptr) { return; }

    CNA_STUDIO_EXPECT(rename->shortcut.key == UiKey::F2);
    CNA_STUDIO_EXPECT(!(build->shortcut == rename->shortcut));
    CNA_STUDIO_EXPECT(build->shortcut.isBound());

    // And no two commands anywhere share a chord, which is what stopped this being noticed.
    for (const StudioAction& one : shell.actions().commands())
    {
        if (!one.shortcut.isBound()) { continue; }
        for (const StudioAction& other : shell.actions().commands())
        {
            if (other.id == one.id || !other.shortcut.isBound()) { continue; }
            if (one.shortcut == other.shortcut)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "'" + one.id + "' and '" + other.id + "' are both bound to "
                    + describeStudioShortcut(one.shortcut)
                    + ", so one of them has quietly stopped working.");
            }
        }
    }
}
