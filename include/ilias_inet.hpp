#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include <charconv>
#include <cstring>
#include <string>
#include <span>

// --- Platform
#if  defined(_WIN32)
    #define ILIAS_INVALID_SOCKET INVALID_SOCKET
    #define ILIAS_ERRNO     ::WSAGetLastError()
    #define ILIAS_H_ERRNO   ::WSAGetLastError()
    #define ILIAS_ERROR_T   ::DWORD
    #define ILIAS_SOCKET_T  ::SOCKET
    #define ILIAS_FD_T      ::HANDLE
    #define ILIAS_SSIZE_T     int
    #define ILIAS_BYTE_T      char
    #define ILIAS_CLOSE(s)  ::closesocket(s)
    #define ILIAS_POLL      ::WSAPoll
    #define ILIAS_SHUT_RD     SD_RECEIVE
    #define ILIAS_SHUT_WR     SD_SEND
    #define ILIAS_SHUT_RDWR   SD_BOTH

    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <MSWSock.h>

    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib")
    #endif
#elif defined(__linux)
    #define ILIAS_INVALID_SOCKET -1
    #define ILIAS_ERRNO     (errno)
    #define ILIAS_H_ERRNO   (h_errno)
    #define ILIAS_ERROR_T     int
    #define ILIAS_SOCKET_T    int
    #define ILIAS_FD_T        int
    #define ILIAS_SSIZE_T   ::ssize_t
    #define ILIAS_BYTE_T      void
    #define ILIAS_CLOSE(s)  ::close(s)
    #define ILIAS_POLL      ::poll
    #define ILIAS_SHUT_RD     SHUT_RD
    #define ILIAS_SHUT_WR     SHUT_WR
    #define ILIAS_SHUT_RDWR   SHUT_RDWR

    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <sys/epoll.h>
    #include <sys/poll.h>
    #include <arpa/inet.h>
    #include <errno.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
#endif

ILIAS_NS_BEGIN

// --- Platform 
using socket_t = ILIAS_SOCKET_T;
using ssize_t  = ILIAS_SSIZE_T;
using byte_t   = ILIAS_BYTE_T;
using error_t  = ILIAS_ERROR_T;
using fd_t     = ILIAS_FD_T;

// --- Enums
enum PollEvent : uint32_t {
    In  = POLLIN,
    Out = POLLOUT,
    Err = POLLERR,
    Hup = POLLHUP,
};

enum Shutdown : int {
    Read  = ILIAS_SHUT_RD,
    Write = ILIAS_SHUT_WR,
    Both  = ILIAS_SHUT_RDWR,
};

/**
 * @brief Wrapper for v4
 * 
 */
class IPAddress4 : public ::in_addr {
public:
    IPAddress4();
    IPAddress4(::in_addr addr4);
    IPAddress4(const IPAddress4 &other) = default;

    /**
     * @brief Convert to human readable string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    /**
     * @brief Convert to uint32 host order
     * 
     * @return uint32_t 
     */
    auto toUint32() const -> uint32_t;
    /**
     * @brief Convert to uint32 network order
     * 
     * @return uint32_t 
     */
    auto toUint32NetworkOrder() const -> uint32_t;
    /**
     * @brief Get the readonly span of the data
     * 
     * @tparam T 
     * @return std::span<const T> 
     */
    template <typename T = uint8_t>
    auto span() const -> std::span<const T, sizeof(::in_addr) / sizeof(T)>;

    /**
     * @brief Check current address is any
     * 
     * @return true 
     * @return false 
     */
    auto isAny() const -> bool;
    /**
     * @brief Check current address is none
     * 
     * @return true 
     * @return false 
     */
    auto isNone() const -> bool;
    /**
     * @brief Check current address is loopback
     * 
     * @return true 
     * @return false 
     */
    auto isLoopback() const -> bool;
    /**
     * @brief Check current address is broadcast
     * 
     * @return true 
     * @return false 
     */
    auto isBroadcast() const -> bool;
    /**
     * @brief Check current address is multicast
     *
     * @return true 
     * @return false 
     */
    auto isMulticast() const -> bool;

    auto operator ==(const IPAddress4 &) const -> bool;
    auto operator !=(const IPAddress4 &) const -> bool;

