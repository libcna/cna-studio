// SPDX-License-Identifier: MS-PL
/**
 * @file StudioWidgets.cpp
 * @brief Button, toggle, checkbox, tab, menu-bar title and menu-item behaviour and appearance.
 */

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include "CNA/Studio/UiCore/StudioTextMeasure.hpp"

#include <algorithm>

namespace CNA::Studio
{
    namespace
    {
        /** @brief The ellipsis used when a label does not fit, as UTF-8. */
        constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /**
         * @brief Whether the keyboard asked to activate the focused widget this frame.
         *
         * Ignored while a text field is taking input: Space is a character there, and a UI in
         * which typing a sentence presses buttons is not one anybody can use.
         *
         * @param frame Frame supplying the router.
         * @return True when Space or Enter went down and text input is not wanted.
         */
        bool keyboardActivationRequested(const StudioFrame& frame)
        {
            const StudioInputRouter& router = frame.router();
            if (router.wantsTextInput()) { return false; }
            return router.keyPressed(UiKey::Space) || router.keyPressed(UiKey::Enter);
        }

        /**
         * @brief Resolves the theme state a control should draw in.
         *
         * `Selected` is not one of @ref StudioInteraction's answers because selection is the
         * caller's concept, not the router's. It outranks hover at rest and yields to an active
         * press, which is the order that makes a chosen toolbar button still respond visibly to
         * being pressed again.
         *
         * @param interaction What the router reported.
         * @param selected Whether the caller considers the control chosen.
         * @return The state to resolve theme colours with.
         */
        StudioControlState resolveState(const StudioInteraction& interaction, bool selected)
        {
            if (interaction.disabled) { return StudioControlState::Disabled; }
            if (interaction.held) { return StudioControlState::Pressed; }
            if (selected) { return StudioControlState::Selected; }
            if (interaction.hovered) { return StudioControlState::Hover; }
            return StudioControlState::Normal;
        }

        /** @brief Draws the focus indicator just inside a control. */
        void drawFocus(StudioFrame& frame, const UiRect& bounds)
        {
            const StudioTheme& theme = frame.theme();
            frame.drawList().drawFocusRing(bounds, theme.color(StudioColorRole::FocusRing),
                                           metricOf(theme, StudioMetric::FocusRingWidth));
        }

        /**
         * @brief The shared body of every activatable control.
         *
         * Registers the tab stop, routes the pointer, folds in keyboard activation and honours the
         * cursor request -- once, so that four widgets cannot end up with four slightly different
         * ideas of what a click is.
         *
         * @param frame Frame to describe into.
         * @param id The widget's identity.
         * @param bounds Its rectangle.
         * @param enabled False for a widget that must not respond.
         * @param focusable False to leave it out of the Tab order.
         * @param cursor Cursor requested while it owns the pointer.
         * @param activateOnRelease True for menu-style controls that commit on button-up.
         * @return What happened to it.
         */
        StudioWidgetResult interactControl(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                           bool enabled, bool focusable, StudioCursor cursor,
                                           bool activateOnRelease = false)
        {
            StudioWidgetResult result;

            if (frame.isInputPass() && enabled && focusable)
            {
                frame.router().registerFocusable(id, true);
            }

            result.interaction = frame.interact(id, bounds, enabled);

            if (result.interaction.hovered && enabled)
            {
                frame.requestCursor(id, cursor);
            }

            if (!frame.isInputPass() || !enabled) { return result; }

            // A click is press and release on the same widget; a menu item commits on release
            // inside it even when the press began on the title that opened the menu, which is what
            // makes press-drag-release through a menu work the way every desktop menu does.
            const bool pointerActivated = activateOnRelease
                ? result.interaction.releasedOver
                : result.interaction.clicked;

            result.activated = pointerActivated
                || (result.interaction.focused && keyboardActivationRequested(frame));
            return result;
        }

