#pragma once

/**
 * @file sockfd.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For wrapping raw socket fd
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "endpoint.hpp"
#include "sys.hpp"

ILIAS_NS_BEGIN

/**
 * @brief A View of socket, witch hold operations
 * 
 */
class SocketView {
public:
    SocketView() = default;
    SocketView(socket_t fd) : mFd(fd) { }
    SocketView(const SocketView &) = default;

    /**
     * @brief Recv num of bytes
     * 
     * @param buf 
     * @param len 
     * @param flags 
     * @return Result<size_t> 
     */
    auto recv(void *buf, size_t len, int flags = 0) const -> Result<size_t>;

    /**
     * @brief Send num of bytes
     * 
     * @param buf 
     * @param len 
     * @param flags 
     * @return Result<size_t> 
     */
    auto send(const void *buf, size_t len, int flags = 0) const -> Result<size_t>;

    /**
     * @brief Sendto num of bytes to target endpoint
     * 
     * @param buf 
     * @param len 
     * @param flags 
     * @param endpoint 
     * @return Result<size_t> 
     */
    auto sendto(const void *buf, size_t len, int flags, const IPEndpoint *endpoint) const -> Result<size_t>;
    auto sendto(const void *buf, size_t len, int flags, const IPEndpoint &endpoint) const -> Result<size_t>;
    /**
     * @brief Recvfrom num of bytes from , it can get the remote endpoint 
     * 
     * @param buf 
     * @param len 
     * @param flags 
     * @param endpoint 
     * @return Result<size_t> 
     */
    auto recvfrom(void *buf, size_t len, int flags, IPEndpoint *endpoint) const -> Result<size_t>;
    auto recvfrom(void *buf, size_t len, int flags, IPEndpoint &endpoint) const -> Result<size_t>;

    /**
     * @brief Send data to the socket
     * 
     * @tparam T 
     * @tparam N 
     * @param buf 
     * @param flags 
     * @return Result<size_t> 
     */
    template <typename T, size_t N>
    auto send(const T (&buf)[N], int flags = 0) const -> Result<size_t>;

    /**
     * @brief Start listening on the socket
     * 
     * @param backlog 
     * @return Result<void> 
     */
    auto listen(int backlog = 0) const -> Result<void>;

    /**
     * @brief Shutdown the socket by how, default shutdown buth read and write
     * 
     * @param how 
     * @return Result<void> 
     */
    auto shutdown(int how = Shutdown::Both) const -> Result<void>;

    /**
     * @brief Connect to the specified endpoint
     * 
     * @param endpoint 
     * @return Result<void> 
     */
    auto connect(const IPEndpoint &endpoint) const -> Result<void>;

    /**
     * @brief Bind the socket to the specified endpoint
     * 
     * @param endpoint 
     * @return Result<void> 
     */
    auto bind(const IPEndpoint &endpoint) const -> Result<void>;

    /**
     * @brief Set blocking mode for the socket
     * 
     * @param blocking 
     * @return Result<void> 
     */
    auto setBlocking(bool blocking) const -> Result<void>;

    /**
     * @brief Set reuse address option for the socket
     * 
     * @param reuse 
     * @return Result<void> 
     */
    auto setReuseAddr(bool reuse) const -> Result<void>;

    /**
     * @brief Set socket option
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return Result<void> 
     */
    auto setOption(int level, int optname, const void *optval, socklen_t optlen) const -> Result<void>;

    /**
     * @brief Set the Option object, template version
     * 
     * @tparam T 
     * @param level 
     * @param optname 
     * @param optval 
     * @return Result<void> 
     */
    template <typename T>
    auto setOption(int level, int optname, const T &optval) const -> Result<void>;

    /**
     * @brief Get socket option
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return Result<void> 
     */
    auto getOption(int level, int optname, void *optval, socklen_t *optlen) const -> Result<void>;

