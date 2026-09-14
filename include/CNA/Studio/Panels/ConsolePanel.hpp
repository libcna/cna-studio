// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Panels/ConsolePanel.hpp
 * @brief The message log, its severity filter and its scroll-lock.
 */

#include "CNA/Studio/Panels/StudioPanel.hpp"

namespace CNA::Studio
{
    /**
     * @brief Draws the console and owns how it is being viewed.
     *
     * The filter and the scroll-lock are not part of the document and are not undoable: what a
     * user chooses to look at is not an edit to the scene.
     */
    class ConsolePanel final : public StudioPanel
    {
    public:
        using StudioPanel::StudioPanel;

        void draw() override;

    private:
        LogSeverity minimumSeverity_ = LogSeverity::Trace;
        bool autoScroll_ = true;
    };
}
