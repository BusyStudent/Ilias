#pragma once

#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/net/msghdr.hpp> // MsgHdr
#include <ilias/io/context.hpp>
#include <ilias/io/ext.hpp>

#if !defined(ILIAS_NO_AF_UNIX)

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Wrapper of the raw socket, for handling the sock file in windows
 * 
 */
class UnixSocket {
public:
    UnixSocket() = default;
    UnixSocket(UnixSocket &&) = default;
    UnixSocket(Socket sock) : mFd(std::move(sock)) {}
    ~UnixSocket() { cleanup(); }

    auto bind(UnixEndpoint endpoint) -> IoResult<void> {
        ILIAS_TRYV(mFd.bind(endpoint));
#if defined(_WIN32)
        if (!endpoint.isAbstract()) {
            // Convert the endpint(utf8) to wchar_t
            auto path = endpoint.path();
            auto len = ::MultiByteToWideChar(CP_UTF8, 0, path.data(), path.size(), nullptr, 0);
            mPath = std::make_unique<wchar_t[]>(len + 1);
            ::MultiByteToWideChar(CP_UTF8, 0, path.data(), path.size(), mPath.get(), len);
            mPath[len] = L'\0';
        }
#endif // _WIN32
        return {};
    }

    // Operator
    auto operator =(UnixSocket &&other) -> UnixSocket & {
        if (this != &other) {
            cleanup();
            mFd = std::move(other.mFd);
#if defined(_WIN32)
            mPath = std::move(other.mPath);
#endif // _WIN32
        }
        return *this;
    }
    auto operator <=>(const UnixSocket &) const = default;
    auto operator ->() const -> const Socket * { return &mFd; }

    // Impl BorrowFileDescriptor
    explicit operator fd_t() const noexcept { return fd_t(mFd); }

    static auto make(int type) -> IoResult<UnixSocket> {
        ILIAS_TRY(auto sock, Socket::make(AF_UNIX, type, 0));
        return UnixSocket{std::move(sock)};
    }
private:
    auto cleanup() -> void {
#if defined(_WIN32)
        if (mPath) {
            mFd.close();
            ::DeleteFileW(mPath.get());
        }
#endif // _WIN32
    }

    Socket mFd;
#if defined(_WIN32)
    std::unique_ptr<wchar_t []> mPath; // If not nullptr, we need to delete the socket file in dtor
#endif // _WIN32
};

} // namespace detail

// Forward declaration
class UnixStream;
class UnixDatagram; // TODO:
class UnixListener;

/**
 * @brief An builder to build unix socket
 * 
 * @code
 *  auto stream = co_await UnixBuilder {SOCK_STREAM}
 *    .option(sockopt::NoDelay(true))
 *    .connect("/tmp/my_socket");
 * @endcode
 * 
 */
class UnixBuilder {
public:
    explicit UnixBuilder(int type) : mFd(detail::UnixSocket::make(type)) {}
    UnixBuilder(UnixBuilder &&) = default;

    /**
     * @brief Set an new socket option on this builder
     * 
     * @tparam T 
     * @param opt 
     * @return TcpBuilder && 
     */
    template <SetSockOption T>
    auto option(const T &opt) -> UnixBuilder && {
        if (mFd) {
            if (auto res = (*mFd)->setOption(opt); !res) {
                mFd = Err(res.error());
            }
        }
        return std::move(*this);
    }

    /**
     * @brief Bind the socket to the endpoint
     * @note self must be a stream socket
     * 
     * @param endpoint 
     * @param backlog 
     * @return Just<IoResult<UnixListener> > 
     */
    auto bind(UnixEndpoint endpoint, int backlog = SOMAXCONN) -> Just<IoResult<UnixListener> >;

    /**
     * @brief Connect to the endpoint
     * @param self must be a stream socket
     * 
     * @param endpoint 
     * @return Just<IoResult<UnixStream> > 
     */
    auto connect(UnixEndpoint endpoint) -> Just<IoResult<UnixStream> >;

