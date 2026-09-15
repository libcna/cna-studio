// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTextEditTests.cpp
 * @brief The caret, the selection, and the field built on them (plan.md STUDIO-03024, STUDIO-03025).
 *
 * Nearly everything that goes wrong with a text field goes wrong in the model rather than in the
 * drawing: a caret landing inside a multi-byte character, Backspace eating one byte of three,
 * Shift+Left collapsing a selection instead of extending it. None of that needs a frame, a font or
 * a window to reproduce, and none of it is visible in a screenshot until the damage is done -- so
 * the model is tested on its own and the widget is tested for the things only a widget has.
 */

#include "TestHarness.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioTextEdit.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <string>

using namespace CNA::Studio;

namespace
{
    /** @brief "héllo" -- five characters, six bytes, and the é is where byte arithmetic breaks. */
    const std::string kAccented = "h\xC3\xA9llo";

    /** @brief A grinning face: one character, four bytes, outside the basic plane. */
    const std::string kEmoji = "\xF0\x9F\x98\x80";

    /**
     * @brief Clicks at (@p x, @p y), starting from a frame with the button up.
     *
     * The un-pressed frame first is not ceremony. The router treats the very first frame's state
     * as its own previous state, so that a user who launched Studio by double-clicking does not
     * get a phantom press delivered into their scene -- which means a test whose first frame has
     * the button already down registers no click at all.
     */
    template <typename Run>
    void clickAt(const Run& run, float x, float y)
    {
        run(x, y, false);
        run(x, y, true);
        run(x, y, false);
    }

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
}

CNA_STUDIO_TEST(CaretMovementStepsWholeCharactersNotBytes)
{
    StudioTextEdit edit{kAccented};
    CNA_STUDIO_EXPECT_EQ(edit.caret(), kAccented.size());

    edit.moveHome(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{0});

    // h(1) é(2) l(1) l(1) o(1): the second step must land on 3, not 2.
    edit.moveRight(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{1});
    edit.moveRight(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{3});

    edit.moveLeft(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{1});

    // And off the ends, rather than past them.
    edit.moveLeft(false);
    edit.moveLeft(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{0});

    edit.moveEnd(false);
    edit.moveRight(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), kAccented.size());
}

CNA_STUDIO_TEST(DeletingRemovesAWholeCharacterAndLeavesValidText)
{
    // One byte of a three-byte character removed leaves a string that is no longer UTF-8, which
    // every later measurement and draw has to cope with -- and the user sees one character turn
    // into two boxes.
    StudioTextEdit edit{kAccented};
    CNA_STUDIO_EXPECT(edit.deleteBackward());
    CNA_STUDIO_EXPECT_EQ(edit.text(), std::string{"h\xC3\xA9ll"});

    edit.moveHome(false);
    edit.moveRight(false);
    CNA_STUDIO_EXPECT(edit.deleteForward());
    CNA_STUDIO_EXPECT_EQ(edit.text(), std::string{"hll"});

    // At the edges there is nothing to delete, and saying so is what stops a caller reporting a
    // change that did not happen.
    edit.moveHome(false);
    CNA_STUDIO_EXPECT(!edit.deleteBackward());
    edit.moveEnd(false);
    CNA_STUDIO_EXPECT(!edit.deleteForward());
}

CNA_STUDIO_TEST(ACharacterOutsideTheBasicPlaneIsOneCharacter)
{
    StudioTextEdit edit{kEmoji};
    CNA_STUDIO_EXPECT_EQ(edit.text().size(), std::size_t{4});

    edit.moveHome(false);
    edit.moveRight(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{4});

    CNA_STUDIO_EXPECT(edit.deleteBackward());
    CNA_STUDIO_EXPECT(edit.text().empty());
}

