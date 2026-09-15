// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioFrame.hpp
 * @brief The explicit frame lifecycle: build, layout, input, draw, retain.
 *
 * `plan.md` STUDIO-03015, STUDIO-03020.
 *
 * ### Why the frame has named phases at all
 *
 * A classic immediate-mode UI collapses everything into one pass: a widget computes its rectangle,
 * hit-tests it and draws it in a single call. That is compact, and it has a defect that is not
 * obvious until a UI is dense enough to hit it — a widget draws its hover state using a hover
 * answer computed *before* the widgets that overlap it were described. The first widget under the
 * pointer lights up; so does the one on top of it; and the click goes to only one of them. In a
 * tool with menus over toolbars over panels, that is not a corner case.
 *
 * So a Studio frame runs in five explicit phases, and the whole UI is described **twice**: once to
 * route input, once to draw. Between the two, every widget has declared itself, so hover, capture
 * and focus are fully resolved before a single pixel is decided.
 *
 * | Phase | What happens | What is refused |
 * |-------|--------------|-----------------|
 * | `Build`  | Application state becomes content descriptors | interaction, drawing |
 * | `Layout` | Content plus theme becomes geometry | interaction, drawing |
 * | `Input`  | Geometry is hit-tested; interaction is resolved and recorded | drawing |
 * | `Draw`   | Geometry and the recorded interaction become draw data | routing new interaction |
 * | `Retain` | State is reclaimed, capture released, focus navigation resolved | everything else |
 *
 * "Refused" is literal: the frame counts the violation and names it, and a test asserts the count
 * is zero. That is what makes the separation a property rather than a convention, and it is what
 * the acceptance condition of STUDIO-03015 — *layout can be tested without drawing* — actually
 * needs. A layout test drives `beginFrame` and `beginLayout`, asserts on rectangles, and never
 * enters a draw phase at all.
 *
 * ### Describing twice is cheap, and it is not the same as drawing twice
 *
 * The description is a walk over content descriptors and pure layout functions. No geometry is
 * emitted in the input pass and no hit test is performed in the draw pass: @ref
 * StudioFrame::interact routes in the first and *replays the recorded answer* in the second. A
 * widget therefore cannot disagree with itself between the passes, which is the failure mode a
 * naive two-pass design would introduce in exchange for the one it fixes.
 *
 * ### Clip and layer go to both sides at once
 *
 * What can be clicked and what can be seen must be the same region. The surest way to make them
 * diverge is to let each side maintain its own stack, so the frame pushes to both the router and
 * the draw list from one call and a widget cannot push to one and forget the other.
 */