    /**
     * @brief Get any ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto any() -> IPAddress4;
    /**
     * @brief Get none ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto none() -> IPAddress4;
    /**
     * @brief Get loop ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto loopback() -> IPAddress4;
    /**
     * @brief Get broadcast ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto broadcast() -> IPAddress4;
    /**
     * @brief Copy data from buffer to create ipv4 address
     * 
     * @param mem pointer to network-format ipv4 address
     * @param n must be sizeof(::in_addr)
     * @return IPAddress4 
     */
    static auto fromRaw(const void *mem, size_t n) -> IPAddress4;
    /**
     * @brief Parse string and create ipv4 address
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromString(const char *value) -> IPAddress4;
    /**
     * @brief Parse the hostname and get ipv4 address
     * 
     * @param hostname 
     * @return IPAddress4 
     */
    static auto fromHostname(const char *hostname) -> IPAddress4;
    /**
     * @brief Create ipv4 address from uint32, host order
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromUint32(uint32_t value)  -> IPAddress4;
    /**
     * @brief Create ipv4 address from uint32, network order
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromUint32NetworkOrder(uint32_t value) -> IPAddress4;
};

/**
 * @brief Wrapper for ipv6 address
 * 
 */
class IPAddress6 : public ::in6_addr {
public:
    IPAddress6();
    IPAddress6(::in6_addr addr6);
    IPAddress6(const IPAddress6 &other) = default;

    /**
     * @brief Convert ipv6 address to human readable string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    
    /**
     * @brief Get the readonly span of the data
     * 
     * @tparam T 
     * @return std::span<const T> 
     */
    template <typename T = uint8_t>
    auto span() const -> std::span<const T, sizeof(::in6_addr) / sizeof(T)>;

    /**
     * @brief Check this ipv6 address is any
     * 
     * @return true 
     * @return false 
     */
    auto isAny() const -> bool;
    /**
     * @brief Check this ipv6 address is none
     * 
     * @return true 
     * @return false 
     */
    auto isNone() const -> bool;
    /**
     * @brief Check this ipv6 address is loopback
     * 
     * @return true 
     * @return false 
     */
    auto isLoopback() const -> bool;
    /**
     * @brief Check this ipv6 address is multicast
     * 
     * @return true 
     * @return false 
     */
    auto isMulticast() const -> bool;

    auto operator ==(const IPAddress6 &other) const -> bool;
    auto operator !=(const IPAddress6 &other) const -> bool;

    /**
     * @brief Get the any ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto any() -> IPAddress6;
    /**
     * @brief Get the none ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto none() -> IPAddress6;
    /**
     * @brief Get the loop ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto loopback() -> IPAddress6;
    /**
     * @brief Parse the ipv6 address from string
     * 
     * @param value 
     * @return IPAddress6 
     */
    static auto fromString(const char *value) -> IPAddress6;
    /**
     * @brief Parse the ipv6 address from hostname
     * 
     * @param hostname 
     * @return IPAddress6 
     */
    static auto fromHostname(const char *hostname) -> IPAddress6;
};

/**
 * @brief Abstraction of v4, v6 
 * 
 */
class IPAddress {
public:
    IPAddress();
    IPAddress(::in_addr addr4);
    IPAddress(::in6_addr addr6);
    IPAddress(const char *str);
    IPAddress(const IPAddress &) = default;

    /**
     * @brief Convert current address to human readable string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    /**
     * @brief Get the family of this address, like AF_INET or AF_INET6
     * 
     * @return int 
     */
    auto family() const -> int;
    /**
     * @brief Get the length of this address, like 4 or 16
     * 
     * @return int 
     */
    auto length() const -> int;
    /**
     * @brief Check this address is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;

    /**
     * @brief Get the raw pointer of stored data
     * 
     * @return const void* 
     */
    auto data() const -> const void *;
    /**
     * @brief Get the raw pointer of stored data
     * 
     * @return void* 
     */
    auto data() -> void *;
    /**
     * @brief Cast to the referenced type
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto data() const -> const T &;
    /**
     * @brief Cast to the referenced type
     * 
     * @tparam T 
     * @return T& 
     */
    template <typename T>
    auto data() -> T &;

    /**
     * @brief Get the span of the contained data
     * 
     * @tparam T 
     * @return std::span<const T> 
     */
    template <typename T = uint8_t>
    auto span() const -> std::span<const T>;

