// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Viewport/CnaStudioHost.hpp
 * @brief Puts the editor in a window.
 *
 * This is plan.md **ED-111**, the last piece between the editor and something a user can look at.
 * Everything it needs already existed:
 *
 * - `ImGuiStudioUi` produces the geometry (`UiDrawData`),
 * - `CnaUiRenderer` draws that geometry through CNA's public graphics API,
 * - `CnaUiPlatform` fills the input (`UiInputState`) from CNA's public input API,
 * - `StudioApplication` decides *what* the panels contain.
 *
 * The host owns the window and the graphics device and wires the four together, one frame at a
 * time.
 *
 * **Why this header contains no CNA type.** The implementation is a
 * `Microsoft::Xna::Framework::Game` subclass — the editor is structurally a CNA application, so
 * reusing `Game`'s window, device, loop and input beats building a parallel path that the games
 * being edited never exercise. But a base class cannot be hidden behind a pimpl, so exposing the
 * class publicly would drag CNA into the public interface and let CNA headers reach every target
 * that links `cna-studio-viewport`. A free function returning an exit code keeps the `Game`
 * subclass entirely inside the `.cpp`, so CNA stays a *private* link dependency and the layering
 * rule stays enforced by the build graph rather than by review.
 */

#include <cstdint>
#include <memory>
#include <string>

#include "CNA/Studio/StudioApplication.hpp"

namespace CNA::Studio
{
    /** @brief Exit code used when the compiled renderer cannot host Studio (`STUDIO-02022`). */
    inline constexpr int kCnaStudioHostUnsupportedRendererExitCode = 6;

    /** @brief Window and loop settings for the hosted editor. */
    struct CnaStudioHostOptions
    {
        int windowWidth = 1600;
        int windowHeight = 900;
        std::string windowTitle = "CNA Studio";

        /** @brief Exit after this many frames. Zero runs until the user closes the window. */
        int frameLimit = 0;

        /** @brief Path for Dear ImGui's dock layout `.ini`. Empty disables persistence. */
        std::string layoutPath;

        /**
         * @brief Write a PNG of the final frame to this path. Empty disables capture.
         *
         * Reads the back buffer through `GraphicsDevice::GetBackBufferData` and writes it with
         * `Texture2D::SaveAsPng` -- both public CNA API. Two uses beyond debugging: a CI smoke test
         * can assert the editor produced a real image rather than a blank window, and the same
         * mechanism is what plan.md ED-510's backend comparison mode will capture through.
         */
        std::string screenshotPath;

        /**
         * @brief Fail the run when the captured frame holds fewer distinct colours than this.
         *
         * Zero asks nothing. Without it a graphical smoke test cannot tell a working editor from
         * one that opened a window and drew nothing into it: both write a file.
         */
        std::size_t screenshotMinColors = 0;

        /**
         * @brief Print the host capability report to the log on start-up.
         *
         * Off by default because it is a dozen lines nobody reads on a working build, and on by
         * `--host-capabilities` when somebody is asking exactly that question.
         */
        bool reportCapabilities = false;

        /**
         * @brief Run the host capability check and exit without entering the loop.
         *
         * For `--host-capabilities`: the device has to exist to be interrogated, so the check
         * cannot happen before a window does. Stopping immediately afterwards is the closest an
         * honest implementation gets to "no window is opened", and pretending otherwise would be
         * worse than saying so.
         */
        bool checkCapabilitiesOnly = false;
    };

    /**
     * @brief What a hosted editor session did.
     *
     * Returned rather than merely logged so that a scripted run (`--frames=N`) can *prove* the
     * editor drew something. A window that opens, runs its loop and closes having issued zero draw
     * calls looks identical to a working one from the outside, and that is precisely the
     * regression a smoke test needs to catch.
     */
    struct CnaStudioHostResult
    {
        /** @brief Process exit code: 0 on a clean exit. */
        int exitCode = 0;

        std::uint64_t frames = 0;
        std::size_t drawCalls = 0;
        std::size_t triangles = 0;
        std::size_t textures = 0;
        std::size_t textureUpdates = 0;

        /** @brief Draw commands skipped because their clip rectangle selected no pixels. */
        std::size_t clippedAway = 0;

        /**
         * @brief The drawing area the last frame used, in pixels.
         *
         * Reported because a zero-sized one is the difference between "the editor drew nothing
         * because it is broken" and "the editor drew nothing because it was never given a window
         * to draw into" -- two failures that are indistinguishable from the outside.
         */
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

        /** @brief The CNA backend the session actually ran on. */
        std::string backend;

        /** @brief Set when the session could not start; @c exitCode is then non-zero. */
        std::string errorMessage;

        /**
         * @brief The start-up host capability report (`STUDIO-02021`).
         *
         * Always produced, whether or not the renderer can host Studio, because "which of Studio's
         * requirements does this build's renderer actually meet" is the first question of every
         * graphics bug report and the last one anybody thinks to ask.
         */
        std::string capabilityReport;

        /** @brief True when the compiled renderer satisfies the Studio host contract. */
        bool rendererCanHostStudio = true;
    };

    /**
     * @brief Runs @p application in a CNA window until the user closes it.
     *
     * @param options Window and loop settings.
     * @param application The editor to host. Its UI must be an `ImGuiStudioUi`; anything else has
     *        no geometry for the renderer to draw, and the call fails rather than opening a window
     *        that would stay blank.
     * @return What the session did; see CnaStudioHostResult.
     */
    CnaStudioHostResult runStudioInWindow(const CnaStudioHostOptions& options,
                                          std::unique_ptr<StudioApplication> application);

    /**
     * @brief Returns the CNA graphics backend this binary was compiled against.
     *
     * Compile-time, because CNA resolves its backend at compile time — there is exactly one in
     * this binary and it cannot change (ANALYSIS.md finding F-01).
     */
    [[nodiscard]] std::string getHostBackendName();
}
