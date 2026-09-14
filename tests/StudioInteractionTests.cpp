// SPDX-License-Identifier: MS-PL
/**
 * @file StudioInteractionTests.cpp
 * @brief Input routing, hover, capture, focus and the command registry.
 *
 * All headless. Whether a click reaches the right widget, whether a drag survives leaving its
 * bounds, and whether a modal blocks the panels beneath it are questions about logic, not about
 * pixels — and they are exactly the questions a screenshot cannot answer.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioActionRegistry.hpp"
#include "CNA/Studio/UiCore/StudioInputRouter.hpp"
#include "CNA/Studio/UiCore/WidgetId.hpp"

#include <set>
#include <string>

using namespace CNA::Studio;

namespace
{
    /** @brief Builds an input snapshot with the pointer somewhere and optionally a button down. */
    UiInputState at(float x, float y, bool leftDown = false, bool rightDown = false)
    {
        UiInputState input;
        input.displayWidth = 800.0f;
        input.displayHeight = 600.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        input.setMouseDown(UiMouseButton::Right, rightDown);
        return input;
    }

    const UiRect kButton{10.0f, 10.0f, 100.0f, 30.0f};
    const WidgetId kA{101};
    const WidgetId kB{202};
}

// ------------------------------------------------------------------------------------------------
// Hover and clicking (STUDIO-03009)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AWidgetUnderThePointerIsHovered)
{
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    const StudioInteraction result = router.interact(kA, kButton);
    router.endFrame();

    CNA_STUDIO_EXPECT(result.hovered);
    CNA_STUDIO_EXPECT(!result.pressed);
    CNA_STUDIO_EXPECT(router.hoveredId() == kA);
}

CNA_STUDIO_TEST(AWidgetElsewhereIsNotHovered)
{
    StudioInputRouter router;
    router.beginFrame(at(500.0f, 500.0f));
    CNA_STUDIO_EXPECT(!router.interact(kA, kButton).hovered);
}

CNA_STUDIO_TEST(NothingIsPressedOnTheFirstFrameWithAButtonAlreadyDown)
{
    // A user who launched Studio by double-clicking still has the button down as the first frame
    // runs. Diffing against a default-constructed previous state would synthesise a press and
    // deliver a phantom click into their scene.
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f, /*leftDown=*/true));
    const StudioInteraction result = router.interact(kA, kButton);

    CNA_STUDIO_EXPECT(!result.pressed);
    CNA_STUDIO_EXPECT(!result.clicked);
}

CNA_STUDIO_TEST(AClickIsPressAndReleaseOnTheSameWidget)
{
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(50.0f, 20.0f, true));
    const StudioInteraction pressed = router.interact(kA, kButton);
    router.endFrame();
    CNA_STUDIO_EXPECT(pressed.pressed);
    CNA_STUDIO_EXPECT(pressed.held);
    CNA_STUDIO_EXPECT(!pressed.clicked);

    router.beginFrame(at(50.0f, 20.0f, false));
    const StudioInteraction released = router.interact(kA, kButton);
    router.endFrame();
    CNA_STUDIO_EXPECT(released.released);
    CNA_STUDIO_EXPECT(released.clicked);
}

CNA_STUDIO_TEST(SlidingOffBeforeReleasingCancelsTheClick)
{
    // The affordance that lets a user press a button, think better of it, and slide away. A UI
    // that fires on press alone takes that away.
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(50.0f, 20.0f, true));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(500.0f, 500.0f, false));
    const StudioInteraction result = router.interact(kA, kButton);
    router.endFrame();

    CNA_STUDIO_EXPECT(result.released);
    CNA_STUDIO_EXPECT(!result.clicked);
}

// ------------------------------------------------------------------------------------------------
// Clipping (STUDIO-03011)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AWidgetClippedOutOfViewCannotBeHovered)
{
    // The single most common way an editor feels haunted: a row scrolled out of a panel still has
    // a rectangle containing the pointer, and responds to a click on something invisible.
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    router.pushClip(UiRect{0.0f, 100.0f, 800.0f, 200.0f});   // the button is above this
    const StudioInteraction result = router.interact(kA, kButton);
    router.popClip();
    router.endFrame();

    CNA_STUDIO_EXPECT(!result.hovered);
}

CNA_STUDIO_TEST(HitTestingClipsCompose)
{
    StudioInputRouter router;
    router.beginFrame(at(60.0f, 60.0f));
    router.pushClip(UiRect{0.0f, 0.0f, 100.0f, 100.0f});
    router.pushClip(UiRect{50.0f, 50.0f, 100.0f, 100.0f});
    CNA_STUDIO_EXPECT(router.currentClip() == UiRect(50.0f, 50.0f, 50.0f, 50.0f));

    CNA_STUDIO_EXPECT(router.interact(kA, UiRect{55.0f, 55.0f, 20.0f, 20.0f}).hovered);
    router.popClip();
    router.popClip();
    router.endFrame();
}

