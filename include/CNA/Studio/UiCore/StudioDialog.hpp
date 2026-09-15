// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioDialog.hpp
 * @brief A modal dialog: a window that owns the frame until it is answered.
 *
 * `plan.md` STUDIO-03040.
 *
 * ### Why it is not a panel that happens to be on top
 *
 * Every part of a dialog that matters is about what it *prevents*. Clicking behind it does nothing,
 * Escape answers it rather than closing something underneath, Tab cycles within it and cannot leave,
 * and Enter takes the default. A window with none of those is a panel, and a user who does not know
 * they are blocked reads the unresponsive editor as a hang — which is why the scrim is drawn too.
 *
 * ### The caller keeps the state, as everywhere else here
 *
 * The dialog is described into a frame like any other widget and answers in the input pass. What is
 * open, and what to do with the answer, belongs to whoever opened it: a dialog that owned its own
 * lifetime would be a second place the application's state lives.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief What a dialog says and what it offers. */
    struct StudioDialogRequest
    {
        /** @brief The title bar's text. */
        std::string title;

        /**
         * @brief The body, as lines.
         *
         * Lines rather than one string with newlines in it, because a dialog that wrapped its own
         * text would need to measure and re-flow on every frame, and every message worth showing in
         * one is short enough for its author to break where it reads best.
         */
        std::vector<std::string> lines;

        /**
         * @brief Button labels, left to right. Empty means a single "OK".
         *
         * The *last* is the affirmative one, which is where every desktop convention outside
         * Windows puts it and where this UI puts the primary action everywhere else.
         */
        std::vector<std::string> buttons;

        /**
         * @brief Index of the button Enter activates. Out of range means the last.
         */
        int defaultButton = -1;

        /**
         * @brief Index of the button Escape activates, or -1 for none.
         *
         * A dialog with no cancel still closes on Escape when @ref dismissable is set; this is for
         * the case where Escape should mean a *particular* answer, such as Don't Save.
         */
        int cancelButton = -1;

        /** @brief False for a dialog that must be answered by a button rather than by Escape. */
        bool dismissable = true;

        /** @brief True to offer a single-line text field above the buttons. */
        bool hasTextField = false;

        /** @brief The field's starting contents. */
        std::string text;

        /** @brief What the field shows when it is empty. */
        std::string placeholder;

        /**
         * @brief True to refuse the affirmative button while the field is empty.
         *
         * A Save As with no name is not an answer, and a button that accepts one and then reports
         * a failure is worse than a button that is plainly not ready.
         */
        bool requireText = false;
    };

    /** @brief What the user did to a dialog this frame. */
    struct StudioDialogResult
    {
        /** @brief The button pressed, or -1. Input pass only. */
        int chosen = -1;

        /** @brief Whether the answer came from Escape rather than a button. Input pass only. */
        bool dismissed = false;

        /** @brief The field's contents, whether or not it was edited. */
        std::string text;

        /** @brief True on the frame the dialog was answered, by a button or by Escape. */
        [[nodiscard]] bool answered() const { return chosen >= 0 || dismissed; }
    };

    /**
     * @brief Where a dialog's state lives between frames.
     *
     * Not the keyboard focus, which belongs to the router: a widget the current layer blocks does
     * not register as focusable, so while a modal is open its own controls are the whole Tab ring
     * and the traversal cannot walk out into the panels it covers. A dialog that kept its own
     * focus index would be a second answer to a question already answered.
     */
    struct StudioDialogState
    {
        /** @brief The field's contents, edited in place. */
        std::string text;

        /**
         * @brief Whether @ref text has been seeded from the request yet.
         *
         * Also what marks the dialog's first frame, which is when the keyboard is placed. Reset it
         * to reopen the same dialog with its starting text and focus back.
         */
        bool seeded = false;
    };

    /**
     * @brief Returns the rectangle a dialog for @p request occupies in a window of @p window.
     *
     * Separate from drawing so the shell can know where the dialog is without describing it —
     * and so a test can assert that it is centred and inside the window at any size.
     *
     * @param frame The frame, for text measurement and the theme.
     * @param window The whole window.
     * @param request What the dialog says.
     * @return The dialog's bounds, centred and clamped inside @p window.
     */
    [[nodiscard]] UiRect studioDialogBounds(const StudioFrame& frame, const UiRect& window,
                                            const StudioDialogRequest& request);

    /**
     * @brief Describes a modal dialog.
     *
     * Call from inside a modal body — `StudioFrame::deferModal` — so it routes in the modal layer
     * over the scrim.
     *
     * @param frame The frame.
     * @param window The whole window; the dialog centres itself in it.
     * @param request What the dialog says and offers.
     * @param state Its retained focus and field contents.
     * @return What the user did. Meaningful only in the input pass.
     */
    StudioDialogResult studioDialog(StudioFrame& frame, const UiRect& window,
                                    const StudioDialogRequest& request, StudioDialogState& state);
}
