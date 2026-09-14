// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Ui/UiInputState.hpp
 * @brief One frame of input, described without reference to any windowing or input library.
 *
 * The mirror image of UiDrawData: that carries geometry *out* of the UI toolkit, this carries
 * events *in*. `CnaUiPlatform` fills it from CNA's `Mouse`, `Keyboard` and `TextInputEXT`;
 * `ImGuiEditorUi` consumes it. Neither includes the other's headers.
 *
 * That keeps the ImGui binding buildable and testable with no CNA checkout at all -- a test can
 * synthesise clicks and keystrokes, run the real editor UI, and assert on the draw data that
 * comes out, on a machine with no window system.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Editor
{
    /** @brief Mouse buttons the editor reacts to. */
    enum class UiMouseButton
    {
        Left,
        Right,
        Middle,
        Count
    };

    /**
     * @brief Keys the editor binds shortcuts or navigation to.
     *
     * Deliberately not an exhaustive keyboard map. Printable characters arrive as text
     * (`characters` below) rather than as key codes, because that is the only way to get layout
     * and IME handling right; this enumeration therefore only needs the keys that *act* rather
     * than type.
     */
    enum class UiKey
    {
        None,
        Tab, LeftArrow, RightArrow, UpArrow, DownArrow,
        PageUp, PageDown, Home, End,
        Insert, Delete, Backspace, Space, Enter, Escape,
        A, C, V, X, Y, Z,
        D, F, N, Q, S, W, E, R,
        F1, F2, F5,

        // Digits, named rather than spelt, because an enumerator cannot begin with one. Appended
        // rather than inserted, like every other addition to a list something else counts through.
        Digit2, Digit3,
        Count
    };

    /** @brief Modifier keys held during this frame. */
    struct UiKeyModifiers
    {
        bool control = false;
        bool shift = false;
        bool alt = false;
        /** @brief Command on macOS, Windows key elsewhere. */
        bool super = false;

        friend bool operator==(const UiKeyModifiers& lhs, const UiKeyModifiers& rhs)
        {
            return lhs.control == rhs.control && lhs.shift == rhs.shift
                && lhs.alt == rhs.alt && lhs.super == rhs.super;
        }
    };

    /** @brief Returns the modifiers for a plain Ctrl-key shortcut. */
    [[nodiscard]] inline UiKeyModifiers withControl()
    {
        UiKeyModifiers modifiers;
        modifiers.control = true;
        return modifiers;
    }

    /**
     * @brief A complete input snapshot for one frame.
     *
     * Mouse position and wheel are *absolute* state; key and button presses are also absolute
     * (held or not), because an immediate-mode UI derives edges itself. Text is the exception:
     * characters are events, and dropping one is a keystroke the user has to retype.
     */
    struct UiInputState
    {
        /** @brief Cursor position in logical units, relative to the drawing area's top-left. */
        float mouseX = 0.0f;
        float mouseY = 0.0f;

        /** @brief True when the cursor is inside the window at all. */
        bool mouseInWindow = true;

        /** @brief Held state per button, indexed by UiMouseButton. */
        bool mouseDown[static_cast<int>(UiMouseButton::Count)] = {};

        /** @brief Wheel movement *this frame*, in notches. Positive is away from the user. */
        float wheelX = 0.0f;
        float wheelY = 0.0f;

        /** @brief Held state per key, indexed by UiKey. */
        bool keyDown[static_cast<int>(UiKey::Count)] = {};

        UiKeyModifiers modifiers;

        /**
         * @brief Characters typed this frame, as UTF-16 code units.
         *
         * UTF-16 rather than UTF-8 because that is what CNA's `TextInputEXT::TextInput` delivers
         * (matching FNA's `Action<char>`), and re-encoding twice would be pure loss. Surrogate
         * pairs arrive as two consecutive units.
         */
        std::vector<char16_t> characters;

        /** @brief Drawing area size in logical units. */
        float displayWidth = 0.0f;
        float displayHeight = 0.0f;

        float framebufferScaleX = 1.0f;
        float framebufferScaleY = 1.0f;

        /** @brief Seconds since the previous frame. Must be greater than zero. */
        float deltaSeconds = 1.0f / 60.0f;

        /** @brief True when the user has asked to close the window. */
        bool quitRequested = false;

        /** @brief Clears the per-frame event fields, keeping absolute state. */
        void clearEvents();

        /** @brief Sets @p button's held state. */
        void setMouseDown(UiMouseButton button, bool down)
        {
            mouseDown[static_cast<int>(button)] = down;
        }

        /** @brief Returns @p button's held state. */
        [[nodiscard]] bool isMouseDown(UiMouseButton button) const
        {
            return mouseDown[static_cast<int>(button)];
        }

        /** @brief Sets @p key's held state. */
        void setKeyDown(UiKey key, bool down) { keyDown[static_cast<int>(key)] = down; }

        /** @brief Returns @p key's held state. */
        [[nodiscard]] bool isKeyDown(UiKey key) const { return keyDown[static_cast<int>(key)]; }

        /** @brief Appends the UTF-16 code units of a UTF-8 string. Convenience for tests. */
        void appendUtf8(std::string_view text);
    };
}