    /**
     * @brief Compare the endpoint, check it is same
     * 
     * @param rhs 
     * @return true 
     * @return false 
     */
    auto compare(const IPAddress &rhs) const -> bool;
    auto operator ==(const IPAddress &rhs) const -> bool;
    auto operator !=(const IPAddress &rhs) const -> bool;

    /**
     * @brief Parse the ip string
     * 
     * @param str 
     * @return IPAddress 
     */
    static auto fromString(const char *str) -> IPAddress;
    /**
     * @brief Parse the hostname 
     * 
     * @param hostname 
     * @return IPAddress 
     */
    static auto fromHostname(const char *hostname) -> IPAddress;
    /**
     * @brief Copy network-format ip address from buffer
     * 
     * @param data The pointer to the buffer
     * @param size The size of the buffer (must be 4 or 16)
     * @return IPAddress 
     */
    static auto fromRaw(const void *data, size_t size) -> IPAddress;
    template <typename T>
    static auto fromRaw(const T &data) -> IPAddress;
private:
    union {
        ::in_addr v4;
        ::in6_addr v6;
    } mStorage;
    enum {
        None = AF_UNSPEC,
        V4 = AF_INET,
        V6 = AF_INET6,
        Unix = AF_UNIX,
    } mFamily = None;
};

/**
 * @brief Abstraction of sockaddr_storage (address + port)
 * 
 */
class IPEndpoint {
public:
    IPEndpoint();
    IPEndpoint(const char *str);
    IPEndpoint(const std::string &str);
    IPEndpoint(std::string_view str);
    IPEndpoint(::sockaddr_in addr4);
    IPEndpoint(::sockaddr_in6 addr6);
    IPEndpoint(::sockaddr_storage storage);
    IPEndpoint(const IPAddress &address, uint16_t port);
    IPEndpoint(const IPEndpoint &) = default;

    /**
     * @brief Cast to human readable string (ip:port)
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    /**
     * @brief Get the ipv4 address of the endpoint
     * 
     * @return IPAddress4 
     */
    auto address4() const -> IPAddress4;
    /**
     * @brief Get the ipv6 address of the endpoint
     * 
     * @return IPAddress6 
     */
    auto address6() const -> IPAddress6;
    /**
     * @brief Get the address of the endpoint
     * 
     * @return IPAddress 
     */
    auto address() const -> IPAddress;
    /**
     * @brief Get the port of the endpoint
     * 
     * @return uint16_t 
     */
    auto port() const -> uint16_t;
    /**
     * @brief Get the family of the endpoint
     * 
     * @return int 
     */
    auto family() const -> int;
    /**
     * @brief Get the length of the endpoint
     * 
     * @return int 
     */
    auto length() const -> int;
    /**
     * @brief Check the endpoint is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;
    /**
     * @brief Get the data of the current endpoint
     * 
     * @return const void* 
     */
    auto data() const -> const void *;
    /**
     * @brief Cast to endpoint data to
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto data() const -> const T &;

    /**
     * @brief Get the data of the current endpoint
     * 
     * @return void* 
     */
    auto data() -> void *;
    /**
     * @brief Cast to endpoint data to
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto data() -> T &;

    /**
     * @brief Compare the endpoint is same
     * 
     * @param rhs 
     * @return true 
     * @return false 
     */
    auto compare(const IPEndpoint &rhs) const -> bool;
    auto operator ==(const IPEndpoint &rhs) const -> bool;
    auto operator !=(const IPEndpoint &rhs) const -> bool;

    /**
     * @brief Parse string to endpoint
     * 
     * @param str 
     * @return IPEndpoint 
     */
    static auto fromString(std::string_view str) -> IPEndpoint;
    /**
     * @brief Copy endpoint from network-format data
     * 
     * @param data The pointer to the data
     * @param size The size of the data, must be sizeof(::sockaddr_in) or sizeof(::sockaddr_in6)
     * @return IPEndpoint 
     */
    static auto fromRaw(const void *data, size_t size) -> IPEndpoint;
    template <typename T>
    static auto fromRaw(const T &data) -> IPEndpoint;
private:
    ::sockaddr_storage mAddr { };
};

/**
 * @brief RAII Guard for windows socket initialization
 * 
 */
class SockInitializer {
public:
    SockInitializer();
    SockInitializer(const SockInitializer &) = delete;
    ~SockInitializer();

