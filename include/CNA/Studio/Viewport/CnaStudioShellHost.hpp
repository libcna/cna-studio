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
         * @brief Fail the run when the captured frame holds fewer distinct colours than this.
         *
         * Zero asks nothing. "The file appearing is the test" above was not quite true: the file
         * appears for a blank window too. This is what makes the capture an assertion about the
         * picture rather than about the process having survived.
         */
        std::size_t screenshotMinColors = 0;

        /**
         * @brief Print the host capability contract once the device exists.
         *
         * Evaluated on every run either way — "which of Studio's requirements does this build's
         * renderer meet" is the first question of every graphics bug report — but printed only
         * when asked, or when the answer is no.
         */
        bool reportCapabilities = false;

        /**
         * @brief Report the contract and exit without opening the editor.
         *
         * A query about the build rather than a session, and it belongs on this host as well as on
         * the legacy one: `--host-capabilities` was answered only by the Dear ImGui path, which
         * made it a flag that would break the day `STUDIO-07030` deleted that path.
         */
        bool checkCapabilitiesOnly = false;

        /**
         * @brief Whether the classic UI renderer may be used when the modern profile is unmet.
         *
         * `plan.md` STUDIO-02072. True by default, because the renderer this project's CI can build
         * — `SOFTWARE`, which needs no display and no GPU — cannot execute a shader, and a Studio
         * that refused to start there would have no automated coverage at all. `--ui-renderer=modern`
         * sets it false, which is what a release build and anybody checking the intended contract
         * should ask for: the fallback then refuses rather than silently degrading.
         */
        bool allowCompatibilityUiRenderer = true;

        /**
         * @brief Use the classic UI renderer even on a host that meets the modern profile.
         *
         * `--ui-renderer=compat`. For `STUDIO-04025`'s A/B comparison, and for the first question
         * anybody asks about something that draws wrong: does it happen on the other renderer.
         */
        bool forceCompatibilityUiRenderer = false;

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
         * @brief A `.cnascene` to open instead of the project's startup scene. Empty uses the
         *        project's own.
         *
         * `STUDIO-07053`. `--scene` was parsed and documented from the beginning and reached the
         * Dear ImGui prototype alone, so on the default UI it did nothing: this struct had no field
         * to carry it in. That is the shape of the defect the flag inventory was looking for -- not
         * a flag that behaves differently on two UIs, but one that stops existing on the way to the
         * second.
         */
        std::string scenePath;

        /**
         * @brief Where to look for plugins, or empty for `plugins/` beside the executable.
         *
         * `STUDIO-07052`. `PluginHost::discover` and `loadAll` were called by
         * `StudioApplication::loadPlugins` and by nothing else, so the native shell loaded no
         * plugins at all -- and `bindStudioPluginMenus` drew the commands they had registered
         * faithfully, which meant an empty menu that looks exactly like a machine with no plugins
         * installed.
         */
        std::string pluginDirectory;

        /**
         * @brief This executable's own path, so the player builds beside it can be found.
         *
         * "Run this on Vulkan" means "launch cna-player-vulkan", and whether that binary exists is
         * a question with a real answer — one the Diagnostics panel reports. Empty means "do not
         * look", which is what a test wants.
         */
        std::string executablePath;

        /**
         * @brief Select the entity with this name at start-up, so the Details panel has content.
         *
         * A screenshot of an inspector with nothing selected is a screenshot of its empty state,
         * which is worth capturing once and useless as a check that the panel works.
         */
        std::string selectEntity;

        /**
         * @brief A command to invoke once the shell is up, or empty.
         *
         * The windowed equivalent of `--shell-invoke`, and for the same reason: a state that has to
         * be *armed* — a tilemap tool, a dialog — is a state no still capture can reach by placing
         * a pointer, and a capture harness that could only photograph the default state could only
         * ever review the default state.
         */
        std::string invokeAction;

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
         * @brief Whether the viewport was compositing a rendered scene when the run ended.
         *
         * The smoke test's assertion. A shell that drew the placeholder grid and one that drew the
         * scene produce the same draw-call count and the same valid screenshot, so only this
         * separates "the viewport works" from "the viewport is a rectangle".
         */
        bool viewportComposited = false;

        /**
         * @brief What the status bar ended up saying on the left: the project and scene.
         *
         * Reported so a smoke test can assert that the shell opened what it was given. A count of
         * log rows says the panel drew; only this says the editor behind it has a project.
         */
        std::string statusLeft;

        /**
         * @brief How many plugins were discovered, and how many of them are running.
         *
         * `STUDIO-07052`. Reported because the interesting number is the *second* one: a plugin
         * that was found and would not start is a fixable problem, and one that was never looked
         * for is a Studio that has no plugin support at all — and from outside, an empty Plugins
         * menu looks identical either way.
         */
        std::size_t pluginsDiscovered = 0;

        /** @brief How many discovered plugins initialised and are running. */
        std::size_t pluginsActive = 0;

        /** @brief How many menu rows those plugins' commands produced. */
        std::size_t pluginMenuRows = 0;

        /** @brief How many World Outliner rows the ported panel put on screen. */
        std::size_t outlinerRowsDrawn = 0;

        /** @brief How many rows the scene and the current expansion produced. */
        std::size_t outlinerRowsTotal = 0;

        /** @brief How many Details rows the ported inspector put on screen. */
        std::size_t detailsRowsDrawn = 0;

        /** @brief How many Content Browser rows the ported panel put on screen. */
        std::size_t contentRowsDrawn = 0;

        /** @brief How many rows the asset database and the current expansion produced. */
        std::size_t contentRowsTotal = 0;

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

        /**
         * @brief True when the capture was refused for holding too few colours to be a picture.
         *
         * Separate from @ref screenshotWritten because the two want different explanations. "No
         * screenshot was written" is otherwise followed by a guess at why -- no frame limit, or a
         * renderer that cannot read back -- and printing that guess after the real reason has
         * already been given sends the reader looking for a second fault that is not there.
         */
        bool screenshotTooFlat = false;

        /**
         * @brief Where the UI benchmark's cost model first disagreed with the render backend.
         *
         * `STUDIO-04028`. Empty when they agreed, which is the only acceptable answer: the model
         * is what `--ui-benchmark` reports and what `STUDIO-04027` decides a renderer's fate on,
         * and it runs with no device, so this is the only place it meets the thing it models.
         * Checked on every frame of a run that has a frame limit -- a capture or a smoke test --
         * and on none of an interactive session's.
         */
        std::string costModelMismatch;

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
