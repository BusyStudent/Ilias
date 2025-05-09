/**
 * @file unix.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Unix domain socket support
 * @version 0.1
 * @date 2024-12-04
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/net/detail/sockbase.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>

#if !defined(ILIAS_NO_AF_UNIX) // In minGW or older version of Windows, AF_UNIX is not supported

ILIAS_NS_BEGIN

/**
 * @brief Unix domain socket client
 * 
 */
class UnixClient final : public StreamMethod<UnixClient> {
public:
    UnixClient() = default;
    UnixClient(IoContext &ctxt, int type) : mBase(ctxt, Socket(AF_UNIX, type, 0)) { }
    UnixClient(IoContext &ctxt, Socket &&sock) : mBase(ctxt, std::move(sock)) { }

    auto close() { return mBase.close(); }
    auto cancel() { return mBase.cancel(); }

    /**
     * @brief Connect to a unix endpoint
     * 
     * @param endpoint The endpoint to connect to
     * @return IoTask<void> 
     */
    auto connect(const UnixEndpoint &endpoint) const -> IoTask<void> { 
        return mBase.connect(endpoint); 
    }

    /**
     * @brief Bind to a unix endpoint
     * 
     * @param endpoint 
     * @return Result<void> 
     */
    auto bind(const UnixEndpoint &endpoint) const -> Result<void> {
        return mBase.bind(endpoint);
    }

    /**
     * @brief Write data to the socket
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(std::span<const std::byte> buffer) const -> IoTask<size_t> {
        return mBase.send(buffer);
    }

    /**
     * @brief Read data from the socket
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(std::span<std::byte> buffer) const -> IoTask<size_t> {
        return mBase.recv(buffer);
    }

    /**
     * @brief Send a message to the socket.
     * 
     * @param msg The message to send.
     * @param flags The flags to use. (like MSG_DONTWAIT)
     * @return IoTask<size_t> 
     */
    auto sendmsg(const MsgHdr &msg, int flags) -> IoTask<size_t> {
        return mBase.sendmsg(msg, flags);
    }

    /**
     * @brief Receive a message from the socket.
     * 
     * @param msg The message to receive.
     * @param flags The flags to use. (like MSG_DONTWAIT)
     * @return IoTask<size_t> 
     */
    auto recvmsg(MsgHdr &msg, int flags) -> IoTask<size_t> {
        return mBase.recvmsg(msg, flags);
    }

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return Result<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) -> Result<void> {
        return socket().setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return Result<T> 
     */
    template <GetSockOption T>
    auto getOption() -> Result<T> {
        return socket().getOption<T>();
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return Result<UnixEndpoint> 
     */
    auto localEndpoint() const -> Result<UnixEndpoint> { 
        return mBase.localEndpoint<UnixEndpoint>(); 
    }

    /**
     * @brief Get the remote endpoint associated with the socket.
     * 
     * @return Result<UnixEndpoint> 
     */
    auto remoteEndpoint() const -> Result<UnixEndpoint> {
        return mBase.remoteEndpoint<UnixEndpoint>();
    }

    /**
     * @brief Poll the socket for events.
     * 
     * @param events 
     * @return IoTask<uint32_t> 
     */
    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mBase.poll(events);
    }

    /**
     * @brief Get the underlying io context.
     * 
     * @return IoContext* 
     */
    auto context() const -> IoContext * {
        return mBase.context();
    }

    /**
     * @brief Get the underlying socket.
     * 
     * @return SocketView 
     */
    auto socket() const -> SocketView {
        return mBase.socket();
    }

    auto operator <=>(const UnixClient &) const = default;

    /**
     * @brief Check if the socket is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mBase); }

    /**
     * @brief Check if the unix domain socket is supported
     * 
     * @return auto 
     */
    static auto isSupported() -> bool {

#if defined(_WIN32)
        static bool supported = []() {
            auto sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock == INVALID_SOCKET) {
                return false;
            }
            ::closesocket(sock);
            return true;
        }();
        return supported;
#else
        return true; //< In Unix or Linux, unix domain socket is always supported
#endif // _WIN32

    }

    /**
     * @brief Create a new unix client by using current coroutine's io context.
     * 
     * @param type The socket type
     * @return Result<UnixClient>
     */
    static auto make(int type) {
        return detail::SocketBase::make<UnixClient>(AF_UNIX, type, 0);
    }
