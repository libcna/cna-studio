// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioWidgets.hpp
 * @brief Buttons, toggles, tabs and menu items, over the one `interact()` the router provides.
 *
 * `plan.md` STUDIO-03003, STUDIO-06003, STUDIO-06004, STUDIO-06006.
 *
 * These are the smallest things in Studio a user can press. They are deliberately free functions
 * over a @ref StudioFrame rather than objects: a widget owns no state of its own — identity comes
 * from the id stack, retained state from the state store, interaction from the router, appearance
 * from the theme — and a type with no state and one method is a function with extra ceremony.
 *
 * ### The rule that makes the two-pass frame safe
 *
 * Every helper is called **twice per frame**: once in the input pass and once in the draw pass.
 * `interaction` is identical in both, because the frame replays it. But
 * @ref StudioWidgetResult::activated and @ref StudioWidgetResult::changed are **true only in the
 * input pass**, and any state a widget owns is mutated only there.
 *
 * That is not a convention to remember. It is what stops the single most likely bug in this
 * architecture: a caller that runs its action on `activated` would otherwise run it twice per
 * click, and a toggle would flip back to where it started before anybody saw it move. Because the
 * flag is false in the draw pass, the obvious code is the correct code.
 *
 * ### What "professional" costs here, concretely
 *
 * - A button acts on **click**, not on press: press-and-slide-off cancels, which is how a user
 *   changes their mind. A menu item acts on release for the same reason.
 * - A **disabled** control is drawn disabled, is not a tab stop, does not hover, and still blocks
 *   the pointer from reaching what is behind it.
 * - **Keyboard** activation is not an afterthought: Space and Enter activate the focused control,
 *   and are ignored while a text field is taking input.
 * - Labels that do not fit are **truncated with an ellipsis** rather than clipped mid-glyph or
 *   allowed to overrun their control.
 * - No helper contains a literal colour or a literal pixel size. Every value comes from the theme.
 */

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioIcons.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"
#include "CNA/Studio/UiCore/WidgetId.hpp"

#include <string>
#include <string_view>

namespace CNA::Studio
{
    /** @brief Horizontal placement of text inside a box. */
    enum class StudioTextAlign : std::uint8_t
    {
        Left,
        Center,
        Right
    };

    /** @brief What happened to a widget this frame. */
    struct StudioWidgetResult
    {
        /** @brief Hover, press, capture, focus and disabled state. Identical in both passes. */
        StudioInteraction interaction;

        /**
         * @brief The user asked for this widget's action.
         *
         * A completed click, or Space/Enter on it while it has focus. **True only in the input
         * pass**, so acting on it runs the action once per gesture.
         */
        bool activated = false;

        /**
         * @brief A value this widget owns changed this frame.
         *
         * **True only in the input pass**, for the same reason as @ref activated.
         */
        bool changed = false;

        /** @brief Convenience: whether the widget is hovered. */
        [[nodiscard]] bool hovered() const { return interaction.hovered; }
        /** @brief Convenience: whether the widget has keyboard focus. */
        [[nodiscard]] bool focused() const { return interaction.focused; }
    };

    /** @brief How a button presents itself. */
    enum class StudioButtonKind : std::uint8_t
    {
        /** @brief An ordinary button: filled control surface, border, label. */
        Normal,
        /** @brief The primary action: accent fill. */
        Accent,
        /** @brief A toolbar button: no fill at rest, fill on hover. */
        Toolbar,
        /** @brief Text only, no surface at all, for low-emphasis actions. */
        Ghost
    };

    /** @brief The adjustable parts of a button. */
    struct StudioButtonOptions
    {
        /** @brief False to draw and route it as disabled. */
        bool enabled = true;
        /** @brief True to draw it as the currently chosen option in a group. */
        bool selected = false;
        /** @brief False to remove it from the Tab order, e.g. a redundant toolbar duplicate. */
        bool focusable = true;
        /** @brief Presentation. */
        StudioButtonKind kind = StudioButtonKind::Normal;
        /** @brief Typographic role for the label. */
        StudioFontRole font = StudioFontRole::Body;
        /** @brief Where the label sits. */
        StudioTextAlign align = StudioTextAlign::Center;
        /** @brief Cursor requested while the pointer is over it. */
        StudioCursor cursor = StudioCursor::Arrow;

