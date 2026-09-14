// SPDX-License-Identifier: MS-PL
/**
 * @file StudioLogPanel.cpp
 * @brief The Output Log on the Studio UI.
 */

#include "CNA/Studio/UiCore/StudioLogPanel.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /** @brief The filter's display name for a severity, as the buttons spell it. */
        constexpr std::array<std::pair<LogSeverity, const char*>, 4> kSeverityNames{{
            {LogSeverity::Trace, "All"},
            {LogSeverity::Info, "Info"},
            {LogSeverity::Warning, "Warnings"},
            {LogSeverity::Error, "Errors"},
        }};

        /** @brief The severity prefix each row carries, matching what the clipboard text says. */
        std::string_view severityPrefix(LogSeverity severity)
        {
            switch (severity)
            {
                case LogSeverity::Trace: return "trace";
                case LogSeverity::Info: return "info";
                case LogSeverity::Warning: return "warn";
                case LogSeverity::Error: return "error";
            }
            return "info";
        }
    }

    StudioColor studioLogSeverityColor(const StudioTheme& theme, LogSeverity severity)
    {
        switch (severity)
        {
            case LogSeverity::Trace: return theme.color(StudioColorRole::TextDisabled);
            case LogSeverity::Info: return theme.color(StudioColorRole::TextPrimary);
            case LogSeverity::Warning: return theme.color(StudioColorRole::Warning);
            case LogSeverity::Error: return theme.color(StudioColorRole::Error);
        }
        return theme.color(StudioColorRole::TextPrimary);
    }

    StudioLogPanelResult studioLogPanel(StudioFrame& frame, const UiRect& bounds,
                                        const StudioLog& log)
    {
        StudioLogPanelResult result;
        const StudioTheme& theme = frame.theme();

        const float padding = metricOf(theme, StudioMetric::SpacingSmall);
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);
        const float controlHeight = metricOf(theme, StudioMetric::ControlHeightSmall);
        const float rowHeight = metricOf(theme, StudioMetric::RowHeight);

        UiRect area = bounds.inset(UiEdges{padding});
        if (area.width <= 0.0f || area.height <= 0.0f) { return result; }

        // -----------------------------------------------------------------------------------
        // Toolbar
        // -----------------------------------------------------------------------------------
        UiRect toolbar = area.splitTop(std::min(controlHeight, area.height));
        area.splitTop(std::min(spacing, area.height));

        // The filter is the panel's own view state, not a document property, so it is retained
        // per-widget rather than commanded: what a person chooses to look at is not an edit, and
        // putting it in the undo stack would be actively wrong.
        WidgetState& view = frame.state().get(frame.ids().make("logview"));

        const auto severityOf = [](std::int64_t index) {
            const auto clamped = static_cast<std::size_t>(
                std::clamp<std::int64_t>(index, 0, static_cast<std::int64_t>(kSeverityNames.size()) - 1));
            return kSeverityNames[clamped].first;
        };
        const LogSeverity minimumSeverity = severityOf(view.integer);

        const auto buttonWidth = [&](std::string_view label) {
            return std::ceil(studioLabelWidth(frame, label, StudioFontRole::BodySmall)
                             + metricOf(theme, StudioMetric::ControlPaddingHorizontal));
        };

        StudioButtonOptions buttonOptions;
        buttonOptions.font = StudioFontRole::BodySmall;

        {
            const UiRect copyBounds = toolbar.splitLeft(std::min(buttonWidth("Copy"), toolbar.width));
            StudioButtonOptions copyOptions = buttonOptions;
            copyOptions.enabled = !log.entries().empty();
            if (studioButton(frame, frame.ids().make("copy"), copyBounds, "Copy", copyOptions)
                    .activated)
            {
                result.copyRequested = true;
                result.copyText = log.toText(minimumSeverity);
            }
            toolbar.splitLeft(std::min(spacing, toolbar.width));

            const UiRect clearBounds = toolbar.splitLeft(std::min(buttonWidth("Clear"), toolbar.width));
            StudioButtonOptions clearOptions = buttonOptions;
            clearOptions.enabled = !log.entries().empty();
            result.cleared =
                studioButton(frame, frame.ids().make("clear"), clearBounds, "Clear", clearOptions)
                    .activated;
            toolbar.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingMedium), toolbar.width));
        }

        // Severity as a row of buttons rather than a dropdown: four options, all of them one click
        // away, and the current one readable without opening anything. A combo would cost a click
        // and hide three quarters of the answer.
        for (std::size_t i = 0; i < kSeverityNames.size(); ++i)
        {
            const char* label = kSeverityNames[i].second;
            const UiRect filterBounds = toolbar.splitLeft(std::min(buttonWidth(label), toolbar.width));
            if (filterBounds.width <= 0.0f) { break; }

            StudioButtonOptions options = buttonOptions;
            options.selected = static_cast<std::size_t>(view.integer) == i;
            if (studioButton(frame, frame.ids().make(label), filterBounds, label, options).activated)
            {
                view.integer = static_cast<std::int64_t>(i);
            }
            toolbar.splitLeft(std::min(spacing, toolbar.width));
        }

        // Follow sits at the right, away from the filters: it is the one control here that changes
        // what happens next rather than what is shown now.
        {
            const float followWidth = std::ceil(
                studioLabelWidth(frame, "Follow", StudioFontRole::BodySmall)
                + metricOf(theme, StudioMetric::IconSizeSmall)
                + metricOf(theme, StudioMetric::SpacingMedium));
            if (toolbar.width > followWidth)
            {
                const UiRect followBounds = toolbar.splitRight(followWidth);
                bool follow = view.checked;
                if (studioCheckbox(frame, frame.ids().make("follow"), followBounds, "Follow", follow)
                        .changed)
                {
                    view.checked = follow;
                }
            }
        }

        if (area.height <= 0.0f) { return result; }

        // -----------------------------------------------------------------------------------
        // The list
        // -----------------------------------------------------------------------------------
        // Filtered into indices rather than copied: a hundred thousand log lines copied every
        // frame, in two passes, is the difference between a console and a stall.
        std::vector<std::size_t> visible;
        visible.reserve(log.entries().size());
        for (std::size_t i = 0; i < log.entries().size(); ++i)
        {
            if (log.entries()[i].severity >= minimumSeverity) { visible.push_back(i); }
        }

        StudioScrollOptions scroll;
        scroll.contentHeight = static_cast<float>(visible.size()) * rowHeight;
        scroll.stickToEnd = view.checked;
        scroll.wheelStep = rowHeight * 3.0f;

        const StudioScrollResult view_ =
            studioBeginScroll(frame, frame.ids().make("scroll"), area, scroll);

        std::size_t first = 0;
        std::size_t last = 0;
        view_.visibleRows(rowHeight, visible.size(), first, last);

        result.rowsMatching = visible.size();
        result.rowsDrawn = last - first;

        if (frame.isDrawPass())
        {
            const float prefixWidth = std::ceil(
                studioLabelWidth(frame, "error", StudioFontRole::Monospace)
                + metricOf(theme, StudioMetric::SpacingMedium));

            for (std::size_t row = first; row < last; ++row)
            {
                const StudioLogEntry& entry = log.entries()[visible[row]];

                const UiRect rowBounds{
                    view_.viewport.left(),
                    std::round(view_.viewport.top() - view_.offsetY
                               + static_cast<float>(row) * rowHeight),
                    view_.viewport.width, rowHeight};

                // Alternating bands rather than separators: a thousand hairlines is visual noise,
                // and the eye tracks a long line across a shaded row without one.
                if (row % 2 == 1)
                {
                    frame.drawList().fillRect(rowBounds,
                                              theme.color(StudioColorRole::ControlBackground));
                }

                UiRect cursor = rowBounds.inset(
                    UiEdges{metricOf(theme, StudioMetric::SpacingSmall), 0.0f,
                            metricOf(theme, StudioMetric::SpacingSmall), 0.0f});
                const UiRect prefixBounds = cursor.splitLeft(std::min(prefixWidth, cursor.width));
                studioDrawText(frame, prefixBounds, severityPrefix(entry.severity),
                               StudioFontRole::Monospace,
                               studioLogSeverityColor(theme, entry.severity));

                std::string text = entry.message;
                if (entry.repeats > 1) { text += "  (x" + std::to_string(entry.repeats) + ")"; }

                studioDrawText(frame, cursor,
                               studioTruncateText(frame, theme.font(StudioFontRole::Monospace),
                                                  text, cursor.width),
                               StudioFontRole::Monospace,
                               studioLogSeverityColor(theme, entry.severity));
            }

            if (visible.empty())
            {
                // An empty state that says which of the two empties this is. "Nothing here" when a
                // filter is hiding four hundred errors is the panel lying to the user.
                const std::string message = log.entries().empty()
                    ? std::string{"No messages yet."}
                    : "No messages at this level. " + std::to_string(log.entries().size())
                          + " are hidden by the filter.";
                studioDrawText(frame,
                               view_.viewport.inset(
                                   UiEdges{metricOf(theme, StudioMetric::SpacingMedium)}),
                               message, StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }
        }

        studioEndScroll(frame);
        return result;
    }
}
