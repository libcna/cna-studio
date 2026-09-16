// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/StudioStartupDocument.hpp
 * @brief What a Studio opens when it starts, from a project path and a scene override.
 *
 * `plan.md` STUDIO-07053.
 *
 * ### Why this is a function rather than four call sites
 *
 * Four things start a Studio context from a command line — the native shell on a real device, the
 * headless shell preview, the UI benchmark and the Dear ImGui prototype — and every one of them had
 * written out the same three-line decision by hand. Three of them had written it out *wrongly*:
 * `--scene` was read by the prototype alone, so the flag that overrides the startup scene did
 * nothing on the UI that is the default. That is the failure a duplicated decision produces: it is
 * not that the copies disagree, it is that nobody notices when one of them was never written.
 *
 * ### It takes paths, not `StudioOptions`
 *
 * The context module knows about projects and scenes and has no business knowing there is a command
 * line. Taking two strings keeps it that way, and keeps this callable from a test that has no
 * options to build.
 */

#pragma once

#include <string>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief What @ref openStudioStartupDocument did, so each caller can report it its own way. */
    struct StudioStartupDocument
    {
        /**
         * @brief Empty when everything asked for opened; otherwise what to tell the user.
         *
         * A sentence rather than a code. Every caller puts it somewhere a person reads — standard
         * error, the Output Log, the status bar — and a code would have to be translated back into
         * this sentence at each of them.
         */
        std::string error;

        /** @brief Whether a project was opened from @p projectPath. */
        bool projectOpened = false;

        /** @brief Whether a scene was opened from @p scenePath, overriding the project's own. */
        bool sceneOpened = false;

        /** @brief Whether everything that was asked for happened. */
        [[nodiscard]] bool succeeded() const { return error.empty(); }
    };

    /**
     * @brief Opens the project and the scene a Studio was started with.
     *
     * With no project path, the context is given a new scene rather than left empty — a scene with
     * no camera renders nothing, which reads as a broken editor rather than as an empty one.
     *
     * A scene override is applied *after* the project, because the project opens its own startup
     * scene and the override's whole purpose is to replace it. A project that would not open stops
     * the sequence: a scene path is resolved against a project, and opening one into whatever
     * document happened to be there would be worse than not opening it.
     *
     * @param context The context to fill. Whatever it held is replaced.
     * @param projectPath A `.cnaproject`, or empty for a new untitled scene.
     * @param scenePath A `.cnascene` to open instead of the project's startup scene, or empty.
     * @return What happened, with a sentence when something did not.
     */
    StudioStartupDocument openStudioStartupDocument(StudioContext& context,
                                                    const std::string& projectPath,
                                                    const std::string& scenePath);
}