    // Operator
    auto operator =(UnixBuilder &&) -> UnixBuilder & = default;
private:
    IoResult<detail::UnixSocket> mFd;
};

/**
 * @brief The unix socket stream class. a connected unix stream.
 * 
 */
class UnixStream final : public StreamExt<UnixStream> {
public:
    UnixStream() = default;
    UnixStream(IoHandle<detail::UnixSocket> fd) : mHandle(std::move(fd)) {}

    auto close() { mHandle.close(); }

    // Readable
    auto read(MutableBuffer buffer) const -> IoTask<size_t> {
        return mHandle.recvfrom(buffer, 0, nullptr);
    }

    // Writable
    auto write(Buffer buffer) const -> IoTask<size_t> {
        return mHandle.sendto(buffer, 0, nullptr);
    }

    auto flush() const -> Just<IoResult<void> > {
        return just(IoResult<void>{});
    }

    auto shutdown(Shutdown how = Shutdown::Write) const -> Just<IoResult<void> > {
        return just(mHandle.fd()->shutdown(how));
    }

    // TODO: sendmsg recvmsg

    /**
     * @brief Set the socket option.
     * 
     * @tparam T 
     * @param opt 
     * @return IoResult<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) const -> IoResult<void> {
        return mHandle.fd()->setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <GetSockOption T>
    auto getOption() const -> IoResult<T> {
        return mHandle.fd()->getOption<T>();
    }

    /**
     * @brief Get the local endpoint of the unix socket
     * 
     * @return IoResult<UnixEndpoint> 
     */
    auto localEndpoint() const -> IoResult<UnixEndpoint> {
        return mHandle.fd()->localEndpoint<UnixEndpoint>();
    }

    /**
     * @brief Get the remote endpoint of the unix socket
     * 
     * @return IoResult<UnixEndpoint> 
     */
    auto remoteEndpoint() const -> IoResult<UnixEndpoint> {
        return mHandle.fd()->remoteEndpoint<UnixEndpoint>();
    }

    // Operator
    auto operator <=>(const UnixStream &) const = default;

    /**
     * @brief Wrap a socket in a UnixStream.
     * 
     * @param sockfd The socket must be SOCK_STREAM. otherwise, IoError::InvalidArgument will be returned.
     * @return IoResult<UnixStream> 
     */
    static auto from(Socket sockfd) -> IoResult<UnixStream> {
        if (sockfd.type() != SOCK_STREAM) {
            return Err(IoError::InvalidArgument);
        }
        ILIAS_TRY(auto handle, IoHandle<detail::UnixSocket>::make(std::move(sockfd), IoDescriptor::Socket));
        return UnixStream{std::move(handle)};
    }

    /**
     * @brief Connect to the endpoint
     * 
     * @param endpoint 
     * @return IoTask<UnixStream>
     */
    static auto connect(UnixEndpoint endpoint) -> Just<IoResult<UnixStream> > {
        return UnixBuilder {SOCK_STREAM}.connect(endpoint);
    }

    /**
     * @brief Create a new connected unix stream pair.
     * @note This method will always failed in Windows (OperationNotSupported)
     * 
     * @return IoTask<std::pair<UnixStream, UnixStream> >
     */
    static auto pair() -> Just<IoResult<std::pair<UnixStream, UnixStream> > > {
        return just([]() -> IoResult<std::pair<UnixStream, UnixStream> > {
            return Err(IoError::OperationNotSupported);
        }());
    }
private:
    IoHandle<detail::UnixSocket> mHandle;
};

/**
 * @brief The unix listener class.
 * 
 */
class UnixListener {
public:
    UnixListener() = default;
    UnixListener(IoHandle<detail::UnixSocket> fd) : mHandle(std::move(fd)) {}