CNA_STUDIO_TEST(SelectionRunsBetweenAnAnchorAndTheCaretInEitherDirection)
{
    // Which end is smaller is not fixed. Dragging left from the middle of a word selects leftwards,
    // and Shift+Right then shrinks that selection from its left edge -- which a model storing only
    // a begin and an end cannot express.
    StudioTextEdit edit{"abcdef"};

    edit.moveTo(3, false);
    edit.moveTo(1, true);
    CNA_STUDIO_EXPECT(edit.hasSelection());
    CNA_STUDIO_EXPECT_EQ(edit.anchor(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(edit.selectedText(), std::string{"bc"});

    edit.moveRight(true);
    CNA_STUDIO_EXPECT_EQ(edit.selectedText(), std::string{"c"});

    edit.moveRight(true);
    CNA_STUDIO_EXPECT(!edit.hasSelection());
}

CNA_STUDIO_TEST(AnArrowWithASelectionGoesToItsEdgeRatherThanPastTheCaret)
{
    // What every text field does, and a field that did not would make deselecting also move the
    // caret somewhere the user did not ask for.
    StudioTextEdit edit{"abcdef"};
    edit.moveTo(1, false);
    edit.moveTo(4, true);

    edit.moveLeft(false);
    CNA_STUDIO_EXPECT(!edit.hasSelection());
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{1});

    edit.moveTo(1, false);
    edit.moveTo(4, true);
    edit.moveRight(false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{4});
}

CNA_STUDIO_TEST(TypingReplacesTheSelection)
{
    StudioTextEdit edit{"hello world"};
    edit.moveTo(0, false);
    edit.moveTo(5, true);

    CNA_STUDIO_EXPECT(edit.insert("goodbye"));
    CNA_STUDIO_EXPECT_EQ(edit.text(), std::string{"goodbye world"});
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{7});
    CNA_STUDIO_EXPECT(!edit.hasSelection());

    edit.selectAll();
    CNA_STUDIO_EXPECT(edit.deleteSelection());
    CNA_STUDIO_EXPECT(edit.text().empty());
}

CNA_STUDIO_TEST(ReplacingTheTextKeepsTheUsersPlaceWhereItStillExists)
{
    // A field refreshed underneath the user -- by an undo, or by something else editing the same
    // property -- should not also cost them their place. Where the place no longer exists, it is
    // clamped rather than left pointing off the end.
    StudioTextEdit edit{"abcdef"};
    edit.moveTo(4, false);

    edit.setText("abcdefghij");
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{4});

    edit.setText("ab");
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{2});

    // And never inside a character.
    edit.setText(kAccented);
    edit.moveEnd(false);
    edit.setText("h\xC3\xA9");
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{3});
}

