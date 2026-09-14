// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/RuntimeBridge/MessageChannel.hpp"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace CNA::Editor
{
    namespace
    {
#if defined(_WIN32)
        using SocketHandle = SOCKET;
        constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

        /**
         * @brief Initialises Winsock once per process.
         *
         * Reference-free by design: the editor and the player both live for the whole process,
         * so tearing Winsock down early would be a bug rather than a saving.
         */
        struct WinsockGuard
        {
            WinsockGuard()
            {
                WSADATA data{};
                WSAStartup(MAKEWORD(2, 2), &data);
            }
            ~WinsockGuard() { WSACleanup(); }
        };

        void ensureWinsock()
        {
            static WinsockGuard guard;
            (void)guard;
        }

        void closeSocket(SocketHandle handle) { closesocket(handle); }

        bool setNonBlocking(SocketHandle handle)
        {
            u_long mode = 1;
            return ioctlsocket(handle, FIONBIO, &mode) == 0;
        }

        bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
        bool inProgress() { return WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEALREADY; }
        std::string lastErrorText() { return "socket error " + std::to_string(WSAGetLastError()); }

        int sendRaw(SocketHandle handle, const char* data, std::size_t size)
        {
            return ::send(handle, data, static_cast<int>(size), 0);
        }
        int receiveRaw(SocketHandle handle, char* data, std::size_t size)
        {
            return ::recv(handle, data, static_cast<int>(size), 0);
        }
#else
        using SocketHandle = int;
        constexpr SocketHandle kInvalidSocket = -1;

        void ensureWinsock() {}
        void closeSocket(SocketHandle handle) { ::close(handle); }

        bool setNonBlocking(SocketHandle handle)
        {
            const int flags = ::fcntl(handle, F_GETFL, 0);
            if (flags < 0) { return false; }
            return ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
        }

        bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
        bool inProgress() { return errno == EINPROGRESS || errno == EALREADY || wouldBlock(); }
        std::string lastErrorText() { return std::strerror(errno); }

        int sendRaw(SocketHandle handle, const char* data, std::size_t size)
        {
            // MSG_NOSIGNAL: without it, writing to a socket whose peer has exited raises SIGPIPE
            // and kills the editor. A player crashing must never take the editor with it.
            return static_cast<int>(::send(handle, data, size,
#    if defined(MSG_NOSIGNAL)
                                           MSG_NOSIGNAL
#    else
                                           0
#    endif
                                           ));
        }
        int receiveRaw(SocketHandle handle, char* data, std::size_t size)
        {
            return static_cast<int>(::recv(handle, data, size, 0));
        }
#endif

        /** @brief Disables Nagle's algorithm, so a small message is not delayed behind a timer. */
        void disableNagle(SocketHandle handle)
        {
            int flag = 1;
            ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY,
                         reinterpret_cast<const char*>(&flag), sizeof(flag));
        }
    }

    const char* toString(ChannelState state)
    {
        switch (state)
        {
            case ChannelState::Closed: return "closed";
            case ChannelState::Listening: return "listening";
            case ChannelState::Connecting: return "connecting";
            case ChannelState::Connected: return "connected";
            case ChannelState::Failed: return "failed";
        }
        return "closed";
    }

    struct MessageChannel::Impl
    {
        SocketHandle listener = kInvalidSocket;
        SocketHandle peer = kInvalidSocket;
        ChannelState state = ChannelState::Closed;
        std::uint16_t port = 0;
        std::string error;
        MessageStreamDecoder decoder;
        std::string sendBuffer;

        void fail(std::string message)
        {
            error = std::move(message);
            state = ChannelState::Failed;
        }

        void closeAll()
        {
            if (peer != kInvalidSocket) { closeSocket(peer); peer = kInvalidSocket; }
            if (listener != kInvalidSocket) { closeSocket(listener); listener = kInvalidSocket; }
            decoder.reset();
            sendBuffer.clear();
        }

        /** @brief Writes as much of sendBuffer as the socket will take. */
        void flushSendBuffer()
        {
            while (!sendBuffer.empty() && peer != kInvalidSocket)
            {
                const int written = sendRaw(peer, sendBuffer.data(), sendBuffer.size());
                if (written > 0)
                {
                    sendBuffer.erase(0, static_cast<std::size_t>(written));
                    continue;
                }
                if (written < 0 && wouldBlock()) { return; }

                // Anything else means the peer is gone. This is the normal way a play session
                // ends -- the user closed the game window -- so it is not an error.
                fail("connection closed while sending");
                closeAll();
                return;
            }
        }
    };

    MessageChannel::MessageChannel() : impl_(std::make_unique<Impl>()) { ensureWinsock(); }

    MessageChannel::~MessageChannel() { if (impl_) { impl_->closeAll(); } }

    MessageChannel::MessageChannel(MessageChannel&&) noexcept = default;
    MessageChannel& MessageChannel::operator=(MessageChannel&&) noexcept = default;

    ChannelState MessageChannel::getState() const { return impl_->state; }
    std::uint16_t MessageChannel::getPort() const { return impl_->port; }
    const std::string& MessageChannel::getError() const { return impl_->error; }
    std::uint64_t MessageChannel::getDroppedCount() const { return impl_->decoder.getDroppedCount(); }
    std::size_t MessageChannel::getPendingSendBytes() const { return impl_->sendBuffer.size(); }

    bool MessageChannel::listen(std::uint16_t port)
    {
        close();

        impl_->listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (impl_->listener == kInvalidSocket)
        {
            impl_->fail("cannot create listening socket: " + lastErrorText());
            return false;
        }

        int reuse = 1;
        ::setsockopt(impl_->listener, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        // Loopback only. The bridge is a local debugging channel and must never be reachable
        // from another machine.
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);

        if (::bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            impl_->fail("cannot bind port " + std::to_string(port) + ": " + lastErrorText());
            impl_->closeAll();
            return false;
        }

        if (::listen(impl_->listener, 1) != 0)
        {
            impl_->fail("cannot listen: " + lastErrorText());
            impl_->closeAll();
            return false;
        }

        // Read the port back rather than trusting the argument: with port 0 the OS chose it, and
        // that value is what has to reach the player's command line.
        sockaddr_in bound{};
#if defined(_WIN32)
        int boundLength = sizeof(bound);
#else
        socklen_t boundLength = sizeof(bound);
#endif
        if (::getsockname(impl_->listener, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0)
        {
            impl_->port = ntohs(bound.sin_port);
        }

        setNonBlocking(impl_->listener);
        impl_->state = ChannelState::Listening;
        impl_->error.clear();
        return true;
    }

    bool MessageChannel::tryAccept()
    {
        if (impl_->state != ChannelState::Listening) { return false; }

        const SocketHandle accepted = ::accept(impl_->listener, nullptr, nullptr);
        if (accepted == kInvalidSocket) { return false; }

        impl_->peer = accepted;
        setNonBlocking(impl_->peer);
        disableNagle(impl_->peer);

        // The listener is closed once a peer arrives: the bridge is strictly one editor to one
        // player, and leaving it open would let a second process attach unnoticed.
        closeSocket(impl_->listener);
        impl_->listener = kInvalidSocket;

        impl_->state = ChannelState::Connected;
        return true;
    }

    bool MessageChannel::connect(std::uint16_t port)
    {
        close();

        impl_->peer = ::socket(AF_INET, SOCK_STREAM, 0);
        if (impl_->peer == kInvalidSocket)
        {
            impl_->fail("cannot create socket: " + lastErrorText());
            return false;
        }

        setNonBlocking(impl_->peer);
        disableNagle(impl_->peer);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);

        impl_->port = port;
        if (::connect(impl_->peer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
        {
            impl_->state = ChannelState::Connected;
            return true;
        }

        if (inProgress())
        {
            impl_->state = ChannelState::Connecting;
            return true;
        }

        impl_->fail("cannot connect to port " + std::to_string(port) + ": " + lastErrorText());
        impl_->closeAll();
        return false;
    }

    void MessageChannel::close()
    {
        impl_->closeAll();
        impl_->state = ChannelState::Closed;
        impl_->error.clear();
    }

    std::vector<EditorMessage> MessageChannel::poll()
    {
        std::vector<EditorMessage> messages;

        if (impl_->state == ChannelState::Listening) { tryAccept(); }

        if (impl_->state == ChannelState::Connecting)
        {
            // A non-blocking connect reports completion through writability plus SO_ERROR; a
            // zero-length write is the portable way to ask without consuming anything.
            int socketError = 0;
#if defined(_WIN32)
            int length = sizeof(socketError);
#else
            socklen_t length = sizeof(socketError);
#endif
            if (::getsockopt(impl_->peer, SOL_SOCKET, SO_ERROR,
                             reinterpret_cast<char*>(&socketError), &length) == 0)
            {
                if (socketError == 0)
                {
                    sockaddr_in remote{};
#if defined(_WIN32)
                    int remoteLength = sizeof(remote);
#else
                    socklen_t remoteLength = sizeof(remote);
#endif
                    if (::getpeername(impl_->peer, reinterpret_cast<sockaddr*>(&remote), &remoteLength) == 0)
                    {
                        impl_->state = ChannelState::Connected;
                    }
                }
                else
                {
                    impl_->fail("connect failed: " + std::to_string(socketError));
                    impl_->closeAll();
                    return messages;
                }
            }
        }

        if (impl_->state != ChannelState::Connected) { return messages; }

        impl_->flushSendBuffer();
        if (impl_->state != ChannelState::Connected) { return messages; }

        char buffer[8192];
        while (true)
        {
            const int received = receiveRaw(impl_->peer, buffer, sizeof(buffer));
            if (received > 0)
            {
                const std::vector<EditorMessage> batch =
                    impl_->decoder.feed(std::string_view{buffer, static_cast<std::size_t>(received)});
                messages.insert(messages.end(), batch.begin(), batch.end());
                continue;
            }

            if (received == 0)
            {
                // Orderly shutdown: the player exited. Normal end of a play session.
                impl_->fail("peer closed the connection");
                impl_->closeAll();
                break;
            }

            if (wouldBlock()) { break; }

            impl_->fail("receive failed: " + lastErrorText());
            impl_->closeAll();
            break;
        }

        return messages;
    }

    bool MessageChannel::send(const EditorMessage& message)
    {
        if (impl_->state != ChannelState::Connected) { return false; }
        impl_->sendBuffer += message.encode();
        impl_->flushSendBuffer();
        return true;
    }
}
