// SPDX-License-Identifier: MS-PL
/**
 * @file StudioFrame.cpp
 * @brief Phase sequencing, the interaction record, and cursor resolution.
 */

#include "CNA/Studio/UiCore/StudioFrame.hpp"

#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"

#include <algorithm>

namespace CNA::Studio
{
    std::string_view studioFramePhaseName(StudioFramePhase phase)
    {
        switch (phase)
        {
            case StudioFramePhase::Idle:   return "Idle";
            case StudioFramePhase::Build:  return "Build";
            case StudioFramePhase::Layout: return "Layout";
            case StudioFramePhase::Input:  return "Input";
            case StudioFramePhase::Draw:   return "Draw";
            case StudioFramePhase::Retain: return "Retain";
        }
        return "";
    }

    std::string_view studioCursorName(StudioCursor cursor)
    {
        switch (cursor)
        {
            case StudioCursor::Arrow:            return "Arrow";
            case StudioCursor::Hand:             return "Hand";
            case StudioCursor::Move:             return "Move";
            case StudioCursor::Text:             return "Text";
            case StudioCursor::ResizeHorizontal: return "ResizeHorizontal";
            case StudioCursor::ResizeVertical:   return "ResizeVertical";
            case StudioCursor::ResizeNwSe:       return "ResizeNwSe";
            case StudioCursor::ResizeNeSw:       return "ResizeNeSw";
            case StudioCursor::Crosshair:        return "Crosshair";
            case StudioCursor::NotAllowed:       return "NotAllowed";
            case StudioCursor::Wait:             return "Wait";
            case StudioCursor::Count:            break;
        }
        return "";
    }

    StudioFrame::StudioFrame() : StudioFrame(StudioTheme::dark()) {}

    StudioFrame::StudioFrame(StudioTheme theme) : theme_(std::move(theme)) {}

    bool StudioFrame::require(bool allowed, const char* operation)
    {
        if (allowed) { return true; }
        violations_.emplace_back(std::string{operation} + " during phase "
                                 + std::string{studioFramePhaseName(phase_)});
        return false;
    }

    void StudioFrame::enter(StudioFramePhase next, StudioFramePhase expectedCurrent)
    {
        if (phase_ != expectedCurrent)
        {
            violations_.emplace_back(std::string{"entered phase "}
                                     + std::string{studioFramePhaseName(next)} + " from "
                                     + std::string{studioFramePhaseName(phase_)} + " rather than "
                                     + std::string{studioFramePhaseName(expectedCurrent)});
        }
        phase_ = next;
    }

    void StudioFrame::beginFrame(const UiInputState& input, int blockingLayer)
    {
        violations_.clear();
        enter(StudioFramePhase::Build, StudioFramePhase::Idle);

        // Here, before any pass, because this is the only point at which no glyph pointer is held
        // and no quad has been emitted. Growing invalidates both -- the texture coordinates are
        // normalised by a side that is about to change -- so doing it where the need is
        // *discovered*, inside a pack that failed partway through a draw pass, would move the
        // glyphs out from under geometry already written against them. That is legacy ED-119's
        // shape again, and it reads as a corrupt font rather than as an atlas that moved.
        if (atlas_ != nullptr) { (void)atlas_->growIfNeeded(); }

        pendingInput_ = input;
        blockingLayer_ = blockingLayer;

        // Once per frame, not once per pass: the retention sweep counts frames, and advancing it
        // twice would halve every widget's retention window without anything looking wrong.
        state_.beginFrame();

        interactions_.clear();
        cursor_ = StudioCursor::Arrow;
        popups_.clear();
        dropTarget_ = WidgetId{};
        dropHover_ = WidgetId{};
        dropUnder_ = WidgetId{};
        if (!input.isMouseDown(UiMouseButton::Left)) { dragSuppressed_ = false; }

        tooltip_ = StudioTooltipRequest{};
    }

    void StudioFrame::openPopup(WidgetId owner)
    {
        if (!owner.isValid()) { return; }
        openPopup_ = owner;
    }

