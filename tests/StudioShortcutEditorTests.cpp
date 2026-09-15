// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShortcutEditorTests.cpp
 * @brief Rebinding a command by pressing the key (plan.md STUDIO-06012).
 *
 * Two things here are easy to write and wrong. The first is taking the *chord* rather than the
 * key: a row that binds on the frame Ctrl goes down binds the command to a chord nobody can press
 * deliberately, and it does it before the user has finished pressing what they meant. The second
 * is the refusal, which has to happen *before* the binding is accepted and has to say which
 * command holds the chord — a rebinding that silently does nothing is indistinguishable from a
 * Studio that ignored the keyboard.
 *
 * The rest is about the two keys a user needs to get out of a row they have armed. Binding a
 * command to Escape or to Tab takes away the cancel and the focus move respectively, and does it
 * from inside the only screen where they could be put back.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShortcutEditor.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 900.0f;
    constexpr float kHeight = 600.0f;

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

    /** @brief Off-screen, so nothing is hovered and no button can be activated by accident. */
    UiInputState away() { return at(-1.0f, -1.0f); }

    StudioShortcut chord(UiKey key, bool control = false, bool shift = false, bool alt = false)
    {
        StudioShortcut shortcut;
        shortcut.key = key;
        shortcut.modifiers.control = control;
        shortcut.modifiers.shift = shift;
        shortcut.modifiers.alt = alt;
        return shortcut;
    }

    /** @brief A command that records nothing: the editor never runs one. */
    StudioAction command(std::string id, std::string label, std::string description,
                         StudioShortcut shortcut = {})
    {
        StudioAction action;
        action.id = std::move(id);
        action.label = std::move(label);
        action.description = std::move(description);
        action.shortcut = shortcut;
        action.run = [] {};
        return action;
    }

    struct Fixture
    {
        StudioActionRegistry registry;
        StudioPreferences preferences;
        StudioShortcutEditorState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioShortcutEditorResult last;

        Fixture()
        {
            registry.add(command("test.save", "Save", "Save the active document.",
                                 chord(UiKey::S, /*control=*/true)));
            registry.add(command("test.open", "Open", "Open an existing project."));
            registry.add(command("test.grid", "Show Grid", "Show or hide the viewport grid."));
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& pass) {
                const StudioShortcutEditorResult drawn =
                    studioShortcutEditor(pass, body, registry, preferences, state);
                if (pass.isInputPass()) { last = drawn; }
            });
        }

        /** @brief Presses and releases @p key with @p modifiers held throughout. */
        void press(UiKey key, bool control = false, bool shift = false)
        {
            UiInputState input = away();
            input.modifiers.control = control;
            input.modifiers.shift = shift;
            input.setKeyDown(key, true);
            run(input);
            run(away());
        }

        /** @brief Arms the row for @p id the way a user does: by pressing its Change button. */
        void arm(const std::string& id)
        {
            const UiRect button = buttonFor(id);
            run(at(button.centerX(), button.centerY()));
            run(at(button.centerX(), button.centerY(), /*leftDown=*/true));
            run(at(button.centerX(), button.centerY()));
        }

        /**
         * @brief Where the Change button for @p id is.
         *
         * Derived from the same layout the editor uses rather than read back out of it: a test
         * that asked the editor where its buttons were could not catch a row drawn off the panel.
         */
        [[nodiscard]] UiRect buttonFor(const std::string& id) const
        {
            const float rowHeight = static_cast<float>(frame.theme().metric(StudioMetric::ControlHeight));
            const float spacing = static_cast<float>(frame.theme().metric(StudioMetric::SpacingSmall));

            std::size_t index = 0;
            for (const StudioAction& action : registry.commands())
            {
                if (action.id == id) { break; }
                ++index;
            }

            const float top = body.y + static_cast<float>(index) * (rowHeight + spacing);
            return UiRect{body.x + body.width - 40.0f, top, 30.0f, rowHeight};
        }

        [[nodiscard]] const StudioAction* find(const std::string& id) const
        {
            return registry.find(id);
        }

        [[nodiscard]] bool hasOverride(const std::string& id, const StudioShortcut& shortcut) const
        {
            return std::any_of(preferences.shortcuts.begin(), preferences.shortcuts.end(),
                               [&](const StudioShortcutOverride& override) {
                                   return override.actionId == id && override.shortcut == shortcut;
                               });
        }
    };

    bool contains(const std::string& text, std::string_view needle)
    {
        return text.find(needle) != std::string::npos;
    }
}

CNA_STUDIO_TEST(TheEditorListsEveryCommandWithItsChord)
{
    // The list is the screen: a user looking for what a key does reads it, and an editor that only
    // listed the commands that happen to have a chord could not be used to give one to a command
    // that has none.
    Fixture fixture;
    fixture.run(away());

    CNA_STUDIO_EXPECT(fixture.last.rowsDrawn == fixture.registry.commands().size());
    CNA_STUDIO_EXPECT(fixture.last.contentHeight > 0.0f);
}

