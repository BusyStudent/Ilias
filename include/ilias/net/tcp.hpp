/**
 * @file tcp.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides TCP socket functionality.
 * @version 0.1
 * @date 2024-08-13
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/generator.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The tcp stream class.
 * 
 */
class TcpStream final : public StreamMethod<TcpStream> {
public:
    TcpStream() = default;
    TcpStream(IoHandle<Socket> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() const { return mHandle.cancel(); }

    // Stream Concept
    /**
     * @brief Write data to the socket.
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) const -> IoTask<size_t> {
        return mHandle.sendto(buffer, 0, nullptr);
    }

    /**
     * @brief Flush the socket. (no-op)
     * 
     * @return IoTask<void> 
     */
    auto flush() const -> IoTask<void> {
        co_return {};
    }

    /**
     * @brief Shutdown the socket.
     * 
     * @param how 
     * @return IoTask<void> 
     */
    auto shutdown(int how = Shutdown::Both) const -> IoTask<void> {
        co_return mHandle.fd().shutdown(how);
    }

    /**
     * @brief Read data from the socket.
     * 
     * @param data 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer data) const -> IoTask<size_t> {
        return mHandle.recvfrom(data, 0, nullptr);
    }

    // Extension Methods
    /**
     * @brief Send data to the socket. like write but with flags.
     * 
     * @param buffer 
     * @param flags 
     * @return IoTask<size_t> 
     */
    auto send(Buffer buffer, int flags = 0) const -> IoTask<size_t> {
        return mHandle.sendto(buffer, flags, nullptr);
    }

    /**
     * @brief Receive data from the socket. like read but with flags.
     * 
     * @param data 
     * @param flags 
     * @return IoTask<size_t> 
     */
    auto recv(MutableBuffer data, int flags = 0) const -> IoTask<size_t> {
        return mHandle.recvfrom(data, flags, nullptr);
    }

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return IoResult<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) const -> IoResult<void> {
        return mHandle.fd().setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <GetSockOption T>
    auto getOption() const -> IoResult<T> {
        return mHandle.fd().getOption<T>();
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return IoResult<IPEndpoint> 
     */
    auto localEndpoint() const -> IoResult<IPEndpoint> { 
        return mHandle.fd().localEndpoint<IPEndpoint>();
    }

    /**
     * @brief Get the remote endpoint associated with the socket.
     * 
     * @return IoResult<IPEndpoint> 
     */
    auto remoteEndpoint() const -> IoResult<IPEndpoint> {
        return mHandle.fd().remoteEndpoint<IPEndpoint>();
    }

    /**
     * @brief Poll the socket for events.
     * 
     * @param events 
     * @return IoTask<uint32_t> 
     */
    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mHandle.poll(events);
    }

    auto operator <=>(const TcpStream &) const = default;

    /**
     * @brief Connect to a remote endpoint.
     * 
     * @return IoTask<TcpStream> 
     */
    static auto connect(IPEndpoint endpoint) -> IoTask<TcpStream> {
        auto sockfd = Socket::make(endpoint.family(), SOCK_STREAM, IPPROTO_TCP);
        if (!sockfd) {
            co_return Err(sockfd.error());
        }
        auto handle = IoHandle<Socket>::make(std::move(*sockfd), IoDescriptor::Socket);
        if (!handle) {
            co_return Err(handle.error());
        }
        if (auto res = co_await handle->connect(endpoint); !res) {
            co_return Err(res.error());
        }
        co_return TcpStream(std::move(*handle));
    }

    /**
     * @brief Wrap a socket into a TcpStream.
     * 
     * @param socket The socket must be SOCK_STREAM. otherwise, IoError::InvalidArgument will be returned.
     * @return IoResult<TcpStream> 
     */
    static auto from(Socket socket) -> IoResult<TcpStream> {
        if (socket.type() != SOCK_STREAM) {
            return Err(IoError::InvalidArgument);
        }
        auto handle = IoHandle<Socket>::make(std::move(socket), IoDescriptor::Socket);
        if (!handle) {
            return Err(handle.error());
        }
        return TcpStream(std::move(*handle));
    }

    /**
     * @brief Check if the socket is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept { return bool(mHandle); }
private:
    IoHandle<Socket> mHandle;
};

/**
 * @brief The tcp listener class.
 * 
 */
class TcpListener {
public:
    TcpListener() = default;
    TcpListener(IoHandle<Socket> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() const { return mHandle.cancel(); }

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return IoResult<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) const -> IoResult<void> {
        return mHandle.fd().setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <GetSockOption T>
    auto getOption() const -> IoResult<T> {
        return mHandle.fd().getOption<T>();
    }

    /**
     * @brief Accept a connection from a remote endpoint.
     * 
     * @return IoTask<std::pair<TcpStream, IPEndpoint> > 
     */
    auto accept() const -> IoTask<std::pair<TcpStream, IPEndpoint> > {
        IPEndpoint endpoint;
        auto client = co_await accept(&endpoint);
        if (!client) {
            co_return Err(client.error());
        }
        co_return std::make_pair(std::move(*client), endpoint);
    }

    /**
     * @brief Accept a connection from a remote endpoint.
     * 
     * @param endpoint The address to receive the connection from. (can be nullptr)
     * @return IoTask<TcpStream> 
     */
    auto accept(IPEndpoint *endpoint) const -> IoTask<TcpStream> {
        auto sockfd = co_await mHandle.accept(endpoint);
        if (!sockfd) {
            co_return Err(sockfd.error());
        }
        auto handle = IoHandle<Socket>::make(Socket(std::move(*sockfd)), IoDescriptor::Socket);
        if (!handle) {
            co_return Err(handle.error());
        }
        co_return TcpStream(std::move(*handle));
    }

    /**
     * @brief Accept a connection from a remote endpoint.
     * 
     * @param endpoint The reference to the address to receive the connection from.
     * @return IoTask<TcpStream> 
     */
    auto accept(IPEndpoint &endpoint) const -> IoTask<TcpStream> {
        return accept(&endpoint);
    }

    /**
     * @brief Poll the socket for events.
     * 
     * @param events 
     * @return IoTask<uint32_t> 
     */
    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mHandle.poll(events);
    }

    /**
     * @brief Get the local endpoint associated with the socket.
     * 
     * @return IoResult<IPEndpoint> 
     */
    auto localEndpoint() const -> IoResult<IPEndpoint> { 
        return mHandle.fd().localEndpoint<IPEndpoint>();
    }

    /**
     * @brief Bind the socket to an endpoint.
     * 
     * @param endpoint The endpoint to bind to.
     * @param backlog The backlog for the socket.
     * @return IoTask<TcpListener> 
     */
    static auto bind(IPEndpoint endpoint, int backlog = SOMAXCONN) -> IoTask<TcpListener> {
        auto sockfd = Socket::make(endpoint.family(), SOCK_STREAM, IPPROTO_TCP);
        if (!sockfd) {
            co_return Err(sockfd.error());
        }
        co_return bindImpl(std::move(*sockfd), endpoint, backlog);
    }

    /**
     * @brief Bind the socket to an endpoint. apply the function to the socket before binding.
     * 
     * @tparam Fn 
     * @param endpoint The endpoint to bind to.
     * @param backlog The backlog for the socket.
     * @param fn The function to apply to the socket before binding.
     * @return IoTask<TcpListener>
     */
    template <typename Fn> requires(std::invocable<Fn, SocketView>)
    static auto bind(IPEndpoint endpoint, int backlog, Fn fn) -> IoTask<TcpListener> {
        auto sockfd = Socket::make(endpoint.family(), SOCK_STREAM, IPPROTO_TCP)
            .and_then([&](Socket self) -> IoResult<Socket> {
                if (auto res = fn(SocketView(self)); !res) {
                    return Err(res.error());
                }
                return self;
            });
        if (!sockfd) {
            co_return Err(sockfd.error());
        }
        co_return bindImpl(std::move(*sockfd), endpoint, backlog);
    }

    /**
     * @brief Wrap a socket in a TcpListener.
     * 
     * @param socket The socket must be SOCK_STREAM. otherwise, IoError::InvalidArgument will be returned.
     * @return IoResult<TcpListener> 
     */
    static auto from(Socket socket) -> IoResult<TcpListener> {
        if (socket.type() != SOCK_STREAM) {
            return Err(IoError::InvalidArgument);
        }
        auto handle = IoHandle<Socket>::make(std::move(socket), IoDescriptor::Socket);
        if (!handle) {
            return Err(handle.error());
        }
        return TcpListener(std::move(*handle));
    }

    /**
     * @brief Check if the socket is valid.
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept { return bool(mHandle); }
private:
    // Common part of it
    static auto bindImpl(Socket sockfd, const IPEndpoint &endpoint, int backlog) -> IoResult<TcpListener> {
        if (auto res = sockfd.bind(endpoint); !res) {
            return Err(res.error());
        }
        if (auto res = sockfd.listen(backlog); !res) {
            return Err(res.error());
        }
        auto handle = IoHandle<Socket>::make(std::move(sockfd), IoDescriptor::Socket);
        if (!handle) {
            return Err(handle.error());
        }
        return TcpListener(std::move(*handle));
    }

    IoHandle<Socket> mHandle;
};

// For compatible with old version.
using TcpClient = TcpStream;

/**
 * @brief Convert the TcpListener to an Generator.
 * 
 * @param listener 
 * @return IoGenerator<TcpStream> 
 */
inline auto toGenerator(TcpListener listener) -> IoGenerator<TcpStream> {
    while (true) {
        auto val = co_await listener.accept(nullptr);
        if (!val) {
            co_yield Err(val.error());
        }
        co_yield std::move(*val);
    }
}

ILIAS_NS_END