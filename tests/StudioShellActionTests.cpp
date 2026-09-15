// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellActionTests.cpp
 * @brief One action object, reached three ways: menu, toolbar and keyboard.
 *
 * `plan.md` STUDIO-06001, STUDIO-06008.
 *
 * The registry exists so those three cannot drift apart, and that promise is only worth anything if
 * something checks it. A menu item that saves while Ctrl+S does nothing, or a toolbar button that
 * stays bright while the menu row greys out, are the two ways an editor loses a user's trust
 * quietly — neither crashes, and both are obvious the moment somebody tries the other route.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace CNA::Studio;

namespace
{
    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 720.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A shell bound to a context holding one renamed entity, so Undo has work to do. */
    struct Fixture
    {
        StudioContext context;
        StudioLog log;
        std::unique_ptr<StudioShell> shell = std::make_unique<StudioShell>(StudioTheme::dark());
        Uuid entity;

        Fixture()
        {
            StudioEntity subject{Uuid::generate(), "Player"};
            entity = subject.getId();
            context.getScene().addEntity(std::move(subject));
            context.select(entity);

            shell->resetLayout();
            CNA_STUDIO_EXPECT(bindStudioShellActions(*shell, context, log) >= 4);
            shell->renderFrame(at(-1.0f, -1.0f));
        }

        [[nodiscard]] std::string name() const
        {
            const StudioEntity* found = context.getScene().findEntity(entity);
            return found != nullptr ? found->getName() : std::string{};
        }

        void rename(const std::string& to)
        {
            context.execute(std::make_unique<RenameEntityCommand>(context.getScene(), entity, to));
        }
    };
}

CNA_STUDIO_TEST(UndoIsDisabledUntilThereIsSomethingToUndo)
{
    // A control that looks available and refuses is indistinguishable from one that is broken.
    // Asking the predicate at the moment the answer is needed is what makes this exact, rather
    // than correct until somebody forgets to refresh a cached boolean.
    Fixture fixture;

    CNA_STUDIO_EXPECT(!fixture.shell->actions().isEnabled("studio.edit.undo"));
    CNA_STUDIO_EXPECT(!fixture.shell->actions().isEnabled("studio.edit.redo"));

    fixture.rename("Hero");
    CNA_STUDIO_EXPECT(fixture.shell->actions().isEnabled("studio.edit.undo"));
    CNA_STUDIO_EXPECT(!fixture.shell->actions().isEnabled("studio.edit.redo"));

    fixture.shell->invoke("studio.edit.undo");
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"Player"});
    CNA_STUDIO_EXPECT(!fixture.shell->actions().isEnabled("studio.edit.undo"));
    CNA_STUDIO_EXPECT(fixture.shell->actions().isEnabled("studio.edit.redo"));

    fixture.shell->invoke("studio.edit.redo");
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"Hero"});
}

CNA_STUDIO_TEST(TheKeyboardAndTheMenuReachTheSameAction)
{
    // Not "both work" -- the same object. A shortcut wired to its own copy of the handler is a
    // shortcut that keeps working after the menu's stops, which is how the two come to disagree.
    Fixture fixture;
    fixture.rename("Hero");

    UiInputState undo = at(600.0f, 400.0f);
    undo.setKeyDown(UiKey::Z, true);
    undo.modifiers.control = true;

    fixture.shell->renderFrame(undo);
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"Player"});

    // And what ran is recorded under the action's id, whichever route invoked it.
    bool sawUndo = false;
    for (const std::string& invoked : fixture.shell->invokedActions())
    {
        if (invoked == "studio.edit.undo") { sawUndo = true; }
    }
    CNA_STUDIO_EXPECT(sawUndo);
}

CNA_STUDIO_TEST(DeletingAnEntityIsUndoable)
{
    // The single operation a user most needs to be able to take back.
    Fixture fixture;
    CNA_STUDIO_EXPECT(fixture.shell->actions().isEnabled("studio.edit.delete"));

    fixture.shell->invoke("studio.edit.delete");
    CNA_STUDIO_EXPECT(fixture.context.getScene().findEntity(fixture.entity) == nullptr);
    CNA_STUDIO_EXPECT(fixture.context.getSelection().empty());

    // With nothing selected, Delete greys out rather than staying bright and doing nothing.
    CNA_STUDIO_EXPECT(!fixture.shell->actions().isEnabled("studio.edit.delete"));

    fixture.shell->invoke("studio.edit.undo");
    CNA_STUDIO_EXPECT(fixture.context.getScene().findEntity(fixture.entity) != nullptr);
}

CNA_STUDIO_TEST(EveryActionTheShellInvokesEitherRunsOrIsRefusedOutLoud)
{
    // The failure this guards against is a menu with rows that quietly do nothing: an id a menu
    // names and the registry does not carry, or one carried with no handler. Both are invisible
    // from the outside and both are exactly what a half-finished migration produces.
    Fixture fixture;

    // Recursive, because a submenu is exactly where a dead row hides: it is one gesture further
    // from anybody who opens the menu to look.
    const auto check = [&](auto&& self, const std::string& where,
                           const std::vector<StudioMenuEntry>& entries) -> void {
        for (const StudioMenuEntry& entry : entries)
        {
            if (entry.isSeparator()) { continue; }
            if (entry.isSubmenu())
            {
                self(self, where + " \u203a " + entry.label, entry.rows);
                continue;
            }
            if (fixture.shell->actions().find(entry.id) == nullptr)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "the " + where + " menu names '" + entry.id
                    + "', which the action registry does not carry. The row would draw and do "
                      "nothing.");
            }
        }
    };

    for (const StudioMenuDefinition& menu : fixture.shell->menus())
    {
        check(check, menu.title, menu.entries);
    }

    // An unbound action refuses rather than pretending. Every menu row is therefore either
    // enabled and working, or greyed out -- never bright and inert.
    fixture.shell->invoke("studio.no.such.action");
    CNA_STUDIO_EXPECT(!fixture.shell->refusedActions().empty());
}

