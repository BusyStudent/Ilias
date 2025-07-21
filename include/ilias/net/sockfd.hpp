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

#pragma once

#include <ilias/net/system.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockopt.hpp>
#include <ilias/log.hpp>
#include <span>

ILIAS_NS_BEGIN

// NOTE: in windows recv and send 's buffer argument is char type and linux is void type
// so we cast it to char type, char * will be casted to void * automatically in linux

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
     * @param flags 
     * @return IoResult<size_t> 
     */
    auto recv(std::span<std::byte> buf, int flags = 0) const -> IoResult<size_t> {
        auto ret = ::recv(mFd, reinterpret_cast<char*>(buf.data()), buf.size_bytes(), flags);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return ret;
    }

    /**
     * @brief Send num of bytes
     * 
     * @param buf 
     * @param flags 
     * @return IoResult<size_t> 
     */
    auto send(std::span<const std::byte> buf, int flags = 0) const -> IoResult<size_t> {
        auto ret = ::send(mFd, reinterpret_cast<const char*>(buf.data()), buf.size_bytes(), flags);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return ret;
    }

    /**
     * @brief Sendto num of bytes to target endpoint
     * 
     * @param buf 
     * @param flags 
     * @param endpoint 
     * @return IoResult<size_t> 
     */
    auto sendto(std::span<const std::byte> buf, int flags, EndpointView endpoint) const -> IoResult<size_t> {
        const ::sockaddr *addr = endpoint.data();
        const ::socklen_t addrLen = endpoint.length();
        auto ret = ::sendto(mFd, reinterpret_cast<const char*>(buf.data()), buf.size_bytes(), flags, addr, addrLen);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return ret;
    }

    /**
     * @brief Recvfrom num of bytes from , it can get the remote endpoint 
     * 
     * @param buf 
     * @param flags 
     * @param endpoint 
     * @return IoResult<size_t> 
     */
    auto recvfrom(std::span<std::byte> buf, int flags, MutableEndpointView endpoint) const -> IoResult<size_t> {
        ::sockaddr *addr = endpoint.data();
        ::socklen_t addrLen = endpoint.bufsize();
        auto ret = ::recvfrom(mFd, reinterpret_cast<char*>(buf.data()), buf.size_bytes(), flags, addr, &addrLen);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return ret;
    }

    /**
     * @brief Start listening on the socket
     * 
     * @param backlog 
     * @return IoResult<void> 
     */
    auto listen(int backlog = 0) const -> IoResult<void> {
        auto ret = ::listen(mFd, backlog);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Shutdown the socket by how, default shutdown buth read and write
     * 
     * @param how 
     * @return IoResult<void> 
     */
    auto shutdown(int how = Shutdown::Both) const -> IoResult<void> {
        auto ret = ::shutdown(mFd, how);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Connect to the specified endpoint
     * 
     * @param endpoint 
     * @return IoResult<void> 
     */
    auto connect(EndpointView endpoint) const -> IoResult<void> {
        auto ret = ::connect(mFd, endpoint.data(), endpoint.length());
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Connect to the specified ip endpoint
     * 
     * @param endpoint 
     * @return IoResult<void> 
     */
    auto connect(const IPEndpoint &endpoint) const -> IoResult<void> {
        return connect(EndpointView(endpoint));
    }

    /**
     * @brief Bind the socket to the specified endpoint
     * 
     * @param endpoint The endpoint view to bind to
     * @return IoResult<void> 
     */
    auto bind(EndpointView endpoint) const -> IoResult<void> {
        auto ret = ::bind(mFd, endpoint.data(), endpoint.length());
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Bind the socket to the specified ip endpoint
     * 
     * @param endpoint The const IPEndpoint to bind to
     * @return IoResult<void> 
     */
    auto bind(const IPEndpoint &endpoint) const -> IoResult<void> {
        return bind(EndpointView(endpoint));
    }

    /**
     * @brief Set blocking mode for the socket
     * 
     * @param blocking 
     * @return IoResult<void> 
     */
    auto setBlocking(bool blocking) const -> IoResult<void> {

#if defined(_WIN32)
    u_long block = blocking ? 0 : 1;
    return ioctl(FIONBIO, &block);
#else
    int flags = ::fcntl(mFd, F_GETFL, 0);
    if (flags < 0) {
        return Err(SystemError::fromErrno());
    }
    if (blocking) {
        flags &= ~O_NONBLOCK;
    }
    else {
        flags |= O_NONBLOCK;
    }
    if (::fcntl(mFd, F_SETFL, flags) < 0) {
        return Err(SystemError::fromErrno());
    }
    return IoResult<void>();
#endif

    }

    /**
     * @brief Set reuse address option for the socket
     * 
     * @param reuse 
     * @return IoResult<void> 
     */
    auto setReuseAddr(bool reuse) const -> IoResult<void> {
        int opt = reuse ? 1 : 0;
        return setOption(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    /**
     * @brief Set socket option
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return IoResult<void> 
     */
    auto setOption(int level, int optname, const void *optval, socklen_t optlen) const -> IoResult<void> {
        auto ret = ::setsockopt(mFd, level, optname, static_cast<const char*>(optval), optlen);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Set socket option by option object
     * 
     * @tparam T 
     * @param opt 
     * @return IoResult<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) const -> IoResult<void> {
        return opt.setopt(mFd);
    }

    /**
     * @brief Get socket option
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return IoResult<void> 
     */
    auto getOption(int level, int optname, void *optval, socklen_t *optlen) const -> IoResult<void> {
        auto ret = ::getsockopt(mFd, level, optname, static_cast<char*>(optval), optlen);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Get the socket option by option object type
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <GetSockOption T>
    auto getOption() const -> IoResult<T> {
        return T::getopt(mFd);
    }

#if defined(_WIN32)
    /**
     * @brief Perform IO control operation on the socket
     * 
     * @param cmd 
     * @param args 
     * @return IoResult<void> 
     */
    auto ioctl(long cmd, u_long *args) const -> IoResult<void> {
        auto ret = ::ioctlsocket(mFd, cmd, args);
        if (ret < 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }
#endif

    /**
     * @brief Check if the socket is valid
     * 
     * @return bool 
     */
    auto isValid() const -> bool {
        return mFd != Invalid;
    }

    /**
     * @brief Get the family of the socket
     * 
     * @return IoResult<int> 
     */
    auto family() const -> IoResult<int> {

#if defined(_WIN32)
        ::WSAPROTOCOL_INFO info;
        ::socklen_t len = sizeof(info);
        if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &len) != 0) {
            return Err(SystemError::fromErrno());
        }
        return info.iAddressFamily;
#else
        int family = 0;
        ::socklen_t len = sizeof(family);
        if (::getsockopt(mFd, SOL_SOCKET, SO_DOMAIN, &family, &len) != 0) {
            return Err(SystemError::fromErrno());
        }
        return family;
#endif

    }

    /**
     * @brief Get the type of the socket
     * 
     * @return IoResult<int> 
     */
    auto type() const -> IoResult<int> {

#if defined(_WIN32)
        ::WSAPROTOCOL_INFO info;
        ::socklen_t len = sizeof(info);
        if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &len) != 0) {
            return Err(SystemError::fromErrno());
        }
        return info.iSocketType;
#else
        int type = 0;
        ::socklen_t len = sizeof(type);
        if (::getsockopt(mFd, SOL_SOCKET, SO_TYPE, &type, &len) != 0) {
            return Err(SystemError::fromErrno());
        }
        return type;
#endif

    }
    
    /**
     * @brief Get the error associated with the socket
     * 
     * @return IoResult<Error> 
     */
    auto error() const -> IoResult<IoError> {
        error_t err = 0;
        socklen_t len = sizeof(err);
        if (auto val = getOption(SOL_SOCKET, SO_ERROR, &err, &len); !val) {
            return Err(val.error());
        }
        return SystemError(err);
    }

    /**
     * @brief Accept a connection on the socket
     * 
     * @tparam T 
     * @param endpoint The endpoint of the remote peer (optional, can be nullptr)
     * @return IoResult<T> 
     */
    template <typename T>
    auto accept(MutableEndpointView endpoint) const -> IoResult<T> {
        ::sockaddr *addr = endpoint.data();
        ::socklen_t len = endpoint.bufsize();
        auto fd = ::accept(mFd, addr, &len);
        if (fd == Invalid) {
            return Err(SystemError::fromErrno());
        }
        return T(fd);
    }

    /**
     * @brief Accept a connection on the socket
     * 
     * @tparam T 
     * @tparam Endpoint must has MutableEndpoint concept like IPEndpoint
     * @return IoResult<std::pair<T, IPEndpoint> > 
     */
    template <typename T, MutableEndpoint Endpoint = IPEndpoint>
    auto accept() const -> IoResult<std::pair<T, Endpoint> > {
        Endpoint endpoint;
        auto fd = accept<T>(&endpoint);
        if (!fd) {
            return Err(fd.error());
        }
        return std::make_pair(T(std::move(*fd)), endpoint);
    }

    /**
     * @brief Get the local endpoint of the socket
     * 
     * @tparam T must has MutableEndpoint concept like IPEndpoint
     * @return IoResult<IPEndpoint> 
     */
    template <MutableEndpoint T = IPEndpoint>
    auto localEndpoint() const -> IoResult<T> {
        T endpoint;
        ::sockaddr *addr = reinterpret_cast<::sockaddr*>(endpoint.data());
        ::socklen_t len = endpoint.bufsize();
        if (::getsockname(mFd, addr, &len) < 0) {
            return Err(SystemError::fromErrno());
        }
        return endpoint;
    }

    /**
     * @brief Get the remote endpoint of the socket
     * 
     * @tparam T must has MutableEndpoint concept like IPEndpoint
     * @return IoResult<IPEndpoint> 
     */
    template <MutableEndpoint T = IPEndpoint>
    auto remoteEndpoint() const -> IoResult<T> {
        T endpoint;
        ::sockaddr *addr = reinterpret_cast<::sockaddr*>(endpoint.data());
        ::socklen_t len = endpoint.bufsize();
        if (::getpeername(mFd, addr, &len) < 0) {
            return Err(SystemError::fromErrno());
        }
        return endpoint;
    }

    /**
     * @brief Get the underlying socket descriptor
     * 
     * @return socket_t 
     */
    auto get() const noexcept -> socket_t {
        return mFd;
    }

    /**
     * @brief Allow comparison between sockets
     * 
     */
    auto operator <=>(const SocketView &) const = default;

    /**
     * @brief Check if the socket is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mFd != Invalid;
    }
    static constexpr socket_t Invalid = ILIAS_INVALID_SOCKET;
protected:
    socket_t mFd = Invalid;
};

/**
 * @brief RAII wrapper for a socket
 * 
 */
class Socket final : public SocketView {
public:
    /**
     * @brief Construct a new empty Socket object
     * 
     */
    Socket() = default;

    /**
     * @brief Construct a new Socket object (disabled)
     * 
     */
    Socket(const Socket &) = delete;

    /**
     * @brief Construct a new Socket object by moving from another Socket
     * 
     * @param other 
     */
    Socket(Socket &&other) : SocketView(std::exchange(other.mFd, Invalid)) { }

    /**
     * @brief Construct a new Socket object
     * 
     * @param family The address family
     * @param type The socket type
     * @param protocol The protocol
     */
    Socket(int family, int type, int protocol) {
        mFd = ::socket(family, type, protocol);
    }

    /**
     * @brief Destroy the Socket object
     * 
     */
    ~Socket() {
        close();
    }

    /**
     * @brief Construct a new Socket object by taking ownership of a socket descriptor
     * 
     * @param fd 
     */
    explicit Socket(socket_t fd) : SocketView(fd) { }

    /**
     * @brief Close the socket
     * 
     */
    auto close() -> void {
        reset();
    }

    /**
     * @brief Release the ownership of the socket
     * 
     * @param newSocket (default = InvalidSocket)
     * @return socket_t 
     */
    auto release(socket_t newSocket = Invalid) -> socket_t {
        auto old = mFd;
        mFd = newSocket;
        return old;
    }

    /**
     * @brief Close current socket and take ownership of newSocket
     * 
     * @param newSocket (default = InvalidSocket)
     */
    auto reset(socket_t newSocket = Invalid) -> void {
        if (mFd != Invalid) {
            if (ILIAS_CLOSE_SOCKET(mFd) != 0) {
                ILIAS_WARN("SocketView", "Failed to close socket {}", mFd);
            }
            mFd = newSocket;
        }
    }

    /**
     * @brief Accept a new connection on the socket
     * 
     * @tparam T 
     * @tparam Endpoint
     * @return IoResult<std::pair<T, IPEndpoint> > 
     */
    template <typename T = Socket, MutableEndpoint Endpoint = IPEndpoint>
    auto accept() -> IoResult<std::pair<T, Endpoint> > {
        return SocketView::accept<T, Endpoint>();
    }

    auto operator =(const Socket &) = delete;

    auto operator =(Socket &&other) -> Socket & {
        close();
        mFd = std::exchange(other.mFd, Invalid);
        return *this;
    }

    /**
     * @brief Create a new socket
     * 
     * @param family The address family
     * @param type The socket type
     * @param protocol The protocol
     * @return IoResult<Socket> 
     */
    static auto make(int family, int type, int protocol) -> IoResult<Socket> {
        auto sockfd = ::socket(family, type, protocol);
        if (sockfd != Invalid) {
            return Socket(sockfd);
        }
        return Err(SystemError::fromErrno());
    }
    
};

ILIAS_NS_END