    void StudioFrame::closePopup() { openPopup_ = WidgetId{}; }

    void StudioFrame::openModal(WidgetId owner)
    {
        // A modal takes the keyboard, so anything a popup was holding has to go with it: a
        // drop-down left open behind a dialog would still be the thing Escape closed.
        closePopup();
        openModal_ = owner;
    }

    void StudioFrame::closeModal() { openModal_ = WidgetId{}; }

    void StudioFrame::deferModal(StudioPopupBody body)
    {
        if (body) { modals_.push_back(std::move(body)); }
    }

    void StudioFrame::deferPopup(StudioPopupBody body)
    {
        if (body) { popups_.push_back(std::move(body)); }
    }

    bool StudioFrame::beginDrag(WidgetId source, StudioDragPayload payload)
    {
        // Two payloads at once is a state with no correct drop, so it is unreachable rather than
        // handled. A gesture abandoned while the button is still down stays abandoned until it is
        // released: without that, Escape cancels the drag and the very next frame starts it again
        // from the same press, which is a cancel the user cannot make stick.
        if (!source.isValid() || isDragging() || dragSuppressed_ || payload.type.empty())
        {
            return false;
        }

        dragSource_ = source;
        drag_ = std::move(payload);
        return true;
    }

    void StudioFrame::cancelDrag()
    {
        dragSource_ = WidgetId{};
        drag_ = StudioDragPayload{};
        // Only while the button is still down. A drag that ended with the release is over anyway,
        // and suppressing there would refuse the *next* drag as well.
        dragSuppressed_ = pendingInput_.isMouseDown(UiMouseButton::Left);
    }

    StudioFrame::StudioDropResult StudioFrame::acceptDrop(WidgetId target, const UiRect& bounds,
                                                          std::string_view type)
    {
        StudioDropResult result;
        if (!target.isValid()) { return result; }

        // The drop is decided in the input pass and *remembered*, so the draw pass gives the same
        // answer. Recomputing it there would let a target draw itself as having received something
        // the input pass gave to a different one.
        if (!isDragging())
        {
            result.dropped = dropTarget_ == target && isInputPass();
            return result;
        }

        // Clip- and layer-aware, but *not* through interact(): its capture rule says nothing else
        // is hovered while a widget holds the mouse, which is right for a splitter and exactly
        // wrong here -- the source holds the mouse for the whole of a drag, and a drag is a
        // gesture whose purpose is to end somewhere else.
        if (!router_.pointerOver(bounds)) { return result; }

        // Which target wins is settled in the input pass and replayed in the draw pass, for the
        // same reason interaction is: two overlapping targets would otherwise both light up, and
        // the one that drew first would be the one that did not receive the drop.
        if (isInputPass())
        {
            dropUnder_ = target;
            if (drag_.type == type) { dropHover_ = target; }
        }

        // The type is the whole point: a target that swallowed anything would let a user drop a
        // texture onto a material slot and see nothing happen, which is indistinguishable from a
        // drag that never worked.
        if (drag_.type != type)
        {
            result.refused = dropUnder_ == target;
            return result;
        }

        result.hovered = dropHover_ == target;
        if (!isInputPass() || !result.hovered) { return result; }
        if (!router_.mouseReleased(UiMouseButton::Left)) { return result; }

        result.dropped = true;
        result.value = drag_.value;
        dropTarget_ = target;
        cancelDrag();
        return result;
    }

    void StudioFrame::flushPopups()
    {
        if (!popups_.empty())
        {
            // Against the window's own clip, not whatever clip happened to be in force when the
            // popup was queued: a drop-down list longer than the panel it sits in is the ordinary
            // case, and clipping it to that panel would cut it off halfway down.
            std::vector<StudioPopupBody> running;
            running.swap(popups_);

            inPopup_ = true;
            pushLayer(kPopupLayer);
            pushClip(UiRect{0.0f, 0.0f, pendingInput_.displayWidth, pendingInput_.displayHeight});
            ids_.push("popup");
            for (const StudioPopupBody& body : running) { body(*this); }
            ids_.pop();
            popClip();
            popLayer();
            inPopup_ = false;

            // Anything a popup queued itself is dropped rather than run: a popup that opened a
            // popup every frame would grow this list without bound, and nothing in the UI needs it.
            popups_.clear();
        }

    }

