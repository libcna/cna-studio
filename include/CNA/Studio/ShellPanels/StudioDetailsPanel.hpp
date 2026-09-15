// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioDetailsPanel.hpp
 * @brief The Details panel — the selected entity's components and their properties.
 *
 * `plan.md` STUDIO-07007.
 *
 * The third panel ported, and the first that *writes* to the document. That is the whole difference
 * between it and the outliner: showing a scene wrong is a bad afternoon, and editing one wrong is a
 * lost afternoon's work, so every edit here goes through the command history rather than touching
 * an entity directly.
 *
 * ### Committed, not continuous
 *
 * A field writes on Enter or on losing focus, never on each keystroke. A property bound to a field
 * that wrote per character would put one undo entry per letter, and would parse a number while it
 * is half-typed — `1e` on the way to `1e-3` is not a number, and rejecting it mid-word is how an
 * inspector becomes impossible to type into.
 *
 * ### What is editable, and what is honestly not yet
 *
 * Booleans, integers, floats, strings, enumerations and the two- and three-component vectors are
 * editable. Colours, quaternions, rectangles, references, lists and structures are *shown* with
 * what they hold and labelled as not editable yet — because a property nobody can see is worse than
 * one nobody can change, and a control that looked editable and silently did nothing would be worse
 * than both. `STUDIO-07018` is the rest, and it wants pickers rather than more text fields: a
 * colour typed as four numbers and a rotation typed as four is how an inspector gets a reputation.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief What the Details panel did this frame. */
    struct StudioDetailsResult
    {
        /** @brief How many property rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many components the selected entity has. */
        std::size_t componentCount = 0;

        /** @brief A property was committed to the document this frame. Input pass only. */
        bool edited = false;

        /** @brief What was edited, for the log: `Transform.position`, say. */
        std::string editedProperty;
    };

    /**
     * @brief Draws the Details panel and applies what the user committed.
     *
     * @param frame The frame.
     * @param bounds The panel's content rectangle.
     * @param context The editor. Its selection decides what is shown; its history receives edits.
     * @return What happened.
     */
    StudioDetailsResult studioDetailsPanel(StudioFrame& frame, const UiRect& bounds,
                                           StudioContext& context);
}
