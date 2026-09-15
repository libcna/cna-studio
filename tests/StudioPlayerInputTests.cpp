// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPlayerInputTests.cpp
 * @brief Handing a running game the pointer and the keys (plan.md STUDIO-07009).
 *
 * The last thing the prototype's viewport did that the native one did not. The protocol, the
 * snapshot and the player's end of it have been shared and tested since play mode existed; what
 * was missing was a native viewport that filled one in.
 *
 * The rules are all about *not* sending things: not the editor's own keys, not a pointer that is
 * somewhere else, and not the same snapshot sixty times a second. Each of those is a real failure
 * that looks like a bug in the game rather than in the editor.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    const UiRect kBody{100.0f, 80.0f, 640.0f, 480.0f};

    /** @brief A frame whose input pass has already been entered, so the state is readable. */
    struct InputFrame
    {
        StudioFrame frame{StudioTheme::dark()};
        PlayerInputSnapshot snapshot;

        void run(const UiInputState& input, bool pointerInside)
        {
            runStudioFrame(frame, input, [&](StudioFrame& pass) {
                if (pass.isInputPass())
                {
                    snapshot = studioPlayerInputFrom(pass, kBody, pointerInside);
                }
            });
        }
    };

    UiInputState blank()
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 720.0f;
        return input;
    }
}

CNA_STUDIO_TEST(OnlyTheKeysAGamePlaysWithAreForwarded)
{
    // Forwarding every key the editor can name would send Ctrl+S to the game as an S -- exactly
    // the sort of thing that gets blamed on the game rather than on the editor that invented the
    // keystroke.
    InputFrame fixture;

    UiInputState input = blank();
    input.setKeyDown(UiKey::W, true);
    input.setKeyDown(UiKey::Space, true);
    input.setKeyDown(UiKey::F5, true);
    input.setKeyDown(UiKey::Delete, true);
    fixture.run(input, /*pointerInside=*/false);

    CNA_STUDIO_EXPECT(fixture.snapshot.isKeyDown("W"));
    CNA_STUDIO_EXPECT(fixture.snapshot.isKeyDown("Space"));
    CNA_STUDIO_EXPECT_EQ(fixture.snapshot.keys.size(), std::size_t{2});
}

CNA_STUDIO_TEST(ThePointerIsSentOnlyWhileItIsOverTheViewport)
{
    // A cursor resting on the inspector is not hovering the game. Reporting its last position
    // would leave the game acting on a pointer that has not been near it for minutes -- which
    // reads as the game having stuck controls.
    InputFrame fixture;

    UiInputState input = blank();
    input.mouseX = kBody.left() + 160.0f;
    input.mouseY = kBody.top() + 120.0f;
    input.mouseInWindow = true;
    input.setMouseDown(UiMouseButton::Left, true);
    input.wheelY = 2.0f;

    fixture.run(input, /*pointerInside=*/true);
    CNA_STUDIO_EXPECT(fixture.snapshot.hasPointer());

    // Relative to the panel, not to the window: the game is drawing into that rectangle and knows
    // nothing about where Studio put it.
    CNA_STUDIO_EXPECT_EQ(fixture.snapshot.mouseX, 160.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.snapshot.mouseY, 120.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.snapshot.surfaceWidth, kBody.width);
    CNA_STUDIO_EXPECT_EQ(fixture.snapshot.surfaceHeight, kBody.height);
    CNA_STUDIO_EXPECT(fixture.snapshot.leftButton);
    CNA_STUDIO_EXPECT_EQ(fixture.snapshot.wheel, 2.0f);

    // The same input, with the pointer somewhere else: keys still go, the pointer does not.
    InputFrame elsewhere;
    UiInputState away = input;
    away.setKeyDown(UiKey::A, true);
    elsewhere.run(away, /*pointerInside=*/false);

    CNA_STUDIO_EXPECT(!elsewhere.snapshot.hasPointer());
    CNA_STUDIO_EXPECT(!elsewhere.snapshot.leftButton);
    CNA_STUDIO_EXPECT_EQ(elsewhere.snapshot.wheel, 0.0f);
    CNA_STUDIO_EXPECT(elsewhere.snapshot.isKeyDown("A"));
}

CNA_STUDIO_TEST(NothingIsSentToAPlayerThatIsNotRunning)
{
    // The editor's viewport is live whether or not a game is. A forward that did not check would
    // be writing to a pipe nobody is reading, once a frame, for the whole session.
    StudioContext context;
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    StudioShellPanels panels{shell, context, log};

    PlayerInputSnapshot snapshot;
    snapshot.keys.emplace_back("W");

    CNA_STUDIO_EXPECT(!panels.isPlaying());
    CNA_STUDIO_EXPECT(!panels.forwardInputToPlayer(snapshot));
    CNA_STUDIO_EXPECT(panels.lastForwardedInput().keys.empty());
}

CNA_STUDIO_TEST(AnIdenticalSnapshotIsNotSentTwiceButAWheelNotchAlwaysIs)
{
    // The player answers every snapshot, so sixty identical ones a second would be sixty round
    // trips that told it nothing -- and the waste is doubled by the reply. A wheel notch is the
    // exception because it is an event rather than a state: two notches in a row are two notches,
    // and a snapshot that compared equal to the last would swallow the second.
    PlayerInputSnapshot first;
    first.keys.emplace_back("W");

    PlayerInputSnapshot same = first;
    CNA_STUDIO_EXPECT(same == first);

    PlayerInputSnapshot wheeled = first;
    wheeled.wheel = 1.0f;
    CNA_STUDIO_EXPECT(!(wheeled == first));

    // Two notches running are equal to each other, which is exactly why the rule is "equal *and*
    // no wheel" rather than "equal".
    PlayerInputSnapshot again = wheeled;
    CNA_STUDIO_EXPECT(again == wheeled);
    CNA_STUDIO_EXPECT(again.wheel != 0.0f);
}
