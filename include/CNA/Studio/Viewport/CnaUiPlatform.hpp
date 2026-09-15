// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Viewport/CnaUiPlatform.hpp
 * @brief Fills a UiInputState from CNA's public input API.
 *
 * The input half of what CnaUiRenderer does for output. Between them they answer ANALYSIS.md
 * question Q-01 in full: not just "can CNA draw a UI" but "can CNA drive one".
 *
 * | An editor UI needs      | CNA public API used                                              |
 * |-------------------------|------------------------------------------------------------------|
 * | Cursor position, buttons| `Mouse::GetState()`                                              |
 * | Vertical scroll         | `MouseState::getScrollWheelValueProperty` (cumulative; differenced here) |
 * | Horizontal scroll       | `MouseState::getHorizontalScrollWheelValueEXTProperty`           |
 * | Key state               | `Keyboard::GetState()`, `IsKeyDown`                              |
 * | Typed characters, IME   | `TextInputEXT::TextInput` + `StartTextInput`/`StopTextInput`     |
 * | Clipboard               | `CNA::Devices::Clipboard` (optional; needs CNA_DEVICES)          |
 *
 * The one thing worth stating plainly: printable characters come from `TextInputEXT`, never from
 * key codes. Synthesising them from `Keyboard::GetState()` plus a shift flag is the traditional
 * shortcut and it is wrong for every non-US layout and for every IME -- which, for a project whose
 * author writes Czech, is not a hypothetical.
 */

#include <memory>
#include <string>
#include <vector>

#include "CNA/Studio/Ui/UiInputState.hpp"

namespace CNA::Studio
{
    /**
     * @brief Polls CNA input into a UiInputState.
     *
     * Construct one per editor window. Subscribing to `TextInputEXT::TextInput` happens in the
     * constructor and is undone in the destructor, so a stale subscriber cannot outlive the
     * platform object and start feeding characters into a destroyed UI.
     */
    /**
     * @brief One entry of the map from Studio's key vocabulary to CNA's.
     *
     * CNA's `Keys` is held as its underlying integer rather than the enumeration itself, so this
     * header stays free of CNA's input headers. Every other Studio header that mentions CNA does
     * the same, and `docs/ARCHITECTURE.md` §2 is why: exactly one module links CNA, and a header
     * that pulled its types in would quietly widen that to everything that includes it.
     */
    struct CnaUiPlatformKeyBinding
    {
        UiKey uiKey;
        int xnaKey = 0;
    };

    /**
     * @brief The whole key map, exposed so a test can check it against the vocabulary.
     *
     * A key Studio can ask about and this table does not carry is a shortcut that never fires, with
     * nothing on screen to see and no error anywhere. Two were missing — the 2D/3D view toggles —
     * until a guard test compared the two lists.
     *
     * @return Every mapping, in declaration order.
     */
    [[nodiscard]] const std::vector<CnaUiPlatformKeyBinding>& cnaUiPlatformKeyBindings();

    class CnaUiPlatform
    {
    public:
        CnaUiPlatform();
        ~CnaUiPlatform();

        CnaUiPlatform(const CnaUiPlatform&) = delete;
        CnaUiPlatform& operator=(const CnaUiPlatform&) = delete;

        /**
         * @brief Returns this frame's input.
         *
         * @param displayWidth Window width in logical units.
         * @param displayHeight Window height in logical units.
         * @param deltaSeconds Seconds since the previous frame.
         */
        [[nodiscard]] UiInputState poll(float displayWidth, float displayHeight, float deltaSeconds);

        /**
         * @brief Starts or stops OS text input, following whether the UI wants keyboard text.
         *
         * Driven from `ImGuiStudioUi`'s "a text field has focus" state. Leaving text input active
         * permanently would keep a mobile on-screen keyboard up for the whole session.
         */
        void setTextInputActive(bool active);

        /** @brief Marks the window as closing, so the next poll reports quitRequested. */
        void requestQuit();

        /** @brief Returns true when CNA's clipboard extension is available in this build. */
        [[nodiscard]] static bool hasClipboard();

        /** @brief Returns the clipboard's text, or an empty string when unavailable. */
        [[nodiscard]] static std::string getClipboardText();

        /** @brief Sets the clipboard's text. Does nothing when unavailable. */
        static void setClipboardText(const std::string& text);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