// ------------------------------------------------------------------------------------------------
// Capture (STUDIO-03010)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ADragKeepsReceivingInputAfterLeavingItsBounds)
{
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(50.0f, 20.0f, true));
    router.interact(kA, kButton);
    router.endFrame();

    // Pointer well outside the widget, button still down.
    router.beginFrame(at(700.0f, 400.0f, true));
    const StudioInteraction result = router.interact(kA, kButton);
    router.endFrame();

    CNA_STUDIO_EXPECT(result.held);
    CNA_STUDIO_EXPECT(!result.hovered);
    CNA_STUDIO_EXPECT(router.activeId() == kA);
}

CNA_STUDIO_TEST(NothingElseCanBeHoveredWhileAWidgetHoldsTheMouse)
{
    // Without this, dragging a splitter past its neighbour hands the gesture to the neighbour
    // halfway through.
    StudioInputRouter router;
    const UiRect neighbour{200.0f, 10.0f, 100.0f, 30.0f};

    router.beginFrame(at(50.0f, 20.0f));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(50.0f, 20.0f, true));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(250.0f, 20.0f, true));
    router.interact(kA, kButton);
    const StudioInteraction other = router.interact(kB, neighbour);
    router.endFrame();

    CNA_STUDIO_EXPECT(!other.hovered);
    CNA_STUDIO_EXPECT(!other.pressed);
}

CNA_STUDIO_TEST(CaptureIsReleasedEvenWhenTheHoldingWidgetIsNeverDescribedAgain)
{
    // Its panel closed, or its row scrolled out of a virtualised list. A capture nobody releases
    // is a UI that has silently stopped responding to the mouse.
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(50.0f, 20.0f, true));
    router.interact(kA, kButton);
    router.endFrame();
    CNA_STUDIO_EXPECT(router.activeId() == kA);

    // The widget is simply not described this frame, and the button comes up.
    router.beginFrame(at(50.0f, 20.0f, false));
    router.endFrame();

    CNA_STUDIO_EXPECT(router.activeId() == kInvalidWidgetId);
    CNA_STUDIO_EXPECT(router.interact(kB, kButton).hovered);
}

CNA_STUDIO_TEST(CaptureCanBeTakenExplicitly)
{
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f, true));
    router.setCapture(kA);
    CNA_STUDIO_EXPECT(router.activeId() == kA);
    router.releaseCapture();
    CNA_STUDIO_EXPECT(router.activeId() == kInvalidWidgetId);
}

// ------------------------------------------------------------------------------------------------
// Disabled widgets
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ADisabledWidgetDoesNotRespondButStillReportsItself)
{
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f, true));
    const StudioInteraction result = router.interact(kA, kButton, /*enabled=*/false);
    router.endFrame();

    CNA_STUDIO_EXPECT(result.disabled);
    CNA_STUDIO_EXPECT(!result.hovered);
    CNA_STUDIO_EXPECT(!result.pressed);
    CNA_STUDIO_EXPECT(result.state() == StudioControlState::Disabled);
}

CNA_STUDIO_TEST(ADisabledWidgetStillBlocksWhatIsBeneathIt)
{
    // A disabled button that lets a click through to what it covers is worse than one that does
    // nothing: the user aimed at the button and hit something else.
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    router.interact(kA, kButton, /*enabled=*/false);
    router.endFrame();

    CNA_STUDIO_EXPECT(router.hoveredId() == kInvalidWidgetId);
}

CNA_STUDIO_TEST(InteractionStatesMapToThemeStatesInPriorityOrder)
{
    StudioInteraction disabledAndHovered;
    disabledAndHovered.disabled = true;
    disabledAndHovered.hovered = true;
    // Disabled wins: a disabled control that highlights is promising a response it will not give.
    CNA_STUDIO_EXPECT(disabledAndHovered.state() == StudioControlState::Disabled);

    StudioInteraction heldAndHovered;
    heldAndHovered.held = true;
    heldAndHovered.hovered = true;
    CNA_STUDIO_EXPECT(heldAndHovered.state() == StudioControlState::Pressed);
}