    /**
     * @brief Check if we are initialized
     * 
     * @return true 
     * @return false 
     */
    auto isInitalized() const noexcept -> bool { return mInited.has_value(); }
    /**
     * @brief Do initialization
     * 
     * @return Result<void> 
     */
    static auto initialize() -> Result<void>;
    static auto uninitialize() -> Result<void>;
private:
    Result<void> mInited { initialize() };
};

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
class Socket : public SocketView {
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

/**
 * @brief A Error category from (system os, like win32, linux, etc)
 * 
 */
class SystemCategory final : public ErrorCategory {
public:
    auto name() const -> std::string_view override;
    auto message(uint32_t code) const -> std::string override;
    auto equivalent(uint32_t self, const Error &other) const -> bool override;

    static auto instance() -> SystemCategory &;
    static auto translate(error_t sysErr) -> Error::Code;
};

// --- IPAddress4 Impl
inline IPAddress4::IPAddress4() { }
inline IPAddress4::IPAddress4(::in_addr addr) : ::in_addr(addr) { }

inline auto IPAddress4::toString() const -> std::string {
    return ::inet_ntoa(*this);
}
inline auto IPAddress4::toUint32() const -> uint32_t {
    return ::ntohl(toUint32NetworkOrder());
}
inline auto IPAddress4::toUint32NetworkOrder() const -> uint32_t {
    return reinterpret_cast<const uint32_t&>(*this);
}
template <typename T>
inline auto IPAddress4::span() const -> std::span<const T, sizeof(::in_addr) / sizeof(T)> {
    static_assert(sizeof(::in_addr) % sizeof(T) == 0, "sizeof mismatch");
    constexpr auto n = sizeof(::in_addr) / sizeof(T);
    auto addr = reinterpret_cast<const T*>(this);
    return std::span<const T, n>(addr, n);
}

inline auto IPAddress4::isAny() const -> bool {
    return toUint32() == INADDR_ANY;
}
inline auto IPAddress4::isNone() const -> bool {
    return toUint32() == INADDR_NONE;
}
inline auto IPAddress4::isLoopback() const -> bool {
    return toUint32() == INADDR_LOOPBACK;
}
inline auto IPAddress4::isBroadcast() const -> bool {
    return toUint32() == INADDR_BROADCAST;
}
inline auto IPAddress4::isMulticast() const -> bool {
    return IN_MULTICAST(toUint32());
}
inline auto IPAddress4::operator ==(const IPAddress4 &other) const -> bool {
    return toUint32NetworkOrder() == other.toUint32NetworkOrder();
}
inline auto IPAddress4::operator !=(const IPAddress4 &other) const -> bool {
    return toUint32NetworkOrder() != other.toUint32NetworkOrder();
}

inline auto IPAddress4::any() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_ANY);
}
inline auto IPAddress4::loopback() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_LOOPBACK);   
}
inline auto IPAddress4::broadcast() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_BROADCAST);
}
inline auto IPAddress4::none() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_NONE);
}
inline auto IPAddress4::fromRaw(const void *raw, size_t size) -> IPAddress4 {
    ILIAS_ASSERT(size == sizeof(::in_addr));
    return *static_cast<const ::in_addr*>(raw);
}
inline auto IPAddress4::fromString(const char *address) -> IPAddress4 {
    IPAddress4 addr;
    if (::inet_pton(AF_INET, address, &addr) != 1) {
        return IPAddress4::none();
    }
    return addr;
}
inline auto IPAddress4::fromHostname(const char *hostnamne) -> IPAddress4 {
    auto ent = ::gethostbyname(hostnamne);
    if (!ent || ent->h_addrtype != AF_INET) {
        return IPAddress4::none();
    }
    return *reinterpret_cast<const IPAddress4*>(ent->h_addr_list[0]);
}
inline auto IPAddress4::fromUint32(uint32_t uint32) -> IPAddress4 {
    static_assert(sizeof(uint32_t) == sizeof(::in_addr), "sizeof mismatch");
    uint32 = ::htonl(uint32);
    return reinterpret_cast<::in_addr&>(uint32);
}
inline auto IPAddress4::fromUint32NetworkOrder(uint32_t uint32) -> IPAddress4 {
    return reinterpret_cast<::in_addr&>(uint32);
}