    void StudioFrame::flushModals()
    {
        if (modals_.empty()) { return; }

        std::vector<StudioPopupBody> running;
        running.swap(modals_);

        const UiRect window{0.0f, 0.0f, pendingInput_.displayWidth, pendingInput_.displayHeight};

        if (isDrawPass())
        {
            // A scrim, because "you cannot use the rest of the window" has to be *visible*. A
            // dialog over an undimmed workspace looks like a panel that happens to be on top, and
            // a user who does not know they are blocked reads the unresponsive editor as a hang.
            StudioColor scrim = theme_.color(StudioColorRole::AppBackground);
            scrim.a = 160;
            drawList().fillRect(window, scrim);
        }

        inModal_ = true;
        pushLayer(kModalLayer);
        pushClip(window);
        ids_.push("modal");
        for (const StudioPopupBody& body : running) { body(*this); }
        ids_.pop();
        popClip();
        popLayer();
        inModal_ = false;

        modals_.clear();
    }

    UiRect StudioFrame::dragPreviewBounds(float width, float height) const
    {
        const float padding = static_cast<float>(theme_.metric(StudioMetric::SpacingSmall));

        // Below and right of the pointer, and flipped when there is no room, so the label never
        // covers the target the user is aiming at.
        float x = pendingInput_.mouseX + padding * 2.0f;
        float y = pendingInput_.mouseY + padding * 2.0f;
        if (x + width > pendingInput_.displayWidth) { x = pendingInput_.mouseX - width - padding; }
        if (y + height > pendingInput_.displayHeight) { y = pendingInput_.mouseY - height - padding; }

        return UiRect{std::round(x), std::round(y), width, height};
    }

    void StudioFrame::beginLayout() { enter(StudioFramePhase::Layout, StudioFramePhase::Build); }

    void StudioFrame::beginInput()
    {
        enter(StudioFramePhase::Input, StudioFramePhase::Layout);

        // The router's frame starts here rather than in beginFrame(), because it computes edges by
        // diffing against the previous snapshot: starting it twice per frame would make the second
        // diff empty and every press and release would vanish.
        // Raised by the frame itself while a deferred popup is open, because the caller cannot
        // know: a drop-down opened inside a panel is not something the shell was told about, and a
        // list whose rows can be clicked *through* is worse than one that does not open at all.
        // A modal outranks a popup and both outrank whatever the caller asked for: a dialog a user
        // can click behind is not a dialog, and a list whose rows can be clicked through is worse
        // than one that never opened.
        const int raised = isAnyModalOpen() ? kModalLayer : (isAnyPopupOpen() ? kPopupLayer : 0);
        router_.beginFrame(pendingInput_, std::max(blockingLayer_, raised));
        ids_.beginFrame();
    }

    void StudioFrame::beginDraw()
    {
        enter(StudioFramePhase::Draw, StudioFramePhase::Input);

        // The same ids, issued again in the same order. Resetting the stack is what makes the
        // second pass produce identical identities rather than a second set collided against the
        // first.
        ids_.beginFrame();

        // Resolved from the draw pass alone. By now hover and capture are final, so a widget's
        // request can be answered with the truth rather than with a partial answer that a later
        // widget in the input pass would have overturned.
        cursor_ = StudioCursor::Arrow;

        draw_.begin(pendingInput_.displayWidth, pendingInput_.displayHeight,
                    pendingInput_.framebufferScaleX);

        if (atlas_ != nullptr)
        {
            draw_.setDefaultTexture(StudioFontAtlas::kTextureId, atlas_->whitePixelU(),
                                    atlas_->whitePixelV());
        }
    }

