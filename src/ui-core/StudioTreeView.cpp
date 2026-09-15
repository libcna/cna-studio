// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTreeView.cpp
 * @brief The scrolling, selectable tree.
 */

#include "CNA/Studio/UiCore/StudioTreeView.hpp"

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

        /**
         * @brief Draws a disclosure triangle, pointing right when closed and down when open.
         *
         * Drawn rather than glyphed. Studio has no icon font yet (`STUDIO-04008`), and a triangle
         * is three points: reaching for a typeface to get one would be the wrong trade even once
         * there is a typeface to reach for.
         */
        void drawDisclosure(StudioFrame& frame, const UiRect& box, bool expanded, StudioColor color)
        {
            const float size = std::round(std::min(box.width, box.height) * 0.45f);
            if (size < 2.0f) { return; }

            const float centerX = std::round(box.centerX());
            const float centerY = std::round(box.centerY());

            if (expanded)
            {
                frame.drawList().fillTriangle(centerX - size, centerY - size * 0.5f,
                                             centerX + size, centerY - size * 0.5f,
                                             centerX, centerY + size * 0.7f, color);
            }
            else
            {
                frame.drawList().fillTriangle(centerX - size * 0.5f, centerY - size,
                                             centerX - size * 0.5f, centerY + size,
                                             centerX + size * 0.7f, centerY, color);
            }
        }
    }

    StudioTreeResult studioTreeView(StudioFrame& frame, const UiRect& bounds,
                                    const std::vector<StudioTreeRow>& rows,
                                    StudioTreeState& state,
                                    std::string_view emptyMessage)
    {
        StudioTreeResult result;
        const StudioTheme& theme = frame.theme();

        if (bounds.width <= 0.0f || bounds.height <= 0.0f) { return result; }

        const float rowHeight = std::max(metricOf(theme, StudioMetric::RowHeight),
                                         metricOf(theme, StudioMetric::MinimumHitTarget));
        const float indent = metricOf(theme, StudioMetric::IndentWidth);
        const float padding = metricOf(theme, StudioMetric::SpacingSmall);

        if (rows.empty())
        {
            if (frame.isDrawPass() && !emptyMessage.empty())
            {
                studioDrawText(frame, bounds.inset(UiEdges{metricOf(theme, StudioMetric::SpacingMedium)}),
                               emptyMessage, StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }
            return result;
        }

        StudioScrollOptions scroll;
        scroll.contentHeight = static_cast<float>(rows.size()) * rowHeight;
        scroll.wheelStep = rowHeight * 3.0f;

        const StudioScrollResult view =
            studioBeginScroll(frame, frame.ids().make("treescroll"), bounds, scroll);

        std::size_t first = 0;
        std::size_t last = 0;
        view.visibleRows(rowHeight, rows.size(), first, last);
        result.rowsDrawn = last - first;

        for (std::size_t index = first; index < last; ++index)
        {
            const StudioTreeRow& row = rows[index];

            const UiRect rowBounds{
                view.viewport.left(),
                std::round(view.viewport.top() - view.offsetY
                           + static_cast<float>(index) * rowHeight),
                view.viewport.width, rowHeight};

            frame.ids().push(row.id);

            // The whole row is the target, not just the text. A tree where a click lands only on
            // the label is a tree with a different hit area on every line.
            const StudioInteraction interaction =
                frame.interact(frame.ids().make("row"), rowBounds, row.enabled);

            UiRect cursor = rowBounds.inset(UiEdges{padding, 0.0f, padding, 0.0f});
            cursor.splitLeft(std::min(indent * static_cast<float>(row.depth), cursor.width));

            const UiRect disclosure = cursor.splitLeft(std::min(indent, cursor.width));
            bool expanded = state.isExpanded(row.id);

            if (row.hasChildren)
            {
                // Its own widget, so clicking the triangle opens the row rather than selecting it.
                // Those are different intentions and a tree that conflated them would make it
                // impossible to look inside a group without also selecting it.
                const StudioInteraction toggle =
                    frame.interact(frame.ids().make("disclosure"), disclosure);
                if (frame.isInputPass() && toggle.clicked)
                {
                    expanded = !expanded;
                    state.setExpanded(row.id, expanded);
                    result.toggled = index;
                }
                if (frame.isDrawPass())
                {
                    drawDisclosure(frame, disclosure, expanded,
                                   theme.color(toggle.hovered ? StudioColorRole::TextPrimary
                                                              : StudioColorRole::TextSecondary));
                }
            }

            if (frame.isInputPass() && interaction.clicked && !result.toggled.has_value())
            {
                result.clicked = index;
                result.additive = frame.input().modifiers.control
                               || frame.input().modifiers.shift;
            }

            if (frame.isDrawPass())
            {
                if (row.selected)
                {
                    frame.drawList().fillRect(rowBounds, theme.color(StudioColorRole::Selection));
                }
                else if (interaction.hovered)
                {
                    frame.drawList().fillRect(rowBounds,
                                              theme.color(StudioColorRole::ControlBackgroundHover));
                }

                const StudioColorRole labelRole =
                    !row.enabled ? StudioColorRole::TextDisabled
                                 : (row.selected ? StudioColorRole::TextPrimary
                                                 : StudioColorRole::TextPrimary);

                UiRect labelArea = cursor;
                if (!row.detail.empty())
                {
                    const float detailWidth =
                        std::ceil(studioLabelWidth(frame, row.detail, StudioFontRole::BodySmall)
                                  + metricOf(theme, StudioMetric::SpacingMedium));
                    if (labelArea.width > detailWidth * 2.0f)
                    {
                        const UiRect detailArea = labelArea.splitRight(detailWidth);
                        studioDrawText(frame, detailArea, row.detail, StudioFontRole::BodySmall,
                                       theme.color(StudioColorRole::TextSecondary),
                                       StudioTextAlign::Right);
                    }
                }

                studioDrawText(frame, labelArea,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body),
                                                  row.label, labelArea.width),
                               StudioFontRole::Body, theme.color(labelRole));

                if (interaction.focused)
                {
                    frame.drawList().drawFocusRing(rowBounds,
                                                   theme.color(StudioColorRole::FocusRing),
                                                   metricOf(theme, StudioMetric::FocusRingWidth));
                }
            }

            frame.ids().pop();
        }

        studioEndScroll(frame);
        return result;
    }
}