    /**
     * @brief Get the Option object, template version
     * 
     * @tparam T 
     * @param level 
     * @param optname 
     * @return Result<T> 
     */
    template <typename T>
    auto getOption(int level, int optname) const -> Result<T>;

#ifdef _WIN32
    /**
     * @brief Perform IO control operation on the socket
     * 
     * @param cmd 
     * @param args 
     * @return Result<void> 
     */
    auto ioctl(long cmd, u_long *args) const -> Result<void>;
#endif

    /**
     * @brief Check if the socket is valid
     * 
     * @return bool 
     */
    auto isValid() const -> bool;

    /**
     * @brief Get the family of the socket
     * 
     * @return Result<int> 
     */
    auto family() const -> Result<int>;

    /**
     * @brief Get the type of the socket
     * 
     * @return Result<int> 
     */
    auto type() const -> Result<int>;
    
    /**
     * @brief Get the error associated with the socket
     * 
     * @return Result<Error> 
     */
    auto error() const -> Result<Error>;

    /**
     * @brief Accept a connection on the socket
     * 
     * @tparam T 
     * @return Result<std::pair<T, IPEndpoint>> 
     */
    template <typename T>
    auto accept() const -> Result<std::pair<T, IPEndpoint>>;

    /**
     * @brief Get the local endpoint of the socket
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint>;

    /**
     * @brief Get the remote endpoint of the socket
     * 
     * @return Result<IPEndpoint> 
     */
    auto remoteEndpoint() const -> Result<IPEndpoint>;

    /**
     * @brief Get the underlying socket descriptor
     * 
     * @return socket_t 
     */
    auto get() const noexcept -> socket_t {
        return mFd;
    }
    static constexpr socket_t InvalidSocket = ILIAS_INVALID_SOCKET;
protected:
    socket_t mFd = InvalidSocket;
};

/**
 * @brief A Wrapper hold socket ownship
 * 
 */
class Socket final : public SocketView {
public:
    Socket() = default;
    explicit Socket(socket_t fd);
    explicit Socket(Socket &&s);
    explicit Socket(const Socket &) = delete;
    explicit Socket(int family, int type, int protocol = 0);
    ~Socket();

    /**
     * @brief Release the socket ownship
     * 
     * @param newSocket 
     * @return socket_t 
     */
    auto release(socket_t newSocket = InvalidSocket) -> socket_t;

    /**
     * @brief Reset the socket and set it to
     * 
     * @param newSocket 
     * @return true 
     * @return false 
     */
    auto reset(socket_t newSocket = InvalidSocket) -> bool;

    /**
     * @brief Close current socket
     * 
     * @return true 
     * @return false 
     */
    auto close() -> bool;

    auto operator =(Socket &&s) -> Socket &;
    auto operator =(const Socket &) -> Socket & = delete;

    /**
     * @brief Accept a new connection
     * 
     * @return Result<std::pair<Socket, IPEndpoint>> 
     */
    auto accept() const -> Result<std::pair<Socket, IPEndpoint> >;

    /**
     * @brief Create a new socket by family type proto
     * 
     * @param family 
     * @param type 
     * @param protocol 
     * @return Result<Socket> 
     */
    static auto create(int family, int type, int protocol) -> Result<Socket>;
};


