// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioLayersPanel.hpp
 * @brief The project's render layers, in draw order, and what is on each.
 *
 * `plan.md` STUDIO-07024.
 *
 * ### The order is the meaning
 *
 * A project's layers are a list rather than a set because index 0 draws first. So the panel shows
 * them in that order and says so, rather than sorting them by name into something that reads
 * tidier and means nothing.
 *
 * ### It answers "what is on this layer"
 *
 * Which is the question a layer list exists for, and the one the outliner cannot answer — the
 * outliner is ordered by the hierarchy, and a layer cuts across it. Clicking a layer selects
 * everything on it, which is how a user turns "the background is wrong" into something they can
 * edit.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief What the Layers panel showed and what the user asked for. */
    struct StudioLayersResult
    {
        /** @brief How many rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many layers the project declares. */
        std::size_t layerCount = 0;

        /** @brief Entities the user asked to select, when a layer was clicked. Input pass only. */
        std::vector<Uuid> selectEntities;

        /** @brief The layer that was clicked, if any. Input pass only. */
        std::string clickedLayer;
    };

    /**
     * @brief Returns the entities on @p layer, in document order.
     *
     * An entity with no Layer component is on the *first* layer, which is what the runtime does
     * with one — reporting it as belonging to nothing would hide every entity in a project that
     * has never touched layers.
     *
     * @param context The editor.
     * @param layer Layer name.
     * @return The entity ids.
     */
    [[nodiscard]] std::vector<Uuid> studioEntitiesOnLayer(const StudioContext& context,
                                                          std::string_view layer);

    /**
     * @brief Builds the layer rows for @p context.
     * @param context The editor.
     * @param state Expansion of each layer.
     * @return One row per layer, in draw order, with its entities beneath it when it is open.
     */
    [[nodiscard]] std::vector<StudioTreeRow> studioLayerRows(const StudioContext& context,
                                                             const StudioTreeState& state);

    /**
     * @brief Draws the Layers panel.
     * @param frame The frame.
     * @param bounds Where the panel's content goes.
     * @param context The editor.
     * @param state The panel's retained expansion state.
     * @return What was shown and what the user asked for.
     */
    StudioLayersResult studioLayersPanel(StudioFrame& frame, const UiRect& bounds,
                                         const StudioContext& context, StudioTreeState& state);
}
