// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioProblemsPanel.hpp
 * @brief The Problems panel — scene validation and broken asset references — on the Studio UI.
 *
 * `plan.md` STUDIO-07012.
 *
 * ### One panel, two reports, and why they belong together
 *
 * A user whose model has the wrong material on it does not know in advance whether that is a
 * structural problem or a broken reference, and asking them to look in two places to find out is
 * asking them to know the answer first (legacy ED-310). So the missing-reference rules and the
 * structural rules report into one list.
 *
 * ### Clearing a reference acts on the selection, not on a button per row
 *
 * The ImGui panel puts a `Clear` button beside every broken asset. That reads fine with three and
 * badly with thirty, and it has no keyboard path at all. Here the action sits above the list and
 * acts on the selected row — the ordinary editor shape, reachable by Tab, and it leaves room for
 * the *other* half of the legacy panel's repair path (dragging the right asset onto the row) to
 * arrive with drag and drop rather than being designed around now.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief The Problems panel's own state: which row is selected, and what is open. */
    struct StudioProblemsState
    {
        /** @brief Expansion of the two groups and of each broken asset. */
        StudioTreeState tree;

        /** @brief Id of the selected row, in the tree's own id space. */
        std::string selectedRow;
    };

    /** @brief What the Problems panel reported and what the user asked for. */
    struct StudioProblemsResult
    {
        /** @brief How many rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many rows the reports produced. */
        std::size_t rowsTotal = 0;

        /** @brief Broken asset references found. */
        std::size_t brokenReferences = 0;

        /** @brief Scene issues at error severity. */
        std::size_t errors = 0;

        /** @brief Scene issues at warning severity. */
        std::size_t warnings = 0;

        /**
         * @brief The entity each row points at, by row id.
         *
         * Built while the rows are, because that is the only place the answer is known without
         * running validation a second time — and running it twice would let the list the user
         * clicked and the list the click is resolved against disagree.
         */
        std::vector<std::pair<std::string, Uuid>> rowEntities;

        /** @brief The entity the user asked to look at, if any. Input pass only. */
        Uuid selectEntity;

        /** @brief The asset whose references the user asked to repoint or clear. Input pass only. */
        Uuid clearAsset;

        /**
         * @brief What @ref clearAsset's references should point at instead.
         *
         * Invalid means "clear them", which is what the toolbar button asks for. A valid id is a
         * *relink*, which is what dropping an asset onto the broken row asks for — the same
         * command either way, because clearing is relinking to nothing.
         */
        Uuid relinkTo;
    };

    /**
     * @brief Builds the report rows for @p context.
     *
     * Separate from drawing so a test can assert on what the panel *says* without a frame, and so
     * the panel's two jobs — deciding what the report is and drawing it — fail independently.
     *
     * @param context The editor context to validate.
     * @param state Expansion and selection.
     * @param outResult Receives the counts.
     * @return The rows, in display order.
     */
    [[nodiscard]] std::vector<StudioTreeRow> studioProblemRows(StudioContext& context,
                                                               const StudioProblemsState& state,
                                                               StudioProblemsResult& outResult);

    /**
     * @brief Draws the Problems panel.
     * @param frame The frame.
     * @param bounds Where the panel's content goes.
     * @param context The editor context.
     * @param state The panel's retained state.
     * @return What was reported and what the user asked for.
     */
    StudioProblemsResult studioProblemsPanel(StudioFrame& frame, const UiRect& bounds,
                                             StudioContext& context, StudioProblemsState& state);

    /** @brief The row id prefix identifying a broken asset's group row. */
    inline constexpr std::string_view kStudioBrokenAssetRowPrefix = "asset:";
}
