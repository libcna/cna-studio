// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioShellActions.hpp
 * @brief Gives the shell's actions handlers that reach the editor.
 *
 * `plan.md` STUDIO-07020's precondition, and the working half of `STUDIO-06001`.
 *
 * The action registry already holds every command with its label, its menu and its shortcut. What
 * it has no way to know is what any of them *do*. This is where that is decided, once — so the menu
 * item, the toolbar button and the keyboard chord invoke the same object and cannot drift apart,
 * which is the entire reason the registry exists rather than three separate lists of handlers.
 *
 * ### Enablement is a predicate, not a flag
 *
 * Every binding supplies one. Asking at the moment the answer is needed is what makes Undo grey
 * itself out the instant the history empties, rather than at whatever point somebody remembered to
 * refresh a cached boolean — and a stale enablement is worse than none, because a control that
 * looks available and refuses is indistinguishable from one that is broken.
 *
 * ### Binding is not declaring
 *
 * Nothing here adds an action. An id that the registry does not carry is skipped: the menus are
 * built from the registry, so an action invented here would be one no menu shows and nothing can
 * reach.
 */

#pragma once

namespace CNA::Studio
{
    class StudioContext;
    class StudioLog;
    class StudioShell;

    /**
     * @brief Binds the shell's editing actions to @p context.
     *
     * Undo, Redo, Save and Delete today; the rest keep their registry entry and stay disabled,
     * which is how a menu says "not yet" rather than doing nothing when clicked.
     *
     * @param shell The shell whose registry is bound.
     * @param context The editor the handlers act on.
     * @param log Where each action reports what it did. A user who pressed Ctrl+Z and saw nothing
     *        change needs to know whether nothing happened or nothing was undoable.
     * @return How many actions were given handlers.
     */
    int bindStudioShellActions(StudioShell& shell, StudioContext& context, StudioLog& log);
}
