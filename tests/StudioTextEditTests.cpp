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
#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioTextEdit.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <string>
#include <vector>

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

// ------------------------------------------------------------------------------------------------
// Grapheme clusters: what a reader calls a character (STUDIO-03026)
// ------------------------------------------------------------------------------------------------

namespace
{
    // Written as escapes rather than as literal characters, because the whole point of these cases
    // is the exact byte sequence, and a source file that has been through an editor that
    // normalises -- composing "e" + U+0301 into U+00E9 -- would silently test nothing.
    const std::string kCombining = "é";               // e + COMBINING ACUTE: one cluster
    const std::string kPrecomposed = "é";              // U+00E9: also one cluster, two bytes
    const std::string kFlagFR = "\U0001F1EB\U0001F1F7";     // regional indicators F + R
    const std::string kFlagDE = "\U0001F1E9\U0001F1EA";     // regional indicators D + E
    const std::string kFamily =                              // man + ZWJ + woman + ZWJ + girl
        "\U0001F468‍\U0001F469‍\U0001F467";
    const std::string kThumbsUp = "\U0001F44D\U0001F3FD";   // thumbs up + medium skin tone
    const std::string kHangul = "한";                    // precomposed LVT syllable
    const std::string kHangulJamo = "한";    // the same syllable as three jamo
    const std::string kDevanagari = "कि";          // KA + vowel sign I (spacing mark)

    /** @brief How many grapheme clusters @p text holds -- the count a reader would give. */
    std::size_t clusterCount(const std::string& text)
    {
        std::size_t count = 0;
        std::size_t at = 0;
        while (at < text.size())
        {
            const std::size_t next = studioGraphemeNext(text, at);
            if (next <= at) { break; }
            at = next;
            ++count;
        }
        return count;
    }

    /** @brief The clusters of @p text, in order, so a case can be read as what a reader sees. */
    std::vector<std::string> clusters(const std::string& text)
    {
        std::vector<std::string> out;
        std::size_t at = 0;
        while (at < text.size())
        {
            const std::size_t next = studioGraphemeNext(text, at);
            if (next <= at) { break; }
            out.push_back(text.substr(at, next - at));
            at = next;
        }
        return out;
    }
}

CNA_STUDIO_TEST(ACombiningMarkIsPartOfTheLetterItSitsOn)
{
    // The failure this prevents is the one a user meets first: pressing Left once after typing an
    // accented letter puts the caret between the letter and its accent, where there is nothing to
    // draw a caret between -- and Backspace then takes the accent off and leaves the letter.
    CNA_STUDIO_EXPECT_EQ(clusterCount(kCombining), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clusterCount(kPrecomposed), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clusterCount("naı̈ve"), std::size_t{5});

    StudioTextEdit edit{kCombining};
    edit.moveLeft(/*extend=*/false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{0});

    StudioTextEdit deleting{kCombining};
    CNA_STUDIO_EXPECT(deleting.deleteBackward());
    CNA_STUDIO_EXPECT_EQ(deleting.text(), std::string{});
}

CNA_STUDIO_TEST(TwoFlagsAreTwoCharactersRatherThanOneLongOne)
{
    // Regional indicators pair. Deciding each boundary by looking only at the two code points
    // either side of it would join every indicator to the one before, so two flags in a row would
    // be one cluster and four would be one -- and the caret could never get between them.
    CNA_STUDIO_EXPECT_EQ(clusterCount(kFlagFR), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clusterCount(kFlagFR + kFlagDE), std::size_t{2});

    const std::vector<std::string> parts = clusters(kFlagFR + kFlagDE);
    CNA_STUDIO_EXPECT_EQ(parts.size(), std::size_t{2});
    if (parts.size() == 2)
    {
        CNA_STUDIO_EXPECT_EQ(parts[0], kFlagFR);
        CNA_STUDIO_EXPECT_EQ(parts[1], kFlagDE);
    }

    // Backwards too, and this is the direction that catches pairing counted from the wrong end:
    // one Backspace takes the second flag and leaves the first whole.
    StudioTextEdit edit{kFlagFR + kFlagDE};
    CNA_STUDIO_EXPECT(edit.deleteBackward());
    CNA_STUDIO_EXPECT_EQ(edit.text(), kFlagFR);
    CNA_STUDIO_EXPECT(edit.deleteBackward());
    CNA_STUDIO_EXPECT_EQ(edit.text(), std::string{});
}

