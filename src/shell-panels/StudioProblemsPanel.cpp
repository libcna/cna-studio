// SPDX-License-Identifier: MS-PL
/**
 * @file StudioProblemsPanel.cpp
 * @brief Scene validation and broken asset references, as one report.
 */

#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Scene/MissingReferences.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneValidation.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <memory>
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

        /** @brief Row id of the broken-references group. */
        constexpr std::string_view kReferencesGroup = "group:references";
        /** @brief Row id of the scene-issues group. */
        constexpr std::string_view kIssuesGroup = "group:issues";

        /** @brief The colour a severity is said in. */
        StudioColorRole severityRole(SceneIssue::Severity severity)
        {
            return severity == SceneIssue::Severity::Error ? StudioColorRole::Error
                                                           : StudioColorRole::Warning;
        }
    }

    std::vector<StudioTreeRow> studioProblemRows(StudioContext& context,
                                                 const StudioProblemsState& state,
                                                 StudioProblemsResult& outResult)
    {
        std::vector<StudioTreeRow> rows;

        // --- Broken asset references -------------------------------------------------------------
        const std::vector<MissingReference> missing =
            findMissingReferences(context.getScene(), context.getAssets());
        const std::vector<Uuid> brokenAssets = collectMissingAssetIds(missing);
        outResult.brokenReferences = missing.size();

        StudioTreeRow referencesGroup;
        referencesGroup.id = std::string{kReferencesGroup};
        referencesGroup.label = "Broken asset references";
        // The good state of a report is emptiness, and an empty group with no count reads as "not
        // implemented" rather than as "nothing is wrong".
        referencesGroup.detail = brokenAssets.empty() ? "none" : std::to_string(brokenAssets.size());
        referencesGroup.hasChildren = !brokenAssets.empty();
        referencesGroup.selected = state.selectedRow == referencesGroup.id;
        rows.push_back(referencesGroup);

        if (state.tree.isExpanded(referencesGroup.id))
        {
            for (const Uuid& assetId : brokenAssets)
            {
                std::size_t uses = 0;
                const char* reason = "";
                for (const MissingReference& reference : missing)
                {
                    if (reference.assetId != assetId) { continue; }
                    ++uses;
                    reason = toString(reference.reason);
                }
                const std::string detail =
                    std::string{reason} + "  --  " + std::to_string(uses)
                    + (uses == 1 ? " use" : " uses");

                StudioTreeRow asset;
                asset.id = std::string{kStudioBrokenAssetRowPrefix} + assetId.toString();
                asset.label = assetId.toString();
                asset.detail = detail;
                asset.detailRole = StudioColorRole::Error;
                asset.depth = 1;
                asset.hasChildren = true;
                asset.selected = state.selectedRow == asset.id;
                // Dimmed but reachable: this is the row a user most needs to click, because
                // clicking it is how they find out what refers to it.
                asset.muted = true;
                // Dropping the right asset onto the broken row is the shortest path from "this is
                // broken" to "this is fixed", and it is the gesture the ImGui panel had.
                asset.dropType = std::string{kStudioAssetDragType};
                rows.push_back(asset);

                if (!state.tree.isExpanded(asset.id)) { continue; }

                for (const MissingReference& reference : missing)
                {
                    if (reference.assetId != assetId) { continue; }

                    StudioTreeRow user;
                    user.id = asset.id + "/" + reference.entityId.toString() + "."
                            + reference.propertyName;
                    user.label = reference.entityName + "." + reference.propertyName;
                    user.detail = reference.componentTypeId;
                    user.depth = 2;
                    user.selected = state.selectedRow == user.id;
                    outResult.rowEntities.emplace_back(user.id, reference.entityId);
                    rows.push_back(user);
                }
            }
        }

        // --- Scene issues --------------------------------------------------------------------------
        std::vector<SceneIssue> issues =
            validateScene(context.getScene(), context.getComponentRegistry());

        // Appended rather than given their own section: a user whose model has the wrong material
        // on it does not know in advance whether that is a structural problem or a geometry one.
        const std::vector<SceneIssue> partIssues =
            validateModelPartMaterials(context.getScene(), context.makeMeshProvider());
        issues.insert(issues.end(), partIssues.begin(), partIssues.end());

        outResult.errors = countIssues(issues, SceneIssue::Severity::Error);
        outResult.warnings = countIssues(issues, SceneIssue::Severity::Warning);

        StudioTreeRow issuesGroup;
        issuesGroup.id = std::string{kIssuesGroup};
        issuesGroup.label = "Scene issues";
        issuesGroup.detail = issues.empty()
            ? std::string{"none"}
            : std::to_string(outResult.errors) + " errors, " + std::to_string(outResult.warnings)
                  + " warnings";
        if (outResult.errors > 0) { issuesGroup.detailRole = StudioColorRole::Error; }
        else if (outResult.warnings > 0) { issuesGroup.detailRole = StudioColorRole::Warning; }
        issuesGroup.hasChildren = !issues.empty();
        issuesGroup.selected = state.selectedRow == issuesGroup.id;
        rows.push_back(issuesGroup);

        if (state.tree.isExpanded(issuesGroup.id))
        {
            for (std::size_t index = 0; index < issues.size(); ++index)
            {
                const SceneIssue& issue = issues[index];

                StudioTreeRow row;
                // The index is part of the identity because one entity can carry several issues,
                // and two rows sharing an id would share selection and expansion.
                row.id = "issue:" + std::to_string(index) + ":" + issue.ruleId;
                row.label = issue.entityName.empty()
                    ? issue.message
                    : issue.entityName + ": " + issue.message;
                row.detail = toString(issue.severity);
                row.detailRole = severityRole(issue.severity);
                row.depth = 1;
                row.selected = state.selectedRow == row.id;
                if (issue.entityId.isValid())
                {
                    outResult.rowEntities.emplace_back(row.id, issue.entityId);
                }
                rows.push_back(row);
            }
        }

        outResult.rowsTotal = rows.size();
        return rows;
    }

    StudioProblemsResult studioProblemsPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioProblemsState& state)
    {
        StudioProblemsResult result;

        const StudioTheme& theme = frame.theme();
        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(bounds, theme.color(StudioColorRole::PanelBackground));
        }

        UiRect body = bounds;
        if (body.width <= 0.0f || body.height <= 0.0f) { return result; }

        const std::vector<StudioTreeRow> rows = studioProblemRows(context, state, result);

        // --- The toolbar --------------------------------------------------------------------------
        //
        // One action, acting on the selection rather than on a button per row: thirty Clear buttons
        // read as thirty, and none of them has a keyboard path.
        const float toolbarHeight = metricOf(theme, StudioMetric::ControlHeight)
                                  + metricOf(theme, StudioMetric::SpacingSmall) * 2.0f;
        UiRect toolbar = body.splitTop(std::min(toolbarHeight, body.height));
        toolbar = toolbar.inset(UiEdges{metricOf(theme, StudioMetric::SpacingSmall),
                                        metricOf(theme, StudioMetric::SpacingSmall)});

        const bool assetSelected =
            state.selectedRow.rfind(std::string{kStudioBrokenAssetRowPrefix}, 0) == 0;

        frame.ids().push("problems");
        {
            StudioButtonOptions options;
            options.enabled = assetSelected;
            options.tooltip = "Clear every reference to the selected missing asset.\n"
                              "Undoable, like any other change to the scene.";
            const UiRect button = toolbar.splitLeft(
                std::min(toolbar.width,
                         std::ceil(studioLabelWidth(frame, "Clear reference"))));

            if (studioButton(frame, frame.ids().make("clear"), button, "Clear reference", options)
                    .activated)
            {
                result.clearAsset = Uuid::parse(
                    state.selectedRow.substr(kStudioBrokenAssetRowPrefix.size()));
            }
        }

        if (frame.isDrawPass())
        {
            frame.drawList().drawHorizontalSeparator(
                UiRect{bounds.left(), body.top(), bounds.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));
        }

        // --- The report ---------------------------------------------------------------------------
        const StudioTreeResult tree = studioTreeView(
            frame, body, rows, state.tree,
            "Nothing to report. The scene validates and every asset reference resolves.");
        result.rowsDrawn = tree.rowsDrawn;

        if (tree.dropped.has_value() && *tree.dropped < rows.size())
        {
            const StudioTreeRow& row = rows[*tree.dropped];
            if (row.id.rfind(std::string{kStudioBrokenAssetRowPrefix}, 0) == 0)
            {
                result.clearAsset = Uuid::parse(
                    row.id.substr(kStudioBrokenAssetRowPrefix.size()));
                result.relinkTo = Uuid::parse(tree.droppedValue);
            }
        }

        if (tree.clicked.has_value() && *tree.clicked < rows.size())
        {
            const StudioTreeRow& row = rows[*tree.clicked];
            state.selectedRow = row.id;

            // A row naming an entity takes the user to it. That is what the report is *for*: the
            // shortest path from "something is wrong" to the thing that is wrong.
            for (const auto& entry : result.rowEntities)
            {
                if (entry.first == row.id) { result.selectEntity = entry.second; break; }
            }
        }

        frame.ids().pop();
        return result;
    }
}