        /**
         * @brief An icon drawn before the label, or @ref StudioIcon::None.
         *
         * With a label, the two sit together and the pair is centred. Without one — which is what
         * a toolbar wants once its icons are recognisable — the icon takes the whole button.
         */
        StudioIcon icon = StudioIcon::None;

        /**
         * @brief What a tooltip says about this button. Empty offers none.
         *
         * Not derived from the label. An icon-only button's tooltip needs to say more than the
         * word the button would have shown — "Undo (Ctrl+Z)" rather than "Undo" — and a button
         * showing its label usually needs no tooltip at all.
         */
        std::string_view tooltip;

        /**
         * @brief Draw the icon only, even when a label is given.
         *
         * The label is still what a screen reader and a tooltip use, and it is still what decides
         * the button's identity. Dropping it from the struct instead would make an icon-only
         * toolbar a toolbar with nothing to say about itself.
         */
        bool iconOnly = false;
    };

    /** @brief The adjustable parts of a tab. */
    struct StudioTabOptions
    {
        /** @brief True for the tab whose panel is showing. */
        bool active = false;
        /** @brief False to draw and route it as disabled. */
        bool enabled = true;
        /** @brief True when the panel behind it has unsaved changes. */
        bool modified = false;
    };

    /** @brief Which way a splitter divides, and therefore which way it drags. */
    enum class StudioSplitterAxis : std::uint8_t
    {
        /** @brief Divides left from right; drags along x. */
        Horizontal,
        /** @brief Divides top from bottom; drags along y. */
        Vertical
    };

    /** @brief What a splitter did this frame. */
    struct StudioSplitterResult
    {
        /** @brief Hover, capture and press state. */
        StudioInteraction interaction;
        /**
         * @brief Movement along the split axis since the previous frame, in logical units.
         *
         * Non-zero only in the input pass and only while the splitter holds the mouse. Reported in
         * pixels because that is what a drag produces; converting to a fraction is the dock tree's
         * job, which is the only place that knows the minimums it has to respect.
         */
        float delta = 0.0f;
        /** @brief Whether a drag is in progress. */
        bool dragging = false;
    };

    /** @brief The adjustable parts of a menu item. */
    struct StudioMenuItemOptions
    {
        /** @brief False to draw it greyed and refuse activation. */
        bool enabled = true;
        /** @brief True to reserve the check column and draw a mark when checked. */
        bool checkable = false;
        /** @brief For a checkable item, whether it is on. */
        bool checked = false;
        /** @brief True to draw a submenu arrow instead of a shortcut hint. */
        bool hasSubmenu = false;
        /** @brief True to draw it as the keyboard-highlighted item. */
        bool highlighted = false;
        /** @brief Shortcut hint, right-aligned, e.g. `"Ctrl+S"`. */
        std::string_view shortcut;
    };

    // --- Text ------------------------------------------------------------------------------------

    /**
     * @brief Returns @p text, truncated with an ellipsis to fit @p maxWidth.
     *
     * Truncates on **code-point** boundaries, so a multi-byte character is never cut in half into
     * bytes no decoder can read. A string that does not fit even as one character plus the
     * ellipsis returns the ellipsis alone rather than nothing: a blank cell reads as missing data,
     * and the data is not missing.
     *
     * @param frame Frame supplying measurement.
     * @param style Font style, already DPI-scaled.
     * @param text Text to fit.
     * @param maxWidth Space available in logical units.
     * @return The text to draw.
     */
    [[nodiscard]] std::string studioTruncateText(const StudioFrame& frame,
                                                 const StudioFontStyle& style,
                                                 std::string_view text, float maxWidth);

    /**
     * @brief Draws one line of text inside a box, on its correct baseline.
     *
     * Does nothing outside the draw pass, so a widget helper can call it unconditionally in both
     * passes and stay readable.
     *
     * @param frame Frame to draw into.
     * @param box Box to place the text in.
     * @param text Text to draw.
     * @param role Typographic role.
     * @param color Text colour.
     * @param align Horizontal placement.
     * @return The rectangle the text occupies.
     */
    UiRect studioDrawText(StudioFrame& frame, const UiRect& box, std::string_view text,
                          StudioFontRole role, StudioColor color,
                          StudioTextAlign align = StudioTextAlign::Left);