CNA_STUDIO_TEST(BoundariesSnapOntoCharactersFromAnywhereInside)
{
    CNA_STUDIO_EXPECT_EQ(studioUtf8BoundaryAt(kAccented, 2), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(studioUtf8BoundaryAt(kAccented, 1), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(studioUtf8BoundaryAt(kAccented, 3), std::size_t{3});

    // Past the end clamps to the end, which is a valid caret position.
    CNA_STUDIO_EXPECT_EQ(studioUtf8BoundaryAt(kAccented, 999), kAccented.size());
    CNA_STUDIO_EXPECT_EQ(studioUtf8Next(kAccented, 999), kAccented.size());
    CNA_STUDIO_EXPECT_EQ(studioUtf8Previous(kAccented, 0), std::size_t{0});

    // And an empty string has exactly one position.
    CNA_STUDIO_EXPECT_EQ(studioUtf8Next("", 0), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(studioUtf8Previous("", 0), std::size_t{0});
}

// -------------------------------------------------------------------------------------------
// The widget
// -------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ATextFieldCommitsOnEnterAndNotBefore)
{
    // Not on every keystroke. A property bound to a field that wrote on every character would put
    // a hundred entries in the undo stack for one edit, and would re-validate a number while it is
    // half-typed.
    StudioFrame frame{StudioTheme::dark()};
    const UiRect bounds{100.0f, 100.0f, 200.0f, 28.0f};
    std::string value = "old";

    // Captured from the *input* pass. `committed` and `cancelled` are true only there, by the
    // same convention as every other widget's `activated`: acting on them runs the action once
    // per gesture rather than twice.
    const auto run = [&](const UiInputState& input) {
        StudioTextFieldResult result;
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            const StudioTextFieldResult pass =
                studioTextField(f, f.ids().make("field"), bounds, value);
            if (f.isInputPass()) { result = pass; }
        });
        return result;
    };

    StudioTextFieldResult afterClick;
    clickAt([&](float x, float y, bool down) { afterClick = run(at(x, y, down)); },
            bounds.centerX(), bounds.centerY());
    CNA_STUDIO_EXPECT(afterClick.interaction.focused);

    UiInputState typing = at(bounds.centerX(), bounds.centerY());
    typing.characters = {u'!'};
    const StudioTextFieldResult afterTyping = run(typing);
    CNA_STUDIO_EXPECT(!afterTyping.committed);
    CNA_STUDIO_EXPECT_EQ(value, std::string{"old"});

    UiInputState enter = at(bounds.centerX(), bounds.centerY());
    enter.setKeyDown(UiKey::Enter, true);
    const StudioTextFieldResult afterEnter = run(enter);
    CNA_STUDIO_EXPECT(afterEnter.committed);
    CNA_STUDIO_EXPECT_EQ(value, std::string{"old!"});
}

CNA_STUDIO_TEST(EscapeAbandonsAnEditAndLeavesTheValueAlone)
{
    StudioFrame frame{StudioTheme::dark()};
    const UiRect bounds{100.0f, 100.0f, 200.0f, 28.0f};
    std::string value = "keep";

    const auto run = [&](const UiInputState& input) {
        StudioTextFieldResult result;
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            const StudioTextFieldResult pass =
                studioTextField(f, f.ids().make("field"), bounds, value);
            if (f.isInputPass()) { result = pass; }
        });
        return result;
    };

    clickAt([&](float x, float y, bool down) { (void)run(at(x, y, down)); },
            bounds.centerX(), bounds.centerY());

    UiInputState typing = at(bounds.centerX(), bounds.centerY());
    typing.characters = {u'X'};
    (void)run(typing);

    UiInputState escape = at(bounds.centerX(), bounds.centerY());
    escape.setKeyDown(UiKey::Escape, true);
    const StudioTextFieldResult cancelled = run(escape);

    CNA_STUDIO_EXPECT(cancelled.cancelled);
    CNA_STUDIO_EXPECT(!cancelled.committed);
    CNA_STUDIO_EXPECT_EQ(value, std::string{"keep"});
}

CNA_STUDIO_TEST(TheClipboardWorksWithoutAPlatformAndDefersToOneWhenThereIs)
{
    // The local fallback is what makes a text field cut-and-pasteable in a headless test and in a
    // build whose CNA has the Devices module switched off (CNA gap G-02). It is real but local:
    // it does not reach other applications, which is why a host installs the platform's.
    StudioFrame frame{StudioTheme::dark()};
    CNA_STUDIO_EXPECT(!frame.hasPlatformClipboard());

    frame.setClipboardText("local");
    CNA_STUDIO_EXPECT_EQ(frame.clipboardText(), std::string{"local"});

    std::string platform;
    frame.setClipboard([&platform]() { return platform; },
                       [&platform](const std::string& text) { platform = text; });
    CNA_STUDIO_EXPECT(frame.hasPlatformClipboard());

    frame.setClipboardText("shared");
    CNA_STUDIO_EXPECT_EQ(platform, std::string{"shared"});
    CNA_STUDIO_EXPECT_EQ(frame.clipboardText(), std::string{"shared"});
}

CNA_STUDIO_TEST(AFocusedFieldTellsTheRouterThatAKeyMeansText)
{
    // Without this, Delete in a name field deletes the selected entity. Every shortcut in the
    // editor is one keystroke away from being the wrong thing while somebody is typing.
    StudioFrame frame{StudioTheme::dark()};
    const UiRect bounds{100.0f, 100.0f, 200.0f, 28.0f};
    std::string value = "name";

    bool wanted = false;
    const auto run = [&](const UiInputState& input) {
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            (void)studioTextField(f, f.ids().make("field"), bounds, value);
            if (f.isInputPass()) { wanted = f.router().wantsTextInput(); }
        });
    };

    run(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(!wanted);

    clickAt([&](float x, float y, bool down) { run(at(x, y, down)); },
            bounds.centerX(), bounds.centerY());
    CNA_STUDIO_EXPECT(wanted);
}

