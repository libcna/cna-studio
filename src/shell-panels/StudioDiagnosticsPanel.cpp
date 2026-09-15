// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDiagnosticsPanel.cpp
 * @brief What this Studio is running on, as a report somebody can paste into a bug report.
 */

#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"

#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

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

        constexpr std::string_view kThisBuild = "group:build";
        constexpr std::string_view kHostGroup = "group:host";
        constexpr std::string_view kPlayerGroup = "group:players";
        constexpr std::string_view kRendererGroup = "group:renderers";

        /** @brief The word for a value the host could not supply. */
        std::string orUnknown(const std::string& value)
        {
            // "unknown" rather than an empty cell. A blank reads as a panel that failed to draw;
            // this reads as the answer, which on a build with no graphics device it is.
            return value.empty() ? std::string{"unknown"} : value;
        }

        /** @brief Appends a leaf row. */
        void leaf(std::vector<StudioTreeRow>& rows, std::string label, std::string detail,
                  StudioColorRole role = StudioColorRole::TextSecondary)
        {
            StudioTreeRow row;
            row.id = "row:" + label + ":" + detail;
            row.label = std::move(label);
            row.detail = std::move(detail);
            row.detailRole = role;
            row.depth = 1;
            rows.push_back(std::move(row));
        }

        /** @brief Appends a group row and reports whether its children should follow. */
        bool group(std::vector<StudioTreeRow>& rows, const StudioTreeState& state,
                   std::string_view id, std::string label, std::string detail)
        {
            StudioTreeRow row;
            row.id = std::string{id};
            row.label = std::move(label);
            row.detail = std::move(detail);
            row.hasChildren = true;
            rows.push_back(std::move(row));
            return state.isExpanded(id);
        }

        /** @brief The colour a requirement's outcome is said in. */
        StudioColorRole outcomeRole(const StudioRequirementOutcome& outcome)
        {
            if (outcome.isMet()) { return StudioColorRole::Success; }
            return outcome.severity == StudioRequirementSeverity::Required
                ? StudioColorRole::Error
                : StudioColorRole::Warning;
        }
    }

    std::vector<StudioTreeRow> studioDiagnosticsRows(const StudioDiagnosticsInfo& info,
                                                     const StudioTreeState& state)
    {
        std::vector<StudioTreeRow> rows;

        if (group(rows, state, kThisBuild, "This build", info.uiBackend))
        {
            leaf(rows, "UI", info.uiBackend);
            leaf(rows, "Renderer", orUnknown(info.renderer));
            leaf(rows, "Platform", orUnknown(info.platform));
            // Reported rather than assumed: PBR, shadows and post-processing are CNAEXT, and "why
            // does a model look different on that machine" usually ends here.
            leaf(rows, "Modern graphics API", info.modernApi ? "yes" : "no",
                 info.modernApi ? StudioColorRole::Success : StudioColorRole::Warning);
            leaf(rows, "Frames rendered", std::to_string(info.frames));
            leaf(rows, "Draw calls, last frame", std::to_string(info.drawCalls));
            leaf(rows, "Triangles, last frame", std::to_string(info.triangles));
        }

        const std::string hostSummary = info.host.outcomes.empty()
            ? std::string{"not evaluated"}
            : (info.host.canHostStudio ? std::string{"met"} : std::string{"not met"});
        if (group(rows, state, kHostGroup, "Studio host contract", hostSummary))
        {
            if (info.host.outcomes.empty())
            {
                // The honest state of a build with no device, said plainly. A silent empty group
                // would read as "this renderer meets nothing".
                leaf(rows, "No graphics device, so nothing was asked of one", {});
            }
            for (const StudioRequirementOutcome& outcome : info.host.outcomes)
            {
                StudioTreeRow row;
                row.id = "req:" + outcome.subject;
                row.label = outcome.subject;
                row.detail = std::string{studioRequirementStatusName(outcome.status)};
                row.detailRole = outcomeRole(outcome);
                row.depth = 1;
                rows.push_back(std::move(row));
            }
        }

        // Because CNA fixes its renderer at compile time, "run this on Vulkan" means "launch
        // cna-player-vulkan", and whether that binary exists is a question with a real answer.
        if (group(rows, state, kPlayerGroup, "Player builds found",
                  std::to_string(info.players.size())))
        {
            if (info.players.empty())
            {
                leaf(rows, "None beside this executable", {}, StudioColorRole::Warning);
            }
            for (const PlayerBuild& player : info.players)
            {
                leaf(rows, player.backend, player.executablePath);
            }
        }

        if (group(rows, state, kRendererGroup, "Renderers Studio knows about",
                  std::to_string(getKnownRenderers().size())))
        {
            for (const RendererInfo& renderer : getKnownRenderers())
            {
                const char* support = "runtime only";
                switch (renderer.hostSupport)
                {
                    case RendererHostSupport::StudioHost:  support = "studio"; break;
                    case RendererHostSupport::PreviewOnly: support = "preview only"; break;
                    case RendererHostSupport::RuntimeOnly: support = "runtime only"; break;
                }

                StudioTreeRow row;
                row.id = "renderer:" + std::string{renderer.commandLineName};
                row.label = std::string{renderer.displayName};
                row.detail = support;
                row.detailRole = renderer.hostSupport == RendererHostSupport::StudioHost
                    ? StudioColorRole::Success
                    : StudioColorRole::TextSecondary;
                row.depth = 1;
                rows.push_back(std::move(row));
            }
        }

        return rows;
    }

    std::string studioDiagnosticsText(const StudioDiagnosticsInfo& info)
    {
        std::string text;
        text += "UI: " + info.uiBackend + "\n";
        text += "Renderer: " + orUnknown(info.renderer) + "\n";
        text += "Platform: " + orUnknown(info.platform) + "\n";
        text += std::string{"Modern graphics API: "} + (info.modernApi ? "yes" : "no") + "\n";
        text += "Frames: " + std::to_string(info.frames)
              + ", draw calls: " + std::to_string(info.drawCalls)
              + ", triangles: " + std::to_string(info.triangles) + "\n";

        if (info.host.outcomes.empty())
        {
            text += "Studio host contract: not evaluated (no graphics device)\n";
        }
        else
        {
            text += std::string{"Studio host contract: "}
                  + (info.host.canHostStudio ? "met" : "NOT met") + "\n";
            for (const StudioRequirementOutcome& outcome : info.host.outcomes)
            {
                text += "  " + outcome.subject + ": "
                      + std::string{studioRequirementStatusName(outcome.status)};
                if (!outcome.detail.empty()) { text += " (" + outcome.detail + ")"; }
                text += "\n";
            }
        }

        text += "Player builds: " + std::to_string(info.players.size()) + "\n";
        for (const PlayerBuild& player : info.players)
        {
            text += "  " + player.backend + "  " + player.executablePath + "\n";
        }
        return text;
    }

    StudioDiagnosticsResult studioDiagnosticsPanel(StudioFrame& frame, const UiRect& bounds,
                                                   const StudioDiagnosticsInfo& info,
                                                   StudioTreeState& state)
    {
        StudioDiagnosticsResult result;

        const StudioTheme& theme = frame.theme();
        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(bounds, theme.color(StudioColorRole::PanelBackground));
        }
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) { return result; }

        const std::vector<StudioTreeRow> rows = studioDiagnosticsRows(info, state);
        result.rowsTotal = rows.size();

        // A panel that exists to be pasted into a bug report needs a way to get out of the window.
        const float toolbarHeight = metricOf(theme, StudioMetric::ControlHeight)
                                  + metricOf(theme, StudioMetric::SpacingSmall) * 2.0f;
        UiRect body = bounds;
        UiRect toolbar = body.splitTop(std::min(toolbarHeight, body.height));
        toolbar = toolbar.inset(UiEdges{metricOf(theme, StudioMetric::SpacingSmall),
                                        metricOf(theme, StudioMetric::SpacingSmall)});

        frame.ids().push("diagnostics");
        {
            StudioButtonOptions options;
            options.tooltip = "Copy this report to the clipboard, for a bug report.";
            const UiRect button = toolbar.splitLeft(
                std::min(toolbar.width, std::ceil(studioLabelWidth(frame, "Copy report"))));
            if (studioButton(frame, frame.ids().make("copy"), button, "Copy report", options)
                    .activated)
            {
                result.copyRequested = true;
                result.copyText = studioDiagnosticsText(info);
            }
        }

        if (frame.isDrawPass())
        {
            frame.drawList().drawHorizontalSeparator(
                UiRect{bounds.left(), body.top(), bounds.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));
        }

        const StudioTreeResult tree = studioTreeView(frame, body, rows, state, {});
        frame.ids().pop();

        result.rowsDrawn = tree.rowsDrawn;
        return result;
    }
}
