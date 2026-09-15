// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioShortcutEditor.hpp
 * @brief Rebinding a command's shortcut, with the conflict shown before it is accepted.
 *
 * `plan.md` STUDIO-06012.
 *
 * ### Capture, not a text field
 *
 * A user rebinding a key presses the key. Asking them to *type the name of it* — `Ctrl+Shift+K` —
 * is asking them to know Studio's spelling of every key, and to get it right without feedback. So a
 * row is armed and the next chord is taken, which is what every editor with rebindable keys does.
 *
 * ### The conflict is shown before it is accepted, not after
 *
 * `StudioActionRegistry::rebind` already refuses a chord that another command holds, and names the
 * holder. What that refusal cannot do on its own is tell the user *why* nothing happened, so the
 * editor keeps the refused chord and the command that owns it and says both — and leaves the row
 * armed, because the user's next act is to try a different chord rather than to start again.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioActionRegistry.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioPreferences.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>

namespace CNA::Studio
{
    /** @brief What the shortcut editor is in the middle of. */
    struct StudioShortcutEditorState
    {
        /** @brief The command listening for a chord, or empty when none is. */
        std::string capturing;

        /** @brief The chord that was refused, for the message. Unbound when nothing was. */
        StudioShortcut refused;

        /** @brief The command already holding @ref refused. */
        std::string refusedBy;

        /** @brief Forgets a refusal, so the next arming starts clean. */
        void clearRefusal()
        {
            refused = StudioShortcut{};
            refusedBy.clear();
        }
    };

    /** @brief What the shortcut editor did this frame. */
    struct StudioShortcutEditorResult
    {
        /** @brief A command was rebound. Input pass only. */
        bool changed = false;

        /** @brief How many rows were drawn, for a test that wants to know it listed anything. */
        std::size_t rowsDrawn = 0;

        /** @brief How tall the content is, so the caller can scroll it. */
        float contentHeight = 0.0f;
    };

    /**
     * @brief Returns the chord pressed this frame, or false when none was.
     *
     * A chord is a non-modifier key plus whatever modifiers were held with it, so this ignores a
     * frame in which only Shift went down: arming a row and pressing Ctrl would otherwise bind the
     * command to Ctrl and nothing, which is not a chord a user can press deliberately.
     *
     * @param frame The frame.
     * @param out Receives the chord.
     * @return False when no non-modifier key was pressed this frame.
     */
    [[nodiscard]] bool studioCapturedShortcut(const StudioFrame& frame, StudioShortcut& out);

    /**
     * @brief The sentence shown when a chord is refused, naming the command that holds it.
     *
     * Separate from the editor because it is the *product* of a refusal: the draw list holds
     * glyphs rather than strings, so a message composed inside the draw pass is a message no test
     * can read, and "the conflict is named" is precisely the acceptance criterion.
     *
     * A label alone is enough when it is unique -- which it is for everything on the menu bar --
     * but twelve commands are called "Close" or "Float", one pair per panel, and "Ctrl+W is
     * already Close" would leave a user guessing which panel they had just failed to rebind. For
     * those the description is added, rather than lengthening every message for the sake of them.
     *
     * @param registry Registry holding the commands.
     * @param refused The chord that was refused.
     * @param refusedBy Identifier of the command already holding it.
     * @return The sentence, or empty when @p refusedBy is empty.
     */
    [[nodiscard]] std::string studioShortcutConflictMessage(const StudioActionRegistry& registry,
                                                            const StudioShortcut& refused,
                                                            const std::string& refusedBy);

    /**
     * @brief Draws the list of commands and their shortcuts, and takes a rebinding.
     *
     * @param frame The frame.
     * @param body Where the list goes.
     * @param registry The commands; rebound in place.
     * @param preferences The overrides, updated so the change survives a restart.
     * @param state What is being captured, and what was refused.
     * @return What happened.
     */
    StudioShortcutEditorResult studioShortcutEditor(StudioFrame& frame, const UiRect& body,
                                                    StudioActionRegistry& registry,
                                                    StudioPreferences& preferences,
                                                    StudioShortcutEditorState& state);
}
