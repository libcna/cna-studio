// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/ConsolePanel.hpp
 * @brief The message log, its severity filter and its scroll-lock.
 */

#include "CNA/Editor/Panels/EditorPanel.hpp"

namespace CNA::Editor
{
    /**
     * @brief Draws the console and owns how it is being viewed.
     *
     * The filter and the scroll-lock are not part of the document and are not undoable: what a
     * user chooses to look at is not an edit to the scene.
     */
    class ConsolePanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

    private:
        LogSeverity minimumSeverity_ = LogSeverity::Trace;
        bool autoScroll_ = true;
    };
}