// --- SocketView Impl
inline auto SocketView::recv(void *buf, size_t len, int flags) const -> Result<size_t> {
    ssize_t ret = ::recv(mFd, static_cast<byte_t*>(buf), len, flags);
    if (ret < 0) {
        return Unexpected(Error::fromErrno());
    }
    return ret;
}
inline auto SocketView::send(const void *buf, size_t len, int flags) const -> Result<size_t> {
    ssize_t ret = ::send(mFd, static_cast<const byte_t*>(buf), len, flags);
    if (ret < 0) {
        return Unexpected(Error::fromErrno());
    }
    return ret;
}
inline auto SocketView::recvfrom(void *buf, size_t len, int flags, IPEndpoint *ep) const -> Result<size_t> {
    ::sockaddr_storage addr {};
    ::socklen_t size = sizeof(addr);
    ssize_t ret = ::recvfrom(mFd, static_cast<byte_t*>(buf), len, flags, reinterpret_cast<::sockaddr*>(&addr), &size);
    if (ret < 0) {
        return Unexpected(Error::fromErrno());
    }
    if (ep) {
        *ep = IPEndpoint::fromRaw(&addr, size);
    }
    return ret;
}
inline auto SocketView::recvfrom(void *buf, size_t len, int flags, IPEndpoint &ep) const -> Result<size_t> {
    return recvfrom(buf, len, flags, &ep);
}
inline auto SocketView::sendto(const void *buf, size_t len, int flags, const IPEndpoint *ep) const -> Result<size_t> {
    const ::sockaddr *addr = ep ? &ep->data<::sockaddr>() : nullptr;
    const ::socklen_t addrLen = ep ? ep->length() : 0;
    ssize_t ret = ::sendto(mFd, static_cast<const byte_t*>(buf), len, flags, addr, addrLen);
    if (ret < 0) {
        return Unexpected(Error::fromErrno());
    }
    return ret;
}
inline auto SocketView::sendto(const void *buf, size_t len, int flags, const IPEndpoint &ep) const -> Result<size_t> {
    return sendto(buf, len, flags, &ep);
}    

// Helper 
template <typename T, size_t N>
inline auto SocketView::send(const T (&buf)[N], int flags) const -> Result<size_t> {
    static_assert(std::is_standard_layout<T>::value && std::is_trivial<T>::value, "T must be POD type");
    return send(buf, sizeof(T) * N, flags);
}