CNA_STUDIO_TEST(AJoinedEmojiSequenceIsOneCharacterAndASkinToneStaysOnItsHand)
{
    CNA_STUDIO_EXPECT_EQ(clusterCount(kFamily), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clusterCount(kThumbsUp), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clusterCount(kFamily + kThumbsUp), std::size_t{2});

    // A family emoji deleted a code point at a time turns into a man, then a man and a woman with
    // a joiner between them, then other people entirely. One press, one emoji.
    StudioTextEdit edit{"ok " + kFamily};
    CNA_STUDIO_EXPECT(edit.deleteBackward());
    CNA_STUDIO_EXPECT_EQ(edit.text(), std::string{"ok "});
}

CNA_STUDIO_TEST(HangulComposesWhetherItArrivesPrecomposedOrAsJamo)
{
    // The same syllable both ways: text typed through an IME arrives as jamo and text pasted from
    // a document arrives precomposed, and a caret that stepped differently between them would be
    // a field that behaves differently depending on where the text came from.
    CNA_STUDIO_EXPECT_EQ(clusterCount(kHangul), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clusterCount(kHangulJamo), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(clusterCount(kHangul + kHangul), std::size_t{2});
}

CNA_STUDIO_TEST(AnIndicVowelSignStaysWithItsConsonant)
{
    // A spacing mark takes width of its own, unlike an accent, which is why it is a different
    // property in the standard -- and the same answer for a caret.
    CNA_STUDIO_EXPECT_EQ(clusterCount(kDevanagari), std::size_t{1});

    StudioTextEdit edit{kDevanagari};
    edit.moveLeft(/*extend=*/false);
    CNA_STUDIO_EXPECT_EQ(edit.caret(), std::size_t{0});
}

CNA_STUDIO_TEST(CrLfIsOneCharacterAndAControlIsItsOwn)
{
    CNA_STUDIO_EXPECT_EQ(clusterCount("a\r\nb"), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(clusterCount("a\n\rb"), std::size_t{4});
}

CNA_STUDIO_TEST(WalkingForwardsAndBackwardsVisitsTheSameBoundaries)
{
    // The two directions are different code -- forwards applies the rules, backwards re-walks from
    // the start so the regional-indicator run is counted from its beginning -- so they are checked
    // against each other over a string with one of everything in it.
    const std::string mixed = "A" + kCombining + kFlagFR + kFlagDE + "中" + kFamily
                            + kHangulJamo + kDevanagari + kThumbsUp + "z";

    std::vector<std::size_t> forwards{0};
    while (forwards.back() < mixed.size())
    {
        const std::size_t next = studioGraphemeNext(mixed, forwards.back());
        CNA_STUDIO_EXPECT(next > forwards.back());
        if (next <= forwards.back()) { break; }
        forwards.push_back(next);
    }

    std::vector<std::size_t> backwards{mixed.size()};
    while (backwards.back() > 0)
    {
        const std::size_t previous = studioGraphemePrevious(mixed, backwards.back());
        CNA_STUDIO_EXPECT(previous < backwards.back());
        if (previous >= backwards.back()) { break; }
        backwards.push_back(previous);
    }
    std::reverse(backwards.begin(), backwards.end());

    CNA_STUDIO_EXPECT_EQ(forwards.size(), backwards.size());
    CNA_STUDIO_EXPECT(forwards == backwards);
    CNA_STUDIO_EXPECT_EQ(clusterCount(mixed), forwards.size() - 1);
}

CNA_STUDIO_TEST(EveryBoundaryIsAlsoACodePointBoundary)
{
    // A cluster boundary inside a UTF-8 sequence would produce a string that is not UTF-8 the
    // moment anything split there -- which Backspace, cut and the selection all do.
    const std::string mixed = "A" + kCombining + kFlagFR + "中" + kFamily + kThumbsUp;
    for (std::size_t at = 0; at <= mixed.size(); at = studioGraphemeNext(mixed, at))
    {
        CNA_STUDIO_EXPECT_EQ(studioUtf8BoundaryAt(mixed, at), at);
        if (at >= mixed.size()) { break; }
    }
}

CNA_STUDIO_TEST(MalformedBytesAdvanceRatherThanStickingOrJoining)
{
    // Text arriving from a project file is not text this code wrote. A walk that could stand still
    // turns one corrupt byte into a hang, which is the failure that matters here -- being wrong
    // about where the boundary is merely draws a box.
    const std::string broken = std::string{"a\xC3"} + "\xFF" + "b\x80" + "c";

    std::size_t at = 0;
    std::size_t steps = 0;
    while (at < broken.size() && steps < 100)
    {
        const std::size_t next = studioGraphemeNext(broken, at);
        CNA_STUDIO_EXPECT(next > at);
        if (next <= at) { break; }
        at = next;
        ++steps;
    }
    CNA_STUDIO_EXPECT_EQ(at, broken.size());

    std::size_t back = broken.size();
    steps = 0;
    while (back > 0 && steps < 100)
    {
        const std::size_t previous = studioGraphemePrevious(broken, back);
        CNA_STUDIO_EXPECT(previous < back);
        if (previous >= back) { break; }
        back = previous;
        ++steps;
    }
    CNA_STUDIO_EXPECT_EQ(back, std::size_t{0});
}

CNA_STUDIO_TEST(AClickCannotPutTheCaretSomewhereTheArrowKeysRefuseToStop)
{
    // The same bug through the mouse. A combining mark adds no width, so the code-point boundary
    // inside `e` + U+0301 sits at the same x as the one before it: clicking there put the caret
    // inside one rendered glyph, invisibly, until the next Backspace took the accent off and left
    // the letter. Driven through a real atlas, because the approximate metrics of a frame with no
    // fonts give a combining mark a width of its own and would hide exactly this.
    StudioFontAtlas atlas;
    const UiRect bounds{20.0f, 20.0f, 300.0f, 28.0f};
    const std::string original = "café " + kFlagFR + " ok";

    // Every offset a caret is allowed to be at.
    std::vector<std::size_t> boundaries;
    for (std::size_t at = 0; at <= original.size(); at = studioGraphemeNext(original, at))
    {
        boundaries.push_back(at);
        if (at >= original.size()) { break; }
    }

    for (int step = 0; step <= 40; ++step)
    {
        const float x = bounds.x + bounds.width * static_cast<float>(step) / 40.0f;

        StudioFrame frame{StudioTheme::dark()};
        frame.setFontAtlas(&atlas);
        std::string value = original;

        const auto run = [&](const UiInputState& input) {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                (void)studioTextField(f, f.ids().make("field"), bounds, value);
            });
        };

        clickAt([&](float cx, float cy, bool down) { run(at(cx, cy, down)); }, x,
                bounds.centerY());

        UiInputState back = at(x, bounds.centerY());
        back.setKeyDown(UiKey::Backspace, true);
        run(back);

        UiInputState commit = at(x, bounds.centerY());
        commit.setKeyDown(UiKey::Enter, true);
        run(commit);

        // Whatever the click selected, one Backspace removed exactly one whole cluster -- so the
        // result is the original with one of its clusters cut out, and never a broken one.
        bool recognised = value == original;   // a click at the very start deletes nothing
        for (std::size_t i = 0; i + 1 < boundaries.size() && !recognised; ++i)
        {
            std::string without = original;
            without.erase(boundaries[i], boundaries[i + 1] - boundaries[i]);
            recognised = value == without;
        }

        if (!recognised)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "a click at x=" + std::to_string(x) + " then Backspace left '" + value
                + "', which is not the field's text with one whole character removed.");
            break;
        }
    }
}

