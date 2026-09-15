// SPDX-License-Identifier: MS-PL
/**
 * @file StudioInputRouter.cpp
 * @brief Hover, capture, focus and edge detection over a frame of raw input.
 */

#include "CNA/Studio/UiCore/StudioInputRouter.hpp"

#include <algorithm>

namespace CNA::Studio
{
    StudioControlState StudioInteraction::state() const
    {
        // Order matters. Disabled wins over everything: a disabled control that highlights on
        // hover is telling the user it will respond, and then not responding.
        if (disabled) { return StudioControlState::Disabled; }
        if (held) { return StudioControlState::Pressed; }
        if (hovered) { return StudioControlState::Hover; }
        return StudioControlState::Normal;
    }

    StudioInputRouter::StudioInputRouter()
    {
        clips_.push_back(UiRect{0.0f, 0.0f, 0.0f, 0.0f});
        layers_.push_back(0);
    }

    void StudioInputRouter::beginFrame(const UiInputState& input, int blockingLayer)
    {
        // On the very first frame there is no previous state to diff against. Treating a
        // default-constructed one as "previous" would synthesise a press for every button that
        // happens to be down as Studio starts -- which, for a user who launched it by
        // double-clicking, is a phantom click delivered into their scene.
        previous_ = hasFrame_ ? current_ : input;
        current_ = input;
        hasFrame_ = true;

        blockingLayer_ = blockingLayer;

        clips_.clear();
        clips_.push_back(UiRect{0.0f, 0.0f, input.displayWidth, input.displayHeight});
        layers_.clear();
        layers_.push_back(0);

        hovered_ = kInvalidWidgetId;
        focusables_.clear();
        wantsTextInput_ = false;

        // Tab is resolved at end of frame, once every focusable has declared itself -- the widget
        // that should receive focus may not have been described yet when Tab is pressed.
        focusMoveRequested_ = false;
        if (keyPressed(UiKey::Tab) && !wantsTextInput_)
        {
            focusMoveRequested_ = true;
            focusMoveBackwards_ = current_.modifiers.shift;
        }

        // Capture is NOT cleared here, even though the button is up. The widget holding it has
        // not been described yet this frame, and it is owed its `released` -- clearing now would
        // make a click that started and ended perfectly normally simply never arrive.
        //
        // It is cleared in endFrame() instead, which also covers the case interact() cannot: the
        // holding widget is never described again because its panel closed or its row scrolled
        // out of a virtualised list. A capture nobody releases is a UI that has silently stopped
        // responding to the mouse.
    }

    void StudioInputRouter::endFrame()
    {
        // The capture holder was not described this frame and the button is up, so nothing will
        // ever release it. See beginFrame() for why this is not done there.
        if (active_.isValid() && !mouseDown(UiMouseButton::Left))
        {
            active_ = kInvalidWidgetId;
            pressedIn_ = kInvalidWidgetId;
        }

        if (!focusMoveRequested_ || focusables_.empty()) { return; }

        const auto position = std::find(focusables_.begin(), focusables_.end(), focused_);
        std::size_t next = 0;

        if (position == focusables_.end())
        {
            // Nothing focused, or the focused widget is gone: start at the appropriate end rather
            // than refusing to move, which would make Tab do nothing after closing a panel.
            next = focusMoveBackwards_ ? focusables_.size() - 1 : 0;
        }
        else
        {
            const auto index = static_cast<std::size_t>(position - focusables_.begin());
            if (focusMoveBackwards_)
            {
                next = index == 0 ? focusables_.size() - 1 : index - 1;
            }
            else
            {
                next = (index + 1) % focusables_.size();
            }
        }
        focused_ = focusables_[next];
    }

    bool StudioInputRouter::mousePressed(UiMouseButton button) const
    {
        return current_.isMouseDown(button) && !previous_.isMouseDown(button);
    }

    bool StudioInputRouter::mouseReleased(UiMouseButton button) const
    {
        return !current_.isMouseDown(button) && previous_.isMouseDown(button);
    }

    bool StudioInputRouter::mouseDown(UiMouseButton button) const
    {
        return current_.isMouseDown(button);
    }

    bool StudioInputRouter::keyPressed(UiKey key) const
    {
        return current_.isKeyDown(key) && !previous_.isKeyDown(key);
    }

    bool StudioInputRouter::keyDown(UiKey key) const { return current_.isKeyDown(key); }

    UiKeyModifiers StudioInputRouter::modifiers() const
    {
        // Carried on the input state rather than derived from key-down flags. `UiKey` deliberately
        // contains only keys that *act* -- the platform layer reports modifiers separately because
        // that is how every platform reports them, and synthesising them from left/right key
        // states is how a UI ends up disagreeing with the OS about whether Shift is held.
        return current_.modifiers;
    }

