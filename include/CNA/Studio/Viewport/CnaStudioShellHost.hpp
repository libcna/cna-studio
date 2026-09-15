// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Viewport/CnaStudioShellHost.hpp
 * @brief Puts the **native** CNA Studio shell in a window, through a real CNA renderer.
 *
 * `plan.md` STUDIO-06015, STUDIO-04001.
 *
 * `CnaStudioHost` does this for the Dear ImGui UI. This is the same job for the shell the Studio UI
 * core builds, and it is deliberately a second host rather than a flag on the first: the two draw
 * completely different things, and the migration ends by deleting one of them. A single host with a
 * branch at every step would end with the branch, not with the deletion.
 *
 * What it proves is the sentence the whole UI workstream is built on — *the native UI renders
 * through CNA's public API* — as something that runs rather than something that is argued. The
 * shell produces `UiDrawData`; `CnaUiRenderer` has drawn `UiDrawData` through CNA's public graphics
 * API since the prototype; `CnaUiPlatform` fills `UiInputState` from CNA's public input API. This
 * host owns the window and wires the three together, and contains no UI code of its own.
 *
 * **The header names no CNA type**, for the same reason `CnaStudioHost.hpp` does not: a `Game`
 * subclass cannot hide behind a pimpl, so exposing it would drag CNA's headers into every target
 * that links `cna-studio-viewport` and break the layering rule `STUDIO-02033` enforces. A free
 * function returning a result keeps the subclass inside the `.cpp`.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief Window and loop settings for the native shell. */
    struct CnaStudioShellHostOptions
    {
        int windowWidth = 1600;
        int windowHeight = 900;
        std::string windowTitle = "CNA Studio";

        /** @brief Exit after this many frames. Zero runs until the user closes the window. */
        int frameLimit = 0;

        /** @brief DPI scale for the UI. 1.0 is 100%. */
        float uiScale = 1.0f;

        /** @brief `"dark"` or `"light"`. */
        std::string theme = "dark";

        /**
         * @brief Write a PNG of the final frame here. Empty disables capture.
         *
         * The assertion a graphical smoke test actually needs: a window that opens blank and one
         * that works are identical from the outside, and the file appearing *is* the test.
         */
        std::string screenshotPath;

        /**
         * @brief Where to remember the workspace arrangement between runs. Empty disables it.
         *
         * A path rather than a flag, so the tests can point it at a temporary file and a developer
         * can point two builds at different ones. `StudioWorkspaceStore::defaultPath()` is what the
         * application passes.
         */
        std::string workspacePath;

        /**
         * @brief A `.cnaproject` to open at start-up. Empty starts with no project.
         *
         * The native shell without one is a shell with nothing in it, which is fine for a
         * screenshot and useless as an editor. With one it has a real `StudioContext` behind it --
         * the same object the ImGui editor uses -- so the panels ported into it are reading the
         * editor's own state rather than a demonstration of it.
         */
        std::string projectPath;

        /**
         * @brief Bring this panel to the front of its tab group before drawing.
         *
         * A panel sharing a tab strip with five others cannot be photographed at all otherwise,
         * because the tab in front is whichever docked last. Empty leaves the layout alone.
         */
        std::string focusPanel;
    };

    /** @brief What a native-shell session did. */
    struct CnaStudioShellHostResult
    {
        /** @brief Process exit code: 0 on a clean exit. */
        int exitCode = 0;

        std::uint64_t frames = 0;

        /**
         * @brief How many Output Log rows the ported panel put on screen in the last drawn frame.
         *
         * Reported because it is otherwise unobservable. A ported panel and the empty surface it
         * replaced both draw *some* geometry, so a triangle count cannot tell them apart -- and a
         * content function that silently stopped running would look, from outside, exactly like a
         * panel that has not been ported yet.
         */
        std::size_t logRowsDrawn = 0;

        /** @brief How many log entries the panel's filter was showing. */
        std::size_t logRowsMatching = 0;

        /**
         * @brief What the status bar ended up saying on the left: the project and scene.
         *
         * Reported so a smoke test can assert that the shell opened what it was given. A count of
         * log rows says the panel drew; only this says the editor behind it has a project.
         */
        std::string statusLeft;

        /** @brief Whether a stored workspace arrangement was found and applied at start-up. */
        bool layoutRestored = false;

        /** @brief Whether the arrangement was written back on exit. */
        bool layoutStored = false;

        /** @brief Why a stored layout was not used or not written, when either happened. */
        std::string layoutProblem;
        std::size_t drawCalls = 0;
        std::size_t triangles = 0;

        /** @brief The drawing area the last frame used, in pixels. */
        float displayWidth = 0.0f;
        float displayHeight = 0.0f;

        /** @brief True when a requested screenshot reached disk. */
        bool screenshotWritten = false;

        /** @brief The CNA renderer the session ran on. */
        std::string renderer;

        /** @brief The start-up host capability report (`STUDIO-02021`). */
        std::string capabilityReport;

        /** @brief Whether the compiled renderer satisfies the Studio host contract. */
        bool rendererCanHostStudio = true;

        /** @brief Actions the user invoked, in order. For a scripted run to assert on. */
        std::vector<std::string> invokedActions;

        /** @brief Set when the session could not start; @c exitCode is then non-zero. */
        std::string errorMessage;
    };

    /**
     * @brief Runs the native Studio shell in a CNA window until the user closes it.
     * @param options Window and loop settings.
     * @return What the session did.
     */
    CnaStudioShellHostResult runStudioShellInWindow(const CnaStudioShellHostOptions& options);
} // namespace CNA::Studio
