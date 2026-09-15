// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDialog.cpp
 * @brief A window that owns the frame until it is answered.
 */

#include "CNA/Studio/UiCore/StudioDialog.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /** @brief The buttons a request offers, with the default filled in. */
        std::vector<std::string> buttonsOf(const StudioDialogRequest& request)
        {
            if (!request.buttons.empty()) { return request.buttons; }
            return {std::string{"OK"}};
        }

        /** @brief The index Enter activates: the affirmative one unless the request says otherwise. */
        int defaultIndexOf(const StudioDialogRequest& request, std::size_t count)
        {
            if (count == 0) { return -1; }
            if (request.defaultButton >= 0 && request.defaultButton < static_cast<int>(count))
            {
                return request.defaultButton;
            }
            return static_cast<int>(count) - 1;
        }

        /** @brief How wide the widest line is, or zero. */
        float widestLine(const StudioFrame& frame, const StudioDialogRequest& request)
        {
            float widest = 0.0f;
            for (const std::string& line : request.lines)
            {
                widest = std::max(widest, studioLabelWidth(frame, line));
            }
            return widest;
        }
    }

    UiRect studioDialogBounds(const StudioFrame& frame, const UiRect& window,
                              const StudioDialogRequest& request)
    {
        const StudioTheme& theme = frame.theme();
        const float pad = metricOf(theme, StudioMetric::SpacingLarge);
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float titleHeight = metricOf(theme, StudioMetric::PanelHeaderHeight);
        const float lineHeight = std::ceil(frame.measureText(StudioFontRole::Body, "Ag").height()
                                           + metricOf(theme, StudioMetric::SpacingXSmall));

        const std::vector<std::string> buttons = buttonsOf(request);

        float buttonRow = 0.0f;
        for (const std::string& label : buttons)
        {
            buttonRow += std::ceil(studioLabelWidth(frame, label)) + pad * 2.0f
                       + metricOf(theme, StudioMetric::SpacingSmall);
        }

        // Wide enough for whichever of the three is widest -- the title, the longest line, or the
        // button row -- rather than a fixed width that truncates one of them.
        float width = std::max({widestLine(frame, request),
                                std::ceil(studioLabelWidth(frame, request.title)) + titleHeight,
                                buttonRow});
        width += pad * 2.0f;
        width = std::max(width, rowHeight * 10.0f);

        float height = titleHeight + pad;
        height += static_cast<float>(request.lines.size()) * lineHeight;
        if (request.hasTextField) { height += pad + rowHeight; }
        height += pad + rowHeight + pad;

        width = std::min(width, std::max(rowHeight * 4.0f, window.width - pad * 2.0f));
        height = std::min(height, std::max(rowHeight * 4.0f, window.height - pad * 2.0f));

        // Centred horizontally and a little above centre vertically, which is where the eye
        // already is and where every desktop toolkit puts a dialog.
        return UiRect{std::round(window.left() + (window.width - width) * 0.5f),
                      std::round(window.top() + (window.height - height) * 0.4f),
                      std::round(width), std::round(height)};
    }

    std::vector<UiRect> studioDialogButtonBounds(const StudioFrame& frame, const UiRect& window,
                                                 const StudioDialogRequest& request)
    {
        const StudioTheme& theme = frame.theme();
        const std::vector<std::string> buttons = buttonsOf(request);

        const UiRect bounds = studioDialogBounds(frame, window, request);
        const float pad = metricOf(theme, StudioMetric::SpacingLarge);
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float titleHeight = metricOf(theme, StudioMetric::PanelHeaderHeight);
        const float gap = metricOf(theme, StudioMetric::SpacingSmall);

        UiRect remaining = bounds;
        remaining.splitTop(std::min(titleHeight, remaining.height));

        UiRect buttonRow = remaining.inset(UiEdges{pad, pad});
        buttonRow.splitTop(std::max(0.0f, buttonRow.height - rowHeight));

        // Right to left, so the affirmative button ends up nearest the corner the pointer travels
        // to and a row that grew from the left would not move every button whenever one of them
        // was reworded.
        std::vector<UiRect> boxes(buttons.size());
        UiRect cursor = buttonRow;
        for (std::size_t i = buttons.size(); i-- > 0;)
        {
            const float width = std::ceil(studioLabelWidth(frame, buttons[i])) + pad * 2.0f;
            boxes[i] = cursor.splitRight(std::min(width, std::max(0.0f, cursor.width)));
            cursor.splitRight(gap);
        }
        return boxes;
    }

    StudioDialogResult studioDialog(StudioFrame& frame, const UiRect& window,
                                    const StudioDialogRequest& request, StudioDialogState& state)
    {
        StudioDialogResult result;

        const StudioTheme& theme = frame.theme();
        const std::vector<std::string> buttons = buttonsOf(request);
        const int defaultIndex = defaultIndexOf(request, buttons.size());

        const bool opening = !state.seeded;
        if (opening)
        {
            state.text = request.text;
            state.seeded = true;
        }
        result.text = state.text;

        const UiRect bounds = studioDialogBounds(frame, window, request);
        const float pad = metricOf(theme, StudioMetric::SpacingLarge);
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float titleHeight = metricOf(theme, StudioMetric::PanelHeaderHeight);
        const float gap = metricOf(theme, StudioMetric::SpacingSmall);
        const float lineHeight = std::ceil(frame.measureText(StudioFontRole::Body, "Ag").height()
                                           + metricOf(theme, StudioMetric::SpacingXSmall));

        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(bounds, theme.color(StudioColorRole::PanelBackground));
            frame.drawList().strokeRect(bounds, theme.color(StudioColorRole::Accent),
                                        metricOf(theme, StudioMetric::BorderWidth));
        }

        UiRect remaining = bounds;
        const UiRect titleBar = remaining.splitTop(std::min(titleHeight, remaining.height));
        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(titleBar, theme.color(StudioColorRole::PanelHeader));
            studioDrawText(frame, titleBar.inset(UiEdges{pad, 0.0f}), request.title,
                           StudioFontRole::Heading, theme.color(StudioColorRole::TextPrimary));
        }

        UiRect body = remaining.inset(UiEdges{pad, pad});

        if (frame.isDrawPass())
        {
            UiRect cursor = body;
            for (const std::string& line : request.lines)
            {
                if (cursor.height <= 0.0f) { break; }
                studioDrawText(frame, cursor.splitTop(lineHeight), line, StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }
        }
        body.splitTop(static_cast<float>(request.lines.size()) * lineHeight);

        frame.ids().push("dialog");

        if (request.hasTextField)
        {
            body.splitTop(gap);
            const UiRect field = body.splitTop(std::min(rowHeight, body.height));
            StudioTextFieldOptions options;
            options.placeholder = request.placeholder;
            options.selectAllOnFocus = true;
            const WidgetId fieldId = frame.ids().make("text");
            // The field first when there is one, because a dialog that asks for a name and starts
            // with the keyboard on a button makes the user reach for the mouse to answer it.
            if (opening && frame.isInputPass()) { frame.router().setFocus(fieldId); }

            // The commit is not what answers the dialog: a text field reports `committed` only
            // when the value actually *changed*, which is right for a property grid and wrong
            // here -- Enter on a name the user did not edit still means "yes, that one".
            (void)studioTextField(frame, fieldId, field, state.text, options);
            if (frame.isInputPass()) { result.text = state.text; }
        }

        // The same layout the accessor above derives, so a caller that points at a button and the
        // dialog that draws one cannot disagree. The text field, when there is one, sits above the
        // row and does not move it: the row is measured from the bottom of the dialog.
        const std::vector<UiRect> boxes = studioDialogButtonBounds(frame, window, request);

        const bool textReady = !request.requireText || !state.text.empty();
        for (std::size_t i = 0; i < buttons.size(); ++i)
        {
            StudioButtonOptions options;
            // Only the affirmative one is refused on an empty field: Cancel must always work, and
            // a dialog whose every button is dead is a dialog with no way out.
            options.enabled = textReady || static_cast<int>(i) != defaultIndex;

            const WidgetId buttonId = frame.ids().make(buttons[i]);
            if (opening && !request.hasTextField && static_cast<int>(i) == defaultIndex
                && frame.isInputPass())
            {
                frame.router().setFocus(buttonId);
            }

            if (studioButton(frame, buttonId, boxes[i], buttons[i], options).activated)
            {
                result.chosen = static_cast<int>(i);
            }
        }

        frame.ids().pop();

        if (!frame.isInputPass()) { return result; }

        // Tab is the router's, not the dialog's: a widget the current layer blocks does not
        // register as focusable, so while the modal is open its own controls are the whole Tab
        // ring and the traversal cannot walk out into the panels it covers.
        if (frame.router().keyPressed(UiKey::Escape))
        {
            if (request.cancelButton >= 0 && request.cancelButton < static_cast<int>(buttons.size()))
            {
                result.chosen = request.cancelButton;
                result.dismissed = true;
            }
            else if (request.dismissable)
            {
                result.dismissed = true;
            }
        }
        else if (result.chosen < 0 && frame.router().keyPressed(UiKey::Enter) && defaultIndex >= 0
                 && textReady)
        {
            // Only when no button claimed it. A focused button activates on Enter itself, and
            // overwriting that with the default would make Enter on a focused Cancel mean Discard
            // -- the one keystroke a confirmation dialog must never get wrong.
            result.chosen = defaultIndex;
        }

        return result;
    }
}
