// SPDX-License-Identifier: MS-PL
/**
 * @file CnaUiPlatform.cpp
 * @brief Drives the editor UI from CNA's public input API.
 *
 * See CnaUiPlatform.hpp for the full mapping. Everything here goes through
 * `Microsoft::Xna::Framework::Input::*` and `CNA::Devices::*`; nothing touches SDL or any
 * `CNA::Internal::*` header.
 */

#include "CNA/Editor/Viewport/CnaUiPlatform.hpp"

#include <mutex>
#include <vector>

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

// CNA_DEVICES is CNA's own feature macro, set by its build when the optional device/sensor
// extensions are enabled. Keying off it directly rather than off an editor-side mirror means the
// two can never disagree about whether the clipboard exists.
#if defined(CNA_DEVICES)
#    include "CNA/Devices/Clipboard.hpp"
#endif

namespace XnaInput = Microsoft::Xna::Framework::Input;

namespace CNA::Editor
{
    namespace
    {
        struct KeyBinding
        {
            UiKey uiKey;
            XnaInput::Keys xnaKey;
        };

        /**
         * @brief The keys the editor binds shortcuts or navigation to.
         *
         * Deliberately not an exhaustive keyboard map: printable characters arrive through
         * TextInputEXT, so this table only needs the keys that *act* rather than type.
         */
        const std::vector<KeyBinding>& keyBindings()
        {
            static const std::vector<KeyBinding> bindings{
                {UiKey::Tab, XnaInput::Keys::Tab},
                {UiKey::LeftArrow, XnaInput::Keys::Left},
                {UiKey::RightArrow, XnaInput::Keys::Right},
                {UiKey::UpArrow, XnaInput::Keys::Up},
                {UiKey::DownArrow, XnaInput::Keys::Down},
                {UiKey::PageUp, XnaInput::Keys::PageUp},
                {UiKey::PageDown, XnaInput::Keys::PageDown},
                {UiKey::Home, XnaInput::Keys::Home},
                {UiKey::End, XnaInput::Keys::End},
                {UiKey::Insert, XnaInput::Keys::Insert},
                {UiKey::Delete, XnaInput::Keys::Delete},
                {UiKey::Backspace, XnaInput::Keys::Back},
                {UiKey::Space, XnaInput::Keys::Space},
                {UiKey::Enter, XnaInput::Keys::Enter},
                {UiKey::Escape, XnaInput::Keys::Escape},
                {UiKey::A, XnaInput::Keys::A},
                {UiKey::C, XnaInput::Keys::C},
                {UiKey::V, XnaInput::Keys::V},
                {UiKey::X, XnaInput::Keys::X},
                {UiKey::Y, XnaInput::Keys::Y},
                {UiKey::Z, XnaInput::Keys::Z},
                {UiKey::D, XnaInput::Keys::D},
                {UiKey::F, XnaInput::Keys::F},
                {UiKey::N, XnaInput::Keys::N},
                {UiKey::S, XnaInput::Keys::S},
                {UiKey::W, XnaInput::Keys::W},
                {UiKey::Q, XnaInput::Keys::Q},
                {UiKey::E, XnaInput::Keys::E},
                {UiKey::R, XnaInput::Keys::R},
                {UiKey::F1, XnaInput::Keys::F1},
                {UiKey::F2, XnaInput::Keys::F2},
                {UiKey::F5, XnaInput::Keys::F5},
            };
            return bindings;
        }

        bool isPressed(XnaInput::ButtonState state)
        {
            return state == XnaInput::ButtonState::Pressed;
        }
    }

    struct CnaUiPlatform::Impl
    {
        /**
         * @brief Characters received since the last poll.
         *
         * Guarded because CNA dispatches TextInputEXT on the thread that pumps events, which is
         * not guaranteed to be the thread that polls. Losing a character is a keystroke the user
         * has to retype, so this is the one input path that buffers rather than sampling.
         */
        std::mutex characterMutex;
        std::vector<char16_t> pendingCharacters;

        System::MulticastAction<SharpRuntime::charcs>::Token textInputToken{};
        bool textInputSubscribed = false;
        bool textInputActive = false;
        bool quitRequested = false;

        /** @brief Cumulative wheel values from the previous poll, for differencing. */
        int previousScroll = 0;
        int previousHorizontalScroll = 0;
        bool haveScrollBaseline = false;
    };

    CnaUiPlatform::CnaUiPlatform() : impl_(std::make_unique<Impl>())
    {
        Impl* impl = impl_.get();
        impl->textInputToken = XnaInput::TextInputEXT::TextInput.Add([impl](SharpRuntime::charcs unit) {
            const std::lock_guard<std::mutex> lock{impl->characterMutex};
            impl->pendingCharacters.push_back(static_cast<char16_t>(unit));
        });
        impl->textInputSubscribed = true;
    }