CNA_STUDIO_TEST(ClickingPlacesTheCaretWhereTheUserAimed)
{
    // The one thing only the widget can get wrong: the model knows nothing about pixels. Clicking
    // in the right half of a character must put the caret after it, which is where somebody aiming
    // between two letters expects it.
    StudioFrame frame{StudioTheme::dark()};
    frame.setFontSet(nullptr);

    const UiRect bounds{0.0f, 0.0f, 400.0f, 28.0f};
    std::string value = "abcdefghij";

    const auto press = [&](float x) {
        clickAt([&](float px, float py, bool down) {
            runStudioFrame(frame, at(px, py, down), [&](StudioFrame& f) {
                (void)studioTextField(f, f.ids().make("field"), bounds, value);
            });
        }, x, bounds.centerY());
    };

    press(bounds.left() + 1.0f);
    const WidgetId id = frame.ids().make("field");
    CNA_STUDIO_EXPECT_EQ(frame.state().get(id).caret, std::size_t{0});

    // Far to the right of any glyph: the caret belongs at the end, not clamped to the first
    // character it could not get past.
    press(bounds.right() - 1.0f);
    CNA_STUDIO_EXPECT_EQ(frame.state().get(id).caret, value.size());
}

CNA_STUDIO_TEST(ATextFieldCommitsWhenFocusLeavesRatherThanThrowingTheEditAway)
{
    // This was broken for as long as text fields have existed, in the way that is hardest to see:
    // the code that committed on focus loss was written, was correct, and could never run. The
    // branch above it had already cleared the "there is an uncommitted edit" flag and overwritten
    // the buffer with the old value, so by the time anything asked, there was nothing to ask about.
    //
    // Every field in Studio silently threw away an edit the user clicked away from -- a name typed
    // into the Details panel, a path typed into Preferences -- and it looks, from the outside,
    // exactly like a field that did not take the typing at all.
    StudioFrame frame{StudioTheme::dark()};
    const UiRect field{100.0f, 100.0f, 200.0f, 28.0f};
    const UiRect elsewhere{100.0f, 200.0f, 200.0f, 28.0f};
    std::string value = "old";
    std::string other = "other";

    const auto run = [&](const UiInputState& input) {
        StudioTextFieldResult result;
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            const StudioTextFieldResult pass =
                studioTextField(f, f.ids().make("field"), field, value);
            (void)studioTextField(f, f.ids().make("other"), elsewhere, other);
            if (f.isInputPass()) { result = pass; }
        });
        return result;
    };

    clickAt([&](float x, float y, bool down) { (void)run(at(x, y, down)); },
            field.centerX(), field.centerY());

    UiInputState typing = at(field.centerX(), field.centerY());
    typing.characters = {u'!'};
    (void)run(typing);
    CNA_STUDIO_EXPECT_EQ(value, std::string{"old"});

    // Clicking the other field takes focus away, which is what a person does when they have
    // finished typing and moved on.
    StudioTextFieldResult afterLeaving;
    clickAt([&](float x, float y, bool down) { afterLeaving = run(at(x, y, down)); },
            elsewhere.centerX(), elsewhere.centerY());

    CNA_STUDIO_EXPECT_EQ(value, std::string{"old!"});
    CNA_STUDIO_EXPECT(afterLeaving.committed);
}

CNA_STUDIO_TEST(AFieldNobodyTouchedCommitsNothingWhenAnotherIsClicked)
{
    // The other half: committing on focus loss must not mean *every* field writes its value back
    // every time focus moves. A field that committed without an edit would put an undo entry on
    // the stack for looking at a property grid.
    StudioFrame frame{StudioTheme::dark()};
    const UiRect field{100.0f, 100.0f, 200.0f, 28.0f};
    const UiRect elsewhere{100.0f, 200.0f, 200.0f, 28.0f};
    std::string value = "untouched";
    std::string other = "other";

    bool committed = false;
    const auto run = [&](const UiInputState& input) {
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            const StudioTextFieldResult pass =
                studioTextField(f, f.ids().make("field"), field, value);
            (void)studioTextField(f, f.ids().make("other"), elsewhere, other);
            if (f.isInputPass() && pass.committed) { committed = true; }
        });
    };

    clickAt([&](float x, float y, bool down) { run(at(x, y, down)); },
            field.centerX(), field.centerY());
    clickAt([&](float x, float y, bool down) { run(at(x, y, down)); },
            elsewhere.centerX(), elsewhere.centerY());

    CNA_STUDIO_EXPECT(!committed);
    CNA_STUDIO_EXPECT_EQ(value, std::string{"untouched"});
}

