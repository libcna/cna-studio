// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioInputRouter.hpp
 * @brief Turns a frame of raw input into per-widget interaction, with capture, focus and layering.
 *
 * `plan.md` STUDIO-03007, STUDIO-03008, STUDIO-03009, STUDIO-03010, STUDIO-03011.
 *
 * `UiInputState` is a *level* snapshot: where the mouse is, which buttons and keys are down right
 * now. Almost nothing a UI wants to know is a level. "Was this button clicked" is an edge, and an
 * edge that belongs to one particular widget: the one the press started in, which is not
 * necessarily the one the pointer is over when the button comes up. This class is where levels
 * become edges and edges become ownership.
 *
 * ### The three pieces of state that make interaction correct
 *
 * **Hover** is "the pointer is over this widget and nothing is stopping it". It respects the clip
 * stack, so a row scrolled out of a panel is not hovered even though its rectangle still contains
 * the pointer — the single most common source of an editor responding to a click on something the
 * user cannot see.
 *
 * **Active** is capture: the widget a press started in. It keeps receiving the drag even when the
 * pointer leaves its bounds, and *no other widget* can be hovered while it is held. Without this,
 * dragging a splitter past its neighbour hands the drag to the neighbour mid-gesture.
 *
 * **Focus** is the keyboard's target, and it is deliberately independent of both. A focused row
 * inside a selection that the pointer is elsewhere on is an ordinary state in an editor, and the
 * three must be separately representable to draw it.
 *
 * ### Input layers, and why the caller declares the blocking one
 *
 * A modal must stop input reaching the panels beneath it. The obvious implementation — notice a
 * modal was described this frame and block everything after it — leaks a frame: the panels were
 * described *before* the modal and already took their input.
 *
 * So the caller states the blocking layer at the start of the frame, before anything is described.
 * It already knows: whether a modal is open is application state, not something to infer from
 * drawing order. Widgets declare which layer they are in, and only the blocking layer routes.
 */

#include "CNA/Studio/Ui/UiInputState.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"
#include "CNA/Studio/UiCore/WidgetId.hpp"