    void StudioFrame::endFrame()
    {
        enter(StudioFramePhase::Retain, StudioFramePhase::Draw);

        // Emitted here rather than at the start of the draw pass, and the difference matters: a
        // glyph can be rasterised at any point in the frame -- by a measurement during layout, or
        // by a widget that draws text nothing measured -- and the renderer applies every texture
        // request before it draws anything. Uploading last therefore covers glyphs that appeared
        // after the draw pass began, which the obvious ordering would show one frame late. The
        // prototype shipped exactly that (legacy ED-119).
        if (atlas_ != nullptr && atlas_->hasPendingUpload())
        {
            draw_.addTextureRequest(atlas_->takeUploadRequest());
        }

        draw_.end();

        // After both passes: Tab moves focus for the *next* frame, so the ring is drawn on the
        // widget that had focus while this frame's input was routed. Resolving it between the
        // passes would draw the ring on one widget and have sent the keystrokes to another.
        router_.endFrame();

        // The hover clock advances here, once per frame, and only now: hover is decided during the
        // input pass, so asking at the start of a frame answers with the *previous* frame's widget.
        // That off-by-one frame is not cosmetic -- it is the whole delay. Sweeping the pointer from
        // one toolbar button to the next, the frame of the move would still be counted against the
        // button just left, the clock would never reset, and the new button's tooltip would appear
        // instantly. The delay would then work only for the first control the pointer ever touched.
        const WidgetId hovered = router_.pointerTargetId();
        if (hovered.isValid() && hovered == tooltipHovered_)
        {
            tooltipHoverSeconds_ += pendingInput_.deltaSeconds > 0.0f ? pendingInput_.deltaSeconds
                                                                     : 1.0f / 60.0f;
        }
        else
        {
            // Reset on *any* change of hovered widget, including to nothing. A clock that kept
            // counting across a gap would make the tooltip snap back the instant the pointer
            // returned, which is the flicker the delay exists to prevent.
            tooltipHoverSeconds_ = 0.0f;
        }
        tooltipHovered_ = hovered;

        // A drag ends when the button does, wherever the pointer was -- but *after* the frame has
        // been described, not before it. Cancelling on the button-up frame's beginFrame would
        // throw the payload away before the target under the pointer ever got to see the release,
        // so every drop would read as a cancellation.
        if (isDragging()
            && (!pendingInput_.isMouseDown(UiMouseButton::Left)
                || pendingInput_.isKeyDown(UiKey::Escape)))
        {
            cancelDrag();
        }

        ++frameIndex_;
        phase_ = StudioFramePhase::Idle;
    }

    void StudioFrame::setTheme(StudioTheme theme)
    {
        if (!require(phase_ == StudioFramePhase::Idle, "setTheme")) { return; }
        theme_ = std::move(theme);
    }

    void StudioFrame::setFontAtlas(StudioFontAtlas* atlas)
    {
        atlas_ = atlas;
        fonts_ = atlas;
    }

    StudioTextMetrics StudioFrame::measureText(StudioFontRole role, std::string_view utf8) const
    {
        return measureText(theme_.font(role), utf8);
    }

    StudioTextMetrics StudioFrame::measureText(const StudioFontStyle& style,
                                               std::string_view utf8) const
    {
        if (fonts_ != nullptr) { return fonts_->measure(style, utf8); }
        return approximateStudioTextMetrics(style, utf8);
    }

    StudioDrawList& StudioFrame::drawList()
    {
        require(phase_ == StudioFramePhase::Draw, "drawList");
        return draw_;
    }

    StudioInteraction StudioFrame::interact(WidgetId id, const UiRect& bounds, bool enabled)
    {
        if (phase_ == StudioFramePhase::Input)
        {
            const StudioInteraction result = router_.interact(id, bounds, enabled);
            interactions_.emplace_back(id, result);
            return result;
        }

        if (phase_ == StudioFramePhase::Draw)
        {
            // Replayed, never recomputed. Re-running the hit test here would let the two passes
            // disagree whenever anything the router owns had moved on -- and a widget that routes
            // a click in one pass and draws itself unpressed in the other is the bug the two-pass
            // design exists to remove, not one to reintroduce at the last step.
            return recordedInteraction(id);
        }

        require(false, "interact");
        StudioInteraction result;
        result.disabled = !enabled;
        return result;
    }