    void StudioInputRouter::pushClip(const UiRect& rect)
    {
        clips_.push_back(currentClip().intersect(rect));
    }

    void StudioInputRouter::popClip()
    {
        if (clips_.size() > 1) { clips_.pop_back(); }
    }

    UiRect StudioInputRouter::currentClip() const
    {
        if (clips_.empty()) { return UiRect{}; }
        return clips_.back();
    }

    void StudioInputRouter::pushLayer(int layer) { layers_.push_back(layer); }

    void StudioInputRouter::popLayer()
    {
        if (layers_.size() > 1) { layers_.pop_back(); }
    }

    bool StudioInputRouter::layerAcceptsInput() const
    {
        return currentLayer() == blockingLayer_;
    }

    bool StudioInputRouter::pointerInside(const UiRect& bounds) const
    {
        if (!current_.mouseInWindow) { return false; }

        // Intersected with the clip, not merely tested against the bounds. A row scrolled out of
        // its panel still has a rectangle containing the pointer, and responding to a click on
        // something the user cannot see is the single most common way an editor feels haunted.
        const UiRect visible = bounds.intersect(currentClip());
        return !visible.isEmpty() && visible.contains(current_.mouseX, current_.mouseY);
    }

    bool StudioInputRouter::pointerOver(const UiRect& bounds) const
    {
        return layerAcceptsInput() && pointerInside(bounds);
    }

    StudioInteraction StudioInputRouter::interact(WidgetId id, const UiRect& bounds, bool enabled)
    {
        StudioInteraction result;
        result.disabled = !enabled;
        result.focused = enabled && id.isValid() && id == focused_;

        if (!id.isValid()) { return result; }

        const bool inside = pointerInside(bounds);

        // While a widget holds the mouse, nothing else can be hovered -- including the widget the
        // pointer is actually over. Without this, dragging a splitter past its neighbour hands the
        // gesture to the neighbour halfway through.
        if (active_.isValid() && active_ != id)
        {
            return result;
        }

        if (!enabled)
        {
            // A disabled widget still consumes hover, so widgets beneath it do not light up
            // through it. A disabled button that lets a click reach what it covers is worse than
            // one that does nothing.
            if (inside && layerAcceptsInput()) { hovered_ = kInvalidWidgetId; }
            return result;
        }

        if (!layerAcceptsInput())
        {
            // A modal is open and this widget is underneath it. It is still described and drawn --
            // it simply cannot be interacted with.
            return result;
        }

        if (active_ == id)
        {
            result.held = true;
            result.hovered = inside;
            hovered_ = inside ? id : hovered_;

            if (mouseReleased(UiMouseButton::Left))
            {
                result.released = true;
                result.releasedOver = inside;
                // A click is press AND release on the same widget. Releasing elsewhere cancels it,
                // which is what lets a user press a button, think better of it, and slide off.
                result.clicked = inside && pressedIn_ == id;
                active_ = kInvalidWidgetId;
                pressedIn_ = kInvalidWidgetId;
            }
            return result;
        }

        if (!inside) { return result; }

        result.hovered = true;
        hovered_ = id;

        if (mousePressed(UiMouseButton::Left))
        {
            active_ = id;
            pressedIn_ = id;
            focused_ = id;
            // Where the gesture started, which is what a drag threshold has to measure from. The
            // widget's own centre is the obvious substitute and is wrong: pressing near an edge
            // would start a drag without the pointer having moved at all.
            pressX_ = current_.mouseX;
            pressY_ = current_.mouseY;
            result.pressed = true;
            result.held = true;
        }

        // Reported even though this widget never held the mouse: see StudioInteraction::releasedOver
        // for the menu gesture that depends on it. Nothing else in the router changes, so a widget
        // that ignores the flag behaves exactly as it did.
        if (mouseReleased(UiMouseButton::Left)) { result.releasedOver = true; }

        if (mouseReleased(UiMouseButton::Right) && pressedIn_ != id)
        {
            result.rightClicked = true;
        }
        if (mousePressed(UiMouseButton::Right))
        {
            focused_ = id;
        }

        return result;
    }

    void StudioInputRouter::setCapture(WidgetId id)
    {
        active_ = id;
        pressedIn_ = id;
    }

    void StudioInputRouter::releaseCapture()
    {
        active_ = kInvalidWidgetId;
        pressedIn_ = kInvalidWidgetId;
    }

    void StudioInputRouter::setFocus(WidgetId id) { focused_ = id; }

    void StudioInputRouter::registerFocusable(WidgetId id, bool enabled)
    {
        if (!enabled || !id.isValid()) { return; }
        focusables_.push_back(id);
    }
} // namespace CNA::Studio