#include <cstddef>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief What happened to one widget this frame.
     *
     * `pressed`, `released` and `clicked` are distinct because they are genuinely different
     * questions. A button acts on `clicked` — press and release both inside it. A drag starts on
     * `pressed`. A menu that opens on press and commits on release needs all three.
     */
    struct StudioInteraction
    {
        /** @brief The pointer is over this widget, unobstructed and unclipped. */
        bool hovered = false;
        /** @brief The primary button went down on this widget this frame. */
        bool pressed = false;
        /** @brief This widget holds the mouse: a drag is in progress. */
        bool held = false;
        /** @brief The primary button came up while this widget held it. */
        bool released = false;
        /** @brief Press and release both happened on this widget: a completed click. */
        bool clicked = false;
        /** @brief The secondary button completed a click on this widget. */
        bool rightClicked = false;
        /** @brief This widget has keyboard focus. */
        bool focused = false;
        /** @brief This widget is not interactive. */
        bool disabled = false;

        /**
         * @brief The theme state this interaction corresponds to.
         *
         * So a widget asks the theme for "the background of the state I am in" rather than
         * deciding for itself that pressed means darker.
         *
         * @return The control state to resolve theme colours with.
         */
        [[nodiscard]] StudioControlState state() const;
    };

    /**
     * @brief Routes one frame of input to widgets.
     *
     * Lives across frames: hover, capture and focus are all state that must persist between them.
     */
    class StudioInputRouter
    {
    public:
        StudioInputRouter();

        /**
         * @brief Starts a frame.
         *
         * Computes edges by comparing @p input with the previous frame's snapshot, so the caller
         * supplies only levels and never has to track "was this down last time" itself.
         *
         * @param input This frame's raw input.
         * @param blockingLayer The layer that receives input. Zero is the ordinary UI; raise it
         *        while a modal is open. Declared here rather than inferred from drawing order,
         *        because inferring it leaks a frame of input to the panels underneath.
         */
        void beginFrame(const UiInputState& input, int blockingLayer = 0);

        /** @brief Ends the frame, resolving focus navigation. */
        void endFrame();

        /** @brief This frame's raw input. */
        [[nodiscard]] const UiInputState& input() const { return current_; }

        /** @brief Pointer x in logical units. */
        [[nodiscard]] float mouseX() const { return current_.mouseX; }
        /** @brief Pointer y in logical units. */
        [[nodiscard]] float mouseY() const { return current_.mouseY; }
        /** @brief How far the pointer moved since the previous frame, on x. */
        [[nodiscard]] float mouseDeltaX() const { return current_.mouseX - previous_.mouseX; }
        /** @brief How far the pointer moved since the previous frame, on y. */
        [[nodiscard]] float mouseDeltaY() const { return current_.mouseY - previous_.mouseY; }

        /**
         * @brief Whether a mouse button went down this frame.
         * @param button Button to test.
         * @return True on the frame the button changed from up to down.
         */
        [[nodiscard]] bool mousePressed(UiMouseButton button) const;

        /**
         * @brief Whether a mouse button came up this frame.
         * @param button Button to test.
         * @return True on the frame the button changed from down to up.
         */
        [[nodiscard]] bool mouseReleased(UiMouseButton button) const;

        /**
         * @brief Whether a mouse button is currently held.
         * @param button Button to test.
         * @return True while the button is down.
         */
        [[nodiscard]] bool mouseDown(UiMouseButton button) const;

        /**
         * @brief Whether a key went down this frame.
         * @param key Key to test.
         * @return True on the frame the key changed from up to down.
         */
        [[nodiscard]] bool keyPressed(UiKey key) const;

        /**
         * @brief Whether a key is currently held.
         * @param key Key to test.
         * @return True while the key is down.
         */
        [[nodiscard]] bool keyDown(UiKey key) const;

        /** @brief The modifier keys held this frame. */
        [[nodiscard]] UiKeyModifiers modifiers() const;

        /** @brief Vertical wheel movement this frame. */
        [[nodiscard]] float wheelY() const { return current_.wheelY; }
        /** @brief Horizontal wheel movement this frame. */
        [[nodiscard]] float wheelX() const { return current_.wheelX; }

        // --- Clipping ---------------------------------------------------------------------------

        /**
         * @brief Pushes a hit-testing clip rectangle, intersected with the one in force.
         *
         * Kept deliberately parallel to `StudioDrawList::pushClip`: what can be *clicked* and what
         * can be *seen* have to be the same region, and the surest way to make them diverge is to
         * let one be derived and the other stated.
         *
         * @param rect Clip rectangle.
         */
        void pushClip(const UiRect& rect);

        /** @brief Pops the innermost hit-testing clip. */
        void popClip();

        /** @brief The hit-testing clip currently in force. */
        [[nodiscard]] UiRect currentClip() const;

        // --- Layers -----------------------------------------------------------------------------

        /**
         * @brief Enters an input layer, e.g. a popup or a modal.
         * @param layer Layer index; higher is nearer the user.
         */
        void pushLayer(int layer);

        /** @brief Leaves the current input layer. */
        void popLayer();

        /** @brief The layer currently being described. */
        [[nodiscard]] int currentLayer() const { return layers_.back(); }

        /** @brief The layer that receives input this frame. */
        [[nodiscard]] int blockingLayer() const { return blockingLayer_; }

        // --- Interaction ------------------------------------------------------------------------

        /**
         * @brief Routes input to one widget and reports what happened to it.
         *
         * @param id The widget's identity.
         * @param bounds Its rectangle, in logical units.
         * @param enabled False for a widget that must not respond. A disabled widget is still
         *        reported as disabled so it can be drawn that way, and still blocks widgets
         *        beneath it — a disabled button that lets clicks through to what it covers is
         *        worse than one that does nothing.
         * @return What happened to it this frame.
         */
        StudioInteraction interact(WidgetId id, const UiRect& bounds, bool enabled = true);

        /** @brief The widget the pointer is currently over, or the invalid id. */
        [[nodiscard]] WidgetId hoveredId() const { return hovered_; }

        /** @brief The widget holding the mouse, or the invalid id. */
        [[nodiscard]] WidgetId activeId() const { return active_; }

        /**
         * @brief Gives a widget the mouse explicitly.
         *
         * For gestures that do not begin with a press inside a rectangle — a splitter drag started
         * from a hit zone wider than the splitter is drawn, for instance.
         *
         * @param id Widget to capture with.
         */
        void setCapture(WidgetId id);

        /** @brief Releases the mouse. */
        void releaseCapture();

        // --- Focus ------------------------------------------------------------------------------

        /** @brief The focused widget, or the invalid id. */
        [[nodiscard]] WidgetId focusedId() const { return focused_; }

        /**
         * @brief Moves keyboard focus.
         * @param id Widget to focus, or the invalid id to clear focus.
         */
        void setFocus(WidgetId id);

        /**
         * @brief Declares a widget as reachable by Tab, in tab order.
         *
         * Order comes from declaration order, which is the order the UI is described in, which is
         * the reading order. Deriving it from geometry instead sounds better and is worse: it
         * breaks the moment a layout is anything other than a single column.
         *
         * @param id Widget to register.
         * @param enabled False to skip it; a disabled control is not a tab stop.
         */
        void registerFocusable(WidgetId id, bool enabled = true);

        /**
         * @brief Whether the caller should consume text input rather than treating it as a shortcut.
         * @return True while a text-editing widget holds focus.
         */
        [[nodiscard]] bool wantsTextInput() const { return wantsTextInput_; }

        /**
         * @brief Declares that the focused widget is editing text this frame.
         * @param editing True while a text field holds focus.
         */
        void setWantsTextInput(bool editing) { wantsTextInput_ = editing; }

    private:
        [[nodiscard]] bool layerAcceptsInput() const;
        [[nodiscard]] bool pointerInside(const UiRect& bounds) const;

        UiInputState current_;
        UiInputState previous_;

        std::vector<UiRect> clips_;
        std::vector<int> layers_;
        int blockingLayer_ = 0;

        WidgetId hovered_;
        WidgetId active_;
        WidgetId pressedIn_;
        WidgetId focused_;

        std::vector<WidgetId> focusables_;
        bool wantsTextInput_ = false;
        bool focusMoveRequested_ = false;
        bool focusMoveBackwards_ = false;
        bool hasFrame_ = false;
    };
} // namespace CNA::Studio
