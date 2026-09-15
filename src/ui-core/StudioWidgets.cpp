// SPDX-License-Identifier: MS-PL
/**
 * @file StudioWidgets.cpp
 * @brief Button, toggle, checkbox, tab, menu-bar title and menu-item behaviour and appearance.
 */

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioTextEdit.hpp"
#include "CNA/Studio/UiCore/StudioTextMeasure.hpp"

#include <algorithm>
#include <cmath>

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
        /** @brief Draws a down-pointing chevron inside a box, as two strokes. */
        void drawDropdownArrow(StudioFrame& frame, const UiRect& box, StudioColor color)
        {
            const float thickness = std::max(1.0f, box.height * 0.10f);
            const float left = box.centerX() - box.width * 0.22f;
            const float right = box.centerX() + box.width * 0.22f;
            const float top = box.centerY() - box.height * 0.09f;
            const float bottom = box.centerY() + box.height * 0.13f;

            StudioDrawList& list = frame.drawList();
            list.drawLine(left, top, box.centerX(), bottom, color, thickness);
            list.drawLine(box.centerX(), bottom, right, top, color, thickness);
        }

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
        if (text.empty() || box.isEmpty()) { return UiRect{}; }

        const StudioFontStyle style = frame.theme().font(role);
        const std::string fitted = studioTruncateText(frame, style, text, box.width);
        if (fitted.empty()) { return UiRect{}; }

        const StudioTextMetrics metrics = frame.measureText(style, fitted);

        // Whole pixels. A baseline at a fractional y lands every glyph in the line on a half pixel,
        // which is the difference between text that looks crisp and text that looks faintly
        // smeared at exactly the sizes a UI uses.
        const float baseline = std::round(metrics.centeredBaseline(box.top(), box.height));

        float x = box.left();
        if (align == StudioTextAlign::Center)
        {
            x = box.left() + std::max(0.0f, std::round((box.width - metrics.width) * 0.5f));
        }
        else if (align == StudioTextAlign::Right)
        {
            x = box.right() - std::min(metrics.width, box.width);
        }
        x = std::round(x);

        const UiRect ink{x, baseline - metrics.ascent, metrics.width, metrics.height()};

        StudioFontAtlas* atlas = frame.fontAtlas();
        if (atlas == nullptr)
        {
            // No atlas: the measured box, at reduced alpha, so a build without fonts looks
            // visibly unfinished rather than silently empty.
            if (frame.isDrawPass()) { frame.drawList().drawTextPlaceholder(ink, color); }
            return ink;
        }

        if (frame.isInputPass())
        {
            // Rasterise now, while there is still a whole pass before anything is drawn. Doing it
            // during the draw pass works too -- the atlas upload is emitted at end of frame for
            // exactly that reason -- but doing it here keeps the draw pass free of allocation.
            atlas->prepare(style, fitted);
            return ink;
        }
        if (!frame.isDrawPass()) { return ink; }

        const StudioFontFace& face = atlas->face(style);
        StudioDrawList& list = frame.drawList();

        float pen = x;
        char32_t previous = 0;
        std::size_t offset = 0;
        while (offset < fitted.size())
        {
            const char32_t codepoint = StudioFontAtlas::decodeUtf8(fitted, offset);
            if (codepoint == 0) { break; }

            // Kerning is applied before the glyph, not after the previous one, so a run that is
            // clipped mid-word still positions every glyph it does draw exactly where an unclipped
            // run would have.
            if (previous != 0) { pen += face.kerning(previous, codepoint); }

            const StudioGlyph* glyph = face.glyph(codepoint);
            if (glyph == nullptr) { previous = codepoint; continue; }

            if (glyph->hasInk())
            {
                const UiRect quad{std::round(pen + glyph->bearingX),
                                  std::round(baseline + glyph->bearingY),
                                  static_cast<float>(glyph->width),
                                  static_cast<float>(glyph->height)};
                list.drawGlyph(quad, glyph->u0, glyph->v0, glyph->u1, glyph->v1,
                               StudioFontAtlas::kTextureId, color);
            }

            pen += glyph->advance;
            previous = codepoint;
        }

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

        // Offered even when disabled: "why is this greyed out" is exactly the moment somebody
        // hovers for an explanation, and a tooltip that vanished then would be missing at the one
        // time it is most wanted.
        if (!options.tooltip.empty()) { (void)frame.requestTooltip(id, options.tooltip, bounds); }

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
        const std::string_view visible = WidgetIdStack::visibleLabel(label);
        const bool showLabel = !options.iconOnly && !visible.empty();

        if (options.icon != StudioIcon::None)
        {
            const float iconSize = metricOf(theme, StudioMetric::IconSize);

            if (!showLabel)
            {
                // The whole button. Centring the icon in the control is what makes a row of
                // icon-only buttons line up, however wide each one happens to be.
                studioDrawIcon(frame, bounds, options.icon, textColor);
            }
            else
            {
                // Icon and label as one unit, centred together. Placing the icon at a fixed inset
                // and centring the label separately makes every button look subtly off-balance,
                // and differently off-balance depending on the length of its word.
                const float gap = metricOf(theme, StudioMetric::SpacingSmall);
                const float labelWidth = studioLabelWidth(frame, visible, options.font);
                const float total = iconSize + gap + labelWidth;
                const float left = options.align == StudioTextAlign::Left
                    ? bounds.left() + padding
                    : std::round(bounds.centerX() - total * 0.5f);

                studioDrawIcon(frame, UiRect{left, bounds.top(), iconSize, bounds.height},
                               options.icon, textColor);
                studioDrawText(frame,
                               UiRect{left + iconSize + gap, bounds.top(), labelWidth, bounds.height},
                               visible, options.font, textColor, StudioTextAlign::Left);
            }
        }
        else if (showLabel)
        {
            studioDrawText(frame, bounds.inset(UiEdges{padding, 0.0f}), visible, options.font,
                           textColor, options.align);
        }

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

    StudioSplitterResult studioSplitter(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                        StudioSplitterAxis axis, float grabPadding)
    {
        const StudioTheme& theme = frame.theme();
        const bool horizontal = axis == StudioSplitterAxis::Horizontal;

        const UiRect grab = horizontal
            ? UiRect{bounds.left() - grabPadding, bounds.top(),
                     bounds.width + grabPadding * 2.0f, bounds.height}
            : UiRect{bounds.left(), bounds.top() - grabPadding,
                     bounds.width, bounds.height + grabPadding * 2.0f};

        StudioSplitterResult result;
        // Not a tab stop: Tab moves between things a keyboard can operate, and a splitter is not
        // one of them yet. Panel resizing from the keyboard belongs with the layout commands.
        result.interaction = frame.interact(id, grab, /*enabled=*/true);
        result.dragging = result.interaction.held;

        if (result.interaction.hovered || result.interaction.held)
        {
            frame.requestCursor(id, horizontal ? StudioCursor::ResizeHorizontal
                                               : StudioCursor::ResizeVertical);
        }

        if (frame.isInputPass() && result.interaction.held)
        {
            result.delta = horizontal ? frame.router().mouseDeltaX() : frame.router().mouseDeltaY();
        }

        if (!frame.isDrawPass()) { return result; }

        StudioColorRole role = StudioColorRole::AppBackground;
        if (result.interaction.held) { role = StudioColorRole::Accent; }
        else if (result.interaction.hovered) { role = StudioColorRole::Border; }
        frame.drawList().fillRect(bounds, theme.color(role));

        return result;
    }

    float studioMenuItemHeight(const StudioTheme& theme)
    {
        // Already whole: theme metrics are integers, so every row boundary in a menu lands on a
        // pixel at every DPI scale rather than accumulating a fraction down the list.
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

        // The router's *winner*, not this widget's own hit test. `interaction.hovered` is true for
        // every widget whose rectangle holds the pointer, and menu popups overlap -- a submenu
        // flipped to the left sits on top of its parent. Reading the hit test would light up both
        // the row the user is on and the one hidden underneath it.
        const bool emphasised = options.highlighted || frame.router().hoveredId() == id;
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

    // ---------------------------------------------------------------------------------------
    // Scrolling
    // ---------------------------------------------------------------------------------------

    void StudioScrollResult::visibleRows(float rowHeight, std::size_t rowCount,
                                         std::size_t& outFirst, std::size_t& outLast) const
    {
        outFirst = 0;
        outLast = 0;
        if (rowHeight <= 0.0f || rowCount == 0) { return; }

        const auto first = static_cast<std::size_t>(std::max(0.0f, std::floor(offsetY / rowHeight)));
        if (first >= rowCount) { outFirst = outLast = rowCount; return; }

        // One row of slack at each end, so a row scrolled half out of view is still described and
        // the edge of the list does not pop in and out as the offset crosses a row boundary.
        const auto visible =
            static_cast<std::size_t>(std::ceil(viewport.height / rowHeight)) + std::size_t{2};

        outFirst = first;
        outLast = std::min(rowCount, first + visible);
    }

    StudioScrollResult studioBeginScroll(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                         const StudioScrollOptions& options)
    {
        const StudioTheme& theme = frame.theme();

        StudioScrollResult result;
        result.viewport = bounds;

        const float contentHeight = std::max(0.0f, options.contentHeight);
        const float maximumOffset = std::max(0.0f, contentHeight - bounds.height);
        result.hasVerticalBar = maximumOffset > 0.0f && bounds.height > 0.0f;

        const float thickness = metricOf(theme, StudioMetric::ScrollbarThickness);
        UiRect track;
        if (result.hasVerticalBar)
        {
            UiRect area = bounds;
            track = area.splitRight(std::min(thickness, area.width));
            result.viewport = area;
        }

        WidgetState& state = frame.state().get(id);

        // Only the input pass moves the view. The draw pass reads what it left, so both passes
        // agree about where the content is -- which is what makes a click land on the row the user
        // saw rather than the row that was there before the wheel turned.
        if (frame.isInputPass())
        {
            // Sticking to the end is honoured only while the view is *already* there. A console
            // that yanked the view back down while somebody was reading further up would be
            // unusable, and it is the single most common complaint about log windows.
            //
            // Measured against how far the content reached *last* frame, not this one. Against the
            // grown content the view is never already at the end -- that is the whole reason it
            // grew -- so comparing with the new maximum would mean following never once engaged.
            const float previousMaximum = state.scalar;
            const bool wasAtEnd = state.scrollY >= previousMaximum - 0.5f;

            if (bounds.contains(frame.input().mouseX, frame.input().mouseY)
                && frame.input().wheelY != 0.0f)
            {
                const float step = options.wheelStep > 0.0f
                                 ? options.wheelStep
                                 : metricOf(theme, StudioMetric::RowHeight) * 3.0f;
                state.scrollY -= frame.input().wheelY * step;
            }
            else if (options.stickToEnd && wasAtEnd)
            {
                state.scrollY = maximumOffset;
            }

            state.scrollY = std::clamp(state.scrollY, 0.0f, maximumOffset);
            state.scalar = maximumOffset;
        }

        result.offsetY = std::clamp(state.scrollY, 0.0f, maximumOffset);
        result.atEnd = result.offsetY >= maximumOffset - 0.5f;

        if (result.hasVerticalBar)
        {
            // A thumb whose length is its share of the content, floored at something a person can
            // actually grab: proportional all the way down means a million-line log gets a thumb
            // one pixel high, which is a scrollbar in name only.
            const float minimumThumb = std::max(metricOf(theme, StudioMetric::MinimumHitTarget),
                                                thickness * 2.0f);
            const float proportion = contentHeight > 0.0f ? bounds.height / contentHeight : 1.0f;
            const float thumbHeight =
                std::max(minimumThumb, std::min(track.height, track.height * proportion));

            const float travel = std::max(0.0f, track.height - thumbHeight);
            const float position = maximumOffset > 0.0f ? result.offsetY / maximumOffset : 0.0f;

            const UiRect thumb{track.left(), std::round(track.top() + travel * position),
                               track.width, std::round(thumbHeight)};

            const WidgetId thumbId = frame.ids().make("scrollthumb");
            const StudioInteraction interaction = frame.interact(thumbId, thumb);

            if (frame.isInputPass() && interaction.pressed && travel > 0.0f)
            {
                // Dragged by where the pointer is *within* the thumb, not by the frame's delta:
                // grabbing the thumb an inch from its top and dragging must not teleport it so the
                // pointer sits at its centre, and accumulating deltas drifts away from the pointer
                // over a long drag.
                WidgetState& thumbState = frame.state().get(thumbId);
                if (!thumbState.active)
                {
                    thumbState.active = true;
                    thumbState.scalar = frame.input().mouseY - thumb.top();
                }

                const float wanted = frame.input().mouseY - thumbState.scalar - track.top();
                state.scrollY = std::clamp(wanted / travel, 0.0f, 1.0f) * maximumOffset;
                result.offsetY = state.scrollY;
                result.atEnd = result.offsetY >= maximumOffset - 0.5f;
            }
            else if (frame.isInputPass())
            {
                frame.state().get(thumbId).active = false;
            }

            if (frame.isDrawPass())
            {
                frame.drawList().fillRect(track, theme.color(StudioColorRole::ScrollbarTrack));
                frame.drawList().fillRect(
                    thumb, theme.color(interaction.pressed || interaction.hovered
                                           ? StudioColorRole::ScrollbarThumbHover
                                           : StudioColorRole::ScrollbarThumb));
            }
        }

        frame.pushClip(result.viewport);
        return result;
    }

    void studioEndScroll(StudioFrame& frame)
    {
        frame.popClip();
    }

    // ---------------------------------------------------------------------------------------
    // Text entry
    // ---------------------------------------------------------------------------------------

    namespace
    {
        /**
         * @brief Converts the frame's typed UTF-16 code units to UTF-8.
         *
         * Surrogate pairs included, because an emoji typed into a name field is a user typing a
         * character, and dropping half of a pair leaves a string that is not valid UTF-16 or UTF-8.
         * An unpaired surrogate -- which a platform can emit when a composition is interrupted --
         * is dropped rather than encoded: there is no character to encode.
         */
        std::string utf8From(const std::vector<char16_t>& units)
        {
            std::string out;
            out.reserve(units.size());

            for (std::size_t i = 0; i < units.size(); ++i)
            {
                char32_t code = units[i];

                if (code >= 0xD800 && code <= 0xDBFF)
                {
                    if (i + 1 >= units.size()) { continue; }
                    const char16_t low = units[i + 1];
                    if (low < 0xDC00 || low > 0xDFFF) { continue; }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
                else if (code >= 0xDC00 && code <= 0xDFFF)
                {
                    continue;
                }

                // Control characters are not text. Tab and Enter mean something to the field and
                // are handled as keys; the rest would be invisible bytes in somebody's entity name.
                if (code < 0x20 || code == 0x7F) { continue; }

                if (code < 0x80) { out.push_back(static_cast<char>(code)); }
                else if (code < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else if (code < 0x10000)
                {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
            }
            return out;
        }

        /**
         * @brief The byte offset in @p text nearest to @p x, measuring from @p left.
         *
         * Nearest boundary rather than the one before: clicking in the right half of a character
         * must put the caret after it, which is where a person aiming between two letters expects
         * it. Measuring prefix by prefix is O(n) per click over a single-line field, which is
         * nothing; a field long enough for that to matter needs a different layout anyway.
         */
        std::size_t offsetNearest(const StudioFrame& frame, const StudioFontStyle& style,
                                  std::string_view text, float left, float x)
        {
            std::size_t best = 0;
            float bestDistance = std::abs(x - left);

            std::size_t offset = 0;
            while (offset < text.size())
            {
                offset = studioUtf8Next(text, offset);
                const float edge =
                    left + frame.measureText(style, text.substr(0, offset)).width;
                const float distance = std::abs(x - edge);
                if (distance < bestDistance) { bestDistance = distance; best = offset; }
            }
            return best;
        }
    }

    StudioTextFieldResult studioTextField(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                          std::string& value,
                                          const StudioTextFieldOptions& options)
    {
        StudioTextFieldResult result;
        const StudioTheme& theme = frame.theme();
        const StudioFontStyle style = theme.font(options.font);

        // Registered before interact(), like every other control: the router assigns focus on a
        // press to a widget it knows is focusable, and one that never registered is one Tab skips
        // and a click never focuses.
        if (frame.isInputPass() && options.enabled) { frame.router().registerFocusable(id, true); }

        result.interaction = frame.interact(id, bounds, options.enabled);
        WidgetState& state = frame.state().get(id);

        const bool focused = result.interaction.focused && options.enabled;

        // The editing buffer is the retained text; `value` is the committed one. Keeping them
        // apart is what lets Escape restore the old value and what stops a half-typed number being
        // parsed into the document on every keystroke.
        // The press, not the focus, starts an edit session. The router grants focus *after* the
        // frame a press happened on -- it reports the focused widget as of the start of the frame
        // and reassigns it during interact() -- so a session that waited for focus would begin on
        // a frame where nothing says the pointer was involved, and select-all-on-focus would then
        // wipe a field somebody merely clicked into.
        const bool pressedHere = result.interaction.pressed && options.enabled;
        const bool editing = focused || pressedHere;

        if (!editing)
        {
            state.text = value;
            state.caret = value.size();
            state.selectionAnchor = value.size();
            state.active = false;
        }
        else if (!state.active)
        {
            state.active = true;
            state.text = value;
            state.caret = value.size();

            // Select-all applies to focus arriving from the *keyboard*. Tabbing to a number and
            // typing should replace it; clicking into a name and typing must not destroy it,
            // because a click is how a person says "I want the caret here". The pointer branch
            // below places the caret for that case, so all that is needed here is to not select.
            state.selectionAnchor =
                (options.selectAllOnFocus && !pressedHere) ? 0 : value.size();
        }

        StudioTextEdit edit{state.text};
        edit.moveTo(state.selectionAnchor, false);
        edit.moveTo(state.caret, true);

        const UiRect textArea =
            bounds.inset(UiEdges{metricOf(theme, StudioMetric::ControlPaddingHorizontal) * 0.5f,
                                 0.0f});

        if (editing && frame.isInputPass())
        {
            // Said every frame the field is focused, so the shell's shortcut dispatch and the
            // keyboard-activation path in the other widgets know a key means text here.
            frame.router().setWantsTextInput(true);

            const UiInputState& input = frame.input();
            const bool shift = input.modifiers.shift;
            const bool control = input.modifiers.control;
            const StudioInputRouter& router = frame.router();

            if (control && router.keyPressed(UiKey::A)) { edit.selectAll(); }
            else if (control && router.keyPressed(UiKey::C))
            {
                if (edit.hasSelection()) { frame.setClipboardText(edit.selectedText()); }
            }
            else if (control && router.keyPressed(UiKey::X))
            {
                if (edit.hasSelection())
                {
                    frame.setClipboardText(edit.selectedText());
                    (void)edit.deleteSelection();
                }
            }
            else if (control && router.keyPressed(UiKey::V))
            {
                const std::string pasted = frame.clipboardText();
                if (!pasted.empty())
                {
                    // One line. A pasted paragraph in a single-line field would otherwise carry
                    // newlines the measurer cannot render and the document would store verbatim.
                    std::string flattened;
                    flattened.reserve(pasted.size());
                    for (const char character : pasted)
                    {
                        flattened.push_back(character == '\n' || character == '\r' ? ' ' : character);
                    }
                    (void)edit.insert(flattened);
                }
            }
            else if (router.keyPressed(UiKey::LeftArrow)) { edit.moveLeft(shift); }
            else if (router.keyPressed(UiKey::RightArrow)) { edit.moveRight(shift); }
            else if (router.keyPressed(UiKey::Home)) { edit.moveHome(shift); }
            else if (router.keyPressed(UiKey::End)) { edit.moveEnd(shift); }
            else if (router.keyPressed(UiKey::Backspace)) { (void)edit.deleteBackward(); }
            else if (router.keyPressed(UiKey::Delete)) { (void)edit.deleteForward(); }
            else if (router.keyPressed(UiKey::Escape))
            {
                edit.setText(value);
                edit.selectAll();
                result.cancelled = true;
                frame.router().setFocus(WidgetId{});
            }
            else if (router.keyPressed(UiKey::Enter))
            {
                if (edit.text() != value) { value = edit.text(); result.committed = true; }
                frame.router().setFocus(WidgetId{});
            }

            if (!result.cancelled)
            {
                const std::string typed = utf8From(input.characters);
                if (!typed.empty()) { (void)edit.insert(typed); }
            }
        }

        // Pointer, in both passes so the caret the input pass hit-tested is the one drawn.
        if (options.enabled && (result.interaction.pressed || result.interaction.held))
        {
            const std::size_t offset =
                offsetNearest(frame, style, edit.text(), textArea.left(), frame.input().mouseX);
            edit.moveTo(offset, !result.interaction.pressed);
        }

        if (frame.isInputPass())
        {
            state.text = edit.text();
            state.caret = edit.caret();
            state.selectionAnchor = edit.anchor();
        }

        // Focus lost with an uncommitted edit commits it. Abandoning somebody's typing because
        // they clicked elsewhere is the behaviour every form gets wrong and nobody forgives.
        if (frame.isInputPass() && !editing && state.active)
        {
            state.active = false;
            if (state.text != value) { value = state.text; result.committed = true; }
        }

        result.editing = editing && edit.text() != value;

        if (options.enabled) { (void)frame.requestCursor(id, StudioCursor::Text); }

        if (frame.isDrawPass())
        {
            const StudioColorRole background =
                !options.enabled ? StudioColorRole::ControlBackgroundDisabled
                                 : (editing ? StudioColorRole::ControlBackgroundSelected
                                            : (result.interaction.hovered
                                                   ? StudioColorRole::ControlBackgroundHover
                                                   : StudioColorRole::ControlBackground));
            frame.drawList().fillRect(bounds, theme.color(background));
            frame.drawList().strokeRect(bounds,
                                        theme.color(editing ? StudioColorRole::FocusRing
                                                            : StudioColorRole::Border),
                                        metricOf(theme, StudioMetric::BorderWidth));

            const std::string_view shown = edit.text();
            if (shown.empty() && !editing && !options.placeholder.empty())
            {
                studioDrawText(frame, textArea, options.placeholder, options.font,
                               theme.color(StudioColorRole::TextDisabled));
            }
            else
            {
                if (editing && edit.hasSelection())
                {
                    const float from =
                        frame.measureText(style, shown.substr(0, edit.selectionBegin())).width;
                    const float to =
                        frame.measureText(style, shown.substr(0, edit.selectionEnd())).width;
                    frame.drawList().fillRect(
                        UiRect{textArea.left() + from, bounds.top() + 2.0f, to - from,
                               std::max(0.0f, bounds.height - 4.0f)},
                        theme.color(StudioColorRole::Selection));
                }

                studioDrawText(frame, textArea,
                               studioTruncateText(frame, style, shown, textArea.width),
                               options.font,
                               theme.color(options.enabled ? StudioColorRole::TextPrimary
                                                           : StudioColorRole::TextDisabled));

                if (editing)
                {
                    // Steady, not blinking. Studio has no animation model yet (STUDIO-03030), and
                    // a caret that blinks off is a caret a golden image catches half the time --
                    // which would make every text screenshot in the suite nondeterministic.
                    const float caretX = std::round(
                        textArea.left()
                        + frame.measureText(style, shown.substr(0, edit.caret())).width);
                    frame.drawList().fillRect(
                        UiRect{caretX, bounds.top() + 2.0f,
                               std::max(1.0f, metricOf(theme, StudioMetric::BorderWidth)),
                               std::max(0.0f, bounds.height - 4.0f)},
                        theme.color(StudioColorRole::TextPrimary));
                }
            }
        }

        return result;
    }

    // ---------------------------------------------------------------------------------------
    // Drag and drop
    // ---------------------------------------------------------------------------------------

    void studioDrawDragPreview(StudioFrame& frame)
    {
        if (!frame.isDragging() || !frame.isDrawPass()) { return; }

        const std::string& label = frame.dragPayload().label;
        if (label.empty()) { return; }

        const StudioTheme& theme = frame.theme();
        const float padding = metricOf(theme, StudioMetric::SpacingSmall);
        const StudioTextMetrics extent = frame.measureText(StudioFontRole::BodySmall, label);
        const UiRect box = frame.dragPreviewBounds(std::ceil(extent.width + padding * 2.0f),
                                                   std::ceil(extent.height() + padding));

        // Clipped to the window rather than to whatever was in force: the preview follows the
        // pointer across panels, and the clip of the panel it started in would cut it in half.
        frame.pushClip(UiRect{0.0f, 0.0f, frame.input().displayWidth,
                              frame.input().displayHeight});
        frame.drawList().fillRect(box, theme.color(StudioColorRole::PopupBackground));
        frame.drawList().strokeRect(box, theme.color(StudioColorRole::Accent),
                                    metricOf(theme, StudioMetric::BorderWidth));
        studioDrawText(frame, box.inset(UiEdges{padding, 0.0f}), label, StudioFontRole::BodySmall,
                       theme.color(StudioColorRole::TextPrimary));
        frame.popClip();
    }

    bool studioDragSource(StudioFrame& frame, WidgetId source,
                          const StudioInteraction& interaction,
                          StudioFrame::StudioDragPayload payload)
    {
        if (!frame.isInputPass() || !interaction.held || frame.isDragging()) { return false; }

        // The button has to be down *now*, not merely have been held. On the frame it comes up a
        // widget still reports `held` -- that is what lets a click resolve -- and starting a drag
        // there would begin a gesture on the frame it ended, which is how an abandoned drag
        // resurrects itself and delivers the payload it was told not to.
        if (!frame.input().isMouseDown(UiMouseButton::Left)) { return false; }

        const float threshold = metricOf(frame.theme(), StudioMetric::SpacingLarge);
        const float dx = frame.input().mouseX - frame.router().pressX();
        const float dy = frame.input().mouseY - frame.router().pressY();
        if (dx * dx + dy * dy <= threshold * threshold) { return false; }

        return frame.beginDrag(source, std::move(payload));
    }

    // ---------------------------------------------------------------------------------------
    // Drop-down
    // ---------------------------------------------------------------------------------------

    namespace
    {
        /**
         * @brief A drop-down's pending choice, stored as index + 1.
         *
         * Zero has to mean "nothing pending", because zero is what retained state holds the first
         * time a widget is seen -- and a sentinel that collides with the default is a control that
         * silently selects its first item the moment it is described.
         */
        constexpr float kNoPendingChoice = 0.0f;

        // A drop-down's retained state, in one place so the meanings are readable together:
        //
        //   integer  the highlighted row of the open list
        //   scalar   the pending choice, as index + 1, or kNoPendingChoice
        //   checked  a pending dismissal
        //   active   the list opened on this very frame
        //   text     the index wearing the check mark, as decimal
        //
        // Four of the five exist because the list is described *after* the control and can only
        // answer it through something that outlives the call.
    }

    StudioDropdownResult studioDropdown(StudioFrame& frame, WidgetId id, const UiRect& bounds,
                                        const std::vector<std::string>& items, int& selected,
                                        const StudioDropdownOptions& options)
    {
        const StudioTheme& theme = frame.theme();
        StudioDropdownResult result;

        // A control with nothing to choose from is disabled rather than one that opens on an empty
        // list: an empty popup is a rectangle the user has to click away.
        const bool enabled = options.enabled && !items.empty();

        WidgetState& state = frame.state().get(id);

        // The list is described *after* this function has returned -- that is what lets it escape
        // the panel it sits in -- so its answer cannot come back through the return value. It is
        // left in the control's own retained state and collected here, on the next pass that
        // routes input. One frame of latency, and the alternative was a reference into a stack
        // frame that has already gone.
        if (frame.isInputPass())
        {
            if (state.scalar != kNoPendingChoice)
            {
                const auto chosen = static_cast<int>(state.scalar) - 1;
                state.scalar = kNoPendingChoice;
                if (chosen >= 0 && static_cast<std::size_t>(chosen) < items.size()
                    && chosen != selected)
                {
                    selected = chosen;
                    result.changed = true;
                }
                if (frame.isPopupOpen(id)) { frame.closePopup(); }
            }
            if (state.checked)
            {
                state.checked = false;
                if (frame.isPopupOpen(id)) { frame.closePopup(); }
            }
        }

        result.selected = selected;
        const bool wasOpen = frame.isPopupOpen(id);

        StudioWidgetResult control =
            interactControl(frame, id, bounds, enabled, /*focusable=*/true, StudioCursor::Arrow);
        result.interaction = control.interaction;

        if (frame.isInputPass() && enabled)
        {
            if (wasOpen)
            {
                // Only the pointer closes it from here. While the list is open the keyboard
                // belongs to the list: `activated` includes Enter on the focused control, and
                // honouring that would shut the list on the keystroke meant to choose from it.
                if (control.interaction.clicked) { frame.closePopup(); }
            }
            else if (control.activated
                     || (control.interaction.focused
                         && frame.router().keyPressed(UiKey::DownArrow)))
            {
                frame.openPopup(id);
                // Opened on what is already selected, so the first Down moves off it rather than
                // jumping to the top of a list the user is part-way through.
                state.integer = selected;
                state.scalar = kNoPendingChoice;
                state.checked = false;
                // The same Down that opened the list must not also move within it: a keystroke
                // that opens a list on the current value and immediately steps off it means the
                // user can never choose the value they started on without going back up.
                state.active = true;
            }
        }

        result.open = frame.isPopupOpen(id);

        // --- The closed control --------------------------------------------------------------
        if (frame.isDrawPass())
        {
            const StudioControlState visual = !enabled
                ? StudioControlState::Disabled
                : resolveState(control.interaction, result.open);

            const float radius = metricOf(theme, StudioMetric::CornerRadius);
            frame.drawList().fillRoundedRect(bounds, theme.controlBackground(visual), radius);
            if (visual != StudioControlState::Disabled)
            {
                frame.drawList().strokeRect(bounds, theme.color(StudioColorRole::Border),
                                            metricOf(theme, StudioMetric::BorderWidth));
            }

            UiRect inner = bounds.inset(
                UiEdges{metricOf(theme, StudioMetric::ControlPaddingHorizontal), 0.0f});
            const UiRect arrow = inner.splitRight(metricOf(theme, StudioMetric::IconSize));
            drawDropdownArrow(frame, arrow, theme.controlText(visual));

            const bool inRange = selected >= 0 && static_cast<std::size_t>(selected) < items.size();
            const std::string_view shown = inRange
                ? std::string_view{items[static_cast<std::size_t>(selected)]}
                : options.placeholder;
            studioDrawText(frame, inner,
                           studioTruncateText(frame, theme.font(StudioFontRole::Body), shown,
                                              inner.width),
                           StudioFontRole::Body,
                           inRange ? theme.controlText(visual)
                                   : theme.color(StudioColorRole::TextDisabled));
        }

        if (!options.tooltip.empty()) { (void)frame.requestTooltip(id, options.tooltip, bounds); }
        if (!result.open) { return result; }

        // --- The list, deferred so it escapes whatever panel this control is in ----------------
        //
        // Items are captured by value. A caller that builds its list inline -- every renderer, or
        // every enum case -- hands this a vector that is gone by the time the body runs, and a
        // reference would be the kind of dangling capture that works in every test and fails on
        // the one panel that does it.
        const float rowHeight = studioMenuItemHeight(theme);
        const float padding = metricOf(theme, StudioMetric::SpacingSmall);
        const auto visibleRows = static_cast<float>(std::max(1, options.visibleRows));
        const float listHeight =
            std::min(static_cast<float>(items.size()), visibleRows) * rowHeight + padding * 2.0f;

        float listTop = bounds.bottom();
        // Flipped above rather than clipped: a list whose last rows fall off the bottom of the
        // window is a list whose last options do not exist as far as the user is concerned.
        if (listTop + listHeight > frame.input().displayHeight)
        {
            listTop = std::max(0.0f, bounds.top() - listHeight);
        }
        const UiRect list{bounds.left(), std::round(listTop), bounds.width, std::round(listHeight)};

        frame.deferPopup([id, list, items, rowHeight, padding](StudioFrame& f) {
            const StudioTheme& popupTheme = f.theme();
            if (f.isDrawPass())
            {
                f.drawList().fillRect(list, popupTheme.color(StudioColorRole::PopupBackground));
                f.drawList().strokeRect(list, popupTheme.color(StudioColorRole::BorderStrong),
                                        metricOf(popupTheme, StudioMetric::BorderWidth));
            }

            WidgetState& popupState = f.state().get(id);
            f.ids().push("dropdown");
            f.ids().pushIndex(static_cast<std::int64_t>(id.value()));

            StudioScrollOptions scrollOptions;
            scrollOptions.contentHeight = static_cast<float>(items.size()) * rowHeight;

            const StudioScrollResult scroll = studioBeginScroll(
                f, f.ids().make("scroll"), list.inset(UiEdges{0.0f, padding}), scrollOptions);

            std::size_t first = 0;
            std::size_t last = 0;
            scroll.visibleRows(rowHeight, items.size(), first, last);

            for (std::size_t i = first; i < last; ++i)
            {
                const UiRect row{scroll.viewport.left(),
                                 std::round(scroll.viewport.top() - scroll.offsetY
                                            + static_cast<float>(i) * rowHeight),
                                 scroll.viewport.width, rowHeight};

                StudioMenuItemOptions rowOptions;
                rowOptions.highlighted = popupState.integer == static_cast<std::int64_t>(i);
                // The current value carries a check rather than only a highlight: the highlight
                // follows the pointer, so on its own it says where the user is, never where they
                // are coming from.
                rowOptions.checkable = true;
                rowOptions.checked = popupState.text == std::to_string(i);

                const StudioWidgetResult rowResult = studioMenuItem(
                    f, f.ids().makeIndex(static_cast<std::int64_t>(i)), row, items[i], rowOptions);

                if (!f.isInputPass()) { continue; }
                if (rowResult.interaction.hovered)
                {
                    popupState.integer = static_cast<std::int64_t>(i);
                }
                if (rowResult.activated)
                {
                    popupState.scalar = static_cast<float>(i) + 1.0f;
                }
            }

            studioEndScroll(f);
            f.ids().pop();
            f.ids().pop();

            if (!f.isInputPass()) { return; }

            if (popupState.active)
            {
                // Opened this frame: the keystroke that opened it is still down, and it belongs to
                // the control rather than to the list.
                popupState.active = false;
                return;
            }

            StudioInputRouter& router = f.router();
            const auto count = static_cast<std::int64_t>(items.size());
            if (router.keyPressed(UiKey::DownArrow))
            {
                popupState.integer =
                    popupState.integer < 0 ? 0 : (popupState.integer + 1) % count;
            }
            if (router.keyPressed(UiKey::UpArrow))
            {
                popupState.integer =
                    popupState.integer < 0 ? count - 1 : (popupState.integer - 1 + count) % count;
            }
            if (router.keyPressed(UiKey::Home)) { popupState.integer = 0; }
            if (router.keyPressed(UiKey::End)) { popupState.integer = count - 1; }
            if (router.keyPressed(UiKey::Enter) && popupState.integer >= 0)
            {
                popupState.scalar = static_cast<float>(popupState.integer) + 1.0f;
            }
            if (router.keyPressed(UiKey::Escape)) { popupState.checked = true; }

            // A press outside the list dismisses it without choosing anything, which is the other
            // half of "click elsewhere to cancel".
            if (router.mousePressed(UiMouseButton::Left)
                && !list.contains(router.mouseX(), router.mouseY()))
            {
                popupState.checked = true;
            }
        });

        // Which row wears the check. Kept as text rather than as another number because `integer`
        // is the highlight and `scalar` is the pending choice, and a third meaning crammed into
        // one of those is how retained state stops being readable.
        if (frame.isInputPass())
        {
            state.text = selected >= 0 ? std::to_string(selected) : std::string{};
        }
        return result;
    }

} // namespace CNA::Studio
