// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/RuntimeBridge/MessageChannel.hpp
 * @brief A TCP socket carrying EditorProtocol messages between the editor and a player.
 *
 * Loopback TCP rather than a Unix domain socket or a named pipe, for one reason: it is the only
 * transport that works identically on every platform CNA targets, and the traffic is a handful of
 * small messages per second, so nothing is lost by it. The port is bound on 127.0.0.1 only -- the
 * bridge is a local debugging channel and must never be reachable from another machine.
 *
 * Every operation is non-blocking. The editor and the player each run a frame loop, and a bridge
 * that could block a frame would turn a wedged peer into a frozen editor.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/RuntimeBridge/EditorProtocol.hpp"

namespace CNA::Editor
{
    /** @brief The state a channel can be in. */
    enum class ChannelState
    {
        /** @brief Not connected and not trying to be. */
        Closed,
        /** @brief A listening socket is open, waiting for the peer to connect. */
        Listening,
        /** @brief A connect attempt is in flight. */
        Connecting,
        Connected,
        /** @brief The connection failed or was closed by the peer; @c getError says which. */
        Failed
    };

    /** @brief Returns the display name of @p state. */
    const char* toString(ChannelState state);

    /**
     * @brief One end of the editor/player bridge.
     *
     * The editor listens; the player connects. That direction is deliberate -- the editor knows
     * its own port before it spawns anything, so it can pass the port on the player's command line
     * and be certain the listener is already up.
     */
    class MessageChannel
    {
    public:
        MessageChannel();
        ~MessageChannel();

        MessageChannel(const MessageChannel&) = delete;
        MessageChannel& operator=(const MessageChannel&) = delete;
        MessageChannel(MessageChannel&&) noexcept;
        MessageChannel& operator=(MessageChannel&&) noexcept;

        /**
         * @brief Binds a listening socket on 127.0.0.1.
         *
         * @param port The port to bind, or 0 to let the OS choose. Use getPort() afterwards to
         *        learn which was chosen -- picking a fixed port risks colliding with whatever else
         *        the developer happens to be running.
         * @return False on failure; getError() says why.
         */
        bool listen(std::uint16_t port = 0);

        /**
         * @brief Accepts a pending connection, if one has arrived.
         * @return True when a peer is now connected.
         */
        bool tryAccept();

        /**
         * @brief Starts connecting to 127.0.0.1 on @p port.
         *
         * Returns as soon as the attempt is under way; poll() drives it to completion.
         */
        bool connect(std::uint16_t port);

        /** @brief Closes the socket and drops any buffered data. */
        void close();

        /**
         * @brief Advances the connection and returns every message that arrived.
         *
         * Call once per frame. Handles connect completion, accepting, reading, and flushing
         * whatever send() could not write immediately.
         */
        std::vector<EditorMessage> poll();

        /**
         * @brief Queues @p message for sending.
         *
         * Buffered rather than written straight through: a peer that has stopped reading must
         * slow the bridge down, not block the caller's frame.
         *
         * @return False when the channel is not connected.
         */
        bool send(const EditorMessage& message);

        [[nodiscard]] ChannelState getState() const;
        [[nodiscard]] bool isConnected() const { return getState() == ChannelState::Connected; }

        /** @brief Returns the bound port after a successful listen(). */
        [[nodiscard]] std::uint16_t getPort() const;

        /** @brief Returns the last error message, or an empty string. */
        [[nodiscard]] const std::string& getError() const;

        /** @brief Returns the number of unparseable lines discarded. */
        [[nodiscard]] std::uint64_t getDroppedCount() const;

        /** @brief Returns the number of bytes still waiting to be written. */
        [[nodiscard]] std::size_t getPendingSendBytes() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