    /**
     * @brief The width a label needs inside a control, including its horizontal padding.
     * @param frame Frame supplying measurement and metrics.
     * @param text Label text.
     * @param role Typographic role.
     * @return The control width in logical units.
     */
    [[nodiscard]] float studioLabelWidth(const StudioFrame& frame, std::string_view text,
                                         StudioFontRole role = StudioFontRole::Body);

    // --- Widgets ----------------------------------------------------------------------------------

    /**
     * @brief A push button.
     *
     * @param frame Frame to describe into.
     * @param id The button's identity.
     * @param bounds Its rectangle.
     * @param label Its text. A `"##"` suffix is used for identity and not drawn.
     * @param options Presentation and state.
     * @return What happened to it.
     */
    StudioWidgetResult studioButton(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                    std::string_view label, const StudioButtonOptions& options = {});

    /**
     * @brief A button that carries an on/off state the caller owns.
     *
     * @p checked is flipped in the input pass only, so the draw pass sees — and draws — the new
     * value on the same frame the user clicked.
     *
     * @param frame Frame to describe into.
     * @param id The toggle's identity.
     * @param bounds Its rectangle.
     * @param label Its text.
     * @param checked The state to show and flip.
     * @param options Presentation and state; `selected` is overridden by @p checked.
     * @return What happened to it; `changed` is true on the frame the state flipped.
     */
    StudioWidgetResult studioToggle(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                    std::string_view label, bool& checked,
                                    const StudioButtonOptions& options = {});

    /**
     * @brief A checkbox: a square indicator and a label beside it.
     *
     * @param frame Frame to describe into.
     * @param id The checkbox's identity.
     * @param bounds Its rectangle, including the label.
     * @param label Its text.
     * @param checked The state to show and flip.
     * @param enabled False to draw and route it as disabled.
     * @return What happened to it.
     */
    StudioWidgetResult studioCheckbox(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                      std::string_view label, bool& checked, bool enabled = true);

    /**
     * @brief One tab in a tab strip.
     *
     * @param frame Frame to describe into.
     * @param id The tab's identity.
     * @param bounds Its rectangle.
     * @param label Its text.
     * @param options Presentation and state.
     * @return What happened to it; `activated` is true on the frame it was chosen.
     */
    StudioWidgetResult studioTab(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                 std::string_view label, const StudioTabOptions& options = {});

    /**
     * @brief One title in the application menu bar.
     *
     * Reports what happened; whether the menu opens is the menu controller's decision, because
     * "click opens, and then hovering a neighbour switches without another click" is a property of
     * the *bar*, not of any one title in it.
     *
     * @param frame Frame to describe into.
     * @param id The title's identity.
     * @param bounds Its rectangle.
     * @param label Its text.
     * @param open True while this menu's popup is showing.
     * @param enabled False to draw and route it as disabled.
     * @return What happened to it.
     */
    StudioWidgetResult studioMenuBarItem(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                         std::string_view label, bool open, bool enabled = true);

    /**
     * @brief One row inside an open menu.
     *
     * @param frame Frame to describe into.
     * @param id The item's identity.
     * @param bounds Its rectangle.
     * @param label Its text.
     * @param options Enablement, check state, submenu arrow and shortcut hint.
     * @return What happened to it; `activated` is true on the frame it was chosen.
     */
    StudioWidgetResult studioMenuItem(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                      std::string_view label,
                                      const StudioMenuItemOptions& options = {});

    /**
     * @brief A draggable divider between two docked regions.
     *
     * The **grab** area is deliberately wider than the drawn divider. A 4-pixel splitter drawn at
     * 4 pixels is a 4-pixel target, which at 150% DPI on a trackpad is a target people miss; the
     * hit zone is widened on both sides so that the thing you can grab is bigger than the thing you
     * can see. Every professional tool does this and none of them mention it.
     *
     * @param frame Frame to describe into.
     * @param id The splitter's identity.
     * @param bounds The divider as drawn.
     * @param axis Which way it divides.
     * @param grabPadding Extra hit distance on each side, in logical units.
     * @return What it did this frame.
     */
    StudioSplitterResult studioSplitter(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                        StudioSplitterAxis axis, float grabPadding = 3.0f);