CNA_STUDIO_TEST(ARowTakesTheNextChordRatherThanTypedText)
{
    Fixture fixture;
    fixture.arm("test.open");
    CNA_STUDIO_EXPECT(fixture.state.capturing == "test.open");

    fixture.press(UiKey::W, /*control=*/true);

    const StudioAction* open = fixture.find("test.open");
    CNA_STUDIO_EXPECT(open != nullptr);
    if (open != nullptr) { CNA_STUDIO_EXPECT(open->shortcut == chord(UiKey::W, true)); }
    CNA_STUDIO_EXPECT(fixture.state.capturing.empty());
}

CNA_STUDIO_TEST(ARebindingIsRecordedSoItSurvivesARestart)
{
    // The registry is rebuilt from nothing every launch, so a rebinding that only changed it would
    // last until the user closed Studio -- which is the same as not working, discovered later.
    Fixture fixture;
    fixture.arm("test.open");
    fixture.press(UiKey::W, /*control=*/true);

    CNA_STUDIO_EXPECT(fixture.hasOverride("test.open", chord(UiKey::W, /*control=*/true)));

    // And applying the stored overrides to a fresh registry puts the chord back.
    StudioActionRegistry rebuilt;
    rebuilt.add(command("test.open", "Open", "Open an existing project."));
    fixture.preferences.applyShortcuts(rebuilt);

    const StudioAction* open = rebuilt.find("test.open");
    CNA_STUDIO_EXPECT(open != nullptr);
    if (open != nullptr) { CNA_STUDIO_EXPECT(open->shortcut == chord(UiKey::W, true)); }
}

CNA_STUDIO_TEST(RebindingTheSameCommandTwiceLeavesOneAnswer)
{
    // Two overrides for one command is two answers to what it is bound to, and which one wins is
    // then decided by the order they happen to be applied in.
    Fixture fixture;
    fixture.arm("test.open");
    fixture.press(UiKey::W, /*control=*/true);
    fixture.arm("test.open");
    fixture.press(UiKey::E, /*control=*/true);

    std::size_t overrides = 0;
    for (const StudioShortcutOverride& override : fixture.preferences.shortcuts)
    {
        if (override.actionId == "test.open") { ++overrides; }
    }
    CNA_STUDIO_EXPECT(overrides == 1);
    CNA_STUDIO_EXPECT(fixture.hasOverride("test.open", chord(UiKey::E, /*control=*/true)));
}

CNA_STUDIO_TEST(AChordAnotherCommandHoldsIsRefusedBeforeItIsAccepted)
{
    Fixture fixture;
    fixture.arm("test.open");
    fixture.press(UiKey::S, /*control=*/true);

    const StudioAction* open = fixture.find("test.open");
    const StudioAction* save = fixture.find("test.save");
    CNA_STUDIO_EXPECT(open != nullptr && save != nullptr);

    // Neither command moved: not the one being rebound, and not the one that held the chord.
    if (open != nullptr) { CNA_STUDIO_EXPECT(!open->shortcut.isBound()); }
    if (save != nullptr) { CNA_STUDIO_EXPECT(save->shortcut == chord(UiKey::S, true)); }
    CNA_STUDIO_EXPECT(fixture.preferences.shortcuts.empty());

    // Still armed: the user's next act is another chord, not finding the button again.
    CNA_STUDIO_EXPECT(fixture.state.capturing == "test.open");
    CNA_STUDIO_EXPECT(fixture.state.refusedBy == "test.save");
    CNA_STUDIO_EXPECT(fixture.state.refused == chord(UiKey::S, /*control=*/true));
}

CNA_STUDIO_TEST(TheRefusalNamesTheChordAndTheCommandHoldingIt)
{
    Fixture fixture;
    const std::string message = studioShortcutConflictMessage(
        fixture.registry, chord(UiKey::S, /*control=*/true), "test.save");

    CNA_STUDIO_EXPECT(contains(message, describeStudioShortcut(chord(UiKey::S, true))));
    CNA_STUDIO_EXPECT(contains(message, "Save"));
    CNA_STUDIO_EXPECT(studioShortcutConflictMessage(fixture.registry, {}, "").empty());
}

CNA_STUDIO_TEST(TheRefusalSaysWhichOfTwelveCommandsCalledCloseHoldsTheChord)
{
    // Every panel registers a Close and a Float, so a flat list of Studio's commands has six rows
    // reading "Close" and six reading "Float". "Ctrl+W is already Close" would leave a user
    // guessing which panel they had just failed to rebind.
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();

    std::string first;
    std::string second;
    for (const StudioAction& action : shell.actions().commands())
    {
        if (action.label != "Close") { continue; }
        if (first.empty()) { first = action.id; }
        else if (second.empty()) { second = action.id; }
    }
    CNA_STUDIO_EXPECT(!first.empty() && !second.empty());
    if (first.empty() || second.empty()) { return; }

    const StudioShortcut pressed = chord(UiKey::W, /*control=*/true);
    const std::string one = studioShortcutConflictMessage(shell.actions(), pressed, first);
    const std::string other = studioShortcutConflictMessage(shell.actions(), pressed, second);

    CNA_STUDIO_EXPECT(one != other);
    CNA_STUDIO_EXPECT(contains(one, shell.actions().find(first)->description.substr(0, 20)));
}