        /** @brief Draws a check mark inside a box, as two strokes. */
        void drawCheckMark(StudioFrame& frame, const UiRect& box, StudioColor color)
        {
            const float thickness = std::max(1.0f, box.height * 0.14f);
            const float left = box.left() + box.width * 0.20f;
            const float middleX = box.left() + box.width * 0.42f;
            const float right = box.left() + box.width * 0.82f;
            const float middleY = box.top() + box.height * 0.52f;
            const float bottom = box.top() + box.height * 0.74f;
            const float top = box.top() + box.height * 0.26f;

            StudioDrawList& list = frame.drawList();
            list.drawLine(left, middleY, middleX, bottom, color, thickness);
            list.drawLine(middleX, bottom, right, top, color, thickness);
        }

        /** @brief Draws a right-pointing submenu arrow inside a box. */
        void drawSubmenuArrow(StudioFrame& frame, const UiRect& box, StudioColor color)
        {
            const float w = box.width * 0.34f;
            const float h = box.height * 0.30f;
            const float cx = box.centerX();
            const float cy = box.centerY();
            frame.drawList().fillTriangle(cx - w * 0.4f, cy - h, cx - w * 0.4f, cy + h,
                                          cx + w * 0.6f, cy, color);
        }
    } // namespace

    std::string studioTruncateText(const StudioFrame& frame, const StudioFontStyle& style,
                                   std::string_view text, float maxWidth)
    {
        if (maxWidth <= 0.0f) { return {}; }
        if (frame.measureText(style, text).width <= maxWidth) { return std::string{text}; }

        const float ellipsisWidth = frame.measureText(style, kEllipsis).width;
        if (ellipsisWidth > maxWidth) { return std::string{kEllipsis}; }

        // Walk code points, not bytes. Cutting a multi-byte character in half produces a sequence
        // no decoder reads, and the glyph that replaces it is wider than the one it replaced --
        // so a byte-wise truncation can overflow the very box it was called to fit.
        std::size_t fit = 0;
        for (std::size_t i = 1; i <= text.size(); ++i)
        {
            if (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0U) == 0x80U)
            {
                continue;
            }
            std::string candidate{text.substr(0, i)};
            candidate += kEllipsis;
            if (frame.measureText(style, candidate).width > maxWidth) { break; }
            fit = i;
        }