    StudioInteraction StudioFrame::recordedInteraction(WidgetId id) const
    {
        const auto found = std::find_if(interactions_.begin(), interactions_.end(),
                                        [id](const auto& entry) { return entry.first == id; });
        if (found != interactions_.end()) { return found->second; }
        return StudioInteraction{};
    }

    void StudioFrame::pushClip(const UiRect& rect)
    {
        router_.pushClip(rect);
        if (phase_ == StudioFramePhase::Draw) { draw_.pushClip(rect); }
    }

    void StudioFrame::popClip()
    {
        router_.popClip();
        if (phase_ == StudioFramePhase::Draw) { draw_.popClip(); }
    }

    void StudioFrame::pushLayer(int layer) { router_.pushLayer(layer); }

    void StudioFrame::popLayer() { router_.popLayer(); }

    bool StudioFrame::requestCursor(WidgetId id, StudioCursor cursor)
    {
        if (!id.isValid()) { return false; }

        // The widget holding the mouse outranks the one under it. Halfway through dragging a
        // splitter the pointer is usually over a panel, and handing that panel the cursor would
        // make the shape flicker back to an arrow for the whole gesture.
        const WidgetId active = router_.activeId();
        const bool owns = active.isValid() ? active == id : router_.hoveredId() == id;
        if (!owns) { return false; }

        cursor_ = cursor;
        return true;
    }

    bool StudioFrame::requestTooltip(WidgetId id, std::string_view text, const UiRect& bounds)
    {
        if (!id.isValid() || text.empty()) { return false; }

        // Only the widget under the pointer, and only when nothing is being dragged: a tooltip
        // that appeared halfway through a splitter drag would cover the thing being dragged.
        if (router_.activeId().isValid()) { return false; }

        // The pointer's *target*, not the hovered widget: a disabled control is deliberately not
        // hovered, and "why is this greyed out" is exactly when somebody hovers for an answer.
        if (router_.pointerTargetId() != id) { return false; }

        // The clock belongs to a widget, not to the pointer. On the frame the pointer crosses from
        // one control to the next, the clock still holds the time spent on the one it left; letting
        // this widget read it would hand it a delay it never waited out.
        if (tooltipHovered_ != id) { return false; }
        if (tooltipHoverSeconds_ < tooltipDelay_) { return false; }

        tooltip_.owner = id;
        tooltip_.text = std::string{text};
        tooltip_.anchor = bounds;
        tooltip_.hoverSeconds = tooltipHoverSeconds_;
        return true;
    }

    void runStudioFrame(StudioFrame& frame, const UiInputState& input,
                        const std::function<void(StudioFrame&)>& describe, int blockingLayer)
    {
        frame.beginFrame(input, blockingLayer);
        frame.beginLayout();
        frame.beginInput();
        if (describe) { describe(frame); }
        frame.flushPopups();
        frame.flushModals();
        frame.beginDraw();
        if (describe) { describe(frame); }
        frame.flushPopups();
        frame.flushModals();
        frame.endFrame();
    }

    void StudioFrame::setClipboard(std::function<std::string()> read,
                                   std::function<void(const std::string&)> write)
    {
        readClipboard_ = std::move(read);
        writeClipboard_ = std::move(write);
    }

    std::string StudioFrame::clipboardText() const
    {
        return readClipboard_ ? readClipboard_() : localClipboard_;
    }

    void StudioFrame::setClipboardText(const std::string& text)
    {
        // Both, when a platform clipboard is installed. Keeping the local copy in step costs a
        // string and means a paste still works when the platform's read comes back empty -- which
        // it does on a machine where another application took ownership and then exited.
        localClipboard_ = text;
        if (writeClipboard_) { writeClipboard_(text); }
    }
} // namespace CNA::Studio