    /**
     * @brief A horizontal rule between groups of menu items.
     * @param frame Frame to draw into.
     * @param bounds The row the separator occupies.
     */
    void studioMenuSeparator(StudioFrame& frame, const UiRect& bounds);

    /**
     * @brief The height one menu row occupies, from theme metrics.
     * @param theme Theme supplying metrics.
     * @return Row height in logical units.
     */
    [[nodiscard]] float studioMenuItemHeight(const StudioTheme& theme);

    /**
     * @brief The height a separator row occupies inside a menu.
     * @param theme Theme supplying metrics.
     * @return Row height in logical units.
     */
    [[nodiscard]] float studioMenuSeparatorHeight(const StudioTheme& theme);

    // ---------------------------------------------------------------------------------------
    // Scrolling
    // ---------------------------------------------------------------------------------------

    /** @brief What a scroll view is being asked to show. */
    struct StudioScrollOptions
    {
        /** @brief Total height of the content, in logical units. */
        float contentHeight = 0.0f;

        /** @brief Total width of the content. Zero, or less than the view, means no horizontal bar. */
        float contentWidth = 0.0f;

        /**
         * @brief Keep the view pinned to the end as content grows.
         *
         * Honoured only while the user is already at the end. A console that yanked the view back
         * to the bottom while somebody was reading further up would be unusable, and "auto-scroll"
         * has never meant "take the scrollbar away from me".
         */
        bool stickToEnd = false;

        /** @brief How far one wheel notch scrolls, in logical units. Zero uses the theme's row height. */
        float wheelStep = 0.0f;
    };

    /** @brief A scroll view's resolved geometry and position. */
    struct StudioScrollResult
    {
        /**
         * @brief Where content should be drawn, excluding any scrollbar.
         *
         * In view coordinates, not content coordinates: a caller draws a row at
         * `viewport.top() - offsetY + rowIndex * rowHeight`.
         */
        UiRect viewport;

        /** @brief How far the content is scrolled down, in logical units. Never negative. */
        float offsetY = 0.0f;

        /** @brief How far the content is scrolled right, in logical units. */
        float offsetX = 0.0f;

        /** @brief Whether the view is showing the end of the content. */
        bool atEnd = true;

        /** @brief Whether a vertical scrollbar was needed. */
        bool hasVerticalBar = false;

        /**
         * @brief The range of rows worth describing, given a uniform row height.
         *
         * Culling by hand rather than relying on the clip is what keeps a hundred-thousand-line log
         * costing the same as a ten-line one: the clip stops the pixels, but only this stops the
         * work of measuring and laying out text that was never going to be seen.
         *
         * @param rowHeight Height of one row.
         * @param rowCount How many rows there are.
         * @param outFirst Receives the first visible row index.
         * @param outLast Receives one past the last visible row index.
         */
        void visibleRows(float rowHeight, std::size_t rowCount,
                         std::size_t& outFirst, std::size_t& outLast) const;
    };

    /**
     * @brief A scrollable region: wheel, draggable thumb, and a clipped viewport.
     *
     * Call it with the area the region occupies and the size of the content that goes in it, draw
     * the content into @ref StudioScrollResult::viewport offset by the returned position, then call
     * @ref studioEndScroll. It pushes a clip, so the two must be paired in both passes.
     *
     * The scroll position is retained state keyed by @p id, so it survives the frame and survives
     * the content changing underneath it.
     *
     * @param frame The frame.
     * @param id Identity of the view.
     * @param bounds Area the view occupies, scrollbar included.
     * @param options What the content is.
     * @return Where and how to draw the content.
     */
    StudioScrollResult studioBeginScroll(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                         const StudioScrollOptions& options);

    /** @brief Ends the region opened by @ref studioBeginScroll, popping its clip. */
    void studioEndScroll(StudioFrame& frame);

