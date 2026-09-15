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

            const bool renaming = !state.renaming().empty() && state.renaming() == row.id;

            // The whole row is the target, not just the text. A tree where a click lands only on
            // the label is a tree with a different hit area on every line.
            //
            // Except while it is being renamed. A row that is a text field must not also be a
            // selectable, draggable row: clicking to place the caret would reselect, and dragging
            // to select a word would pick the entity up.
            const StudioInteraction interaction = renaming
                ? StudioInteraction{}
                : frame.interact(frame.ids().make("row"), rowBounds, row.enabled);

            UiRect cursor = rowBounds.inset(UiEdges{padding, 0.0f, padding, 0.0f});
            cursor.splitLeft(std::min(indent * static_cast<float>(row.depth), cursor.width));

            const UiRect disclosure = cursor.splitLeft(std::min(indent, cursor.width));
            bool expanded = state.isExpanded(row.id);
            bool disclosureHovered = false;

            if (row.hasChildren && !renaming)
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
                // Remembered rather than drawn here. The row's background -- selection, hover,
                // the alternating fill and the indent guides -- is decided further down, and
                // drawing the triangle first put every one of those fills straight over it. The
                // symptom was that expandable rows lost their triangle on alternate lines only,
                // which reads as a data problem rather than as a painting order.
                disclosureHovered = toggle.hovered;
            }

            if (frame.isInputPass() && interaction.clicked && !result.toggled.has_value())
            {
                result.clicked = index;
                result.additive = frame.input().modifiers.control
                               || frame.input().modifiers.shift;
            }

            // A row that says what it carries can be dragged off. Declared on the row rather than
            // wired up by the caller, so there is no second list to keep in step with these.
            if (!row.dragType.empty() && row.enabled && !renaming)
            {
                StudioFrame::StudioDragPayload payload;
                payload.type = row.dragType;
                payload.value = row.dragValue.empty() ? row.id : row.dragValue;
                payload.label = row.label;
                if (studioDragSource(frame, frame.ids().make("row"), interaction,
                                     std::move(payload)))
                {
                    result.dragStarted = index;
                }
            }

            // And a row that says what it accepts is a target. Its own id, because a row is
            // already a control and two interactions sharing one id would be one entry.
            bool dropHovered = false;
            if (!row.dropType.empty() && !renaming)
            {
                const StudioFrame::StudioDropResult drop =
                    frame.acceptDrop(frame.ids().make("drop"), rowBounds, row.dropType);
                dropHovered = drop.hovered;
                if (drop.dropped)
                {
                    result.dropped = index;
                    result.droppedValue = drop.value;
                }
            }

            if (renaming)
            {
                // Over the whole row, indent and all: the field is *where the name is*, so it has
                // to start where the name started or the text jumps sideways as editing begins.
                const UiRect field = cursor;

                const WidgetId id = frame.ids().make("rename");
                if (state.renameStarting())
                {
                    // Focused by the widget rather than by whoever asked for the rename, because a
                    // field the user has to click before typing is a rename that begins by making
                    // them find the thing they just asked to rename.
                    frame.router().setFocus(id);
                    state.clearRenameStarting();
                }

                if (frame.isDrawPass())
                {
                    frame.drawList().fillRect(rowBounds, theme.color(StudioColorRole::Selection));
                }

                StudioTextFieldOptions options;
                options.selectAllOnFocus = true;

                const StudioTextFieldResult edit =
                    studioTextField(frame, id, field, state.renameText(), options);

                if (frame.isInputPass())
                {
                    if (edit.cancelled) { state.cancelRename(); }
                    else if (edit.committed || !edit.interaction.focused)
                    {
                        // Focus leaving commits, the way every other field in Studio does: a user
                        // who typed a name and clicked away meant the name.
                        std::string name = state.renameText();
                        state.cancelRename();
                        if (!name.empty() && name != row.label)
                        {
                            result.renamed = index;
                            result.renamedTo = std::move(name);
                        }
                    }
                }

                frame.ids().pop();
                continue;
            }

            // The trailing toggle, described before the row's own drawing so it wins the click
            // against the row underneath it -- the row is one widget covering the whole line, and a
            // button described after it would be a button the row swallows every press of.
            if (row.toggleIcon != StudioIcon::None)
            {
                const float size = metricOf(theme, StudioMetric::IconSize);
                const UiRect box{rowBounds.right() - padding - size,
                                 std::round(rowBounds.centerY() - size * 0.5f), size, size};

                // Drawn only while the row is hovered or the toggle is off, which is what every
                // outliner that has one does: a column of forty identical eyes is a column of
                // noise, and the rows that matter are the ones *not* in the default state.
                const bool show = interaction.hovered || !row.toggleOn;

                StudioButtonOptions options;
                options.icon = (!row.toggleOn && row.toggleOffIcon != StudioIcon::None)
                    ? row.toggleOffIcon
                    : row.toggleIcon;
                options.iconOnly = true;
                options.kind = StudioButtonKind::Ghost;
                options.tooltip = row.toggleTooltip;
                options.focusable = false;

                // Described in both passes either way, so the hit area does not appear and vanish
                // under the pointer; only the *drawing* is conditional. A button that existed only
                // while hovered would be one a user cannot click, because the frame in which they
                // press is the frame it was there.
                const bool clicked = show
                    ? studioButton(frame, frame.ids().make("toggle"), box, row.toggleTooltip,
                                   options).activated
                    : frame.interact(frame.ids().make("toggle"), box, /*enabled=*/true).clicked;

                if (frame.isInputPass() && clicked) { result.toggledRowAction = index; }

                // And the label stops where the toggle starts, hovered or not: text that reflowed
                // as the pointer crossed a row would be the most distracting thing in the panel.
                cursor.splitRight(std::min(cursor.width, size + padding));
            }

            if (frame.isDrawPass())
            {
                if (dropHovered)
                {
                    // The target says so before the drop, not after: a drag with no feedback is a
                    // drag the user has to complete to discover whether it would have worked.
                    frame.drawList().fillRect(rowBounds, theme.color(StudioColorRole::Selection));
                    frame.drawList().strokeRect(rowBounds, theme.color(StudioColorRole::Accent),
                                                metricOf(theme, StudioMetric::BorderWidth));
                }
                else if (row.selected)
                {
                    frame.drawList().fillRect(rowBounds, theme.color(StudioColorRole::Selection));
                }
                else if (interaction.hovered)
                {
                    frame.drawList().fillRect(rowBounds, theme.color(StudioColorRole::RowHover));
                }
                else if (index % 2 == 1)
                {
                    // `STUDIO-35031`. Every other row, four values off the panel: enough to trace
                    // a row across nine hundred pixels of outliner, not enough to read as a
                    // stripe. Keyed on the row's index in the *model* rather than on its position
                    // on screen, so scrolling does not make the whole list flicker between two
                    // phases -- which is what keying on a visible counter does, and it is far
                    // worse than no striping at all.
                    frame.drawList().fillRect(rowBounds, theme.color(StudioColorRole::RowAlternate));
                }

                // Indent guides, one per level the row is nested under. Drawn under everything
                // else so a selected row covers them: a hierarchy line crossing a selection fill
                // reads as a scratch on the highlight.
                //
                // Only for rows that are actually nested, and never for the level the row itself
                // sits at -- a guide beside a row's own disclosure triangle is a line through the
                // triangle.
                for (int level = 0; level < row.depth; ++level)
                {
                    const float x = std::round(rowBounds.left() + padding
                                               + indent * (static_cast<float>(level) + 0.5f));
                    frame.drawList().fillRect(
                        UiRect{x, rowBounds.top(),
                               metricOf(theme, StudioMetric::SeparatorThickness),
                               rowBounds.height},
                        theme.color(StudioColorRole::Separator));
                }

                // Over every fill above it, which is the whole reason it is drawn here.
                if (row.hasChildren)
                {
                    drawDisclosure(frame, disclosure, expanded,
                                   theme.color(disclosureHovered ? StudioColorRole::TextPrimary
                                                                 : StudioColorRole::TextSecondary));
                }

                const StudioColorRole labelRole = (!row.enabled || row.muted)
                    ? StudioColorRole::TextDisabled
                    : StudioColorRole::TextPrimary;

                UiRect labelArea = cursor;
                if (row.icon != StudioIcon::None)
                {
                    // The full icon size rather than the small one. These are read at a glance and
                    // never studied, and twelve pixels is where an isometric cube stops being a
                    // cube -- the interior edges land on the same pixel as the silhouette and it
                    // comes out a grey hexagon. Sixteen fits a 22-pixel row with three to spare.
                    const float iconSize = metricOf(theme, StudioMetric::IconSize);
                    const UiRect iconArea =
                        labelArea.splitLeft(std::min(iconSize + metricOf(theme,
                                                        StudioMetric::SpacingSmall),
                                                     labelArea.width));
                    studioDrawIcon(frame,
                                   UiRect{iconArea.left(),
                                          std::round(iconArea.centerY() - iconSize * 0.5f),
                                          iconSize, iconSize},
                                   row.icon,
                                   theme.color((!row.enabled || row.muted)
                                                   ? StudioColorRole::TextDisabled
                                                   : row.iconRole));
                }
                if (!row.detail.empty())
                {
                    const float detailWidth =
                        std::ceil(studioLabelWidth(frame, row.detail, StudioFontRole::BodySmall)
                                  + metricOf(theme, StudioMetric::SpacingMedium));
                    if (labelArea.width > detailWidth * 2.0f)
                    {
                        const UiRect detailArea = labelArea.splitRight(detailWidth);
                        studioDrawText(frame, detailArea, row.detail, StudioFontRole::BodySmall,
                                       theme.color(row.enabled ? row.detailRole
                                                               : StudioColorRole::TextDisabled),
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
