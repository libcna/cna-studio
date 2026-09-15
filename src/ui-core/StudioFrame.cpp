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

        pendingInput_ = input;
        blockingLayer_ = blockingLayer;

        // Once per frame, not once per pass: the retention sweep counts frames, and advancing it
        // twice would halve every widget's retention window without anything looking wrong.
        state_.beginFrame();

        interactions_.clear();
        cursor_ = StudioCursor::Arrow;
        popups_.clear();

        tooltip_ = StudioTooltipRequest{};
    }

    void StudioFrame::openPopup(WidgetId owner)
    {
        if (!owner.isValid()) { return; }
        openPopup_ = owner;
    }

    void StudioFrame::closePopup() { openPopup_ = WidgetId{}; }

    void StudioFrame::deferPopup(StudioPopupBody body)
    {
        if (body) { popups_.push_back(std::move(body)); }
    }

    void StudioFrame::flushPopups()
    {
        if (popups_.empty()) { return; }

        // Against the window's own clip, not whatever clip happened to be in force when the popup
        // was queued: a drop-down list longer than the panel it sits in is the ordinary case, and
        // clipping it to that panel would cut it off halfway down.
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

        // Anything a popup queued itself is dropped rather than run: a popup that opened a popup
        // every frame would grow this list without bound, and nothing in the UI needs it.
        popups_.clear();
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
        router_.beginFrame(pendingInput_, std::max(blockingLayer_, isAnyPopupOpen() ? kPopupLayer : 0));
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
        const WidgetId hovered = router_.hoveredId();
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
        if (router_.hoveredId() != id) { return false; }

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
        frame.beginDraw();
        if (describe) { describe(frame); }
        frame.flushPopups();
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