inline auto SocketView::listen(int backlog) const -> Result<void> {
    if (::listen(mFd, backlog) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline auto SocketView::connect(const IPEndpoint &ep) const -> Result<void> {
    if (::connect(mFd, &ep.data<::sockaddr>(), ep.length()) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline auto SocketView::bind(const IPEndpoint &ep) const -> Result<void> {
    if (::bind(mFd, &ep.data<::sockaddr>(), ep.length()) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline auto SocketView::shutdown(int how) const -> Result<void> {
    if (::shutdown(mFd, how) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline auto SocketView::isValid() const -> bool {
    return mFd != ILIAS_INVALID_SOCKET;
}
inline auto SocketView::getOption(int level, int optname, void *optval, socklen_t *optlen) const -> Result<void> {
    if (::getsockopt(mFd, level, optname, static_cast<byte_t*>(optval), optlen) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
template <typename T>
inline auto SocketView::getOption(int level, int optname) const -> Result<T> {
    T val;
    socklen_t len = sizeof(val);
    if (auto ret = getOption(level, optname, val, &len); !ret) {
        return Unexpected(ret.error());
    }
    return val;
}
inline auto SocketView::setOption(int level, int optname, const void *optval, socklen_t optlen) const -> Result<void> {
    if (::setsockopt(mFd, level, optname, static_cast<const byte_t*>(optval), optlen) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
template <typename T>
inline auto SocketView::setOption(int level, int optname, const T &optval) const -> Result<void> {
    return setOption(level, optname, &optval, sizeof(T));
}
inline auto SocketView::setReuseAddr(bool reuse) const -> Result<void> {
    int data = reuse ? 1 : 0;
    return setOption(SOL_SOCKET, SO_REUSEADDR, &data, sizeof(data));
}
inline auto SocketView::setBlocking(bool blocking) const -> Result<void> {
#ifdef _WIN32
    u_long block = blocking ? 0 : 1;
    return ioctl(FIONBIO, &block);
#else
    int flags = ::fcntl(mFd, F_GETFL, 0);
    if (flags < 0) {
        return Unexpected(Error::fromErrno());
    }
    if (blocking) {
        flags &= ~O_NONBLOCK;
    }
    else {
        flags |= O_NONBLOCK;
    }
    if (::fcntl(mFd, F_SETFL, flags) < 0) {
        return Unexpected(Error::fromErrno());
    }
    return Result<void>();
#endif
}

#ifdef _WIN32
inline auto SocketView::ioctl(long cmd, u_long *pargs) const -> Result<void> {
    if (::ioctlsocket(mFd, cmd, pargs) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
#endif

inline auto SocketView::family() const -> Result<int> {
#ifdef _WIN32
    ::WSAPROTOCOL_INFO info;
    ::socklen_t len = sizeof(info);
    if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &len) != 0) {
        return Unexpected(Error::fromErrno());
    }
    return info.iAddressFamily;
#else
    int family = 0;
    ::socklen_t len = sizeof(family);
    if (::getsockopt(mFd, SOL_SOCKET, SO_DOMAIN, &family, &len) != 0) {
        return Unexpected(Error::fromErrno());
    }
    return family;
#endif
}

inline auto SocketView::type() const -> Result<int> {
#ifdef _WIN32
    ::WSAPROTOCOL_INFO info;
    ::socklen_t len = sizeof(info);
    if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &len) != 0) {
        return Unexpected(Error::fromErrno());
    }
    return info.iSocketType;
#else
    int type = 0;
    ::socklen_t len = sizeof(type);
    if (::getsockopt(mFd, SOL_SOCKET, SO_TYPE, &type, &len) != 0) {
        return Unexpected(Error::fromErrno());
    }
    return type;
#endif
}

template <typename T>
inline auto SocketView::accept() const -> Result<std::pair<T, IPEndpoint>> {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    int fd = ::accept(mFd, reinterpret_cast<::sockaddr*>(&addr), &len);
    if (fd != ILIAS_INVALID_SOCKET) {
        return std::make_pair(T(fd), IPEndpoint::fromRaw(&addr, len));
    }
    return Unexpected(Error::fromErrno());
}

inline auto SocketView::localEndpoint() const -> Result<IPEndpoint> {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    if (::getsockname(mFd, reinterpret_cast<::sockaddr*>(&addr), &len) == 0) {
        return IPEndpoint::fromRaw(&addr, len);
    }
    return Unexpected(Error::fromErrno());
}
inline auto SocketView::remoteEndpoint() const -> Result<IPEndpoint> {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    if (::getpeername(mFd, reinterpret_cast<::sockaddr*>(&addr), &len) == 0) {
        return IPEndpoint::fromRaw(&addr, len);
    }
    return Unexpected(Error::fromErrno());
}
inline auto SocketView::error() const -> Result<Error> {
    error_t err;
    ::socklen_t len = sizeof(error_t);
    if (::getsockopt(mFd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == 0) {
        return Error::fromErrno(err);
    }
    return Unexpected(Error::fromErrno());
}

// --- Socket Impl
inline Socket::Socket(socket_t fd) : SocketView(fd) { }
inline Socket::Socket(Socket &&s) : Socket(s.release()) { }
inline Socket::Socket(int family, int type, int proto) {
    mFd = ::socket(family, type, proto);
}
inline Socket::~Socket() { close(); }

inline auto Socket::release(socket_t newSocket) -> socket_t {
    socket_t prev = mFd;
    mFd = newSocket;
    return prev;
}
inline auto Socket::reset(socket_t newSocket) -> bool {
    bool ret = true;
    if (isValid()) {
        ret = (ILIAS_CLOSE(mFd) == 0);
    }
    mFd = newSocket;
    return ret;
}
inline auto Socket::close() -> bool {
    return reset();
}

inline auto Socket::operator =(Socket &&s) -> Socket & {
    if (this == &s) {
        return *this;
    }
    reset(s.release());
    return *this;
}

inline auto Socket::accept() const -> Result<std::pair<Socket, IPEndpoint>> {
    return SocketView::accept<Socket>();
}

inline auto Socket::create(int family, int type, int proto) -> Result<Socket> {
    auto sock = ::socket(family, type, proto);
    if (sock != ILIAS_INVALID_SOCKET) {
        return Result<Socket>(Socket(sock));
    }
    return Unexpected(Error::fromErrno());
}

ILIAS_NS_END