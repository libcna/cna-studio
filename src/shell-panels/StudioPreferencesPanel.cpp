// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPreferencesPanel.cpp
 * @brief What the user has decided about Studio, as rows they can change.
 */

#include "CNA/Studio/ShellPanels/StudioPreferencesPanel.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /** @brief The height a scrolled cursor is given before anything is laid out. */
        constexpr float kUnboundedContentHeight = 100000.0f;

        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /** @brief Parses a float, refusing anything with characters left over. */
        bool parseFloat(const std::string& text, float& out)
        {
            try
            {
                std::size_t consumed = 0;
                const float parsed = std::stof(text, &consumed);
                if (consumed != text.size()) { return false; }
                out = parsed;
                return true;
            }
            catch (...) { return false; }
        }

        /** @brief Parses an integer, refusing anything with characters left over. */
        bool parseInteger(const std::string& text, int& out)
        {
            try
            {
                std::size_t consumed = 0;
                const int parsed = std::stoi(text, &consumed);
                if (consumed != text.size()) { return false; }
                out = parsed;
                return true;
            }
            catch (...) { return false; }
        }

        /** @brief Formats a float with two decimals, without a trailing run of zeroes. */
        std::string shortFloat(float value)
        {
            std::string text = std::to_string(value);
            const std::size_t point = text.find('.');
            if (point != std::string::npos && text.size() > point + 3) { text.resize(point + 3); }
            return text;
        }

        /** @brief Lays out one labelled row and returns where its control goes. */
        UiRect labelledRow(StudioFrame& frame, UiRect& cursor, std::string_view label)
        {
            const StudioTheme& theme = frame.theme();
            const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
            const float spacing = metricOf(theme, StudioMetric::SpacingSmall);

            UiRect row = cursor.splitTop(rowHeight);
            cursor.splitTop(spacing);

            // A fixed label column, like the Build panel's: measuring would make the controls step
            // sideways as the words change, which is what a preferences page does most.
            const float labelWidth = std::min(row.width * 0.45f,
                                              metricOf(theme, StudioMetric::PanelHeaderHeight)
                                                  * 7.0f);
            const UiRect labelBox = row.splitLeft(labelWidth);
            if (frame.isDrawPass())
            {
                studioDrawText(frame, labelBox, label, StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }
            row.splitLeft(spacing);

            return row.splitLeft(std::min(row.width,
                                          metricOf(theme, StudioMetric::PanelHeaderHeight) * 9.0f));
        }

        /** @brief Draws a section heading and the rule under it. */
        void heading(StudioFrame& frame, UiRect& cursor, std::string_view text)
        {
            const StudioTheme& theme = frame.theme();
            const float lineHeight = std::ceil(
                frame.measureText(StudioFontRole::Heading, "Ag").height()
                + metricOf(theme, StudioMetric::SpacingSmall));

            cursor.splitTop(metricOf(theme, StudioMetric::SpacingMedium));
            const UiRect box = cursor.splitTop(lineHeight);
            if (frame.isDrawPass())
            {
                studioDrawText(frame, box, text, StudioFontRole::Heading,
                               theme.color(StudioColorRole::TextPrimary));
                frame.drawList().drawHorizontalSeparator(
                    UiRect{box.left(), box.bottom(), box.width, 0.0f},
                    theme.color(StudioColorRole::Separator),
                    metricOf(theme, StudioMetric::SeparatorThickness));
            }
            cursor.splitTop(metricOf(theme, StudioMetric::SpacingSmall));
        }

        /** @brief A row holding a number the user types. Returns true when it changed. */
        bool numberRow(StudioFrame& frame, UiRect& cursor, std::string_view label,
                       std::string_view id, const std::string& shown, float& value)
        {
            const UiRect control = labelledRow(frame, cursor, label);
            std::string text = shown;
            StudioTextFieldOptions options;
            options.font = StudioFontRole::Monospace;
            options.selectAllOnFocus = true;
            if (!studioTextField(frame, frame.ids().make(id), control, text, options).committed)
            {
                return false;
            }

            float parsed = 0.0f;
            if (!parseFloat(text, parsed)) { return false; }
            value = parsed;
            return true;
        }
    }

    StudioPreferencesPanelResult studioPreferencesPanel(
        StudioFrame& frame, const UiRect& body, StudioPreferences& preferences,
        const StudioPreferencesPanelContext& context, StudioShortcutEditorState& shortcuts)
    {
        StudioPreferencesPanelResult result;

        const StudioTheme& theme = frame.theme();
        const float spacing = metricOf(theme, StudioMetric::SpacingMedium);
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);

        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(body, theme.color(StudioColorRole::PanelBackground));
        }
        if (body.width <= 0.0f || body.height <= 0.0f) { return result; }

        // Scrolled, because the page is taller than any dock it is likely to be put in and Reset
        // is at the end of it. A panel whose last row cannot be reached is not a panel.
        const WidgetId scrollId = frame.ids().make("preferences.scroll");
        StudioScrollOptions scrollOptions;
        scrollOptions.contentHeight = frame.state().get(scrollId).scalar;
        const StudioScrollResult scroll = studioBeginScroll(frame, scrollId, body, scrollOptions);

        UiRect content{scroll.viewport.left() + spacing,
                       scroll.viewport.top() - scroll.offsetY + spacing,
                       std::max(0.0f, scroll.viewport.width - spacing * 2.0f),
                       kUnboundedContentHeight};
        const float contentTop = content.top();

        frame.ids().push("preferences");

        // --- Appearance ------------------------------------------------------------------------
        heading(frame, content, "Appearance");
        {
            static const std::vector<std::string> kThemes{"dark", "light"};
            const UiRect control = labelledRow(frame, content, "Theme");
            int index = preferences.theme == "light" ? 1 : 0;
            if (studioDropdown(frame, frame.ids().make("theme"), control, kThemes, index).changed)
            {
                preferences.theme = kThemes[static_cast<std::size_t>(index)];
                result.changed = true;
            }
        }
        if (numberRow(frame, content, "UI scale", "uiScale", shortFloat(preferences.uiScale),
                      preferences.uiScale))
        {
            result.changed = true;
        }
        if (numberRow(frame, content, "Font size", "fontSize",
                      shortFloat(preferences.fontSizePoints), preferences.fontSizePoints))
        {
            result.changed = true;
        }

        // --- Viewport --------------------------------------------------------------------------
        heading(frame, content, "Viewport");
        {
            static const std::vector<std::string> kStyles{"studio", "maya", "blender"};
            const UiRect control = labelledRow(frame, content, "Navigation");
            int index = static_cast<int>(preferences.navigation);
            if (studioDropdown(frame, frame.ids().make("navigation"), control, kStyles, index)
                    .changed)
            {
                preferences.navigation = static_cast<StudioNavigationStyle>(index);
                result.changed = true;
            }
        }
        if (numberRow(frame, content, "Camera speed", "cameraSpeed",
                      shortFloat(preferences.cameraSpeed), preferences.cameraSpeed))
        {
            result.changed = true;
        }
        {
            const UiRect control = labelledRow(frame, content, "Invert zoom");
            if (studioCheckbox(frame, frame.ids().make("invertZoom"), control, "",
                               preferences.invertZoom).changed)
            {
                result.changed = true;
            }
        }

        // --- Documents -------------------------------------------------------------------------
        heading(frame, content, "Documents");
        {
            const UiRect control = labelledRow(frame, content, "Autosave (seconds, 0 for none)");
            std::string text = std::to_string(preferences.autosaveSeconds);
            StudioTextFieldOptions options;
            options.font = StudioFontRole::Monospace;
            options.selectAllOnFocus = true;
            if (studioTextField(frame, frame.ids().make("autosave"), control, text, options)
                    .committed)
            {
                int parsed = 0;
                if (parseInteger(text, parsed))
                {
                    preferences.autosaveSeconds = parsed;
                    result.changed = true;
                }
            }
        }
        {
            const UiRect control = labelledRow(frame, content, "Reopen last project");
            if (studioCheckbox(frame, frame.ids().make("reopen"), control, "",
                               preferences.reopenLastProject).changed)
            {
                result.changed = true;
            }
        }

        // --- Tools -----------------------------------------------------------------------------
        heading(frame, content, "Tools");
        {
            const UiRect control = labelledRow(frame, content, "External editor");
            StudioTextFieldOptions options;
            options.placeholder = "the system default";
            if (studioTextField(frame, frame.ids().make("editor"), control,
                                preferences.externalEditor, options).committed)
            {
                result.changed = true;
            }
        }
        {
            const UiRect control = labelledRow(frame, content, "CMake");
            StudioTextFieldOptions options;
            options.placeholder = "whatever is on the PATH";
            if (studioTextField(frame, frame.ids().make("cmake"), control, preferences.cmakePath,
                                options).committed)
            {
                result.changed = true;
            }
        }
        {
            const UiRect control = labelledRow(frame, content, "Build jobs (0 for one per core)");
            std::string text = std::to_string(preferences.buildJobs);
            StudioTextFieldOptions options;
            options.font = StudioFontRole::Monospace;
            options.selectAllOnFocus = true;
            if (studioTextField(frame, frame.ids().make("jobs"), control, text, options).committed)
            {
                int parsed = 0;
                if (parseInteger(text, parsed))
                {
                    preferences.buildJobs = parsed;
                    result.changed = true;
                }
            }
        }

        // --- Workspace -------------------------------------------------------------------------
        heading(frame, content, "Workspace");
        {
            // "Whatever I left it as" first, because that is what a workspace does by default and
            // what most users want: a named layout is the deliberate choice, not the fallback.
            std::vector<std::string> options{"last arrangement"};
            options.insert(options.end(), context.layoutNames.begin(), context.layoutNames.end());

            int index = 0;
            for (std::size_t i = 1; i < options.size(); ++i)
            {
                if (options[i] == preferences.defaultLayout) { index = static_cast<int>(i); }
            }

            const UiRect control = labelledRow(frame, content, "Open with");
            if (studioDropdown(frame, frame.ids().make("layout"), control, options, index).changed)
            {
                preferences.defaultLayout =
                    index == 0 ? std::string{} : options[static_cast<std::size_t>(index)];
                result.changed = true;
            }
        }

        // --- Shortcuts ---------------------------------------------------------------------------
        if (context.actions != nullptr)
        {
            heading(frame, content, "Shortcuts");
            const StudioShortcutEditorResult edited =
                studioShortcutEditor(frame, content, *context.actions, preferences, shortcuts);
            content.splitTop(edited.contentHeight);
            if (edited.changed) { result.changed = true; }
        }

        // --- Reset -----------------------------------------------------------------------------
        content.splitTop(spacing);
        {
            const UiRect row = content.splitTop(rowHeight);
            const UiRect button = UiRect{row.left(), row.top(),
                                         std::min(row.width,
                                                  std::ceil(studioLabelWidth(frame, "Reset to defaults"))
                                                      + spacing * 2.0f),
                                         row.height};
            StudioButtonOptions options;
            options.tooltip = "Put every preference back the way Studio ships.";
            if (studioButton(frame, frame.ids().make("reset"), button, "Reset to defaults", options)
                    .activated)
            {
                result.resetRequested = true;
            }
        }

        frame.ids().pop();

        result.contentHeight = content.top() - contentTop + spacing;
        if (frame.isInputPass()) { frame.state().get(scrollId).scalar = result.contentHeight; }
        if (result.changed) { preferences = studioClampPreferences(std::move(preferences)); }

        studioEndScroll(frame);
        return result;
    }
}