private:
    UnixClient(detail::SocketBase &&base) : mBase(std::move(base)) { }

    detail::SocketBase mBase;
friend class detail::SocketBase;
};

/**
 * @brief The unix domain socket listener
 * 
 */
class UnixListener {
public:
    UnixListener() = default;
    UnixListener(IoContext &ctxt, int type) : mBase(ctxt, Socket(AF_UNIX, type, 0)) { }
    UnixListener(IoContext &ctxt, Socket &&sock) : mBase(ctxt, std::move(sock)) { }

    /**
     * @brief Close the socket
     * 
     */
    auto close() {
        return mBase.close();
    }

    /**
     * @brief Bind the socket and listen.
     * 
     * @param endpoint The unix endpoint to bind to
     * @param backlog The maximum length of the queue of pending connections. (default: 0, let os decide)
     * @return Result<void> 
     */
    auto bind(const UnixEndpoint &endpoint, int backlog) const -> Result<void> {
        auto ret = mBase.bind(endpoint);
        if (!ret) {
            return ret;
        }
        return mBase.listen(backlog);
    }

    /**
     * @brief Accept a connection.
     * 
     * @return IoTask<std::pair<UnixClient, UnixEndpoint> > 
     */
    auto accept() const -> IoTask<std::pair<UnixClient, UnixEndpoint> > {
        UnixEndpoint endpoint;
        auto ctxt = mBase.context();
        auto ret = co_await mBase.accept(endpoint);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        co_return std::pair { UnixClient(*ctxt, Socket(*ret)), endpoint };
    }

    /**
     * @brief Accept a connection.
     * 
     * @param endpoint The unix endpoint to recieve the connection's endpoint (optional, can be nullptr)
     * @return IoTask<UnixClient> 
     */
    auto accept(UnixEndpoint *endpoint) const -> IoTask<UnixClient> {
        auto ctxt = mBase.context();
        auto ret = co_await mBase.accept(endpoint);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        co_return UnixClient(*ctxt, Socket(*ret));
    }

    /**
     * @brief Accept a connection.
     * 
     * @param endpoint The unix endpoint to recieve the connection's endpoint
     * @return IoTask<UnixClient> 
     */
    auto accept(UnixEndpoint &endpoint) const -> IoTask<UnixClient> {
        return accept(&endpoint);
    }

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return Result<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) -> Result<void> {
        return socket().setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return Result<T> 
     */
    template <GetSockOption T>
    auto getOption() -> Result<T> {
        return socket().getOption<T>();
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return Result<UnixEndpoint> 
     */
    auto localEndpoint() const -> Result<UnixEndpoint> { 
        return mBase.localEndpoint<UnixEndpoint>(); 
    }

    /**
     * @brief Poll the socket for events.
     * 
     * @param events 
     * @return IoTask<uint32_t> 
     */
    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mBase.poll(events);
    }

    /**
     * @brief Get the underlying io context.
     * 
     * @return IoContext* 
     */
    auto context() const -> IoContext * {
        return mBase.context();
    }

    /**
     * @brief Get the underlying socket.
     * 
     * @return SocketView 
     */
    auto socket() const -> SocketView {
        return mBase.socket();
    }

    auto operator <=>(const UnixListener &) const = default;

    /**
     * @brief Check if the socket is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const { return bool(mBase); }

    /**
     * @brief Check if the unix domain socket is supported
     * 
     * @return true 
     * @return false 
     */
    static auto isSupported() -> bool {
        return UnixClient::isSupported();
    }

    /**
     * @brief Create a new udp listener by using current coroutine's io context.
     * 
     * @param type The socket type
     * @return Result<UnixListener>
     */
    static auto make(int type) {
        return detail::SocketBase::make<UnixListener>(AF_UNIX, type, 0);
    }
private:
    UnixListener(detail::SocketBase &&base) : mBase(std::move(base)) { }

    detail::SocketBase mBase;
friend class detail::SocketBase;
};

ILIAS_NS_END

#endif // defined(ILIAS_NO_AF_UNIX)