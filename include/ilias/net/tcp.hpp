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
#include <ilias/net/msghdr.hpp> // MsgHdr
#include <ilias/io/context.hpp>
#include <ilias/io/ext.hpp>

ILIAS_NS_BEGIN

// Forward declarations
class TcpStream;
class TcpListener;

/**
 * @brief An builder to build tcp socket
 * 
 * @code
 *  auto stream = co_await TcpBuilder {AF_INET}
 *    .option(sockopt::NoDelay(true))
 *    .connect("127.0.0.1:8080");
 * @endcode
 * 
 */
class TcpBuilder {
public:
    /**
     * @brief Construct a new Tcp Builder by given address family.
     * 
     * @param family 
     */
    explicit TcpBuilder(int family) : mFd(Socket::make(family, SOCK_STREAM, IPPROTO_TCP)) {}
    TcpBuilder(TcpBuilder &&) = default;

    /**
     * @brief Set an new socket option on this builder
     * 
     * @tparam T 
     * @param opt 
     * @return TcpBuilder && 
     */
    template <SetSockOption T>
    auto option(const T &opt) -> TcpBuilder && {
        if (mFd) {
            if (auto res = mFd->setOption(opt); !res) {
                mFd = Err(res.error());
            }
        }
        return std::move(*this);
    }

    /**
     * @brief Connect to a remote endpoint.
     * @note It will consume the builder.
     * 
     * @param endpoint 
     * @return IoTask<TcpStream> 
     */
    auto connect(IPEndpoint endpoint) -> IoTask<TcpStream>;

    /**
     * @brief Bind to a local endpoint.
     * @note It will consume the builder.
     * 
     * @param endpoint 
     * @param backlog 
     * @return IoTask<TcpListener> 
     */
    auto bind(IPEndpoint endpoint, int backlog = SOMAXCONN) -> IoTask<TcpListener>;

    // Operator
    auto operator =(TcpBuilder &&) -> TcpBuilder & = default;
private:
    IoResult<Socket> mFd;
};

/**
 * @brief The tcp stream class.
 * 
 */
class TcpStream final : public StreamExt<TcpStream> {
public:
    TcpStream() = default;
    TcpStream(IoHandle<Socket> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }

    // Readable Concept
    /**
     * @brief Read data from the socket.
     * 
     * @param data 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer data) const -> IoTask<size_t> {
        return mHandle.recvfrom(data, 0, nullptr);
    }

    // Writable Concept
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
    auto shutdown(int how = Shutdown::Write) const -> IoTask<void> {
        co_return mHandle.fd().shutdown(how);
    }

    // ScatterReadable
    /**
     * @brief Read an sequence of buffers to the socket.
     * 
     * @tparam T 
     * @param buffers 
     * @return IoTask<size_t> 
     */
    template <MutableBufferSequence T>
    auto readv(T &buffers) const -> IoTask<size_t> {
        auto sequence = makeIoSequence(buffers);
        MutableMsgHdr msg;
        msg.setBuffers(sequence);
        co_return co_await mHandle.recvmsg(msg, 0);
    }

    // GatherWritable
    /**
     * @brief Write an sequence of buffers to the socket.
     * 
     * @tparam T 
     * @param buffers The sequence of buffers to write.
     * @return IoTask<size_t> 
     */
    template <BufferSequence T>
    auto writev(const T &buffers) const -> IoTask<size_t> {
        auto sequence = makeIoSequence(buffers);
        MsgHdr msg;
        msg.setBuffers(sequence);
        co_return co_await mHandle.sendmsg(msg, 0);
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
     * @param endpoint
     * 
     * @return IoTask<TcpStream> 
     */
    static auto connect(IPEndpoint endpoint) -> IoTask<TcpStream> {
        return TcpBuilder {endpoint.family()}.connect(endpoint);
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
        ILIAS_CO_TRY(auto client, co_await accept(&endpoint));
        co_return std::pair {
            TcpStream {std::move(client)},
            endpoint
        };
    }

    /**
     * @brief Accept a connection from a remote endpoint.
     * 
     * @param endpoint The address to receive the connection from. (can be nullptr)
     * @return IoTask<TcpStream> 
     */
    auto accept(IPEndpoint *endpoint) const -> IoTask<TcpStream> {
        ILIAS_CO_TRY(auto sockfd, co_await mHandle.accept(endpoint));
        ILIAS_CO_TRY(auto handle, IoHandle<Socket>::make(Socket {sockfd}, IoDescriptor::Socket));
        co_return TcpStream {std::move(handle)};
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
        return TcpBuilder {endpoint.family()}.bind(endpoint, backlog);
    }

    /**
     * @brief Wrap a socket in a TcpListener.
     * 
     * @param sockfd The socket must be SOCK_STREAM. otherwise, IoError::InvalidArgument will be returned.
     * @return IoResult<TcpListener> 
     */
    static auto from(Socket sockfd) -> IoResult<TcpListener> {
        if (sockfd.type() != SOCK_STREAM) {
            return Err(IoError::InvalidArgument);
        }
        ILIAS_TRY(auto handle, IoHandle<Socket>::make(std::move(sockfd), IoDescriptor::Socket));
        return TcpListener {std::move(handle)};
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

// Impl
inline auto TcpBuilder::connect(IPEndpoint endpoint) -> IoTask<TcpStream> {
    auto fn = [](TcpBuilder self, IPEndpoint endpoint) -> IoTask<TcpStream> {
        ILIAS_CO_TRY(auto sockfd, std::move(self.mFd));
        ILIAS_CO_TRY(auto handle, IoHandle<Socket>::make(std::move(sockfd), IoDescriptor::Socket));
        ILIAS_CO_TRYV(co_await handle.connect(endpoint));
        co_return TcpStream {std::move(handle)};
    };
    return fn(std::move(*this), endpoint);
}

inline auto TcpBuilder::bind(IPEndpoint endpoint, int backlog) -> IoTask<TcpListener> {
    auto fn = [](TcpBuilder self, IPEndpoint endpoint, int backlog) -> IoTask<TcpListener> {
        ILIAS_CO_TRY(auto sockfd, std::move(self.mFd));
        ILIAS_CO_TRYV(sockfd.bind(endpoint));
        ILIAS_CO_TRYV(sockfd.listen(backlog));
        ILIAS_CO_TRY(auto handle, IoHandle<Socket>::make(std::move(sockfd), IoDescriptor::Socket));
        co_return TcpListener {std::move(handle)};
    };
    return fn(std::move(*this), endpoint, backlog);
};

// For compatible with old version.
using TcpClient [[deprecated("Use TcpStream instead")]] = TcpStream;

ILIAS_NS_END