// --- IPAddress6 Impl
inline IPAddress6::IPAddress6() { }
inline IPAddress6::IPAddress6(::in6_addr addr) : ::in6_addr(addr) { }

inline auto IPAddress6::toString() const -> std::string {
    char buf[INET6_ADDRSTRLEN] {0};
    ::inet_ntop(AF_INET6, this, buf, sizeof(buf));
    return buf;
}
template <typename T>
inline auto IPAddress6::span() const -> std::span<const T, sizeof(::in6_addr) / sizeof(T)> {
    static_assert(sizeof(::in6_addr) % sizeof(T) == 0, "sizeof mismatch");
    constexpr auto n = sizeof(::in6_addr) / sizeof(T);
    auto addr = reinterpret_cast<const T*>(this);
    return std::span<const T, n>(addr, n);
}
inline auto IPAddress6::isAny() const -> bool {
    return IN6_IS_ADDR_UNSPECIFIED(this);
}
inline auto IPAddress6::isNone() const -> bool {
    return IN6_IS_ADDR_UNSPECIFIED(this);
}
inline auto IPAddress6::isLoopback() const -> bool {
    return IN6_IS_ADDR_LOOPBACK(this);
}
inline auto IPAddress6::isMulticast() const -> bool {
    return IN6_IS_ADDR_MULTICAST(this);
}
inline auto IPAddress6::operator ==(const IPAddress6 &addr) const -> bool {
    return IN6_ARE_ADDR_EQUAL(this, &addr);
}
inline auto IPAddress6::operator !=(const IPAddress6 &addr) const -> bool {
    return !IN6_ARE_ADDR_EQUAL(this, &addr);
}

inline auto IPAddress6::any() -> IPAddress6 {
    return ::in6_addr IN6ADDR_ANY_INIT;
}
inline auto IPAddress6::none() -> IPAddress6 {
    return ::in6_addr IN6ADDR_ANY_INIT;
}
inline auto IPAddress6::loopback() -> IPAddress6 {
    return ::in6_addr IN6ADDR_LOOPBACK_INIT;
}
inline auto IPAddress6::fromString(const char *str) -> IPAddress6 {
    IPAddress6 addr;
    if (::inet_pton(AF_INET6, str, &addr) != 1) {
        return IPAddress6::any();
    }
    return addr;
}

// --- IPAddress Impl
inline IPAddress::IPAddress() { }
inline IPAddress::IPAddress(::in_addr addr) : mFamily(V4) { mStorage.v4 = addr; }
inline IPAddress::IPAddress(::in6_addr addr) : mFamily(V6) { mStorage.v6 = addr; }
inline IPAddress::IPAddress(const char *addr) {
    // IPV6 contains ':'
    mFamily = ::strchr(addr, ':') ? V6 : V4;
    if (::inet_pton(mFamily, addr, &mStorage) != 1) {
        mFamily = None;
    }
}

inline auto IPAddress::isValid() const -> bool {
    return mFamily != None;
}
inline auto IPAddress::family() const -> int {
    return mFamily;
}
inline auto IPAddress::length() const -> int {
    switch (mFamily) {
        case V4:  return sizeof(::in_addr);
        case V6:  return sizeof(::in6_addr);
        default:  return 0;
    }
}
inline auto IPAddress::toString() const -> std::string {
    if (!isValid()) {
        return std::string();
    }
    char buf[INET6_ADDRSTRLEN] {0};
    ::inet_ntop(family(), &mStorage, buf, sizeof(buf));
    return buf;
}

template <typename T>
inline auto IPAddress::data() const -> const T & {
    return *reinterpret_cast<const T *>(&mStorage);
}
inline auto IPAddress::data() const -> const void * {
    return &mStorage;
}
inline auto IPAddress::data() -> void * {
    return &mStorage;
}

template <typename T>
inline auto IPAddress::span() const -> std::span<const T> {
    switch (mFamily) {
        case V4: return data<IPAddress4>().span<T>();
        case V6: return data<IPAddress6>().span<T>();
        default: return {};
    }
}

