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

#include <ilias/net/endpoint.hpp>
#include <ilias/net/sockopt.hpp>
#include <ilias/net/system.hpp>
#include <ilias/io/error.hpp> // IoResult
#include <ilias/buffer.hpp> // Buffer
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

// NOTE: in windows recv and send 's buffer argument is char type and linux is void type
// so we cast it to char type, char * will be casted to void * automatically in linux

/**
 * @brief A View of socket, witch hold operations
 * 
 */
class SocketView {
public:
    constexpr SocketView() = default;
    constexpr SocketView(socket_t fd) : mFd(fd) {}

    /**
     * @brief Recv num of bytes
     * 
     * @param buf 
     * @param flags 
     * @return IoResult<size_t> 
     */
    auto recv(MutableBuffer buf, int flags = 0) const -> IoResult<size_t> {
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
    auto send(Buffer buf, int flags = 0) const -> IoResult<size_t> {
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
    auto sendto(Buffer buf, int flags, EndpointView endpoint) const -> IoResult<size_t> {
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
    auto recvfrom(MutableBuffer buf, int flags, MutableEndpointView endpoint) const -> IoResult<size_t> {
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
    auto shutdown(Shutdown how = Shutdown::Both) const -> IoResult<void> {
        auto ret = ::shutdown(mFd, static_cast<int>(how));
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
     * @brief Set blocking mode for the socket
     * 
     * @param blocking 
     * @return IoResult<void> 
     */
    auto setBlocking(bool blocking) const -> IoResult<void> {

#if defined(_WIN32)
        ::u_long block = blocking ? 0 : 1;
        ::DWORD bytes = 0;
        if (::WSAIoctl(mFd, FIONBIO, &block, sizeof(block), nullptr, 0, &bytes, nullptr, nullptr) != 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
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
        return {};
#endif // _WIN32

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

    /**
     * @brief Check if the socket is valid
     * 
     * @return bool 
     */
    auto isValid() const -> bool {
        return mFd != invalid();
    }

    /**
     * @brief Get the family of the socket
     * 
     * @return IoResult<int> 
     */
    auto family() const -> IoResult<int> {

#if defined(_WIN32)
        ::WSAPROTOCOL_INFOW info;
        ::socklen_t len = sizeof(info);
        if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char *>(&info), &len) != 0) {
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
#endif // _WIN32

    }

    /**
     * @brief Get the type of the socket
     * 
     * @return IoResult<int> 
     */
    auto type() const -> IoResult<int> {

#if defined(_WIN32)
        ::WSAPROTOCOL_INFOW info;
        ::socklen_t len = sizeof(info);
        if (::getsockopt(mFd, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char *>(&info), &len) != 0) {
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
#endif // _WIN32

    }
    
    /**
     * @brief Get the error associated with the socket
     * 
     * @return IoResult<SystemError> 
     */
    auto error() const -> IoResult<SystemError> {
        error_t err = 0;
        socklen_t len = sizeof(err);
        ILIAS_TRYV(getOption(SOL_SOCKET, SO_ERROR, &err, &len));
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
        if (fd == invalid()) {
            return Err(SystemError::fromErrno());
        }
        return T{fd};
    }

    /**
     * @brief Duplicate the socket
     * 
     * @tparam T
     * @return IoResult<T> 
     */
    template <typename T>
    auto dup() const -> IoResult<T> {

#if defined(_WIN32)
        ::WSAPROTOCOL_INFOW info {};
        if (::WSADuplicateSocketW(mFd, ::GetCurrentProcessId(), &info) != 0) {
            return Err(SystemError::fromErrno());
        }
        auto fd = ::WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &info, 0, WSA_FLAG_OVERLAPPED);
#else
        auto fd = ::dup(mFd);
#endif // _WIN32

        if (fd == invalid()) {
            return Err(SystemError::fromErrno());
        }
        return T{fd};
    }

    /**
     * @brief Get the local endpoint of the socket
     * 
     * @tparam T must has MutableEndpoint concept like IPEndpoint
     * @return IoResult<T> 
     */
    template <MutableEndpoint T>
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
     * @return IoResult<T> 
     */
    template <MutableEndpoint T>
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
        return mFd != invalid();
    }

    /**
     * @brief Get the underlying socket descriptor
     * 
     * @return fd_t 
     */
    explicit operator fd_t() const noexcept {
        return fd_t(mFd);
    }

    /**
     * @brief Get invalid socket sentinel on the current platform
     * 
     * @return socket_t 
     */
    static constexpr auto invalid() noexcept -> socket_t {
        return ILIAS_INVALID_SOCKET;
    }    
protected:
    socket_t mFd = invalid();
};

/**
 * @brief RAII wrapper for a native socket
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
    Socket(Socket &&other) : SocketView(std::exchange(other.mFd, invalid())) { }

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
     * @return socket_t 
     */
    auto release() -> socket_t {
        return std::exchange(mFd, invalid());
    }

    /**
     * @brief Close current socket and take ownership of newSocket
     * 
     * @param newSocket (default = InvalidSocket)
     */
    auto reset(socket_t newSocket = invalid()) -> void {
        if (mFd != invalid()) {
            if (ILIAS_CLOSE_SOCKET(mFd) != 0) {
                ILIAS_WARN("SocketView", "Failed to close socket {}", mFd);
            }
        }
        mFd = newSocket;
    }

    /**
     * @brief Accept a new connection on the socket
     * 
     * @tparam T 
     * @tparam Endpoint
     * @param endpoint The endpoint of the remote peer (optional, can be nullptr)
     * @return IoResult<T> 
     */
    template <typename T = Socket>
    auto accept(MutableEndpointView endpoint) const -> IoResult<T> {
        return SocketView::accept<T>(endpoint);
    }

    /**
     * @brief Duplicate the socket
     * 
     * @tparam T 
     * @return IoResult<T> 
     */
    template <typename T = Socket>
    auto dup() const -> IoResult<T> {
        return SocketView::dup<T>();
    }

    /**
     * @brief Move assignment operator
     * 
     * @param other 
     * @return Socket& 
     */
    auto operator =(Socket &&other) -> Socket & {
        close();
        mFd = std::exchange(other.mFd, invalid());
        return *this;
    }

    // Operator
    auto operator =(const Socket &) = delete;
    auto operator <=>(const Socket &) const = default;

    /**
     * @brief Create a new socket
     * 
     * @param family The address family
     * @param type The socket type
     * @param protocol The protocol
     * @return IoResult<Socket> 
     */
    static auto make(int family, int type, int protocol) -> IoResult<Socket> {

#if defined(_WIN32)
        auto sockfd = ::WSASocketW(family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
#else // POSIX
        auto sockfd = ::socket(family, type | SOCK_CLOEXEC, protocol);
#endif // _WIN32
        if (sockfd != invalid()) {
            return Socket{sockfd};
        }
        return Err(SystemError::fromErrno());
    }

};

ILIAS_NS_END

// Formatter
#if !defined(ILIAS_NO_FORMATTER)
ILIAS_FORMATTER(ilias::SocketView) {
    auto format(const auto &sock, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", sock.get());
    }
};

ILIAS_FORMATTER(ilias::Socket) {
    auto format(const auto &sock, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", sock.get());
    }
};
#endif // ILIAS_NO_FORMATTER