CNA_STUDIO_TEST(TheRefusalIsJustTheLabelWhenNothingElseSharesIt)
{
    // The disambiguation is for the rows that need it. Adding a description to every message would
    // make the common case -- a chord held by something on the menu bar -- longer to read for no
    // gain, and would push the sentence past the width it is drawn in.
    Fixture fixture;
    const std::string message = studioShortcutConflictMessage(
        fixture.registry, chord(UiKey::S, /*control=*/true), "test.save");

    CNA_STUDIO_EXPECT(!contains(message, "Save the active document"));
}

CNA_STUDIO_TEST(AModifierOnItsOwnIsNotAChord)
{
    // A user pressing Ctrl+K holds Ctrl down first. A capture that took the first frame with any
    // key down would bind the command to Ctrl-and-nothing, before they had finished pressing what
    // they meant -- and Ctrl-and-nothing is not a chord anybody can press on purpose afterwards.
    Fixture fixture;
    fixture.arm("test.open");

    UiInputState holding = away();
    holding.modifiers.control = true;
    fixture.run(holding);

    CNA_STUDIO_EXPECT(fixture.state.capturing == "test.open");
    const StudioAction* open = fixture.find("test.open");
    CNA_STUDIO_EXPECT(open != nullptr);
    if (open != nullptr) { CNA_STUDIO_EXPECT(!open->shortcut.isBound()); }
}

CNA_STUDIO_TEST(EscapeCancelsTheCaptureInsteadOfBecomingTheBinding)
{
    Fixture fixture;
    fixture.arm("test.open");
    fixture.press(UiKey::Escape);

    CNA_STUDIO_EXPECT(fixture.state.capturing.empty());
    const StudioAction* open = fixture.find("test.open");
    CNA_STUDIO_EXPECT(open != nullptr);
    if (open != nullptr) { CNA_STUDIO_EXPECT(!open->shortcut.isBound()); }
    CNA_STUDIO_EXPECT(fixture.preferences.shortcuts.empty());
}

CNA_STUDIO_TEST(TabIsNotBindableBecauseItIsHowAUserLeavesTheRow)
{
    Fixture fixture;
    fixture.arm("test.open");
    fixture.press(UiKey::Tab);

    const StudioAction* open = fixture.find("test.open");
    CNA_STUDIO_EXPECT(open != nullptr);
    if (open != nullptr) { CNA_STUDIO_EXPECT(!open->shortcut.isBound()); }
}

CNA_STUDIO_TEST(PressingChangeAgainStopsListening)
{
    // The armed row's button says Cancel, and a user who armed the wrong row presses it. Without
    // this the only way out is Escape, which is not written anywhere on the row.
    Fixture fixture;
    fixture.arm("test.open");
    CNA_STUDIO_EXPECT(fixture.state.capturing == "test.open");

    fixture.arm("test.open");
    CNA_STUDIO_EXPECT(fixture.state.capturing.empty());
}

CNA_STUDIO_TEST(ARefusalIsForgottenWhenAnotherRowIsArmed)
{
    // The message names a chord and a command. Left standing over a different row it describes a
    // refusal that did not happen to the row the user is now looking at.
    Fixture fixture;
    fixture.arm("test.open");
    fixture.press(UiKey::S, /*control=*/true);
    CNA_STUDIO_EXPECT(!fixture.state.refusedBy.empty());

    fixture.arm("test.grid");
    CNA_STUDIO_EXPECT(fixture.state.refusedBy.empty());
    CNA_STUDIO_EXPECT(!fixture.state.refused.isBound());
}

CNA_STUDIO_TEST(TheEditorSurvivesAPanelTooSmallToListAnything)
{
    // The Shortcuts section is the last one on the Preferences page, so it is the first to be cut
    // off when the panel is short -- and a row drawn half outside its body, or a division by a
    // zero width, would happen there rather than in the arrangement anybody photographs.
    for (const UiRect& body : {UiRect{0.0f, 0.0f, 0.0f, 0.0f},
                               UiRect{0.0f, 0.0f, 20.0f, 10.0f},
                               UiRect{0.0f, 0.0f, 900.0f, 4.0f}})
    {
        Fixture fixture;
        fixture.body = body;
        fixture.run(away());

        CNA_STUDIO_EXPECT(fixture.frame.phaseViolations() == 0);
        const UiDrawDataValidation checked = validate(fixture.frame.drawData());
        CNA_STUDIO_EXPECT(checked.valid);
    }
}
