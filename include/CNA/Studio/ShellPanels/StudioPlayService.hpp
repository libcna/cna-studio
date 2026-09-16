// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/ShellPanels/StudioPlayService.hpp
 * @brief Everything about the game Studio launches: the process, its state and its messages.
 *
 * `plan.md` STUDIO-02050, STUDIO-02054.
 *
 * ### Why this is a service and most things are not
 *
 * `StudioShellPanels` had grown to hold play control, build control, packaging, preferences,
 * recovery, the renderer comparison, plugin polling, notifications, viewport state and the binding
 * of every panel. That is the shape an application object takes on just before it stops being
 * reviewable, and the way it happens is that each addition is individually reasonable.
 *
 * The decomposition is deliberately not "one class per noun". A service earns its own type by
 * owning **state with a lifetime**, **operations with rules**, and a **failure mode of its own**.
 * Play has all three: a child process that outlives any frame, six operations with real
 * preconditions, and half a dozen ways to go wrong that are nothing like a build failing or a
 * preference not saving. Extracting it makes those rules testable without a shell, a device or a
 * panel — which they were not, because reaching them meant constructing the object that binds
 * every panel in Studio.
 *
 * ### Dependencies are arguments, never a locator
 *
 * The context it saves through, the log it reports to and the sink it raises notifications on are
 * constructor arguments. There is no registry to ask, which means a test constructs one with two
 * doubles and a lambda, and the set of things play control can reach is the set visible in its
 * constructor.
 *
 * ### What stayed behind, and why
 *
 * The renderer comparison launches players too and is **not** here. It launches several, in
 * sequence, over half an hour, to answer a question about renderers rather than to play the game —
 * it has its own state machine, its own report and its own failure modes, and folding it in would
 * have made this type "things that start processes" rather than "the game the user pressed Play
 * on". It stops the running player through this service, which is the only overlap there is.
 */

#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioNotifications.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace CNA::Studio
{
    class StudioContext;

    /**
     * @brief Whether a game launched from the editor is running, paused, or not running.
     *
     * Declared here rather than reusing the prototype's `PlayMode`, which lives in the Dear ImGui
     * panel headers that `STUDIO-07030` deletes. The native module must not depend on them, and the
     * alternative -- moving the prototype's enum somewhere shared -- would be rearranging code that
     * is on its way out.
     */
    enum class StudioPlayState
    {
        Stopped,
        Playing,
        Paused
    };

    /** @brief Launches, drives and reports on the game Studio plays. */
    class StudioPlayService
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param context The open project and scene. Play saves through it before launching.
         * @param log Where every start, stop and player message is reported.
         * @param notify Raised for the failures a user must see even with the log docked away.
         *        A crash is the only one today; it is a sink rather than a direct call so that a
         *        test can read what was raised without a shell to raise it on.
         */
        StudioPlayService(StudioContext& context, StudioLog& log,
                          std::function<void(StudioNotification)> notify);

        StudioPlayService(const StudioPlayService&) = delete;
        StudioPlayService& operator=(const StudioPlayService&) = delete;

        /**
         * @brief Replaces the list of player binaries this Studio found beside itself.
         *
         * An override naming a build that is no longer installed is dropped, so what a panel shows
         * is what Play will do.
         *
         * @param builds The installed players.
         */
        void setBuilds(std::vector<PlayerBuild> builds);

        /** @brief The installed players. */
        [[nodiscard]] const std::vector<PlayerBuild>& builds() const { return builds_; }

        /**
         * @brief Chooses the renderer Play uses for this session.
         * @param backend A renderer naming an installed build, or empty to clear the override.
         * @return False when no installed build has that renderer; nothing changes then.
         */
        bool selectBuild(const std::string& backend);

        /**
         * @brief The renderer chosen for this session, or empty.
         *
         * Not persisted, deliberately. The project's renderer is what the game ships on; this is a
         * thing somebody did to one session to look at something, and a Studio that remembered it
         * across restarts would quietly ship a different answer from the one in the project.
         */
        [[nodiscard]] const std::string& buildOverride() const { return override_; }

        /** @brief Which build a Play now would launch, or null when none is installed. */
        [[nodiscard]] const PlayerBuild* chooseBuild() const;

        /** @brief Whether the player process is alive. */
        [[nodiscard]] bool isRunning() const { return player_.isRunning(); }

        /** @brief Whether the game is playing, paused, or not running. */
        [[nodiscard]] StudioPlayState state() const { return state_; }

        /** @brief Saves the scene if needed and launches the player. Reports every refusal. */
        void start();

        /** @brief Stops the player, if one is running. */
        void stop();

        /**
         * @brief Stops and starts.
         *
         * Rather than a message asking the game to reload itself: the player reads the scene from
         * disk when it starts, so a restart is how the user sees the edits made since — which is
         * what they mean by it.
         */
        void restart();

        /**
         * @brief Pauses or resumes the running game.
         * @param paused The state to move to.
         * @return Whether the request reached the player and the state changed.
         */
        bool setPaused(bool paused);

        /**
         * @brief Advances a paused game by one frame.
         * @return Whether the request was sent. False unless the game is paused.
         */
        bool stepFrame();

        /**
         * @brief Sends the editor's pointer and keys to the running game.
         *
         * Only on a change, and a wheel notch always counts as one: sixty identical snapshots a
         * second would be sixty round trips that told the player nothing, and the player answers
         * every one, so the waste would be doubled.
         *
         * @param snapshot What the viewport saw.
         * @return Whether anything was sent.
         */
        bool forwardInput(const PlayerInputSnapshot& snapshot);

        /** @brief The last snapshot actually sent. */
        [[nodiscard]] const PlayerInputSnapshot& lastForwardedInput() const
        {
            return lastForwarded_;
        }

        /**
         * @brief Tells a running game that one of its assets changed on disk.
         *
         * `STUDIO-07051`. Only while a game is actually running: sending to a stopped player is
         * not merely useless — there is no process to send to, and the failure would be reported
         * as a broken bridge, which is a bug report about the wrong thing.
         *
         * @param assetId The asset to reload.
         * @return Whether a message was sent.
         */
        bool reloadAsset(const Uuid& assetId);

        /**
         * @brief Drains the player's messages and notices an ending exactly once.
         * @return How many messages were read, for the panel counts.
         */
        std::size_t poll();

        /** @brief The process itself, for the comparison run that has to stop it. */
        [[nodiscard]] PlayerProcess& process() { return player_; }

    private:
        StudioContext& context_;
        StudioLog& log_;
        std::function<void(StudioNotification)> notify_;

        PlayerProcess player_;
        std::vector<PlayerBuild> builds_;
        std::string override_;
        StudioPlayState state_ = StudioPlayState::Stopped;

        /** @brief Whether the player was running when it was last polled. See poll(). */
        bool wasRunning_ = false;

        /** @brief The last snapshot sent to the player, so identical ones are not re-sent. */
        PlayerInputSnapshot lastForwarded_;
    };
} // namespace CNA::Studio