// ------------------------------------------------------------------------------------------------
// Layers and modals
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AModalBlocksInputToThePanelsBeneathIt)
{
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f, true), /*blockingLayer=*/1);

    const StudioInteraction beneath = router.interact(kA, kButton);

    router.pushLayer(1);
    const StudioInteraction inModal = router.interact(kB, kButton);
    router.popLayer();
    router.endFrame();

    CNA_STUDIO_EXPECT(!beneath.hovered);
    CNA_STUDIO_EXPECT(!beneath.pressed);
    CNA_STUDIO_EXPECT(inModal.hovered);
}

CNA_STUDIO_TEST(TheBlockingLayerIsDeclaredUpFrontSoNoInputLeaksFirst)
{
    // The reason the caller states the blocking layer rather than the router inferring it from
    // drawing order: panels are described BEFORE the modal, so an inferred barrier arrives one
    // widget too late and the panel underneath has already taken the click.
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f, true), 1);
    const StudioInteraction firstDescribed = router.interact(kA, kButton);
    router.endFrame();

    CNA_STUDIO_EXPECT(!firstDescribed.pressed);
}

// ------------------------------------------------------------------------------------------------
// Focus (STUDIO-03007, STUDIO-03008)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ClickingAWidgetFocusesIt)
{
    StudioInputRouter router;
    router.beginFrame(at(50.0f, 20.0f));
    router.interact(kA, kButton);
    router.endFrame();

    router.beginFrame(at(50.0f, 20.0f, true));
    const StudioInteraction result = router.interact(kA, kButton);
    router.endFrame();

    CNA_STUDIO_EXPECT(router.focusedId() == kA);
    CNA_STUDIO_EXPECT(result.pressed);
}

CNA_STUDIO_TEST(FocusIsIndependentOfHoverAndCapture)
{
    // A focused row inside a selection, with the pointer somewhere else entirely, is an ordinary
    // state in an editor. All three must be separately representable to draw it.
    StudioInputRouter router;
    router.beginFrame(at(500.0f, 500.0f));
    router.setFocus(kA);
    const StudioInteraction result = router.interact(kA, kButton);
    router.endFrame();

    CNA_STUDIO_EXPECT(result.focused);
    CNA_STUDIO_EXPECT(!result.hovered);
    CNA_STUDIO_EXPECT(router.activeId() == kInvalidWidgetId);
}

CNA_STUDIO_TEST(TabMovesFocusInDeclarationOrder)
{
    StudioInputRouter router;
    const WidgetId first{1}, second{2}, third{3};

    UiInputState tab = at(0.0f, 0.0f);
    tab.setKeyDown(UiKey::Tab, true);

    router.beginFrame(at(0.0f, 0.0f));
    router.setFocus(first);
    router.registerFocusable(first);
    router.registerFocusable(second);
    router.registerFocusable(third);
    router.endFrame();
    CNA_STUDIO_EXPECT(router.focusedId() == first);

    router.beginFrame(tab);
    router.registerFocusable(first);
    router.registerFocusable(second);
    router.registerFocusable(third);
    router.endFrame();
    CNA_STUDIO_EXPECT(router.focusedId() == second);
}

CNA_STUDIO_TEST(TabWrapsAroundAndShiftTabGoesBackwards)
{
    StudioInputRouter router;
    const WidgetId first{1}, second{2};

    UiInputState tab = at(0.0f, 0.0f);
    tab.setKeyDown(UiKey::Tab, true);
    UiInputState shiftTab = tab;
    shiftTab.modifiers.shift = true;

    const auto describe = [&](const UiInputState& input) {
        router.beginFrame(input);
        router.registerFocusable(first);
        router.registerFocusable(second);
        router.endFrame();
    };

    describe(at(0.0f, 0.0f));
    router.setFocus(second);

    describe(tab);
    CNA_STUDIO_EXPECT(router.focusedId() == first);   // wrapped

    describe(at(0.0f, 0.0f));                          // release Tab so the next press is an edge
    describe(shiftTab);
    CNA_STUDIO_EXPECT(router.focusedId() == second);   // wrapped backwards
}

CNA_STUDIO_TEST(ADisabledWidgetIsNotATabStop)
{
    StudioInputRouter router;
    const WidgetId first{1}, disabled{2}, third{3};

    UiInputState tab = at(0.0f, 0.0f);
    tab.setKeyDown(UiKey::Tab, true);

    router.beginFrame(at(0.0f, 0.0f));
    router.setFocus(first);
    router.endFrame();

    router.beginFrame(tab);
    router.registerFocusable(first);
    router.registerFocusable(disabled, /*enabled=*/false);
    router.registerFocusable(third);
    router.endFrame();

    CNA_STUDIO_EXPECT(router.focusedId() == third);
}