        if (fit == 0) { return std::string{kEllipsis}; }
        std::string result{text.substr(0, fit)};
        result += kEllipsis;
        return result;
    }

    UiRect studioDrawText(StudioFrame& frame, const UiRect& box, std::string_view text,
                          StudioFontRole role, StudioColor color, StudioTextAlign align)
    {
        if (!frame.isDrawPass() || text.empty() || box.isEmpty()) { return UiRect{}; }

        const StudioFontStyle style = frame.theme().font(role);
        const std::string fitted = studioTruncateText(frame, style, text, box.width);
        if (fitted.empty()) { return UiRect{}; }

        const StudioTextMetrics metrics = frame.measureText(style, fitted);
        const float baseline = metrics.centeredBaseline(box.top(), box.height);

        float x = box.left();
        if (align == StudioTextAlign::Center)
        {
            x = box.left() + std::max(0.0f, (box.width - metrics.width) * 0.5f);
        }
        else if (align == StudioTextAlign::Right)
        {
            x = box.right() - std::min(metrics.width, box.width);
        }

        // Positioned from the baseline rather than from the box, so that the day glyphs replace
        // the placeholder nothing above this line has to move (STUDIO-04006).
        const UiRect ink{x, baseline - metrics.ascent, metrics.width, metrics.height()};
        frame.drawList().drawTextPlaceholder(ink, color);
        return ink;
    }

    float studioLabelWidth(const StudioFrame& frame, std::string_view text, StudioFontRole role)
    {
        const float padding = metricOf(frame.theme(), StudioMetric::ControlPaddingHorizontal);
        return frame.measureText(role, WidgetIdStack::visibleLabel(text)).width + padding * 2.0f;
    }

    StudioWidgetResult studioButton(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                    std::string_view label, const StudioButtonOptions& options)
    {
        const StudioTheme& theme = frame.theme();
        StudioWidgetResult result = interactControl(frame, id, bounds, options.enabled,
                                                    options.focusable, options.cursor);
        if (!frame.isDrawPass()) { return result; }

        const StudioControlState state = resolveState(result.interaction, options.selected);
        const float radius = metricOf(theme, StudioMetric::CornerRadius);

        switch (options.kind)
        {
            case StudioButtonKind::Normal:
                frame.drawList().fillRoundedRect(bounds, theme.controlBackground(state), radius);
                if (state != StudioControlState::Disabled)
                {
                    frame.drawList().strokeRect(bounds, theme.color(StudioColorRole::Border),
                                                metricOf(theme, StudioMetric::BorderWidth));
                }
                break;
            case StudioButtonKind::Accent:
                frame.drawList().fillRoundedRect(bounds, theme.accent(state), radius);
                break;
            case StudioButtonKind::Toolbar:
                // No surface at rest. A toolbar of twenty filled squares reads as a wall; the fill
                // appearing on hover is what tells the user which one they are about to press.
                if (state != StudioControlState::Normal)
                {
                    frame.drawList().fillRoundedRect(bounds, theme.controlBackground(state), radius);
                }
                break;
            case StudioButtonKind::Ghost:
                if (state == StudioControlState::Hover || state == StudioControlState::Pressed)
                {
                    frame.drawList().fillRoundedRect(bounds, theme.controlBackground(state), radius);
                }
                break;
        }

        StudioColor textColor = theme.controlText(state);
        if (options.kind == StudioButtonKind::Accent && state != StudioControlState::Disabled)
        {
            textColor = theme.color(StudioColorRole::AccentForeground);
        }
        else if (options.kind == StudioButtonKind::Toolbar && state == StudioControlState::Normal
                 && !options.selected)
        {
            textColor = theme.color(StudioColorRole::TextSecondary);
        }

        const float padding = metricOf(theme, StudioMetric::ControlPaddingHorizontal);
        studioDrawText(frame, bounds.inset(UiEdges{padding, 0.0f}),
                       WidgetIdStack::visibleLabel(label), options.font, textColor, options.align);

        if (result.interaction.focused) { drawFocus(frame, bounds); }
        return result;
    }

    StudioWidgetResult studioToggle(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                    std::string_view label, bool& checked,
                                    const StudioButtonOptions& options)
    {
        StudioButtonOptions effective = options;
        effective.selected = checked;

        StudioWidgetResult result = studioButton(frame, id, bounds, label, effective);
        if (result.activated)
        {
            // Input pass only -- studioButton only sets `activated` there -- so the draw pass that
            // follows reads and draws the new value. Flipping in both passes would return it to
            // where it started before anybody saw it move.
            checked = !checked;
            result.changed = true;
        }
        return result;
    }

    StudioWidgetResult studioCheckbox(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                      std::string_view label, bool& checked, bool enabled)
    {
        const StudioTheme& theme = frame.theme();
        StudioWidgetResult result =
            interactControl(frame, id, bounds, enabled, /*focusable=*/true, StudioCursor::Arrow);

        if (result.activated)
        {
            checked = !checked;
            result.changed = true;
        }

        if (!frame.isDrawPass()) { return result; }

        const StudioControlState state = resolveState(result.interaction, /*selected=*/false);
        const float indicator = std::min(metricOf(theme, StudioMetric::IconSize), bounds.height);
        UiRect row = bounds;
        UiRect box = row.splitLeft(indicator);
        box.y = bounds.top() + (bounds.height - indicator) * 0.5f;
        box.height = indicator;

        if (checked)
        {
            frame.drawList().fillRoundedRect(box, theme.accent(state),
                                             metricOf(theme, StudioMetric::CornerRadius));
            drawCheckMark(frame, box,
                          state == StudioControlState::Disabled
                              ? theme.color(StudioColorRole::TextDisabled)
                              : theme.color(StudioColorRole::AccentForeground));
        }
        else
        {
            frame.drawList().fillRoundedRect(box, theme.controlBackground(state),
                                             metricOf(theme, StudioMetric::CornerRadius));
            frame.drawList().strokeRect(box, theme.color(StudioColorRole::Border),
                                        metricOf(theme, StudioMetric::BorderWidth));
        }

        const float gap = metricOf(theme, StudioMetric::SpacingSmall);
        row.splitLeft(gap);
        studioDrawText(frame, row, WidgetIdStack::visibleLabel(label), StudioFontRole::Body,
                       theme.controlText(state), StudioTextAlign::Left);

        if (result.interaction.focused) { drawFocus(frame, bounds); }
        return result;
    }

    StudioWidgetResult studioTab(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                 std::string_view label, const StudioTabOptions& options)
    {
        const StudioTheme& theme = frame.theme();
        StudioWidgetResult result = interactControl(frame, id, bounds, options.enabled,
                                                    /*focusable=*/true, StudioCursor::Arrow);
        if (!frame.isDrawPass()) { return result; }

        const StudioControlState state = resolveState(result.interaction, options.active);

        if (options.active)
        {
            frame.drawList().fillRect(bounds, theme.color(StudioColorRole::PanelHeaderActive));

            // An accent rule along the top edge, because the active tab's fill merges into the
            // panel body beneath it and a fill alone leaves "which tab is this" ambiguous.
            UiRect marker = bounds;
            marker.height = std::max(1.0f, metricOf(theme, StudioMetric::FocusRingWidth) * 2.0f);
            frame.drawList().fillRect(marker, theme.color(StudioColorRole::Accent));
        }
        else if (state == StudioControlState::Hover || state == StudioControlState::Pressed)
        {
            frame.drawList().fillRect(bounds, theme.controlBackground(state));
        }

        const float padding = metricOf(theme, StudioMetric::SpacingMedium);
        UiRect label_area = bounds.inset(UiEdges{padding, 0.0f});

        if (options.modified)
        {
            // A dot rather than an asterisk in the text: the marker must not change the label's
            // width, or every tab in the strip shifts when a document is edited.
            const float dot = metricOf(theme, StudioMetric::SpacingSmall);
            UiRect marker = label_area.splitRight(dot * 2.0f);
            frame.drawList().fillRoundedRect(
                UiRect{marker.centerX() - dot * 0.5f, marker.centerY() - dot * 0.5f, dot, dot},
                theme.color(StudioColorRole::TextSecondary), dot * 0.5f);
        }

        studioDrawText(frame, label_area, WidgetIdStack::visibleLabel(label), StudioFontRole::Body,
                       options.enabled
                           ? theme.color(options.active ? StudioColorRole::TextPrimary
                                                        : StudioColorRole::TextSecondary)
                           : theme.color(StudioColorRole::TextDisabled),
                       StudioTextAlign::Left);

        if (result.interaction.focused) { drawFocus(frame, bounds); }
        return result;
    }

    StudioWidgetResult studioMenuBarItem(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                         std::string_view label, bool open, bool enabled)
    {
        const StudioTheme& theme = frame.theme();
        StudioWidgetResult result =
            interactControl(frame, id, bounds, enabled, /*focusable=*/true, StudioCursor::Arrow);
        if (!frame.isDrawPass()) { return result; }

        const StudioControlState state = resolveState(result.interaction, open);
        if (open)
        {
            frame.drawList().fillRect(bounds, theme.color(StudioColorRole::PopupBackground));
        }
        else if (state == StudioControlState::Hover || state == StudioControlState::Pressed)
        {
            frame.drawList().fillRect(bounds, theme.controlBackground(state));
        }

        const float padding = metricOf(theme, StudioMetric::SpacingMedium);
        studioDrawText(frame, bounds.inset(UiEdges{padding, 0.0f}),
                       WidgetIdStack::visibleLabel(label), StudioFontRole::Body,
                       enabled ? theme.color(StudioColorRole::TextPrimary)
                               : theme.color(StudioColorRole::TextDisabled),
                       StudioTextAlign::Center);

        if (result.interaction.focused && !open) { drawFocus(frame, bounds); }
        return result;
    }

    float studioMenuItemHeight(const StudioTheme& theme)
    {
        return std::max(metricOf(theme, StudioMetric::RowHeight),
                        metricOf(theme, StudioMetric::MinimumHitTarget));
    }

    float studioMenuSeparatorHeight(const StudioTheme& theme)
    {
        return metricOf(theme, StudioMetric::SpacingMedium);
    }

    void studioMenuSeparator(StudioFrame& frame, const UiRect& bounds)
    {
        if (!frame.isDrawPass() || bounds.isEmpty()) { return; }
        const StudioTheme& theme = frame.theme();
        const float inset = metricOf(theme, StudioMetric::SpacingMedium);
        frame.drawList().drawHorizontalSeparator(
            UiRect{bounds.left() + inset, bounds.centerY(), std::max(0.0f, bounds.width - inset * 2.0f),
                   0.0f},
            theme.color(StudioColorRole::Separator),
            metricOf(theme, StudioMetric::SeparatorThickness));
    }

    StudioWidgetResult studioMenuItem(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                      std::string_view label, const StudioMenuItemOptions& options)
    {
        const StudioTheme& theme = frame.theme();
        StudioWidgetResult result =
            interactControl(frame, id, bounds, options.enabled, /*focusable=*/false,
                            StudioCursor::Arrow, /*activateOnRelease=*/true);

        if (!frame.isDrawPass()) { return result; }

        const bool emphasised = options.highlighted || result.interaction.hovered;
        if (emphasised && options.enabled)
        {
            frame.drawList().fillRect(bounds, theme.color(StudioColorRole::Selection));
        }

        const StudioColor textColor = options.enabled
            ? theme.color(StudioColorRole::TextPrimary)
            : theme.color(StudioColorRole::TextDisabled);

        const float padding = metricOf(theme, StudioMetric::SpacingMedium);
        UiRect row = bounds.inset(UiEdges{padding, 0.0f});

        // The check column is reserved whether or not this item is checked, so that the labels of
        // a menu's items line up instead of stepping left and right as toggles change.
        const float checkColumn = metricOf(theme, StudioMetric::IconSize);
        UiRect check = row.splitLeft(checkColumn);
        if (options.checkable && options.checked)
        {
            drawCheckMark(frame, check.inset(UiEdges{0.0f, check.height * 0.25f}),
                          options.enabled ? theme.color(StudioColorRole::Accent) : textColor);
        }
        row.splitLeft(metricOf(theme, StudioMetric::SpacingSmall));

        if (options.hasSubmenu)
        {
            UiRect arrow = row.splitRight(checkColumn);
            drawSubmenuArrow(frame, arrow, textColor);
        }
        else if (!options.shortcut.empty())
        {
            const float hintWidth = frame.measureText(StudioFontRole::BodySmall, options.shortcut).width;
            UiRect hint = row.splitRight(std::min(hintWidth + padding, row.width));
            studioDrawText(frame, hint, options.shortcut, StudioFontRole::BodySmall,
                           options.enabled ? theme.color(StudioColorRole::TextSecondary) : textColor,
                           StudioTextAlign::Right);
        }

        studioDrawText(frame, row, WidgetIdStack::visibleLabel(label), StudioFontRole::Body,
                       textColor, StudioTextAlign::Left);
        return result;
    }
} // namespace CNA::Studio
