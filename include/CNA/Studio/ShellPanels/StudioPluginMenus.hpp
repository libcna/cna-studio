// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/ShellPanels/StudioPluginMenus.hpp
 * @brief A plugin's menu commands as *actions*, not as rows somebody draws.
 *
 * `plan.md` STUDIO-07002, and the last row of the migration inventory's menu table.
 *
 * The prototype draws plugin menus itself: it walks the extension registry every frame and calls
 * `beginMenu` and `menuItem` for each. That works and it is the reason a plugin command there can
 * never have a shortcut, never be greyed out, never appear on a toolbar, and never show up in the
 * shortcut editor — it is not a command, it is a row.
 *
 * The native shell has one place a command is defined, so a plugin's command becomes a command:
 * registered in the action registry with an id, invoked through it, and put on a menu by naming
 * that id. Everything the registry offers then applies to it for free.
 *
 * ### The menu a plugin asks for may already exist
 *
 * A command registered under "Tools" belongs in Studio's Tools menu rather than in a second menu of
 * the same name beside it — which is what the prototype produces, because Dear ImGui's `BeginMenu`
 * has no opinion about a title it has already seen. Appending to the existing menu is the only
 * reading a user would expect.
 */

#include <cstddef>
#include <string>

namespace CNA::Studio
{
    class StudioContext;
    class StudioLog;
    class StudioShell;

    /** @brief The prefix every action registered for a plugin command carries. */
    inline constexpr std::string_view kStudioPluginActionPrefix = "studio.plugin.";

    /**
     * @brief Rebuilds the shell's plugin actions and menu rows from @p context's extensions.
     *
     * Idempotent: every action it registered before is removed first, so a plugin that has been
     * unloaded leaves no row behind and a reloaded one does not appear twice. Studio's own menus are
     * rebuilt from scratch around them, because a plugin menu that outlived its plugin would be a
     * menu whose every row reports that the command has gone.
     *
     * @param shell Shell whose registry and menus are rewritten.
     * @param context Editor holding the plugin extensions; handed to each command when it runs.
     * @param log Where a command that throws is reported.
     * @return How many plugin commands are now on the menus.
     */
    std::size_t bindStudioPluginMenus(StudioShell& shell, StudioContext& context, StudioLog& log);

    /**
     * @brief Removes every plugin action from @p shell, leaving Studio's own untouched.
     *
     * **Must be called while the plugins' libraries are still mapped.** A registered action holds a
     * `std::function` copied out of the plugin, and destroying one runs a manager function that
     * lives in the plugin's library — so a registry cleared *after* `dlclose` does not fail to find
     * the command, it jumps into unmapped memory. That is a segmentation fault in the shell's
     * destructor, which is the hardest place to read a backtrace from and the least likely place
     * anybody looks.
     *
     * `bindStudioPluginMenus` does this first, which is what makes it idempotent. It is exposed
     * separately for shutdown, where the menus are not being rebuilt afterwards and there is
     * nothing to rebuild them from.
     *
     * @param shell Shell whose registry is cleared of plugin actions.
     * @return How many actions were removed.
     */
    std::size_t studioClearPluginMenus(StudioShell& shell);
}