CNA_STUDIO_TEST(EnterCommitsOnceAndTheFramesAfterItCommitNothing)
{
    // Enter commits and clears focus, so the frame after it takes the field through the "focus has
    // gone" path -- which now commits. It must see that the value it holds is the value it just
    // wrote, or every committed edit lands in the document twice: once as the change and once as a
    // no-op that still occupies an undo slot, and Ctrl+Z then appears to do nothing.
    StudioFrame frame{StudioTheme::dark()};
    const UiRect bounds{100.0f, 100.0f, 200.0f, 28.0f};
    std::string value = "old";

    int commits = 0;
    const auto run = [&](const UiInputState& input) {
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            const StudioTextFieldResult pass =
                studioTextField(f, f.ids().make("field"), bounds, value);
            if (f.isInputPass() && pass.committed) { ++commits; }
        });
    };

    clickAt([&](float x, float y, bool down) { run(at(x, y, down)); },
            bounds.centerX(), bounds.centerY());

    UiInputState typing = at(bounds.centerX(), bounds.centerY());
    typing.characters = {u'!'};
    run(typing);

    UiInputState enter = at(bounds.centerX(), bounds.centerY());
    enter.setKeyDown(UiKey::Enter, true);
    run(enter);

    CNA_STUDIO_EXPECT_EQ(commits, 1);
    CNA_STUDIO_EXPECT_EQ(value, std::string{"old!"});

    // And the frames after it, which is where a second commit would appear.
    for (int i = 0; i < 5; ++i) { run(at(bounds.centerX(), bounds.centerY())); }
    CNA_STUDIO_EXPECT_EQ(commits, 1);
    CNA_STUDIO_EXPECT_EQ(value, std::string{"old!"});
}

CNA_STUDIO_TEST(ACallerThatNormalisesWhatItStoresStillCommitsOnce)
{
    // The case the plain round trip above cannot reach, and the one every numeric field in Studio
    // is: the caller writes the value back in its own spelling, so "00.5" typed becomes "0.5"
    // stored. The editing buffer still holds what was typed, and a session left open across that
    // difference reads it as an uncommitted edit on the next frame and commits it again -- landing
    // every edit twice, once as the change and once as a no-op that still takes an undo slot.
    StudioFrame frame{StudioTheme::dark()};
    const UiRect bounds{100.0f, 100.0f, 200.0f, 28.0f};

    float stored = 0.0f;
    int commits = 0;

    const auto run = [&](const UiInputState& input) {
        runStudioFrame(frame, input, [&](StudioFrame& f) {
            // Rebuilt from the stored value every pass, the way a property row does.
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(stored));
            std::string text = buffer;

            const StudioTextFieldResult pass =
                studioTextField(f, f.ids().make("field"), bounds, text);
            if (f.isInputPass() && pass.committed)
            {
                ++commits;
                stored = std::strtof(text.c_str(), nullptr);
            }
        });
    };

    clickAt([&](float x, float y, bool down) { run(at(x, y, down)); },
            bounds.centerX(), bounds.centerY());

    UiInputState typing = at(bounds.centerX(), bounds.centerY());
    typing.characters = {u'0', u'.', u'5'};
    run(typing);

    UiInputState enter = at(bounds.centerX(), bounds.centerY());
    enter.setKeyDown(UiKey::Enter, true);
    run(enter);

    for (int i = 0; i < 5; ++i) { run(at(bounds.centerX(), bounds.centerY())); }

    CNA_STUDIO_EXPECT_EQ(commits, 1);
    CNA_STUDIO_EXPECT(std::abs(stored - 0.5f) < 0.001f);
}
