// SPDX-License-Identifier: MS-PL
/**
 * @file StudioLayersPanel.cpp
 * @brief The project's render layers, and what is on each.
 */

#include "CNA/Studio/ShellPanels/StudioLayersPanel.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <string>

namespace CNA::Studio
{
    namespace
    {
        /** @brief The layer an entity is on, falling back to the first the project declares. */
        std::string layerOf(const StudioEntity& entity, const std::string& fallback)
        {
            const StudioComponent* component = entity.findComponent(BuiltinComponentIds::kLayer);
            if (component == nullptr) { return fallback; }

            const PropertyValue value = component->getProperty("layer");
            if (value.getType() != PropertyType::Enum) { return fallback; }

            const std::string name = value.get<PropertyValue::EnumValue>().name;
            return name.empty() ? fallback : name;
        }
    }

    std::vector<Uuid> studioEntitiesOnLayer(const StudioContext& context, std::string_view layer)
    {
        const std::vector<std::string>& layers = context.getProject().getLayers();
        const std::string fallback = layers.empty() ? std::string{} : layers.front();

        std::vector<Uuid> result;
        for (const StudioEntity& entity : context.getScene().getEntities())
        {
            if (layerOf(entity, fallback) == layer) { result.push_back(entity.getId()); }
        }
        return result;
    }

    std::vector<StudioTreeRow> studioLayerRows(const StudioContext& context,
                                               const StudioTreeState& state)
    {
        const std::vector<std::string>& layers = context.getProject().getLayers();

        std::vector<StudioTreeRow> rows;
        rows.reserve(layers.size());

        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            const std::string& layer = layers[index];
            const std::vector<Uuid> entities = studioEntitiesOnLayer(context, layer);

            StudioTreeRow row;
            row.id = "layer:" + layer;
            row.label = layer;
            // The draw order, because that *is* what a layer list means: index 0 draws first, and
            // sorting them by name would read tidier and say nothing.
            row.detail = entities.empty()
                ? std::string{"empty"}
                : std::to_string(entities.size())
                      + (entities.size() == 1 ? " entity" : " entities");
            row.hasChildren = !entities.empty();
            // An empty layer is still a layer the project declares: hiding it would make a user
            // wonder where the one they just added went.
            row.muted = entities.empty();
            rows.push_back(std::move(row));

            if (entities.empty() || !state.isExpanded(rows.back().id)) { continue; }

            for (const Uuid& id : entities)
            {
                const StudioEntity* entity = context.getScene().findEntity(id);
                if (entity == nullptr) { continue; }

                StudioTreeRow child;
                child.id = "layer:" + layer + "/" + id.toString();
                child.label = entity->getName().empty() ? std::string{"(unnamed)"}
                                                        : entity->getName();
                child.depth = 1;
                child.selected = context.isSelected(id);
                child.muted = !entity->isEnabled();
                rows.push_back(std::move(child));
            }
        }
        return rows;
    }

    StudioLayersResult studioLayersPanel(StudioFrame& frame, const UiRect& bounds,
                                         const StudioContext& context, StudioTreeState& state)
    {
        StudioLayersResult result;

        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(bounds,
                                      frame.theme().color(StudioColorRole::PanelBackground));
        }
        if (bounds.isEmpty()) { return result; }

        const std::vector<StudioTreeRow> rows = studioLayerRows(context, state);
        result.layerCount = context.getProject().getLayers().size();

        const std::string_view empty = context.hasProject()
            ? std::string_view{"This project declares no render layers."}
            : std::string_view{"No project is open."};

        frame.ids().push("layers");
        const StudioTreeResult tree = studioTreeView(frame, bounds, rows, state, empty);
        frame.ids().pop();

        result.rowsDrawn = tree.rowsDrawn;

        if (!tree.clicked.has_value() || *tree.clicked >= rows.size()) { return result; }

        const StudioTreeRow& row = rows[*tree.clicked];
        if (row.depth == 0)
        {
            // Clicking a layer selects everything on it, which is how a user turns "the background
            // is wrong" into something they can edit. The outliner cannot answer this: it is
            // ordered by the hierarchy, and a layer cuts across it.
            result.clickedLayer = row.label;
            result.selectEntities = studioEntitiesOnLayer(context, row.label);
            return result;
        }

        const std::size_t separator = row.id.rfind('/');
        if (separator != std::string::npos)
        {
            result.selectEntities.push_back(Uuid::parse(row.id.substr(separator + 1)));
        }
        return result;
    }
}