CNA_STUDIO_TEST(TabStillMovesAfterTheFocusedWidgetDisappears)
{
    // Closing the panel that held focus must not leave Tab doing nothing.
    StudioInputRouter router;
    const WidgetId gone{99}, first{1};

    UiInputState tab = at(0.0f, 0.0f);
    tab.setKeyDown(UiKey::Tab, true);

    router.beginFrame(at(0.0f, 0.0f));
    router.setFocus(gone);
    router.endFrame();

    router.beginFrame(tab);
    router.registerFocusable(first);
    router.endFrame();

    CNA_STUDIO_EXPECT(router.focusedId() == first);
}

// ------------------------------------------------------------------------------------------------
// Command registry (STUDIO-06001, STUDIO-06002)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ACommandRunsThroughTheRegistry)
{
    StudioActionRegistry registry;
    int ran = 0;

    StudioAction save;
    save.id = "test.save";
    save.label = "Save";
    save.run = [&] { ++ran; };
    registry.add(std::move(save));

    CNA_STUDIO_EXPECT(registry.invoke("test.save") == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT_EQ(ran, 1);
    CNA_STUDIO_EXPECT(registry.invoke("test.missing") == StudioActionResult::NotFound);
}

CNA_STUDIO_TEST(EnablementIsCheckedByTheRegistryNotByEachCaller)
{
    // Every surface would otherwise have to remember, and the one that forgets is the one that
    // runs Save on a project that is not open.
    StudioActionRegistry registry;
    bool allowed = false;
    int ran = 0;

    StudioAction save;
    save.id = "test.save";
    save.isEnabled = [&] { return allowed; };
    save.run = [&] { ++ran; };
    registry.add(std::move(save));

    CNA_STUDIO_EXPECT(registry.invoke("test.save") == StudioActionResult::Disabled);
    CNA_STUDIO_EXPECT_EQ(ran, 0);

    allowed = true;
    CNA_STUDIO_EXPECT(registry.invoke("test.save") == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT_EQ(ran, 1);
}

CNA_STUDIO_TEST(EnablementIsPulledSoItCannotGoStale)
{
    StudioActionRegistry registry;
    int documents = 0;

    StudioAction close;
    close.id = "test.close";
    close.isEnabled = [&] { return documents > 0; };
    close.run = [] {};
    registry.add(std::move(close));

    CNA_STUDIO_EXPECT(!registry.isEnabled("test.close"));
    documents = 1;
    CNA_STUDIO_EXPECT(registry.isEnabled("test.close"));
    documents = 0;
    CNA_STUDIO_EXPECT(!registry.isEnabled("test.close"));
}

CNA_STUDIO_TEST(ACommandWithNoHandlerSaysSoRatherThanSilentlySucceeding)
{
    // An unimplemented action that reports success is indistinguishable from a broken one.
    StudioActionRegistry registry;
    StudioAction planned;
    planned.id = "test.planned";
    registry.add(std::move(planned));

    CNA_STUDIO_EXPECT(registry.invoke("test.planned") == StudioActionResult::NotImplemented);
}

CNA_STUDIO_TEST(AShortcutInvokesTheSameCommandTheMenuDoes)
{
    StudioActionRegistry registry;
    int ran = 0;

    StudioAction save;
    save.id = "test.save";
    save.shortcut.key = UiKey::S;
    save.shortcut.modifiers.control = true;
    save.run = [&] { ++ran; };
    registry.add(std::move(save));

    StudioShortcut pressed;
    pressed.key = UiKey::S;
    pressed.modifiers.control = true;

    CNA_STUDIO_EXPECT(registry.invokeShortcut(pressed) == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT_EQ(ran, 1);

    // The same key without the modifier is a different chord and must not fire it.
    StudioShortcut bare;
    bare.key = UiKey::S;
    CNA_STUDIO_EXPECT(registry.invokeShortcut(bare) == StudioActionResult::NotFound);
}

CNA_STUDIO_TEST(RebindingRefusesAChordAlreadyInUse)
{
    // Two commands on one chord means one of them has stopped working, and the user who bound the
    // second has no way to discover which.
    StudioActionRegistry registry;
    registerCoreStudioActions(registry);

    StudioShortcut ctrlS;
    ctrlS.key = UiKey::S;
    ctrlS.modifiers.control = true;

    std::string conflict;
    CNA_STUDIO_EXPECT(!registry.rebind("studio.edit.undo", ctrlS, &conflict));
    CNA_STUDIO_EXPECT_EQ(conflict, std::string{"studio.file.save"});

    // Rebinding a command to the chord it already holds is not a conflict with itself.
    CNA_STUDIO_EXPECT(registry.rebind("studio.file.save", ctrlS));
}

CNA_STUDIO_TEST(RegisteringAnIdTwiceReplacesRatherThanShadows)
{
    StudioActionRegistry registry;
    int first = 0, second = 0;

    StudioAction a;
    a.id = "test.action";
    a.run = [&] { ++first; };
    registry.add(std::move(a));

    StudioAction b;
    b.id = "test.action";
    b.run = [&] { ++second; };
    CNA_STUDIO_EXPECT(registry.add(std::move(b)));

    registry.invoke("test.action");
    CNA_STUDIO_EXPECT_EQ(registry.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(first, 0);
    CNA_STUDIO_EXPECT_EQ(second, 1);
}

CNA_STUDIO_TEST(AHandlerMayRegisterMoreCommandsWithoutCrashing)
{
    // A plugin loading, or a tool installing its own actions. Registering reallocates the command
    // vector, and invoking through a pointer into it would read freed memory -- intermittently,
    // and looking like anything but this.
    StudioActionRegistry registry;

    StudioAction loader;
    loader.id = "test.loadPlugin";
    loader.run = [&] {
        for (int i = 0; i < 64; ++i)
        {
            StudioAction added;
            added.id = "plugin.command." + std::to_string(i);
            added.run = [] {};
            registry.add(std::move(added));
        }
    };
    registry.add(std::move(loader));

    CNA_STUDIO_EXPECT(registry.invoke("test.loadPlugin") == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT_EQ(registry.size(), std::size_t{65});
}

CNA_STUDIO_TEST(TheCoreCommandSetIsRegisteredAndCoherent)
{
    StudioActionRegistry registry;
    registerCoreStudioActions(registry);

    CNA_STUDIO_EXPECT(registry.size() >= 20);

    std::set<std::string> ids;
    for (const StudioAction& command : registry.commands())
    {
        // Ids are written into preferences and shortcut bindings; a duplicate would make one of
        // them unreachable.
        CNA_STUDIO_EXPECT(ids.insert(command.id).second);
        CNA_STUDIO_EXPECT(!command.label.empty());
        // A command with no description has no tooltip and no entry in the shortcut editor.
        CNA_STUDIO_EXPECT(!command.description.empty());
    }

    for (const char* expected : {"studio.file.save", "studio.edit.undo", "studio.edit.redo",
                                 "studio.view.focusSelected", "studio.play.play",
                                 "studio.build.build"})
    {
        CNA_STUDIO_EXPECT(registry.find(expected) != nullptr);
    }
}

CNA_STUDIO_TEST(NoTwoCoreCommandsShareAShortcut)
{
    StudioActionRegistry registry;
    registerCoreStudioActions(registry);

    std::vector<StudioShortcut> seen;
    for (const StudioAction& command : registry.commands())
    {
        if (!command.shortcut.isBound()) { continue; }
        for (const StudioShortcut& other : seen)
        {
            if (other == command.shortcut)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "two core commands share " + describeStudioShortcut(command.shortcut)
                    + "; one of them can never be reached from the keyboard");
            }
            CNA_STUDIO_EXPECT(!(other == command.shortcut));
        }
        seen.push_back(command.shortcut);
    }
}

CNA_STUDIO_TEST(EveryCoreCommandLandsInANamedMenu)
{
    StudioActionRegistry registry;
    registerCoreStudioActions(registry);

    std::size_t total = 0;
    for (const StudioActionCategory category :
         {StudioActionCategory::File, StudioActionCategory::Edit, StudioActionCategory::View,
          StudioActionCategory::Project, StudioActionCategory::Build, StudioActionCategory::Play,
          StudioActionCategory::Tools, StudioActionCategory::Window, StudioActionCategory::Help})
    {
        CNA_STUDIO_EXPECT(!studioActionCategoryName(category).empty());
        total += registry.inCategory(category).size();
    }
    // Every command is reachable from some menu. One that is in no category is invocable only by
    // a shortcut nobody can discover.
    CNA_STUDIO_EXPECT_EQ(total, registry.size());
}

CNA_STUDIO_TEST(ShortcutsRenderInTheConventionalOrder)
{
    StudioShortcut shortcut;
    shortcut.key = UiKey::S;
    shortcut.modifiers.control = true;
    shortcut.modifiers.shift = true;
    // Ctrl before Alt before Shift, as every platform's menus write it. Reordering makes a
    // familiar chord read as an unfamiliar one.
    CNA_STUDIO_EXPECT_EQ(describeStudioShortcut(shortcut), std::string{"Ctrl+Shift+S"});

    CNA_STUDIO_EXPECT(describeStudioShortcut(StudioShortcut{}).empty());
}
