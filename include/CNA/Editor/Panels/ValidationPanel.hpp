// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/ValidationPanel.hpp
 * @brief The one place that answers "what is wrong with this scene?".
 *
 * It began as the missing-reference report and grew the structural rules of plan.md ED-310. The
 * two stayed in one panel on purpose: a user whose scene misbehaves does not know in advance
 * whether the cause is a deleted texture or two cameras both claiming to be primary, and sending
 * them to two panels to find out would be answering a question they did not ask.
 *
 * Broken references come first, because they are the only section with a repair the panel can
 * perform. Relinking is one command, so forty sprites are fixed, and undone, together. The
 * structural issues below are reported and never repaired -- every one of them describes a legal
 * state, and an editor that silently rewrote a legal scene would be worse than one that stayed
 * quiet.
 */

#include "CNA/Editor/Panels/EditorPanel.hpp"

namespace CNA::Editor
{
    /**
     * @brief Lists unresolvable asset references and structural scene problems.
     *
     * References are grouped by asset id rather than by entity, because a broken reference is
     * almost always one asset that forty sprites point at -- and the fix is the same for all forty.
     *
     * A reference row is a drop target for the asset browser: dragging the right asset onto it is
     * the shortest path from "this is broken" to "this is fixed", and it reuses the drag-and-drop
     * the inspector's slots already use. Clicking a structural issue selects the entity at fault,
     * which is the whole point of reporting one -- finding which of two hundred entities carries it
     * is the sort of hunt an editor exists to prevent.
     */
    class ValidationPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

    private:
        /** @brief Draws the broken-reference section, including its relink and clear actions. */
        void drawMissingReferences();

        /** @brief Draws the structural rules section. */
        void drawSceneIssues();

        /** @brief Payload type for an asset dragged out of the browser. */
        static constexpr const char* kAssetDragType = "asset";
    };
}
