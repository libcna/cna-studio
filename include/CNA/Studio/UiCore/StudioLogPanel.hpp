// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioLogPanel.hpp
 * @brief The Output Log, on the Studio UI. The first panel ported off Dear ImGui.
 *
 * `plan.md` STUDIO-07005.
 *
 * Chosen first deliberately. It is the simplest panel that is still a real one — a filtered,
 * scrolling, selectable list with a toolbar — so it exercises the parts of the new UI a panel
 * actually needs (scrolling, virtualised rows, a row of controls, retained view state) without also
 * needing a property grid, a tree, drag and drop or a graphics device. If the strangler seam is
 * wrong, this is where it is cheapest to find out.
 *
 * ### It owns its view state, not the log
 *
 * The log is a `StudioLog` the panel is handed. What the panel owns is what the *user chose to look
 * at*: the severity filter and whether to follow new output. Those are not document state, not
 * undoable, and not shared between two windows showing the same log — a person filtering one
 * console to errors has not asked the other to hide anything.
 */

#pragma once

#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>

namespace CNA::Studio
{
    /** @brief What the user asked the log panel to do this frame. */
    struct StudioLogPanelResult
    {
        /** @brief The Clear button was pressed. True only in the input pass. */
        bool cleared = false;

        /** @brief The Copy button was pressed; @ref copyText is what to put on the clipboard. */
        bool copyRequested = false;

        /** @brief The text Copy asked for, filtered exactly as the panel is showing it. */
        std::string copyText;

        /** @brief How many rows the panel actually put on screen this pass. */
        std::size_t rowsDrawn = 0;

        /** @brief How many entries the current filter is showing, drawn or scrolled out of view. */
        std::size_t rowsMatching = 0;
    };

    /**
     * @brief The Output Log panel: a toolbar and a virtualised, filtered list.
     *
     * Draws in whichever pass the frame is in, like every other widget here.
     *
     * @param frame The frame.
     * @param bounds The panel's content rectangle.
     * @param log The log to show. Not modified — clearing is reported, not done, so the owner of
     *        the log decides.
     * @return What the user asked for.
     */
    StudioLogPanelResult studioLogPanel(StudioFrame& frame, const UiRect& bounds,
                                        const StudioLog& log);

    /**
     * @brief The colour a severity is drawn in.
     *
     * Exposed so a test can assert that an error does not read as ordinary text without repeating
     * the mapping and thereby being able to agree with itself while both are wrong.
     *
     * @param theme Theme supplying colours.
     * @param severity The severity.
     * @return Its text colour.
     */
    [[nodiscard]] StudioColor studioLogSeverityColor(const StudioTheme& theme, LogSeverity severity);
}