#include "CNA/Studio/Ui/UiInputState.hpp"
#include "CNA/Studio/UiCore/StudioDrawList.hpp"
#include "CNA/Studio/UiCore/StudioInputRouter.hpp"
#include "CNA/Studio/UiCore/StudioTextMeasure.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"
#include "CNA/Studio/UiCore/WidgetId.hpp"
#include "CNA/Studio/UiCore/WidgetStateStore.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace CNA::Studio
{
    class StudioFontAtlas;

    /** @brief Which phase of the frame is currently running. */
    enum class StudioFramePhase : std::uint8_t
    {
        /** @brief Between frames. Nothing may be described. */
        Idle,
        /** @brief Application state is being turned into content descriptors. */
        Build,
        /** @brief Content and theme are being turned into geometry. */
        Layout,
        /** @brief Geometry is being hit-tested; interaction is resolved and recorded. */
        Input,
        /** @brief Geometry and recorded interaction are being turned into draw data. */
        Draw,
        /** @brief State is being reclaimed and the frame closed. */
        Retain
    };

    /**
     * @brief Returns a stable English name for a phase, for diagnostics.
     * @param phase Phase to name.
     * @return A stable identifier such as `"Layout"`.
     */
    [[nodiscard]] std::string_view studioFramePhaseName(StudioFramePhase phase);

    /**
     * @brief A cursor shape a widget can ask the platform for.
     *
     * `plan.md` STUDIO-03020. Resolved once per frame rather than set by whichever widget asked
     * last: a splitter that sets a resize cursor on hover and a panel that sets the arrow cursor
     * underneath it would otherwise fight, and which one won would depend on description order.
     */
    enum class StudioCursor : std::uint8_t
    {
        /** @brief The ordinary pointer. */
        Arrow,
        /** @brief Over something that can be clicked through, e.g. a link. */
        Hand,
        /** @brief Over editable text. */
        Text,
        /** @brief A vertical splitter: drag left and right. */
        ResizeHorizontal,
        /** @brief A horizontal splitter: drag up and down. */
        ResizeVertical,
        /** @brief A corner grip running top-left to bottom-right. */
        ResizeNwSe,
        /** @brief A corner grip running top-right to bottom-left. */
        ResizeNeSw,
        /** @brief Precise positioning, e.g. a colour picker. */
        Crosshair,
        /** @brief The gesture in progress cannot be completed here. */
        NotAllowed,
        /** @brief Something is running and the UI is not accepting this interaction yet. */
        Wait,
        /** @brief Number of declared shapes; not itself a shape. */
        Count
    };

    /**
     * @brief Returns a stable English name for a cursor shape, for diagnostics and platform maps.
     * @param cursor Shape to name.
     * @return A stable identifier such as `"ResizeHorizontal"`.
     */
    [[nodiscard]] std::string_view studioCursorName(StudioCursor cursor);

    /**
     * @brief One Studio UI frame: the services a widget needs, sequenced.
     *
     * Owns the theme, the id stack, the retained state store, the input router and the draw list,
     * and is the only thing that advances any of them. A widget takes a `StudioFrame&` and needs
     * nothing else.
     */
    class StudioFrame
    {
    public:
        /** @brief Constructs a frame on the dark theme. */
        StudioFrame();

        /**
         * @brief Constructs a frame on a given theme.
         * @param theme Theme to resolve every colour and metric through.
         */
        explicit StudioFrame(StudioTheme theme);

        // --- Lifecycle ---------------------------------------------------------------------------

        /**
         * @brief Starts a frame and enters the build phase.
         *
         * @param input This frame's raw input.
         * @param blockingLayer The input layer that receives input; raise it while a modal is
         *        open. Stated by the caller rather than inferred from description order, because
         *        inferring it leaks a frame of input to the panels underneath.
         */
        void beginFrame(const UiInputState& input, int blockingLayer = 0);

        /** @brief Enters the layout phase. */
        void beginLayout();

        /** @brief Enters the input phase: the first description pass. */
        void beginInput();

        /** @brief Enters the draw phase: the second description pass. */
        void beginDraw();

        /** @brief Runs the retain phase and returns the frame to idle. */
        void endFrame();

        /** @brief The phase currently running. */
        [[nodiscard]] StudioFramePhase phase() const { return phase_; }

        /** @brief Whether this is the input pass. */
        [[nodiscard]] bool isInputPass() const { return phase_ == StudioFramePhase::Input; }

        /** @brief Whether this is the draw pass. */
        [[nodiscard]] bool isDrawPass() const { return phase_ == StudioFramePhase::Draw; }

        /** @brief How many frames have been completed since construction. */
        [[nodiscard]] std::uint64_t frameIndex() const { return frameIndex_; }

        // --- Services ----------------------------------------------------------------------------

        /** @brief The theme every colour and metric resolves through. */
        [[nodiscard]] const StudioTheme& theme() const { return theme_; }

        /**
         * @brief Replaces the theme.
         *
         * Between frames only: changing the theme mid-frame would give the input pass and the draw
         * pass different metrics, and every rectangle the user clicks would be in the wrong place
         * for exactly one frame.
         *
         * @param theme New theme.
         */
        void setTheme(StudioTheme theme);

        /**
         * @brief Supplies real glyph metrics.
         *
         * Null — the default — measures from the font size alone (@ref
         * approximateStudioTextMetrics), which is enough for a well-formed layout and is what the
         * shell uses until the atlas of STUDIO-04005 exists. The pointer is borrowed, not owned;
         * the font set must outlive the frame.
         *
         * @param fonts Font set, or null to measure approximately.
         */
        void setFontSet(const StudioFontSet* fonts) { fonts_ = fonts; }

        /** @brief The font set in use, or null when measuring approximately. */
        [[nodiscard]] const StudioFontSet* fontSet() const { return fonts_; }

        /**
         * @brief Supplies the glyph atlas: real text rather than measured boxes.
         *
         * Sets the measurement source too, so the extents a layout computes and the glyphs a draw
         * pass emits can never come from different fonts. The pointer is borrowed; the atlas must
         * outlive the frame.
         *
         * @param atlas Atlas to draw and measure through, or null for neither.
         */
        void setFontAtlas(StudioFontAtlas* atlas);

        /** @brief The glyph atlas, or null when this frame draws no real text. */
        [[nodiscard]] StudioFontAtlas* fontAtlas() const { return atlas_; }

        /**
         * @brief Measures one line of text in a theme font role.
         * @param role Typographic role.
         * @param utf8 Text to measure.
         * @return The measured extent, DPI-scaled.
         */
        [[nodiscard]] StudioTextMetrics measureText(StudioFontRole role,
                                                    std::string_view utf8) const;

        /**
         * @brief Measures one line of text in an explicit style.
         * @param style Font style, already DPI-scaled.
         * @param utf8 Text to measure.
         * @return The measured extent.
         */
        [[nodiscard]] StudioTextMetrics measureText(const StudioFontStyle& style,
                                                    std::string_view utf8) const;

        /** @brief The scope stack widget ids are derived from. */
        [[nodiscard]] WidgetIdStack& ids() { return ids_; }

        /** @brief The retained per-widget state store. */
        [[nodiscard]] WidgetStateStore& state() { return state_; }

        /** @brief The input router. Prefer @ref interact for widget interaction. */
        [[nodiscard]] StudioInputRouter& router() { return router_; }

        /** @brief The input router. */
        [[nodiscard]] const StudioInputRouter& router() const { return router_; }

        /** @brief This frame's raw input. */
        [[nodiscard]] const UiInputState& input() const { return router_.input(); }

        /**
         * @brief The draw list.
         *
         * Touching it outside the draw phase is recorded as a phase violation: geometry emitted in
         * any other phase is either discarded by the next `begin()` or duplicated by the second
         * description pass, and both failures look like a rendering glitch rather than a sequencing
         * mistake.
         *
         * @return The draw list.
         */
        [[nodiscard]] StudioDrawList& drawList();

        /** @brief The geometry produced this frame. Valid until the next @ref beginFrame. */
        [[nodiscard]] const UiDrawData& drawData() const { return draw_.drawData(); }

        // --- Interaction -------------------------------------------------------------------------

        /**
         * @brief Routes input to one widget, or replays what the input pass decided.
         *
         * In the input pass this hit-tests and records the answer. In the draw pass it returns the
         * recorded answer without touching the router, so the two passes cannot disagree about
         * what the user did.
         *
         * @param id The widget's identity.
         * @param bounds Its rectangle, in logical units.
         * @param enabled False for a widget that must not respond.
         * @return What happened to it this frame.
         */
        StudioInteraction interact(WidgetId id, const UiRect& bounds, bool enabled = true);

        /**
         * @brief Returns what the input pass decided about a widget, without describing it.
         * @param id The widget's identity.
         * @return The recorded interaction, or a default-constructed one.
         */
        [[nodiscard]] StudioInteraction recordedInteraction(WidgetId id) const;

        /** @brief Number of widgets that interacted this frame. */
        [[nodiscard]] std::size_t interactionCount() const { return interactions_.size(); }

        // --- Clipping and layers -----------------------------------------------------------------

        /**
         * @brief Pushes a clip rectangle onto both the router and the draw list.
         * @param rect Clip rectangle in logical units.
         */
        void pushClip(const UiRect& rect);

        /** @brief Pops the innermost clip from both the router and the draw list. */
        void popClip();

        /**
         * @brief Enters an input layer on both the router and the draw list.
         * @param layer Layer index; higher is nearer the user.
         */
        void pushLayer(int layer);

        /** @brief Leaves the current input layer. */
        void popLayer();

        // --- Cursor (STUDIO-03020) ----------------------------------------------------------------

        /**
         * @brief Asks for a cursor shape on behalf of a widget.
         *
         * Honoured only for the widget that holds the mouse, or — when nothing does — the widget
         * under the pointer. Anything else is ignored, which is what stops a panel underneath a
         * splitter from taking the cursor back on the frame the user is dragging.
         *
         * @param id The requesting widget.
         * @param cursor Shape wanted.
         * @return True when the request was honoured.
         */
        bool requestCursor(WidgetId id, StudioCursor cursor);

        /** @brief The cursor shape the platform should show this frame. */
        [[nodiscard]] StudioCursor cursor() const { return cursor_; }

        // --- Tooltips (STUDIO-03021) --------------------------------------------------------------

        /** @brief A tooltip that has waited long enough to be shown. */
        struct StudioTooltipRequest
        {
            /** @brief The widget it belongs to. Invalid when nothing is showing one. */
            WidgetId owner;

            /** @brief What it says. */
            std::string text;

            /** @brief The widget's rectangle, so the tooltip can be placed beside rather than over it. */
            UiRect anchor;

            /** @brief How long the pointer has rested on the widget, in seconds. */
            float hoverSeconds = 0.0f;

            /** @brief Whether there is a tooltip to draw. */
            [[nodiscard]] bool visible() const { return owner.isValid() && !text.empty(); }
        };

        /**
         * @brief Offers a tooltip for a widget.
         *
         * Honoured only for the widget the pointer is actually resting on, like @ref requestCursor
         * — every widget can offer one, and at most one is showing.
         *
         * A tooltip appears after a delay rather than immediately. Without one, moving the pointer
         * across a toolbar flashes six tooltips on the way to the seventh, which is worse than
         * having none: the flicker is what the eye follows, so the one the user wanted is the one
         * they do not read.
         *
         * @param id The offering widget.
         * @param text What to say. Empty offers nothing.
         * @param bounds The widget's rectangle, so the tooltip can avoid covering it.
         * @return True when this widget is the one whose tooltip would show.
         */
        bool requestTooltip(WidgetId id, std::string_view text, const UiRect& bounds);

        /** @brief The tooltip to draw this frame, if any. */
        [[nodiscard]] const StudioTooltipRequest& tooltip() const { return tooltip_; }

        /**
         * @brief How long the pointer must rest before a tooltip appears, in seconds.
         *
         * A setting rather than a constant because it is the kind of number people disagree about,
         * and because a test wants to reach the shown state without waiting.
         */
        void setTooltipDelay(float seconds) { tooltipDelay_ = std::max(0.0f, seconds); }

        /** @brief The tooltip delay in seconds. */
        [[nodiscard]] float tooltipDelay() const { return tooltipDelay_; }

        // --- Clipboard (STUDIO-03025) -------------------------------------------------------------

        /**
         * @brief Installs the platform's clipboard.
         *
         * Until one is installed the frame keeps its own string, so a text field can be cut and
         * pasted in a headless test and in a build whose CNA has the Devices module switched off
         * (CNA gap G-02). That fallback is real but local: it does not reach other applications,
         * which is why the host installs the platform's as soon as there is one.
         *
         * @param read Returns the clipboard's text.
         * @param write Puts text on the clipboard.
         */
        void setClipboard(std::function<std::string()> read,
                          std::function<void(const std::string&)> write);

        /** @brief Whether the platform's clipboard is installed, rather than the local fallback. */
        [[nodiscard]] bool hasPlatformClipboard() const { return readClipboard_ != nullptr; }

        /** @brief The clipboard's text. */
        [[nodiscard]] std::string clipboardText() const;

        /** @brief Puts @p text on the clipboard. */
        void setClipboardText(const std::string& text);

        // --- Misuse --------------------------------------------------------------------------------

        /** @brief Number of operations attempted in a phase that refuses them. */
        [[nodiscard]] std::size_t phaseViolations() const { return violations_.size(); }

        /** @brief Every phase violation this frame, each naming the operation and the phase. */
        [[nodiscard]] const std::vector<std::string>& phaseViolationLog() const
        {
            return violations_;
        }

    private:
        /** @brief Records a violation when @p allowed does not hold, and reports whether it did. */
        bool require(bool allowed, const char* operation);

        /** @brief Advances to @p next, recording a violation when the transition is out of order. */
        void enter(StudioFramePhase next, StudioFramePhase expectedCurrent);

        StudioTheme theme_;
        WidgetIdStack ids_;
        WidgetStateStore state_;
        StudioInputRouter router_;
        StudioDrawList draw_;

        StudioFramePhase phase_ = StudioFramePhase::Idle;
        std::uint64_t frameIndex_ = 0;
        int blockingLayer_ = 0;

        UiInputState pendingInput_;

        /**
         * @brief This frame's interaction answers, in description order.
         *
         * A flat vector rather than a map: a frame has a few hundred interactive widgets at most,
         * lookups are a linear scan over contiguous memory, and the ordering is itself useful —
         * it is description order, which is tab order.
         */
        std::vector<std::pair<WidgetId, StudioInteraction>> interactions_;

        const StudioFontSet* fonts_ = nullptr;
        StudioFontAtlas* atlas_ = nullptr;

        StudioCursor cursor_ = StudioCursor::Arrow;
        StudioTooltipRequest tooltip_;
        WidgetId tooltipHovered_;
        float tooltipHoverSeconds_ = 0.0f;
        float tooltipDelay_ = 0.6f;
        std::function<std::string()> readClipboard_;
        std::function<void(const std::string&)> writeClipboard_;
        std::string localClipboard_;
        std::vector<std::string> violations_;
    };

    /**
     * @brief Runs a complete frame over a description function.
     *
     * Calls @p describe once in the input phase and once in the draw phase, with the build, layout
     * and retain phases entered around them. Convenient for a panel or a test; the application
     * shell drives the phases itself so that it can compute content and geometry once rather than
     * per pass.
     *
     * @param frame Frame to run.
     * @param input This frame's raw input.
     * @param describe Called twice: once to route input, once to draw.
     * @param blockingLayer The input layer that receives input.
     */
    void runStudioFrame(StudioFrame& frame, const UiInputState& input,
                        const std::function<void(StudioFrame&)>& describe, int blockingLayer = 0);
} // namespace CNA::Studio
