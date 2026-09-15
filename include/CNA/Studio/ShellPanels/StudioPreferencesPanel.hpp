// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioPreferencesPanel.hpp
 * @brief The Preferences panel — what the user has decided about Studio itself.
 *
 * `plan.md` STUDIO-06011.
 *
 * ### A panel rather than a modal
 *
 * Preferences are read and changed while working — "the camera is too fast", "I cannot read this at
 * this size" — and a modal makes every one of those a trip out of and back into whatever the user
 * was doing. A dockable panel lets them put it beside the viewport, drag a value and watch the
 * viewport answer. That is also why every change applies immediately rather than on an OK button:
 * a preferences dialog with Apply is one where the user finds out whether they liked it only after
 * committing to it.
 *
 * ### Changing is not saving
 *
 * The panel reports *that* something changed; whoever owns the file decides when to write it. A
 * panel that wrote to disk on every drag of a slider would do a hundred writes for one decision.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioPreferences.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief What the Preferences panel did this frame. */
    struct StudioPreferencesPanelResult
    {
        /** @brief The user changed something. Input pass only. */
        bool changed = false;

        /**
         * @brief The user asked for the defaults back. Input pass only.
         *
         * Reported rather than done, because it is the one thing here that discards a decision the
         * user made deliberately, and the caller is where a confirmation belongs.
         */
        bool resetRequested = false;

        /** @brief How tall the content is, so the panel can be scrolled. */
        float contentHeight = 0.0f;
    };

    /**
     * @brief What the panel needs from its host that preferences alone cannot supply.
     */
    struct StudioPreferencesPanelContext
    {
        /** @brief The saved layouts the default-layout row offers. */
        std::vector<std::string> layoutNames;
    };

    /**
     * @brief Describes the Preferences panel.
     *
     * @param frame The frame.
     * @param body Where the panel's content goes.
     * @param preferences Edited in place; already clamped on return.
     * @param context What the host knows that preferences do not.
     * @return What the user changed and asked for.
     */
    StudioPreferencesPanelResult studioPreferencesPanel(StudioFrame& frame, const UiRect& body,
                                                        StudioPreferences& preferences,
                                                        const StudioPreferencesPanelContext& context);
}