inline auto IPAddress::compare(const IPAddress &other) const -> bool {
    if (family() != other.family()) {
        return false;
    }
    switch (mFamily) {
        case V4:  return data<IPAddress4>() == other.data<IPAddress4>();
        case V6:  return data<IPAddress6>() == other.data<IPAddress6>();
        default:  return true; //< All are invalid address
    }
}
inline auto IPAddress::operator ==(const IPAddress &other) const -> bool {
    return compare(other);
}
inline auto IPAddress::operator !=(const IPAddress &other) const -> bool {
    return !compare(other);
}

inline auto IPAddress::fromString(const char *str) -> IPAddress {
    return IPAddress(str);
}
inline auto IPAddress::fromHostname(const char *hostname) -> IPAddress {
    auto ent = ::gethostbyname(hostname);
    if (!ent) {
        return IPAddress();
    }
    return IPAddress::fromRaw(ent->h_addr_list[0], ent->h_length);
}
inline auto IPAddress::fromRaw(const void *raw, size_t len) -> IPAddress {
    switch (len) {
        case sizeof(::in_addr): {
            return IPAddress(*static_cast<const ::in_addr *>(raw));
        }
        case sizeof(::in6_addr): {
            return IPAddress(*static_cast<const ::in6_addr *>(raw));
        }
        default: {
            return IPAddress();
        }
    }
}
template <typename T>
inline auto IPAddress::fromRaw(const T &data) -> IPAddress {
    static_assert(sizeof(T) == sizeof(::in_addr) || sizeof(T) == sizeof(::in6_addr), "Invalid size");
    return IPAddress::fromRaw(&data, sizeof(T));
}

// --- IPEndpoint Impl
inline IPEndpoint::IPEndpoint() { }
inline IPEndpoint::IPEndpoint(const char *str) : IPEndpoint(fromString(str)) { }
inline IPEndpoint::IPEndpoint(std::string_view str) : IPEndpoint(fromString(str)) { }
inline IPEndpoint::IPEndpoint(const std::string &str) : IPEndpoint(fromString(str)) { }
inline IPEndpoint::IPEndpoint(::sockaddr_in addr4) { ::memcpy(&mAddr, &addr4, sizeof(addr4)); }
inline IPEndpoint::IPEndpoint(::sockaddr_in6 addr6) { ::memcpy(&mAddr, &addr6, sizeof(addr6)); }
inline IPEndpoint::IPEndpoint(::sockaddr_storage addr) : mAddr(addr) { }
inline IPEndpoint::IPEndpoint(const IPAddress &addr, uint16_t port) {
    mAddr.ss_family = addr.family();
    switch (mAddr.ss_family) {
        case AF_INET: {
            auto &sockaddr = data<::sockaddr_in>();
            sockaddr.sin_port = ::htons(port);
            sockaddr.sin_addr = addr.data<::in_addr>();
            break;
        }
        case AF_INET6: {
            auto &sockaddr = data<::sockaddr_in6>();
            sockaddr.sin6_port = ::htons(port);
            sockaddr.sin6_addr = addr.data<::in6_addr>();
            break;
        }
    }
}

inline auto IPEndpoint::isValid() const -> bool {
    return mAddr.ss_family != 0;
}
inline auto IPEndpoint::family() const -> int {
    return mAddr.ss_family;
}
inline auto IPEndpoint::length() const -> int {
    switch (mAddr.ss_family) {
        case AF_INET: return sizeof(::sockaddr_in);
        case AF_INET6: return sizeof(::sockaddr_in6);
        default: return 0;
    }
}
inline auto IPEndpoint::port() const -> uint16_t {
    switch (mAddr.ss_family) {
        case AF_INET: 
            return ::ntohs(data<::sockaddr_in>().sin_port);
        case AF_INET6: 
            return ::ntohs(data<::sockaddr_in6>().sin6_port);
        default : return 0;
    }
}
inline auto IPEndpoint::address() const -> IPAddress {
    switch (mAddr.ss_family) {
        case AF_INET: 
            return IPAddress::fromRaw(data<::sockaddr_in>().sin_addr);
        case AF_INET6: 
            return IPAddress::fromRaw(data<::sockaddr_in6>().sin6_addr);
        default: return IPAddress();
    }
}
inline auto IPEndpoint::address4() const -> IPAddress4 {
    ILIAS_ASSERT(mAddr.ss_family == AF_INET);
    return IPAddress4(data<::sockaddr_in>().sin_addr);
}
inline auto IPEndpoint::address6() const -> IPAddress6 {
    ILIAS_ASSERT(mAddr.ss_family == AF_INET6);
    return IPAddress6(data<::sockaddr_in6>().sin6_addr);
}
inline auto IPEndpoint::toString() const -> std::string {
    if (!isValid()) {
        return std::string();
    }
    if (family() == AF_INET6) {
        return '[' + address().toString() + ']' + ':' + std::to_string(port());
    }
    return address().toString() + ':' + std::to_string(port());
}
inline auto IPEndpoint::data() const -> const void * {
    return &mAddr;
}
template <typename T>
inline auto IPEndpoint::data() const -> const T & {
    return *reinterpret_cast<const T*>(&mAddr);
}
template <typename T>
inline auto IPEndpoint::data() -> T & {
    return *reinterpret_cast<T*>(&mAddr);
}

