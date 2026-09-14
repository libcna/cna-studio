// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Viewport/CnaPlayerHost.hpp
 * @brief Puts `cna-player` in a window, and draws the scene it loaded.
 *
 * The counterpart to `CnaEditorHost`, and built the same way and for the same reason: the
 * implementation is a `Microsoft::Xna::Framework::Game` subclass, a base class cannot hide behind a
 * pimpl, and exposing it would drag CNA into a public header. A free function keeps the subclass
 * inside the `.cpp`, so CNA stays a *private* link dependency of exactly one module (ANALYSIS.md
 * decision D-03) and the build graph goes on enforcing it.
 *
 * This is the half `PlayerHost` deliberately does not have. `PlayerHost` decides what a message
 * *means* and is CNA-free, so the whole protocol surface is unit-tested headless; this decides what
 * a frame *looks like*, and needs a device, a window and a backend. The seam between them is the
 * frame hook below: the caller keeps the socket, the host keeps the pixels.
 */

#include <cstdint>
#include <functional>
#include <string>

#include "CNA/Editor/Player/PlayerHost.hpp"
#include "CNA/Editor/RuntimeBridge/EditorProtocol.hpp"

namespace CNA::Editor
{
    /** @brief Window and loop settings for the hosted player. */
    struct CnaPlayerHostOptions
    {
        int windowWidth = 1280;
        int windowHeight = 720;
        std::string windowTitle = "CNA Player";

        /** @brief Exit after this many drawn frames. Zero runs until the window closes. */
        int frameLimit = 0;

        /**
         * @brief Write a PNG of the final frame here. Empty disables capture.
         *
         * The same mechanism the editor's `--screenshot` uses, and the same one the editor drives
         * over the bridge with a `screenshot` message. Having it on the command line too is what
         * lets CI assert that a player build draws a scene rather than opening a blank window --
         * the failure those two cases share is invisible from the outside.
         */
        std::string screenshotPath;
    };

    /** @brief What a hosted player session did. */
    struct CnaPlayerHostResult
    {
        /** @brief Process exit code: 0 on a clean exit. */
        int exitCode = 0;

        std::uint64_t frames = 0;

        /** @brief Sprites drawn on the last frame, and tiles with them. */
        std::size_t spritesDrawn = 0;
        std::size_t tilesDrawn = 0;

        /** @brief True when a `screenshotPath` capture reached disk. */
        bool screenshotWritten = false;

        /** @brief The CNA backend this build ran on. */
        std::string backend;

        /** @brief Set when the session could not start; @c exitCode is then non-zero. */
        std::string errorMessage;
    };

    /**
     * @brief Called once per frame before drawing. Return false to end the session.
     *
     * Where the caller pumps its socket, applies whatever arrived and sends back what the host
     * produced. Keeping it a callback rather than moving the channel in here is what stops this
     * module from needing to know that the bridge exists at all.
     */
    using PlayerFrameHook = std::function<bool()>;

    /** @brief Called with each message the host produced -- screenshot replies, today. */
    using PlayerMessageSink = std::function<void(const EditorMessage&)>;

    /**
     * @brief Runs @p host in a CNA window until it exits, drawing its scene each frame.
     *
     * @param options Window, frame limit and capture settings.
     * @param host The loaded project and scene, and the play/pause state that decides whether the
     *        simulation advances. Not owned; must outlive the call.
     * @param frameHook Pumped once per frame; returning false ends the session.
     * @param sink Receives replies the host generated, such as a screenshot's `ScreenshotReady`.
     *        May be empty when nobody is listening.
     */
    CnaPlayerHostResult runPlayerInWindow(const CnaPlayerHostOptions& options,
                                          PlayerHost& host,
                                          const PlayerFrameHook& frameHook,
                                          const PlayerMessageSink& sink = {});
}