    /**
     * @brief Accept a new connection
     * 
     * @return IoTask<std::pair<UnixStream, UnixEndpoint> > 
     */
    auto accept() const -> IoTask<std::pair<UnixStream, UnixEndpoint> > {
        UnixEndpoint endpoint;
        ILIAS_CO_TRY(auto sock, co_await accept(&endpoint));
        co_return std::pair {
            std::move(sock), 
            endpoint
        };
    }

    /**
     * @brief Accept a new connection
     * 
     * @param endpoint The address to receive the connection from. (can be nullptr)
     * @return IoTask<UnixStream> 
     */
    auto accept(UnixEndpoint *endpoint) const -> IoTask<UnixStream> {
        ILIAS_CO_TRY(auto sock, co_await mHandle.accept(endpoint));
        ILIAS_CO_TRY(auto handle, IoHandle<detail::UnixSocket>::make(Socket{sock}, IoDescriptor::Socket));
        co_return UnixStream {std::move(handle)};
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
        return mHandle.fd()->setOption(opt);
    }

    /**
     * @brief Get the socket option.
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <GetSockOption T>
    auto getOption() const -> IoResult<T> {
        return mHandle.fd()->getOption<T>();
    }

    /**
     * @brief Get the local endpoint of the unix socket
     * 
     * @return IoResult<UnixEndpoint> 
     */
    auto localEndpoint() const -> IoResult<UnixEndpoint> {
        return mHandle.fd()->localEndpoint<UnixEndpoint>();
    }

    // Operator
    auto operator <=>(const UnixListener &) const = default;

    /**
     * @brief Wrap a socket in a UnixListener.
     * 
     * @param sockfd The socket must be SOCK_STREAM. otherwise, IoError::InvalidArgument will be returned.
     * @return IoResult<UnixListener> 
     */
    static auto from(Socket sockfd) -> IoResult<UnixListener> {
        if (sockfd.type() != SOCK_STREAM) {
            return Err(IoError::InvalidArgument);
        }
        ILIAS_TRY(auto handle, IoHandle<detail::UnixSocket>::make(std::move(sockfd), IoDescriptor::Socket));
        return UnixListener{std::move(handle)};
    }

    /**
     * @brief Bind the unix socket to the endpoint
     * 
     * @param endpoint The endpoint to bind
     * @param backlog
     * @return IoTask<UnixListener>
     */
    static auto bind(UnixEndpoint endpoint, int backlog = SOMAXCONN) -> Just<IoResult<UnixListener> > {
        return UnixBuilder {SOCK_STREAM}.bind(endpoint, backlog);
    }
private:
    IoHandle<detail::UnixSocket> mHandle;
};

inline auto UnixBuilder::connect(UnixEndpoint endpoint) -> Just<IoResult<UnixStream> > {
    return just([&]() -> IoResult<UnixStream> {
        ILIAS_TRY(auto sockfd, std::move(mFd));
        ILIAS_TRYV(sockfd->connect(endpoint)); // Emm, I think the UDS connect would not block
        ILIAS_TRY(auto handle, IoHandle<detail::UnixSocket>::make(std::move(sockfd), IoDescriptor::Socket));
        return UnixStream {std::move(handle)};
    }());
}

inline auto UnixBuilder::bind(UnixEndpoint endpoint, int backlog) -> Just<IoResult<UnixListener> > {
    return just([&]() -> IoResult<UnixListener> {
        ILIAS_TRY(auto sockfd, std::move(mFd));
        ILIAS_TRYV(sockfd.bind(endpoint)); // We need handle bind specially, because of the sock file in windows
        ILIAS_TRYV(sockfd->listen(backlog));
        ILIAS_TRY(auto handle, IoHandle<detail::UnixSocket>::make(std::move(sockfd), IoDescriptor::Socket));
        return UnixListener {std::move(handle)};
    }());
}

ILIAS_NS_END

#endif // ILIAS_NO_AF_UNIX