inline auto IPEndpoint::compare(const IPEndpoint &other) const -> bool {
    return family() == other.family() && address() == other.address() && port() == other.port();
}
inline auto IPEndpoint::operator ==(const IPEndpoint &other) const -> bool {
    return compare(other);
}
inline auto IPEndpoint::operator !=(const IPEndpoint &other) const -> bool {
    return !compare(other);
}

inline auto IPEndpoint::fromString(std::string_view str) -> IPEndpoint {
    std::string buffer(str);

    // Split to addr and port
    auto pos = buffer.find(':');
    if (pos == buffer.npos || pos == 0) {
        return IPEndpoint();
    }
    buffer[pos] = '\0';
    
    int port = 0;
    if (::sscanf(buffer.c_str() + pos + 1, "%d", &port) != 1) {
        return IPEndpoint();
    }
    IPAddress addr;
    if (buffer.front() == '[' && buffer[pos - 1] == ']') {
        buffer[pos - 1] = '\0';
        addr = IPAddress::fromString(buffer.c_str() + 1);
    }
    else {
        addr = IPAddress::fromString(buffer.c_str());
    }
    return IPEndpoint(addr, port);
}
inline auto IPEndpoint::fromRaw(const void *raw, size_t len) -> IPEndpoint {
    switch (len) {
        case sizeof(::sockaddr_in):
            return IPEndpoint(*static_cast<const ::sockaddr_in*>(raw));
        case sizeof(::sockaddr_in6):
            return IPEndpoint(*static_cast<const ::sockaddr_in6*>(raw));
        case sizeof(::sockaddr_storage):
            return IPEndpoint(*static_cast<const ::sockaddr_storage*>(raw));
        default:
            return IPEndpoint();
    }
}
template <typename T>
inline auto IPEndpoint::fromRaw(const T &raw) -> IPEndpoint {
    static_assert(sizeof(T) == sizeof(::sockaddr_in) || 
                  sizeof(T) == sizeof(::sockaddr_in6) ||
                  sizeof(T) == sizeof(::sockaddr_storage), 
                  "Invalid raw type"
    );
    return fromRaw(&raw, sizeof(T));
}

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

// -- Network order / Host
inline auto ToNetworkOrder(uint16_t v) -> uint16_t {
    return ::htons(v);
}
inline auto ToNetworkOrder(uint32_t v) -> uint32_t {
    return ::htonl(v);
}
inline auto ToHostOrder(uint16_t v) -> uint16_t {
    return ::ntohs(v);
}
inline auto ToHostOrder(uint32_t v) -> uint32_t {
    return ::ntohl(v);
}

// --- Init spec
inline SockInitializer::SockInitializer() {

}
inline SockInitializer::~SockInitializer() {
    if (mInited) {
        uninitialize();
    }
}

inline auto SockInitializer::initialize() -> Result<void> {

#if defined(_WIN32)
    ::WSADATA data { };
    if (::WSAStartup(WINSOCK_VERSION, &data) != 0) {
        return Unexpected(Error::fromErrno());
    }
#endif
    return {};
}

inline auto SockInitializer::uninitialize() -> Result<void> {

#if defined(_WIN32)
    if (::WSACleanup() != 0) {
        return Unexpected(Error::fromErrno());
    }
#endif
    return {};
}