    // ---------------------------------------------------------------------------------------
    // Text entry
    // ---------------------------------------------------------------------------------------

    /** @brief How a text field behaves and what it says when empty. */
    struct StudioTextFieldOptions
    {
        /** @brief False to draw and route it read-only. */
        bool enabled = true;

        /** @brief Shown, dimmed, when the field is empty and unfocused. */
        std::string_view placeholder;

        /** @brief Typographic role. Monospace suits a number or an identifier. */
        StudioFontRole font = StudioFontRole::Body;

        /**
         * @brief Select everything when the field takes focus.
         *
         * What a property grid wants: tabbing to a number and typing should replace it, not append
         * to it. What a long free-text field does not want, because one keystroke then loses the
         * lot.
         */
        bool selectAllOnFocus = false;
    };

    /** @brief What a text field did this frame. */
    struct StudioTextFieldResult
    {
        /** @brief Hover, press, focus and disabled state. */
        StudioInteraction interaction;

        /**
         * @brief The value changed and was committed. Input pass only.
         *
         * Committed means Enter, or focus leaving the field. Not every keystroke: a property bound
         * to a field that wrote on every character would put a hundred entries in the undo stack
         * for one edit, and would re-validate a number while it is half-typed.
         */
        bool committed = false;

        /** @brief The user is editing: the text differs from @p value. Both passes. */
        bool editing = false;

        /** @brief The edit was abandoned with Escape. Input pass only. */
        bool cancelled = false;
    };

    /**
     * @brief An editable single-line text field.
     *
     * Click to place the caret, drag to select, Shift with the arrows and Home/End to extend,
     * Ctrl+A to select all, Ctrl+C/X/V through the frame's clipboard, Escape to abandon and Enter
     * to commit. The in-progress text is retained state keyed by @p id, so it survives the frames
     * between keystrokes and a value changing underneath it does not throw away what was typed.
     *
     * @param frame The frame.
     * @param id Identity of the field.
     * @param bounds Area it occupies.
     * @param value Read for the displayed value; written on commit.
     * @param options Behaviour.
     * @return What happened.
     */
    StudioTextFieldResult studioTextField(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                          std::string& value,
                                          const StudioTextFieldOptions& options = {});

    // ---------------------------------------------------------------------------------------
    // Drop-down
    // ---------------------------------------------------------------------------------------

    /** @brief How a drop-down behaves. */
    struct StudioDropdownOptions
    {
        /** @brief False to draw it dimmed and refuse interaction. */
        bool enabled = true;

        /** @brief Shown when the selection is out of range, e.g. `"(none)"`. */
        std::string_view placeholder = "";

        /** @brief Hover help, offered after the pointer rests. */
        std::string_view tooltip = "";

        /**
         * @brief How many rows the list shows before it scrolls.
         *
         * A list of every renderer or every font on the machine must not become a popup taller
         * than the window.
         */
        int visibleRows = 10;
    };

    /** @brief What a drop-down did this frame. */
    struct StudioDropdownResult
    {
        /** @brief Hover, press and focus of the closed control. */
        StudioInteraction interaction;

        /** @brief The selection changed. Input pass only. */
        bool changed = false;

        /** @brief The index now selected, or -1. */
        int selected = -1;

        /** @brief The list is showing. Both passes. */
        bool open = false;
    };

    /**
     * @brief A drop-down selection.
     *
     * Click or press Enter/Space/Down to open, arrows to move, Enter to choose, Escape or a press
     * elsewhere to dismiss. The list is a *deferred* popup, so it escapes the panel it sits in
     * rather than being clipped by it, and it flips above the control when there is no room below.
     *
     * @param frame The frame.
     * @param id Identity of the control.
     * @param bounds Area the closed control occupies.
     * @param items What can be chosen.
     * @param selected Index of the current selection, or -1. Written when the user chooses.
     * @param options Behaviour.
     * @return What happened.
     */
    StudioDropdownResult studioDropdown(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                        const std::vector<std::string>& items, int& selected,
                                        const StudioDropdownOptions& options = {});
} // namespace CNA::Studio