    CnaUiPlatform::~CnaUiPlatform()
    {
        // Unsubscribing by token, not by clearing the event: clearing would also drop any
        // subscriber the game or another tool installed, and a dangling subscriber writing into
        // a destroyed buffer is exactly the failure this avoids.
        if (impl_->textInputSubscribed)
        {
            XnaInput::TextInputEXT::TextInput.Remove(impl_->textInputToken);
            impl_->textInputSubscribed = false;
        }
        if (impl_->textInputActive) { XnaInput::TextInputEXT::StopTextInput(); }
    }

    void CnaUiPlatform::setTextInputActive(bool active)
    {
        if (active == impl_->textInputActive) { return; }
        impl_->textInputActive = active;
        if (active) { XnaInput::TextInputEXT::StartTextInput(); }
        else { XnaInput::TextInputEXT::StopTextInput(); }
    }

    void CnaUiPlatform::requestQuit() { impl_->quitRequested = true; }

    UiInputState CnaUiPlatform::poll(float displayWidth, float displayHeight, float deltaSeconds)
    {
        UiInputState input;
        input.displayWidth = displayWidth;
        input.displayHeight = displayHeight;
        input.deltaSeconds = deltaSeconds > 0.0f ? deltaSeconds : 1.0f / 60.0f;
        input.quitRequested = impl_->quitRequested;

        const XnaInput::MouseState mouse = XnaInput::Mouse::GetState();
        input.mouseX = static_cast<float>(mouse.getXProperty());
        input.mouseY = static_cast<float>(mouse.getYProperty());
        input.mouseInWindow = input.mouseX >= 0.0f && input.mouseY >= 0.0f
                           && input.mouseX < displayWidth && input.mouseY < displayHeight;

        input.setMouseDown(UiMouseButton::Left, isPressed(mouse.getLeftButtonProperty()));
        input.setMouseDown(UiMouseButton::Right, isPressed(mouse.getRightButtonProperty()));
        input.setMouseDown(UiMouseButton::Middle, isPressed(mouse.getMiddleButtonProperty()));

        // XNA's wheel value is cumulative since the game started, so the per-frame delta has to
        // be differenced out. The first poll establishes the baseline and reports no movement --
        // otherwise the editor would scroll by however far the user had scrolled before it opened.
        const int scroll = mouse.getScrollWheelValueProperty();
        const int horizontalScroll = mouse.getHorizontalScrollWheelValueEXTProperty();
        if (impl_->haveScrollBaseline)
        {
            // 120 units per notch is the wheel convention XNA inherits from Windows.
            input.wheelY = static_cast<float>(scroll - impl_->previousScroll) / 120.0f;
            input.wheelX = static_cast<float>(horizontalScroll - impl_->previousHorizontalScroll) / 120.0f;
        }
        impl_->previousScroll = scroll;
        impl_->previousHorizontalScroll = horizontalScroll;
        impl_->haveScrollBaseline = true;

        const XnaInput::KeyboardState keyboard = XnaInput::Keyboard::GetState();
        for (const KeyBinding& binding : keyBindings())
        {
            input.setKeyDown(binding.uiKey, keyboard.IsKeyDown(binding.xnaKey));
        }

        input.modifiers.control = keyboard.IsKeyDown(XnaInput::Keys::LeftControl)
                               || keyboard.IsKeyDown(XnaInput::Keys::RightControl);
        input.modifiers.shift = keyboard.IsKeyDown(XnaInput::Keys::LeftShift)
                             || keyboard.IsKeyDown(XnaInput::Keys::RightShift);
        input.modifiers.alt = keyboard.IsKeyDown(XnaInput::Keys::LeftAlt)
                           || keyboard.IsKeyDown(XnaInput::Keys::RightAlt);
        input.modifiers.super = keyboard.IsKeyDown(XnaInput::Keys::LeftWindows)
                             || keyboard.IsKeyDown(XnaInput::Keys::RightWindows);

        {
            const std::lock_guard<std::mutex> lock{impl_->characterMutex};
            input.characters.swap(impl_->pendingCharacters);
            impl_->pendingCharacters.clear();
        }

        return input;
    }

    bool CnaUiPlatform::hasClipboard()
    {
#if defined(CNA_DEVICES)
        return true;
#else
        // CNA's clipboard lives behind its optional CNA_DEVICES feature. Copy and paste in text
        // fields degrade to doing nothing rather than the editor failing to build without it.
        return false;
#endif
    }

    std::string CnaUiPlatform::getClipboardText()
    {
#if defined(CNA_DEVICES)
        return CNA::Devices::Clipboard::getTextProperty();
#else
        return {};
#endif
    }

    void CnaUiPlatform::setClipboardText(const std::string& text)
    {
#if defined(CNA_DEVICES)
        CNA::Devices::Clipboard::setTextProperty(text);
#else
        (void)text;
#endif
    }
}
