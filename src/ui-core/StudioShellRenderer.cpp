// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellRenderer.cpp
 * @brief Shell drawing: menu bar, toolbar, docks, tab strips, splitters, status bar.
 */

#include "CNA/Studio/UiCore/StudioShellRenderer.hpp"

#include <algorithm>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Rough advance per character for the text placeholder, until STUDIO-04005. */
        constexpr float kApproximateCharacterWidth = 0.52f;

        /** @brief Width a label occupies, approximated from the font size. */
        float approximateTextWidth(const std::string& text, float fontSize)
        {
            return static_cast<float>(text.size()) * fontSize * kApproximateCharacterWidth;
        }

        /** @brief Draws a panel's tab strip and returns the body area beneath it. */
        UiRect drawDockTabStrip(StudioDrawList& list, const UiRect& dock, const StudioTheme& theme,
                                const std::vector<StudioDockedPanel>& panels)
        {
            if (dock.isEmpty()) { return dock; }

            UiRect remaining = dock;
            const auto tabHeight = static_cast<float>(theme.metric(StudioMetric::TabHeight));
            const UiRect strip = remaining.splitTop(tabHeight);

            list.fillRect(strip, theme.color(StudioColorRole::PanelHeader));

            const auto padding = static_cast<float>(theme.metric(StudioMetric::SpacingMedium));
            const float fontSize = theme.font(StudioFontRole::Body).sizePx;

            UiRect cursor = strip;
            for (const StudioDockedPanel& panel : panels)
            {
                const float width = approximateTextWidth(panel.title, fontSize) + padding * 2.0f;
                if (cursor.width <= 0.0f) { break; }

                UiRect tab = cursor.splitLeft(std::min(width, cursor.width));
                if (panel.active)
                {
                    list.fillRect(tab, theme.color(StudioColorRole::PanelHeaderActive));
                    // The active tab is marked by an accent rule along its top edge. A fill alone
                    // is ambiguous against the panel body it merges into; the rule is what makes
                    // "this tab" readable at a glance without an outline boxing it in.
                    UiRect marker = tab;
                    marker.height = std::max(1.0f, static_cast<float>(
                        theme.metric(StudioMetric::FocusRingWidth)) * 2.0f);
                    list.fillRect(marker, theme.color(StudioColorRole::Accent));
                }

                const UiRect label = tab.inset(UiEdges{padding, (tab.height - fontSize) * 0.5f});
                list.drawTextPlaceholder(label,
                    theme.color(panel.active ? StudioColorRole::TextPrimary
                                             : StudioColorRole::TextSecondary));
            }

            list.drawHorizontalSeparator(UiRect{strip.left(), strip.bottom(), strip.width, 0.0f},
                                         theme.color(StudioColorRole::Separator),
                                         static_cast<float>(theme.metric(StudioMetric::SeparatorThickness)));

            list.fillRect(remaining, theme.color(StudioColorRole::PanelBackground));
            return remaining;
        }
    } // namespace

    StudioShellContent StudioShellContent::defaults()
    {
        StudioShellContent content;
        content.menus = {{"File"}, {"Edit"}, {"View"}, {"Project"}, {"Build"},
                         {"Play"}, {"Tools"}, {"Window"}, {"Help"}};
        content.leftPanels = {{"World Outliner", true}, {"Layers", false}};
        content.rightPanels = {{"Details", true}, {"Material", false}};
        content.bottomPanels = {{"Content Browser", true}, {"Output Log", false},
                                {"Build", false}, {"Problems", false}};
        content.documentTabs = {{"Viewport", true}};
        content.statusLeft = "No project open";
        content.statusRight = "Renderer: unknown";
        content.toolbarButtonCount = 9;
        return content;
    }

    void drawStudioShell(StudioDrawList& list, const StudioShellLayout& layout,
                         const StudioTheme& theme, const StudioShellContent& content)
    {
        const auto separatorThickness =
            static_cast<float>(theme.metric(StudioMetric::SeparatorThickness));
        const auto spacing = static_cast<float>(theme.metric(StudioMetric::SpacingMedium));
        const auto smallSpacing = static_cast<float>(theme.metric(StudioMetric::SpacingSmall));

        list.fillRect(layout.window, theme.color(StudioColorRole::AppBackground));

        // --- Menu bar -------------------------------------------------------------------------
        if (!layout.menuBar.isEmpty())
        {
            list.fillRect(layout.menuBar, theme.color(StudioColorRole::PanelHeader));

            const float fontSize = theme.font(StudioFontRole::Body).sizePx;
            UiRect cursor = layout.menuBar.inset(UiEdges{smallSpacing, 0.0f});
            for (const StudioMenuBarItem& menu : content.menus)
            {
                const float width = approximateTextWidth(menu.label, fontSize) + spacing * 2.0f;
                if (cursor.width <= 0.0f) { break; }
                UiRect item = cursor.splitLeft(std::min(width, cursor.width));
                if (menu.open) { list.fillRect(item, theme.color(StudioColorRole::ControlBackgroundHover)); }
                list.drawTextPlaceholder(item.inset(UiEdges{spacing, (item.height - fontSize) * 0.5f}),
                                         theme.color(StudioColorRole::TextPrimary));
            }

            list.drawHorizontalSeparator(
                UiRect{layout.menuBar.left(), layout.menuBar.bottom(), layout.menuBar.width, 0.0f},
                theme.color(StudioColorRole::Separator), separatorThickness);
        }

        // --- Toolbar --------------------------------------------------------------------------
        if (!layout.toolbar.isEmpty())
        {
            list.fillRect(layout.toolbar, theme.color(StudioColorRole::PanelBackground));

            const auto iconSize = static_cast<float>(theme.metric(StudioMetric::IconSize));
            const auto buttonSize = static_cast<float>(theme.metric(StudioMetric::ControlHeight));
            UiRect cursor = layout.toolbar.inset(UiEdges{spacing, (layout.toolbar.height - buttonSize) * 0.5f});

            for (int i = 0; i < content.toolbarButtonCount && cursor.width > 0.0f; ++i)
            {
                // Groups of three, separated by a rule: save, history, transform mode. Grouping is
                // what stops a toolbar reading as an undifferentiated row of squares.
                if (i > 0 && i % 3 == 0 && cursor.width > spacing * 2.0f)
                {
                    UiRect gap = cursor.splitLeft(spacing);
                    list.fillRect(UiRect{gap.centerX(), gap.top() + smallSpacing,
                                         separatorThickness, gap.height - smallSpacing * 2.0f},
                                  theme.color(StudioColorRole::Separator));
                }

                UiRect button = cursor.splitLeft(std::min(buttonSize, cursor.width));
                list.fillRoundedRect(button, theme.color(StudioColorRole::ControlBackground),
                                     static_cast<float>(theme.metric(StudioMetric::CornerRadius)));
                const float inset = std::max(0.0f, (button.width - iconSize) * 0.5f);
                list.drawTextPlaceholder(button.inset(inset),
                                         theme.color(StudioColorRole::TextSecondary));
                cursor.splitLeft(smallSpacing);
            }

            list.drawHorizontalSeparator(
                UiRect{layout.toolbar.left(), layout.toolbar.bottom(), layout.toolbar.width, 0.0f},
                theme.color(StudioColorRole::Separator), separatorThickness);
        }

        // --- Docks ----------------------------------------------------------------------------
        drawDockTabStrip(list, layout.leftDock, theme, content.leftPanels);
        drawDockTabStrip(list, layout.rightDock, theme, content.rightPanels);
        drawDockTabStrip(list, layout.bottomDock, theme, content.bottomPanels);

        // --- Centre: document tabs and viewport -------------------------------------------------
        if (!layout.centerTabStrip.isEmpty())
        {
            drawDockTabStrip(list, layout.centerDock, theme, content.documentTabs);
        }

        if (!layout.viewport.isEmpty())
        {
            // Darker than the panels, so the viewport reads as a window into the scene rather than
            // as another panel. The grid is drawn here as shell furniture; the real scene arrives
            // when the viewport panel is ported (STUDIO-07009).
            list.fillRect(layout.viewport, theme.color(StudioColorRole::ViewportBackground));

            const float gridSpacing = 32.0f * theme.scale();
            const StudioColor minor = theme.color(StudioColorRole::ViewportGrid);
            const StudioColor major = theme.color(StudioColorRole::ViewportGridMajor);

            list.pushClip(layout.viewport);
            int line = 0;
            for (float x = layout.viewport.left(); x < layout.viewport.right(); x += gridSpacing, ++line)
            {
                list.drawLine(x, layout.viewport.top(), x, layout.viewport.bottom(),
                              (line % 4 == 0) ? major : minor, 1.0f);
            }
            line = 0;
            for (float y = layout.viewport.top(); y < layout.viewport.bottom(); y += gridSpacing, ++line)
            {
                list.drawLine(layout.viewport.left(), y, layout.viewport.right(), y,
                              (line % 4 == 0) ? major : minor, 1.0f);
            }
            list.popClip();
        }

        // --- Splitters ---------------------------------------------------------------------------
        for (const UiRect* splitter : {&layout.leftSplitter, &layout.rightSplitter,
                                       &layout.bottomSplitter})
        {
            if (!splitter->isEmpty())
            {
                list.fillRect(*splitter, theme.color(StudioColorRole::AppBackground));
            }
        }

        // --- Status bar --------------------------------------------------------------------------
        if (!layout.statusBar.isEmpty())
        {
            list.fillRect(layout.statusBar, theme.color(StudioColorRole::PanelHeader));
            list.drawHorizontalSeparator(
                UiRect{layout.statusBar.left(), layout.statusBar.top(), layout.statusBar.width, 0.0f},
                theme.color(StudioColorRole::Separator), separatorThickness);

            const float fontSize = theme.font(StudioFontRole::BodySmall).sizePx;
            const UiRect inner = layout.statusBar.inset(
                UiEdges{spacing, std::max(0.0f, (layout.statusBar.height - fontSize) * 0.5f)});

            if (!content.statusLeft.empty() && !inner.isEmpty())
            {
                UiRect left = inner;
                left.width = std::min(approximateTextWidth(content.statusLeft, fontSize), inner.width);
                left.height = fontSize;
                list.drawTextPlaceholder(left, theme.color(StudioColorRole::TextSecondary));
            }
            if (!content.statusRight.empty() && !inner.isEmpty())
            {
                const float width = std::min(approximateTextWidth(content.statusRight, fontSize),
                                             inner.width);
                UiRect right{inner.right() - width, inner.top(), width, fontSize};
                list.drawTextPlaceholder(right, theme.color(StudioColorRole::TextSecondary));
            }
        }
    }
} // namespace CNA::Studio