CNA_STUDIO_TEST(TruncationCutsBetweenCharactersRatherThanInsideOne)
{
    // Worse than cutting a multi-byte character in half, because that produces a sequence no
    // decoder reads and shows up at once. Cutting between a letter and its accent is valid UTF-8,
    // so nothing complains: it renders as a stray mark sitting on the ellipsis, and the only
    // person who ever finds out is the one whose name it happened to.
    StudioFontAtlas atlas;
    StudioFrame frame{StudioTheme::dark()};
    frame.setFontAtlas(&atlas);

    const std::string text = "André " + kFlagFR + kFlagDE + " Pontés";
    const StudioFontStyle style = frame.theme().font(StudioFontRole::Body);
    const float full = frame.measureText(style, text).width;

    for (int step = 1; step <= 60; ++step)
    {
        const float width = full * static_cast<float>(step) / 60.0f;
        const std::string cut = studioTruncateText(frame, style, text, width);

        // Whatever came back is the text's own prefix plus the ellipsis, cut at a boundary a
        // reader would recognise -- never inside one character.
        std::string body = cut;
        const std::string ellipsis = "…";
        if (body.size() >= ellipsis.size()
            && body.compare(body.size() - ellipsis.size(), ellipsis.size(), ellipsis) == 0)
        {
            body.resize(body.size() - ellipsis.size());
        }

        if (text.compare(0, body.size(), body) != 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "truncation at width " + std::to_string(width) + " returned '" + cut
                + "', which is not a prefix of the text.");
            break;
        }
        if (studioGraphemeNext(text, studioGraphemePrevious(text, body.size())) != body.size()
            && body.size() != 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "truncation at width " + std::to_string(width) + " cut inside a character, at byte "
                + std::to_string(body.size()) + ".");
            break;
        }
    }
}
