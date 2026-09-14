// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/ValidationPanel.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/EditorContext.hpp"
#include "CNA/Editor/Scene/MissingReferences.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"
#include "CNA/Editor/Scene/SceneValidation.hpp"

namespace CNA::Editor
{
    void ValidationPanel::draw()
    {
        if (!ui_.beginPanel("Validation", DockSide::Bottom)) { ui_.endPanel(); return; }

        drawMissingReferences();
        drawSceneIssues();

        ui_.endPanel();
    }

    void ValidationPanel::drawMissingReferences()
    {
        const std::vector<MissingReference> missing =
            findMissingReferences(context_.getScene(), context_.getAssets());

        if (missing.empty())
        {
            // Said plainly rather than left blank. An empty panel reads as "not implemented yet",
            // which is exactly the wrong thing for a report whose good state is emptiness.
            ui_.text("No broken asset references.");
            ui_.separator();
            return;
        }

        ui_.text(std::to_string(missing.size()) + " broken reference(s)");
        ui_.separator();

        // Recorded and applied after the loop: relinking rewrites the very properties the report
        // was built from, so the vector above would describe a scene that no longer exists.
        Uuid relinkFrom;
        Uuid relinkTo;

        for (const Uuid& assetId : collectMissingAssetIds(missing))
        {
            std::size_t count = 0;
            const char* reason = "";
            std::string users;

            for (const MissingReference& reference : missing)
            {
                if (reference.assetId != assetId) { continue; }

                ++count;
                reason = toString(reference.reason);

                // Three names and a count, rather than forty names. The list exists to make the
                // breakage recognisable, not to enumerate it.
                if (count <= 3)
                {
                    if (!users.empty()) { users += ", "; }
                    users += reference.entityName + "." + reference.propertyName;
                }
            }

            if (count > 3) { users += " and " + std::to_string(count - 3) + " more"; }

            // A tree leaf rather than a line of text: it carries the broken asset's own id as its
            // widget identity, which is what a drop target needs to be told apart from its
            // neighbours.
            ui_.treeNode(assetId, assetId.toString() + "  (" + reason + ")", false, true);

            // Dragging the right asset from the browser onto the row is the shortest path from
            // "this is broken" to "this is fixed", and it reuses the drag the inspector already has.
            if (const std::optional<std::string> dropped = ui_.acceptDrop(kAssetDragType))
            {
                relinkFrom = assetId;
                relinkTo = Uuid::parse(*dropped);
            }

            ui_.text("    " + users);

            if (ui_.button("Clear##" + assetId.toString()))
            {
                relinkFrom = assetId;
                relinkTo = Uuid{};
            }

            ui_.separator();
        }

        if (!relinkFrom.isValid()) { return; }

        auto command = std::make_unique<RelinkAssetCommand>(context_.getScene(), relinkFrom, relinkTo);
        if (!command->isValid()) { return; }

        const std::string summary = command->getDescription();
        context_.execute(std::move(command));
        context_.log(LogSeverity::Info, summary + ".");
    }

    void ValidationPanel::drawSceneIssues()
    {
        std::vector<SceneIssue> issues =
            validateScene(context_.getScene(), context_.getComponentRegistry());

        // Appended rather than reported in their own section: a user whose model has the wrong
        // material on it does not know in advance whether that is a structural problem or a
        // geometry one, which is the same argument that put the missing-reference rules and the
        // structural rules in one panel to begin with (ED-310).
        const std::vector<SceneIssue> partIssues =
            validateModelPartMaterials(context_.getScene(), context_.makeMeshProvider());
        issues.insert(issues.end(), partIssues.begin(), partIssues.end());

        if (issues.empty())
        {
            ui_.text("No scene issues.");
            return;
        }

        const std::size_t errors = countIssues(issues, SceneIssue::Severity::Error);
        const std::size_t warnings = countIssues(issues, SceneIssue::Severity::Warning);
        ui_.text(std::to_string(errors) + " error(s), " + std::to_string(warnings) + " warning(s)");

        // Applied after the loop, for the same reason the relink is: selecting changes what the
        // inspector and the viewport draw, and doing it mid-list would leave half the rows drawn
        // against one selection and half against another.
        Uuid selectTarget;

        for (std::size_t index = 0; index < issues.size(); ++index)
        {
            const SceneIssue& issue = issues[index];

            std::string label = std::string{"["} + toString(issue.severity) + "] ";
            if (!issue.entityName.empty()) { label += issue.entityName + ": "; }
            label += issue.message;

            // The index is part of the widget identity because one entity can carry several
            // issues, and two rows sharing an id would share click state.
            const UiTreeNodeResult row =
                ui_.treeNode("issue-" + std::to_string(index) + "-" + issue.ruleId, label,
                             issue.entityId.isValid() && context_.isSelected(issue.entityId), true);

            if (row.clicked && issue.entityId.isValid()) { selectTarget = issue.entityId; }
        }

        if (selectTarget.isValid()) { context_.select(selectTarget); }
    }
}