CNA_STUDIO_TEST(EachActionSaysWhatItDidRatherThanLeavingTheUserGuessing)
{
    // A user who pressed Ctrl+Z and saw nothing change needs to know whether nothing happened or
    // nothing was undoable -- and the description is read *before* the undo, because afterwards it
    // names whatever is now on top of the stack.
    Fixture fixture;
    fixture.rename("Hero");

    const std::size_t before = fixture.log.entries().size();
    fixture.shell->invoke("studio.edit.undo");

    CNA_STUDIO_EXPECT(fixture.log.entries().size() > before);
    CNA_STUDIO_EXPECT(fixture.log.toText().find("Undid") != std::string::npos);
}

CNA_STUDIO_TEST(EveryCommandThatIsStillUnimplementedIsNamedRatherThanDiscovered)
{
    // A command with no handler is drawn unavailable, which is right — but it means the *number*
    // of them is invisible from the UI, and a half-migrated menu can quietly stay half-migrated.
    // This is the list, and it has to be edited deliberately: binding one fails this test until
    // its name is removed, and adding an unbound command fails it until somebody writes down why.
    //
    // Each entry says what it is waiting for. None of them is waiting on nothing.
    const std::vector<std::pair<std::string, std::string>> pending = {
        {"studio.file.newProject", "a project template and a file picker (STUDIO-08001); the "
                                   "modal it also needed now exists"},
        {"studio.file.openProject", "a file picker; --project opens one today"},
        {"studio.view.toggleGrid", "a grid option on the viewport, which the renderer does not take"},
    };

    Fixture fixture;
    StudioCamera2D camera;
    StudioShellPanels panels{*fixture.shell, fixture.context, fixture.log};
    panels.setViewportServices(camera, {});

    std::vector<std::string> unimplemented;
    for (const StudioAction& action : fixture.shell->actions().commands())
    {
        // The per-panel show/hide commands are generated, not declared, and are always bound.
        if (action.id.rfind(std::string{kStudioPanelActionPrefix}, 0) == 0) { continue; }
        if (action.id.rfind(std::string{kStudioClosePanelActionPrefix}, 0) == 0) { continue; }
        if (!action.run) { unimplemented.push_back(action.id); }
    }

    for (const std::string& id : unimplemented)
    {
        const bool named = std::any_of(pending.begin(), pending.end(),
                                       [&](const auto& entry) { return entry.first == id; });
        if (!named)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "'" + id + "' has no handler and is not on the pending list. Either bind it, or "
                "add it with the reason it cannot be bound yet.");
        }
    }

    for (const auto& [id, reason] : pending)
    {
        const bool still = std::find(unimplemented.begin(), unimplemented.end(), id)
                        != unimplemented.end();
        if (!still)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "'" + id + "' is bound now, so remove it from the pending list (it was waiting on "
                + reason + ").");
        }
    }

    // And every one of them is drawn unavailable, which is what stops a user discovering the gap
    // by clicking something that appears to work.
    for (const auto& [id, reason] : pending)
    {
        (void)reason;
        CNA_STUDIO_EXPECT(!fixture.shell->actions().isEnabled(id));
    }
}

CNA_STUDIO_TEST(EveryPanelWithoutContentIsNamedRatherThanBeingAnEmptyRectangle)
{
    // A panel with no content is a grey rectangle with a tab on it, and that is indistinguishable
    // from a panel whose content failed to draw. The same discipline as the unimplemented
    // commands: the list is here, with a reason, and it fails in both directions.
    const std::vector<std::pair<std::string, std::string>> pending = {
        {"material", "a material editor over .cnamaterial assets (Phase 19)"},
    };

    Fixture fixture;
    StudioCamera2D camera;
    StudioShellPanels panels{*fixture.shell, fixture.context, fixture.log};
    panels.setViewportServices(camera, {});

    std::vector<std::string> empty;
    for (const StudioPanelDescriptor& descriptor : fixture.shell->registeredPanels())
    {
        if (!fixture.shell->hasPanelContent(descriptor.id)) { empty.push_back(descriptor.id); }
    }

    for (const std::string& id : empty)
    {
        const bool named = std::any_of(pending.begin(), pending.end(),
                                       [&](const auto& entry) { return entry.first == id; });
        if (!named)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the '" + id + "' panel draws nothing and is not on the pending list. Either give "
                "it content, or add it with the reason it has none yet.");
        }
    }

    for (const auto& [id, reason] : pending)
    {
        const bool still = std::find(empty.begin(), empty.end(), id) != empty.end();
        if (!still)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the '" + id + "' panel has content now, so remove it from the pending list (it "
                "was waiting on " + reason + ").");
        }
    }
}
