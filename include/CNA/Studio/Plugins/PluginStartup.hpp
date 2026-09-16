// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Plugins/PluginStartup.hpp
 * @brief Finding and loading the project's plugins when a Studio starts.
 *
 * `plan.md` STUDIO-07052.
 *
 * ### Why this is not a method on `PluginHost`
 *
 * `PluginHost` answers "load what is in this directory". *Which* directory, and whether there is
 * one at all, is a start-up policy: a `--plugins=DIR` override, otherwise `plugins/` beside the
 * executable, otherwise nothing — and "otherwise nothing" is silent, because having no plugins is
 * the ordinary case and a warning about a directory nobody created teaches users to ignore
 * warnings.
 *
 * That policy was written once, inside `StudioApplication::loadPlugins`, and the native shell never
 * had it. `bindStudioPluginMenus` drew the commands a plugin registered faithfully, and no plugin
 * was ever loaded to register one — so the Plugins menu was empty on every native run, which is
 * indistinguishable from a machine with no plugins installed.
 */

#pragma once

#include <cstddef>
#include <string>

namespace CNA::Studio
{
    class PluginHost;
    class StudioContext;

    /** @brief What @ref studioLoadPlugins found and loaded. */
    struct StudioPluginLoad
    {
        /** @brief The directory searched. Empty when there was nothing to search. */
        std::string directory;

        /** @brief How many plugins `discover` accepted. */
        std::size_t discovered = 0;

        /** @brief How many of those initialised and are running. */
        std::size_t active = 0;

        /** @brief Whether a directory was found and searched at all. */
        [[nodiscard]] bool searched() const { return !directory.empty(); }
    };

    /**
     * @brief Discovers and loads the plugins a Studio should start with.
     *
     * Every failure is reported through @p context's log, per plugin and in the manifest's own
     * words: a single "some plugins failed" is a message a user cannot act on.
     *
     * @param host The host to load into. Its previous contents are replaced by `discover`.
     * @param context The editor, which plugins are handed on activation and which carries the log.
     * @param pluginDirectory An explicit directory (`--plugins=DIR`), or empty for the default.
     * @param executablePath This executable's path, which is what "beside Studio" is relative to.
     *                       Empty means there is no default directory — see below.
     * @return What was searched, found and loaded.
     */
    StudioPluginLoad studioLoadPlugins(PluginHost& host, StudioContext& context,
                                       const std::string& pluginDirectory,
                                       const std::string& executablePath);

    /**
     * @brief Shuts every loaded plugin down, while the context is still alive.
     *
     * A named step rather than something left to a destructor, and the reason is the argument: a
     * plugin's `shutdown()` is handed the context, so the context has to outlive the unload. A host
     * destroyed after its context would call shutdown on a dangling reference.
     *
     * @param host The host to unload.
     * @param context The editor, handed to each plugin's shutdown.
     */
    void studioUnloadPlugins(PluginHost& host, StudioContext& context);
}
