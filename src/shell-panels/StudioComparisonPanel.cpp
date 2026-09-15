// SPDX-License-Identifier: MS-PL
/**
 * @file StudioComparisonPanel.cpp
 * @brief The same scene on every installed renderer, and where the pictures disagree.
 */

#include "CNA/Studio/ShellPanels/StudioComparisonPanel.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
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

        /** @brief Returns @p value as a percentage with two decimals, e.g. "0.13%". */
        std::string toPercentage(double value)
        {
            std::string text = std::to_string(value * 100.0);
            const std::size_t point = text.find('.');
            if (point != std::string::npos && text.size() > point + 3) { text.resize(point + 3); }
            return text + "%";
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
            catch (...)
            {
                return false;
            }
        }

        /** @brief One renderer's verdict, and the colour it is said in. */
        struct Verdict
        {
            std::string text;
            StudioColorRole role = StudioColorRole::TextSecondary;
        };

        Verdict verdictOf(const ComparisonEntry& entry, ComparisonState state)
        {
            if (!entry.errorMessage.empty()) { return {entry.errorMessage, StudioColorRole::Error}; }

            if (!entry.captured)
            {
                // "Waiting" and "never arrived" are different things to read at a glance, and only
                // the run's state can tell them apart.
                return state == ComparisonState::Finished || state == ComparisonState::Failed
                    ? Verdict{"no frame was captured", StudioColorRole::Error}
                    : Verdict{"waiting for a frame", StudioColorRole::TextSecondary};
            }

            if (entry.isReference) { return {"reference", StudioColorRole::TextSecondary}; }

            if (!entry.difference.comparable)
            {
                // Not a disagreement about pixels: a capture that went wrong. The two call for
                // entirely different actions, so they must not read the same.
                return {"cannot compare: " + entry.difference.incomparableReason,
                        StudioColorRole::Warning};
            }

            if (entry.difference.matches()) { return {"identical", StudioColorRole::Success}; }

            return {std::to_string(entry.difference.differingPixels) + " of "
                        + std::to_string(entry.difference.totalPixels) + " pixels differ ("
                        + toPercentage(entry.difference.getDifferingFraction()) + ")",
                    StudioColorRole::Warning};
        }

        /** @brief Appends a detail row under a renderer. */
        void leaf(std::vector<StudioTreeRow>& rows, std::string_view parent, std::string label,
                  std::string detail, StudioColorRole role = StudioColorRole::TextSecondary)
        {
            StudioTreeRow row;
            row.id = std::string{parent} + ":" + label;
            row.label = std::move(label);
            row.detail = std::move(detail);
            row.detailRole = role;
            row.depth = 1;
            rows.push_back(std::move(row));
        }
    }

    std::vector<StudioTreeRow> studioComparisonRows(const std::vector<ComparisonEntry>& entries,
                                                    ComparisonState state,
                                                    const StudioTreeState& expansion)
    {
        std::vector<StudioTreeRow> rows;

        for (const ComparisonEntry& entry : entries)
        {
            const Verdict verdict = verdictOf(entry, state);
            const std::string id = "backend:" + entry.backend;

            StudioTreeRow row;
            row.id = id;
            row.label = entry.isReference ? entry.backend + "  (reference)" : entry.backend;
            row.detail = verdict.text;
            row.detailRole = verdict.role;
            row.hasChildren = true;
            rows.push_back(std::move(row));

            if (!expansion.isExpanded(id)) { continue; }

            if (!entry.capturePath.empty())
            {
                leaf(rows, id, "Capture", entry.capturePath);
            }
            if (!entry.errorMessage.empty())
            {
                leaf(rows, id, "Problem", entry.errorMessage, StudioColorRole::Error);
            }

            if (!entry.captured || entry.isReference || !entry.difference.comparable) { continue; }

            leaf(rows, id, "Largest channel difference",
                 std::to_string(entry.difference.maxChannelDelta));

            if (entry.difference.matches()) { continue; }

            // Where, not just how much. A band along one edge is a viewport or scissor problem; a
            // scattering over one sprite is a filtering one. The rectangle usually is the diagnosis.
            const StudioRectangle& box = entry.difference.boundingBox;
            leaf(rows, id, "Differs within",
                 std::to_string(box.width) + "x" + std::to_string(box.height) + " at ("
                     + std::to_string(box.x) + ", " + std::to_string(box.y) + ")");

            if (!entry.differencePath.empty())
            {
                leaf(rows, id, "Difference image", entry.differencePath);
            }
        }

        return rows;
    }

    std::string studioComparisonSummary(ComparisonState state, bool allAgree)
    {
        if (state != ComparisonState::Finished) { return {}; }
        return allAgree ? "Every renderer drew the same picture."
                        : "The renderers do not agree; see the rows below.";
    }

    StudioComparisonResult studioComparisonPanel(StudioFrame& frame, const UiRect& bounds,
                                                 const StudioComparisonView& view,
                                                 StudioTreeState& state)
    {
        StudioComparisonResult result;
        result.tolerance = view.tolerance;

        const StudioTheme& theme = frame.theme();
        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(bounds, theme.color(StudioColorRole::PanelBackground));
        }
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) { return result; }

        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);
        const float lineHeight = std::ceil(frame.measureText(StudioFontRole::Body, "Ag").height()
                                           + metricOf(theme, StudioMetric::SpacingXSmall));

        // The two empty states come before anything else, because neither leaves a button that
        // could do anything. A Compare that is drawn and does nothing is worse than no Compare.
        const auto sayOnly = [&](std::string_view message, StudioColorRole role) {
            if (frame.isDrawPass())
            {
                UiRect empty = bounds.inset(UiEdges{spacing * 2.0f, spacing * 2.0f});
                studioDrawText(frame, empty.splitTop(lineHeight), message, StudioFontRole::Body,
                               theme.color(role));
            }
        };

        if (!view.hasProject)
        {
            sayOnly("Open a project to compare what its renderers draw.",
                    StudioColorRole::TextSecondary);
            return result;
        }

        const bool running = view.state == ComparisonState::Launching
                          || view.state == ComparisonState::Capturing;

        if (!view.problem.empty() && !running)
        {
            sayOnly("Cannot compare: " + view.problem, StudioColorRole::Warning);
            return result;
        }

        static const std::vector<ComparisonEntry> kNoEntries;
        const std::vector<ComparisonEntry>& entries =
            view.entries != nullptr ? *view.entries : kNoEntries;

        const std::vector<StudioTreeRow> rows = studioComparisonRows(entries, view.state, state);
        result.rowsTotal = rows.size();

        frame.ids().push("comparison");

        UiRect body = bounds;
        const float toolbarHeight = metricOf(theme, StudioMetric::ControlHeight) + spacing * 2.0f;
        UiRect toolbar = body.splitTop(std::min(toolbarHeight, body.height))
                             .inset(UiEdges{spacing, spacing});

        if (running)
        {
            const UiRect button = toolbar.splitLeft(
                std::min(toolbar.width, std::ceil(studioLabelWidth(frame, "Cancel")) + spacing * 4.0f));
            StudioButtonOptions options;
            options.tooltip = "Stop the players and abandon this comparison.";
            if (studioButton(frame, frame.ids().make("cancel"), button, "Cancel", options).activated)
            {
                result.cancelRequested = true;
            }
            toolbar.splitLeft(spacing);
            if (frame.isDrawPass())
            {
                studioDrawText(frame, toolbar, std::string{"Comparing: "} + toString(view.state),
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary));
            }
        }
        else
        {
            const UiRect button = toolbar.splitLeft(
                std::min(toolbar.width, std::ceil(studioLabelWidth(frame, "Compare")) + spacing * 4.0f));
            StudioButtonOptions options;
            options.tooltip = "Run this scene on every installed player build and compare the frames.";
            if (studioButton(frame, frame.ids().make("compare"), button, "Compare", options)
                    .activated)
            {
                result.compareRequested = true;
            }

            toolbar.splitLeft(spacing * 2.0f);
            const UiRect labelBox = toolbar.splitLeft(
                std::min(toolbar.width, std::ceil(studioLabelWidth(frame, "Tolerance")) + spacing));
            if (frame.isDrawPass())
            {
                studioDrawText(frame, labelBox, "Tolerance", StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }

            const UiRect field = toolbar.splitLeft(
                std::min(toolbar.width, metricOf(theme, StudioMetric::ControlHeight) * 2.5f));
            std::string text = std::to_string(view.tolerance);
            StudioTextFieldOptions fieldOptions;
            fieldOptions.font = StudioFontRole::Monospace;
            fieldOptions.selectAllOnFocus = true;
            if (studioTextField(frame, frame.ids().make("tolerance"), field, text, fieldOptions)
                    .committed)
            {
                int parsed = 0;
                if (parseInteger(text, parsed))
                {
                    result.tolerance = std::clamp(parsed, 0, kStudioMaxComparisonTolerance);
                    result.toleranceChanged = result.tolerance != view.tolerance;
                }
            }
        }

        // Which renderer Play will use, and a way to say otherwise for this session. Here rather
        // than on the toolbar because this is the panel a user comes to in order to think about
        // renderers, and because a chooser beside the *comparison* of them is a chooser whose
        // meaning needs no explaining.
        if (view.builds != nullptr && view.builds->size() > 1)
        {
            UiRect strip = body.splitTop(std::min(toolbarHeight, body.height))
                               .inset(UiEdges{spacing, spacing});

            const UiRect labelBox = strip.splitLeft(
                std::min(strip.width, std::ceil(studioLabelWidth(frame, "Play on")) + spacing));
            if (frame.isDrawPass())
            {
                studioDrawText(frame, labelBox, "Play on", StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }

            frame.ids().push("playon");

            // "Project" first, because it is the answer that survives the session and the one a
            // user should be able to get back to without remembering what it was.
            const std::string projectLabel = "Project";
            const UiRect projectButton = strip.splitLeft(std::min(
                strip.width, std::ceil(studioLabelWidth(frame, projectLabel)) + spacing * 3.0f));

            StudioButtonOptions projectOptions;
            projectOptions.selected = !view.playBackendIsOverride;
            projectOptions.tooltip = "Launch on whatever the project's target profile names.";
            if (studioButton(frame, frame.ids().make("project"), projectButton, projectLabel,
                             projectOptions).activated)
            {
                result.playBackendChosen = std::string{};
            }

            for (const PlayerBuild& build : *view.builds)
            {
                if (strip.width <= 0.0f) { break; }
                strip.splitLeft(std::min(spacing, strip.width));

                const UiRect button = strip.splitLeft(std::min(
                    strip.width, std::ceil(studioLabelWidth(frame, build.backend)) + spacing * 3.0f));
                if (button.width <= 0.0f) { break; }

                StudioButtonOptions options;
                options.selected = view.playBackendIsOverride && view.playBackend == build.backend;
                options.tooltip = "Launch on " + build.backend + " for this session.";
                if (studioButton(frame, frame.ids().make(build.backend), button, build.backend,
                                 options).activated)
                {
                    result.playBackendChosen = build.backend;
                }
            }

            frame.ids().pop();
        }

        if (frame.isDrawPass())
        {
            frame.drawList().drawHorizontalSeparator(
                UiRect{bounds.left(), body.top(), bounds.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));
        }

        // Said above the rows rather than below them: the verdict is what the user came for, and a
        // summary under a list they have to scroll past is a summary they will not read.
        const std::string summary = studioComparisonSummary(view.state, view.allAgree);
        const std::string headline = !view.error.empty() ? "Problem: " + view.error : summary;
        if (!headline.empty())
        {
            UiRect line = body.splitTop(std::min(body.height, lineHeight + spacing));
            if (frame.isDrawPass())
            {
                const StudioColorRole role = !view.error.empty()
                    ? StudioColorRole::Error
                    : (view.allAgree ? StudioColorRole::Success : StudioColorRole::Warning);
                studioDrawText(frame, line.inset(UiEdges{spacing, 0.0f}), headline,
                               StudioFontRole::Body, theme.color(role));
            }
        }

        const StudioTreeResult tree = studioTreeView(
            frame, body, rows, state,
            view.outputDirectory.empty()
                ? std::string_view{"Press Compare to run this scene on every installed renderer."}
                : std::string_view{"Press Compare. Captures are written beside the project."});
        frame.ids().pop();

        result.rowsDrawn = tree.rowsDrawn;
        return result;
    }
}
