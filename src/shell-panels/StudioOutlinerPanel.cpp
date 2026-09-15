// SPDX-License-Identifier: MS-PL
/**
 * @file StudioOutlinerPanel.cpp
 * @brief The World Outliner.
 */

#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"

#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <memory>

#include <algorithm>

namespace CNA::Studio
{
    namespace
    {
        /**
         * @brief Appends @p id and, when it is open, its children.
         *
         * Depth-limited rather than cycle-detecting. A scene document is kept acyclic by every
         * operation that can reparent, so a cycle here would be a bug elsewhere -- but it would
         * present as a hang, and a hang is the one failure a user cannot diagnose or report.
         */
        void flatten(const SceneDocument& scene, const Uuid& id, int depth,
                     const std::vector<Uuid>& selection, const StudioTreeState& state,
                     std::vector<StudioTreeRow>& out)
        {
            constexpr int kMaxDepth = 64;

            const StudioEntity* entity = scene.findEntity(id);
            if (entity == nullptr || depth > kMaxDepth) { return; }

            const std::vector<Uuid> children = scene.getChildren(id);

            StudioTreeRow row;
            row.id = id.toString();
            row.label = entity->getName().empty() ? std::string{"(unnamed)"} : entity->getName();
            row.depth = depth;
            row.hasChildren = !children.empty();
            row.selected = std::find(selection.begin(), selection.end(), id) != selection.end();
            row.enabled = entity->isEnabled();

            // The component list is what tells a camera from a sprite at a glance, and it is the
            // first thing anybody looks for in an outliner. One name reads; five is a wall.
            if (entity->getComponents().size() == 1)
            {
                row.detail = entity->getComponents().front().getTypeId();
            }
            else if (entity->getComponents().size() > 1)
            {
                row.detail = std::to_string(entity->getComponents().size()) + " components";
            }

            out.push_back(std::move(row));

            if (!state.isExpanded(id.toString())) { return; }
            for (const Uuid& child : children)
            {
                flatten(scene, child, depth + 1, selection, state, out);
            }
        }
    }

    std::vector<StudioTreeRow> studioOutlinerRows(const SceneDocument& scene,
                                                  const std::vector<Uuid>& selection,
                                                  const StudioTreeState& state)
    {
        std::vector<StudioTreeRow> rows;
        rows.reserve(scene.getEntityCount());
        for (const Uuid& root : scene.getRootEntities())
        {
            flatten(scene, root, 0, selection, state, rows);
        }
        return rows;
    }

    bool studioBeginOutlinerRename(const SceneDocument& scene, const Uuid& entityId,
                                   StudioTreeState& state)
    {
        const StudioEntity* entity = scene.findEntity(entityId);
        if (entity == nullptr) { return false; }

        state.beginRename(entityId.toString(), entity->getName());
        return true;
    }

    StudioOutlinerResult studioOutlinerPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioTreeState& state)
    {
        StudioOutlinerResult result;

        const std::vector<StudioTreeRow> rows =
            studioOutlinerRows(context.getScene(), context.getSelection(), state);
        result.rowsTotal = rows.size();

        // Two empties, said apart. "No scene is open" and "this scene is empty" call for different
        // next actions, and a panel that gave the same words for both would send half its readers
        // looking in the wrong place.
        const std::string_view empty = context.hasProject()
            ? std::string_view{"This scene has no entities yet."}
            : std::string_view{"No project is open."};

        const StudioTreeResult tree = studioTreeView(frame, bounds, rows, state, empty);
        result.rowsDrawn = tree.rowsDrawn;

        if (tree.renamed.has_value())
        {
            const Uuid id = Uuid::parse(rows[*tree.renamed].id);
            if (id.isValid())
            {
                // Through the history, like every other edit. A rename that could not be undone
                // would be the one change in the editor that is not a change.
                context.execute(std::make_unique<RenameEntityCommand>(context.getScene(), id,
                                                                      tree.renamedTo));
                result.renamed = true;
            }
        }

        if (tree.clicked.has_value())
        {
            const StudioTreeRow& row = rows[*tree.clicked];
            // The row id *is* the entity's UUID, printed by the flattener above -- so a parse that
            // came back nil would mean the rows and the scene had gone out of step, not that the
            // user clicked something odd. Checked anyway, because selecting the nil entity would
            // clear the inspector and look like a bug in the inspector.
            const Uuid id = Uuid::parse(row.id);
            if (id.isValid())
            {
                // Through the context, which is what the viewport, the inspector and the gizmos
                // all read. A panel with its own idea of what is selected disagrees with the rest
                // of the editor the moment anything else changes it.
                if (tree.additive) { context.toggleSelection(id); }
                else { context.select(id); }
                result.selectionChanged = true;
            }
        }

        return result;
    }
}
