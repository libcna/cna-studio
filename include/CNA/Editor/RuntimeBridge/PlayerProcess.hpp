// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/RuntimeBridge/PlayerProcess.hpp
 * @brief The editor's side of play mode: find a player build, launch it, talk to it, clean up.
 *
 * Discovery is a first-class part of this, not a detail. Because CNA fixes its backend at compile
 * time (ANALYSIS.md finding F-01), "run the game on the Software backend" means "launch
 * `cna-player-software`", and whether that binary exists is a question with a real answer. The
 * editor offers the user only the backends it can actually find, rather than a menu of fourteen
 * entries of which two work.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/RuntimeBridge/EditorProtocol.hpp"
#include "CNA/Editor/RuntimeBridge/MessageChannel.hpp"

namespace CNA::Editor
{
    /** @brief One `cna-player-<backend>` binary found on disk. */
    struct PlayerBuild
    {
        /** @brief Lower-case backend name, e.g. "software". */
        std::string backend;

        /** @brief Absolute path to the executable. */
        std::string executablePath;
    };

    /**
     * @brief Finds the player builds installed alongside the editor.
     *
     * @param searchDirectory Directory to scan, normally the editor's own.
     * @return One entry per `cna-player-<backend>` (or `cna-player` itself, reported as "default"),
     *         ordered by backend name.
     */
    std::vector<PlayerBuild> discoverPlayerBuilds(const std::string& searchDirectory);

    /** @brief How a play session ended. */
    enum class PlayerExitReason
    {
        StillRunning,
        /** @brief The player exited cleanly. */
        Exited,
        /** @brief The player died or the connection dropped unexpectedly. */
        Crashed,
        /** @brief The editor asked it to stop. */
        StoppedByEditor,
        /** @brief The player could not be started at all. */
        FailedToStart
    };

    /** @brief Returns the display name of @p reason. */
    const char* toString(PlayerExitReason reason);

    /**
     * @brief A running (or finished) player process and its bridge.
     *
     * The editor listens *before* spawning, so the port is known and the listener is guaranteed
     * to be up by the time the player tries to connect -- no retry loop, no race.
     */
    class PlayerProcess
    {
    public:
        PlayerProcess();
        ~PlayerProcess();

        PlayerProcess(const PlayerProcess&) = delete;
        PlayerProcess& operator=(const PlayerProcess&) = delete;

        /**
         * @brief Binds a port, then launches @p build with the project and that port.
         *
         * @param build The player binary to run.
         * @param projectPath Absolute path to the `.cnaproject`.
         * @param scenePath Optional project-relative scene, overriding the startup scene.
         * @return False when the listener could not be bound or the process could not be spawned;
         *         getError() says which.
         */
        bool start(const PlayerBuild& build,
                   const std::string& projectPath,
                   const std::string& scenePath = {});

        /**
         * @brief Pumps the bridge and returns whatever the player sent.
         *
         * Call once per editor frame. Also completes the connection handshake and reaps the
         * process when it exits.
         *
         * The Hello is sent from here, on the first poll after the connection comes up, rather
         * than being left to the caller: the player treats a missing Hello as an incomplete
         * handshake, and a caller that forgot it would get a session that looks connected and is
         * quietly degraded.
         */
        std::vector<EditorMessage> poll();

        /** @brief Sends @p message to the player. Returns false when not connected. */
        bool send(const EditorMessage& message);

        /**
         * @brief Asks the player to exit, then waits briefly for it to do so.
         *
         * A player that ignores the request is terminated. Leaving an orphan game window behind
         * is worse than a hard kill on something that was already unresponsive.
         */
        void stop();

        [[nodiscard]] bool isRunning() const;
        [[nodiscard]] PlayerExitReason getExitReason() const { return exitReason_; }
        [[nodiscard]] const std::string& getError() const { return error_; }
        [[nodiscard]] std::uint16_t getPort() const { return channel_.getPort(); }
        [[nodiscard]] const MessageChannel& getChannel() const { return channel_; }

        /** @brief Returns the backend the player reported in its Ready message, if it has. */
        [[nodiscard]] const std::string& getReportedBackend() const { return reportedBackend_; }

        /**
         * @brief Returns true once the Hello has gone out.
         *
         * The player considers the handshake incomplete until it arrives, and nothing it sends
         * back says so -- without this, "the editor forgot to introduce itself" is a state no test
         * could observe from this side.
         */
        [[nodiscard]] bool isHelloSent() const { return helloSent_; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        MessageChannel channel_;
        std::string error_;
        std::string projectPath_;
        bool helloSent_ = false;
        std::string reportedBackend_;
        PlayerExitReason exitReason_ = PlayerExitReason::StillRunning;
    };
}