// --- Error mapping
inline auto SystemCategory::name() const -> std::string_view {
    return "os";
}
inline auto SystemCategory::message(uint32_t code) const -> std::string {

#ifdef _WIN32
    wchar_t *args = nullptr;
    ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&args), 0, nullptr
    );
    auto len = ::WideCharToMultiByte(CP_UTF8, 0, args, -1, nullptr, 0, nullptr, nullptr);
    std::string ret(len, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, args, -1, &ret[0], len, nullptr, nullptr);
    ::LocalFree(args);
    return ret;
#else
    return ::strerror(code);
#endif

} 
inline auto SystemCategory::translate(error_t code) -> Error::Code {

#ifdef _WIN32
    #define MAP(x) WSA##x
#else
    #define MAP(x) x
#endif

    switch (code) {
        case 0: return Error::Ok;
        case MAP(EACCES): return Error::AccessDenied;
        case MAP(EADDRINUSE): return Error::AddressInUse;
        case MAP(EADDRNOTAVAIL): return Error::AddressNotAvailable;
        case MAP(EAFNOSUPPORT): return Error::AddressFamilyNotSupported;
        case MAP(EALREADY): return Error::AlreadyInProgress;
        case MAP(EBADF): return Error::BadFileDescriptor;
        case MAP(ECONNABORTED): return Error::ConnectionAborted;
        case MAP(ECONNREFUSED): return Error::ConnectionRefused;
        case MAP(ECONNRESET): return Error::ConnectionReset;
        case MAP(EDESTADDRREQ): return Error::DestinationAddressRequired;
        case MAP(EFAULT): return Error::BadAddress;
        case MAP(EHOSTDOWN): return Error::HostDown;
        case MAP(EHOSTUNREACH): return Error::HostUnreachable;
        case MAP(EINPROGRESS): return Error::InProgress;
        case MAP(EINVAL): return Error::InvalidArgument;
        case MAP(EISCONN): return Error::SocketIsConnected;
        case MAP(EMFILE): return Error::TooManyOpenFiles;
        case MAP(EMSGSIZE): return Error::MessageTooLarge;
        case MAP(ENETDOWN): return Error::NetworkDown;
        case MAP(ENETRESET): return Error::NetworkReset;
        case MAP(ENETUNREACH): return Error::NetworkUnreachable;
        case MAP(ENOBUFS): return Error::NoBufferSpaceAvailable;
        case MAP(ENOPROTOOPT): return Error::ProtocolOptionNotSupported;
        case MAP(ENOTCONN): return Error::SocketIsNotConnected;
        case MAP(ENOTSOCK): return Error::NotASocket;
        case MAP(EOPNOTSUPP): return Error::OperationNotSupported;
        case MAP(EPFNOSUPPORT): return Error::ProtocolFamilyNotSupported;
        case MAP(EPROTONOSUPPORT): return Error::ProtocolNotSupported;
        case MAP(EPROTOTYPE): return Error::ProtocolNotSupported;
        case MAP(ESHUTDOWN): return Error::SocketShutdown;
        case MAP(ESOCKTNOSUPPORT): return Error::SocketTypeNotSupported;
        case MAP(ETIMEDOUT): return Error::TimedOut;
        case MAP(EWOULDBLOCK): return Error::WouldBlock;
        default: return Error::Unknown;
    }
#undef MAP

}
inline auto SystemCategory::equivalent(uint32_t value, const Error &other) const -> bool {
    if (this == &other.category() && value == other.value()) {
        //< Category is same, value is same
        return true;
    }
    if (other.category() == IliasCategory::instance()) {
        // Is bultin error code
        return translate(value) == other.value();
    }
    return false;
}
inline auto SystemCategory::instance() -> SystemCategory & {
    static SystemCategory c;
    return c;
}

inline auto Error::fromErrno(uint32_t code) -> Error {

#if 0
    return SystemCategory::translate(code);
#else
    return Error(code, SystemCategory::instance());
#endif

}
inline auto Error::fromHErrno(uint32_t code) -> Error {
    return Error::fromErrno(code);
}
inline auto Error::fromErrno() -> Error {
    return Error::fromErrno(ILIAS_ERRNO);
}
inline auto Error::fromHErrno() -> Error {
    return Error::fromHErrno(ILIAS_H_ERRNO);
}

ILIAS_NS_END