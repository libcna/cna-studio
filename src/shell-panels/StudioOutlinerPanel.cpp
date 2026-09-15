// SPDX-License-Identifier: MS-PL
/**
 * @file StudioOutlinerPanel.cpp
 * @brief The World Outliner.
 */

#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
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
         * @brief What an entity *is*, from what it carries.
         *
         * `STUDIO-35030`. The first thing anybody looks for in an outliner is which row is the
         * camera, and reading that off a detail column of component names is reading rather than
         * scanning. Decided from the components rather than from a field on the entity, because a
         * scene has no such field and inventing one would put a *presentation* concern into the
         * document format -- where it would then have to be migrated, validated and exported.
         *
         * Ordered by how much the answer tells a user, not by how common the component is. An
         * entity with a camera and a light is a camera with a light attached, because the camera is
         * the thing somebody is looking for.
         */
        StudioIcon iconFor(const StudioEntity& entity)
        {
            const auto has = [&entity](const char* typeId) {
                for (const StudioComponent& component : entity.getComponents())
                {
                    if (component.getTypeId() == typeId) { return true; }
                }
                return false;
            };

            if (has(BuiltinComponentIds::kCamera)) { return StudioIcon::Camera; }
            if (has(BuiltinComponentIds::kLight)) { return StudioIcon::Light; }
            if (has(BuiltinComponentIds::kModelRenderer)) { return StudioIcon::Mesh; }
            if (has(BuiltinComponentIds::kSpriteRenderer)
                || has(BuiltinComponentIds::kSpriteAnimation)
                || has(BuiltinComponentIds::kTilemap)) { return StudioIcon::Sprite; }
            if (has(BuiltinComponentIds::kAudioSource)
                || has(BuiltinComponentIds::kAudioListener)) { return StudioIcon::Audio; }

            // A transform and nothing else is still an entity and still gets an icon: an empty
            // used as a pivot or a group is a real thing in the scene, and a blank where every
            // other row has a picture reads as a row that failed to load.
            return StudioIcon::Entity;
        }

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
            row.icon = iconFor(*entity);

            // `STUDIO-35060`. An outliner where hiding an entity means selecting it, finding the
            // Details panel and unticking a box is one where nobody hides anything -- and hiding
            // things is how a large scene is worked on at all.
            //
            // The entity's `enabled` flag rather than a second "visible" one. A scene has no such
            // field, and inventing one would put a presentation concern into the document format,
            // where it would then have to be migrated, validated and exported -- and it would be a
            // second thing that hides an entity, which is one too many.
            row.toggleIcon = StudioIcon::Visible;
            row.toggleOffIcon = StudioIcon::Hidden;
            row.toggleOn = entity->isEnabled();
            row.toggleTooltip = entity->isEnabled() ? "Hide this entity" : "Show this entity";

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

        if (tree.toggledRowAction.has_value())
        {
            const Uuid id = Uuid::parse(rows[*tree.toggledRowAction].id);
            const StudioEntity* entity = context.getScene().findEntity(id);
            if (entity != nullptr)
            {
                // Through the history, like every other edit, and *before* the click below is
                // considered: a press on the toggle is not a press on the row, and handling both
                // would hide an entity and select it in one gesture.
                context.execute(std::make_unique<SetEntityEnabledCommand>(
                    context.getScene(), id, !entity->isEnabled()));
                result.visibilityChanged = true;
            }
            return result;
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
