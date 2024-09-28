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
     * @return Result<size_t> 
     */
    auto recv(std::span<std::byte> buf, int flags = 0) const -> Result<size_t> {
        auto ret = ::recv(mFd, reinterpret_cast<char*>(buf.data()), buf.size_bytes(), flags);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return ret;
    }

    /**
     * @brief Send num of bytes
     * 
     * @param buf 
     * @param flags 
     * @return Result<size_t> 
     */
    auto send(std::span<const std::byte> buf, int flags = 0) const -> Result<size_t> {
        auto ret = ::send(mFd, reinterpret_cast<const char*>(buf.data()), buf.size_bytes(), flags);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return ret;
    }

    /**
     * @brief Sendto num of bytes to target endpoint
     * 
     * @param buf 
     * @param flags 
     * @param endpoint 
     * @return Result<size_t> 
     */
    auto sendto(std::span<const std::byte> buf, int flags, const IPEndpoint *endpoint) const -> Result<size_t> {
        const ::sockaddr *addr = endpoint ? &endpoint->cast<::sockaddr>() : nullptr;
        const ::socklen_t addrLen = endpoint ? endpoint->length() : 0;
        auto ret = ::sendto(mFd, reinterpret_cast<const char*>(buf.data()), buf.size_bytes(), flags, addr, addrLen);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return ret;
    }

    /**
     * @brief Sendto num of bytes to target endpoint
     * 
     * @param buf 
     * @param flags 
     * @param endpoint 
     * @return Result<size_t> 
     */
    auto sendto(std::span<const std::byte> buf, int flags, const IPEndpoint &endpoint) const -> Result<size_t> {
        return sendto(buf, flags, &endpoint);
    }

    /**
     * @brief Recvfrom num of bytes from , it can get the remote endpoint 
     * 
     * @param buf 
     * @param flags 
     * @param endpoint 
     * @return Result<size_t> 
     */
    auto recvfrom(std::span<std::byte> buf, int flags, IPEndpoint *endpoint) const -> Result<size_t> {
        ::sockaddr_storage addr {};
        ::socklen_t size = sizeof(addr);
        auto ret = ::recvfrom(mFd, reinterpret_cast<char*>(buf.data()), buf.size_bytes(), flags, reinterpret_cast<::sockaddr*>(&addr), &size);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        if (endpoint) {
            *endpoint = IPEndpoint::fromRaw(&addr, size).value();
        }
        return ret;
    }

    /**
     * @brief Recvfrom num of bytes from , it can get the remote endpoint 
     * 
     * @param buf 
     * @param flags 
     * @param endpoint 
     * @return Result<size_t> 
     */
    auto recvfrom(std::span<std::byte> buf, int flags, IPEndpoint &endpoint) const -> Result<size_t> {
        return recvfrom(buf, flags, &endpoint);
    }

    /**
     * @brief Start listening on the socket
     * 
     * @param backlog 
     * @return Result<void> 
     */
    auto listen(int backlog = 0) const -> Result<void> {
        auto ret = ::listen(mFd, backlog);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Shutdown the socket by how, default shutdown buth read and write
     * 
     * @param how 
     * @return Result<void> 
     */
    auto shutdown(int how = Shutdown::Both) const -> Result<void> {
        auto ret = ::shutdown(mFd, how);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Connect to the specified endpoint
     * 
     * @param endpoint 
     * @return Result<void> 
     */
    auto connect(const IPEndpoint &endpoint) const -> Result<void> {
        auto ret = ::connect(mFd, &endpoint.cast<::sockaddr>(), endpoint.length());
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Bind the socket to the specified endpoint
     * 
     * @param endpoint 
     * @return Result<void> 
     */
    auto bind(const IPEndpoint &endpoint) const -> Result<void> {
        auto ret = ::bind(mFd, &endpoint.cast<::sockaddr>(), endpoint.length());
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Set blocking mode for the socket
     * 
     * @param blocking 
     * @return Result<void> 
     */
    auto setBlocking(bool blocking) const -> Result<void> {

#if defined(_WIN32)
    u_long block = blocking ? 0 : 1;
    return ioctl(FIONBIO, &block);
#else
    int flags = ::fcntl(mFd, F_GETFL, 0);
    if (flags < 0) {
        return Unexpected(SystemError::fromErrno());
    }
    if (blocking) {
        flags &= ~O_NONBLOCK;
    }
    else {
        flags |= O_NONBLOCK;
    }
    if (::fcntl(mFd, F_SETFL, flags) < 0) {
        return Unexpected(SystemError::fromErrno());
    }
    return Result<void>();
#endif

    }

    /**
     * @brief Set reuse address option for the socket
     * 
     * @param reuse 
     * @return Result<void> 
     */
    auto setReuseAddr(bool reuse) const -> Result<void> {
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
     * @return Result<void> 
     */
    auto setOption(int level, int optname, const void *optval, socklen_t optlen) const -> Result<void> {
        auto ret = ::setsockopt(mFd, level, optname, static_cast<const char*>(optval), optlen);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Set socket option by option object
     * 
     * @tparam T 
     * @param opt 
     * @return Result<void> 
     */
    template <SetSockOption T>
    auto setOption(const T &opt) const -> Result<void> {
        return opt.setopt(mFd);
    }

    /**
     * @brief Get socket option
     * 
     * @param level 
     * @param optname 
     * @param optval 
     * @param optlen 
     * @return Result<void> 
     */
    auto getOption(int level, int optname, void *optval, socklen_t *optlen) const -> Result<void> {
        auto ret = ::getsockopt(mFd, level, optname, static_cast<char*>(optval), optlen);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return {};
    }

    /**
     * @brief Get the socket option by option object type
     * 
     * @tparam T 
     * @return Result<T> 
     */
    template <GetSockOption T>
    auto getOption() const -> Result<T> {
        return T::getopt(mFd);
    }

#if defined(_WIN32)
    /**
     * @brief Perform IO control operation on the socket
     * 
     * @param cmd 
     * @param args 
     * @return Result<void> 
     */
    auto ioctl(long cmd, u_long *args) const -> Result<void> {
        auto ret = ::ioctlsocket(mFd, cmd, args);
        if (ret < 0) {
            return Unexpected(SystemError::fromErrno());
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
     * @return Result<int> 
     */
    auto family() const -> Result<int> {

#if defined(_WIN32)
        ::WSAPROTOCOL_INFO info;
        ::socklen_t len = sizeof(info);
        if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &len) != 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return info.iAddressFamily;
#else
        int family = 0;
        ::socklen_t len = sizeof(family);
        if (::getsockopt(mFd, SOL_SOCKET, SO_DOMAIN, &family, &len) != 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return family;
#endif

    }

    /**
     * @brief Get the type of the socket
     * 
     * @return Result<int> 
     */
    auto type() const -> Result<int> {

#if defined(_WIN32)
        ::WSAPROTOCOL_INFO info;
        ::socklen_t len = sizeof(info);
        if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &len) != 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return info.iSocketType;
#else
        int type = 0;
        ::socklen_t len = sizeof(type);
        if (::getsockopt(mFd, SOL_SOCKET, SO_TYPE, &type, &len) != 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return type;
#endif

    }
    
    /**
     * @brief Get the error associated with the socket
     * 
     * @return Result<Error> 
     */
    auto error() const -> Result<Error> {
        error_t err = 0;
        socklen_t len = sizeof(err);
        if (auto val = getOption(SOL_SOCKET, SO_ERROR, &err, &len); !val) {
            return Unexpected(val.error());
        }
        return SystemError(err);
    }

    /**
     * @brief Accept a connection on the socket
     * 
     * @tparam T 
     * @return Result<std::pair<T, IPEndpoint> > 
     */
    template <typename T>
    auto accept() const -> Result<std::pair<T, IPEndpoint> > {
        ::sockaddr_storage addr;
        ::socklen_t len = sizeof(addr);
        auto fd = ::accept(mFd, reinterpret_cast<sockaddr*>(&addr), &len);
        if (fd == Invalid) {
            return Unexpected(SystemError::fromErrno());
        }
        return std::make_pair(T(fd), IPEndpoint::fromRaw(&addr, len).value());
    }

    /**
     * @brief Get the local endpoint of the socket
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint> {
        ::sockaddr_storage addr;
        ::socklen_t len = sizeof(addr);
        if (::getsockname(mFd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return IPEndpoint::fromRaw(&addr, len).value();
    }

    /**
     * @brief Get the remote endpoint of the socket
     * 
     * @return Result<IPEndpoint> 
     */
    auto remoteEndpoint() const -> Result<IPEndpoint> {
        ::sockaddr_storage addr;
        ::socklen_t len = sizeof(addr);
        if (::getpeername(mFd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
            return Unexpected(SystemError::fromErrno());
        }
        return IPEndpoint::fromRaw(&addr, len).value();
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
    Socket(Socket &&other) : SocketView(other.mFd) {
        other.mFd = Invalid;
    }

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
     * @return Result<std::pair<T, IPEndpoint> > 
     */
    template <typename T = Socket>
    auto accept() -> Result<std::pair<T, IPEndpoint> > {
        return SocketView::accept<T>();
    }

    auto operator =(const Socket &) = delete;

    auto operator =(Socket &&other) -> Socket & {
        close();
        mFd = other.mFd;
        other.mFd = Invalid;
        return *this;
    }
};

ILIAS_NS_END