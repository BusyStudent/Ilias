#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include <cstring>
#include <string>

// Platform
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

// Platform 
using socket_t = ILIAS_SOCKET_T;
using ssize_t  = ILIAS_SSIZE_T;
using byte_t   = ILIAS_BYTE_T;
using error_t  = ILIAS_ERROR_T;
using fd_t     = ILIAS_FD_T;

/**
 * @brief Wrapper for v4
 * 
 */
class IPAddress4 : public ::in_addr {
public:
    IPAddress4();
    IPAddress4(::in_addr addr4);
    IPAddress4(const IPAddress4 &other) = default;

    std::string toString() const;
    uint32_t    toUint32() const;
    uint32_t    toUint32NetworkOrder() const;

    bool isAny() const;
    bool isNone() const;
    bool isBroadcast() const;
    bool isLoopback() const;

    bool operator ==(const IPAddress4 &other) const;
    bool operator !=(const IPAddress4 &other) const;
    
    static IPAddress4 any();
    static IPAddress4 none();
    static IPAddress4 broadcast();
    static IPAddress4 loopback();
    static IPAddress4 fromString(const char *value);
    static IPAddress4 fromHostname(const char *hostname);
    static IPAddress4 fromUint32(uint32_t value);
    static IPAddress4 fromUint32NetworkOrder(uint32_t value);
};
/**
 * @brief Wrapper for v6
 * 
 */
class IPAddress6 : public ::in6_addr {
public:
    IPAddress6();
    IPAddress6(::in6_addr addr6);
    IPAddress6(const IPAddress6 &other) = default;

    std::string toString() const;

    bool isAny() const;
    bool isNone() const;
    bool isLoopback() const;
    bool isMulticast() const;

    bool operator ==(const IPAddress6 &other) const;
    bool operator !=(const IPAddress6 &other) const;

    static IPAddress6 any();
    static IPAddress6 none();
    static IPAddress6 loopback();
    static IPAddress6 fromString(const char *value);
    static IPAddress6 fromHostname(const char *hostname);
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

    std::string toString() const;
    int  family() const;
    int  length() const;
    bool isValid() const;
    const void *data() const;
    template <typename T>
    const T &data() const;

    void *data();
    template <typename T>
    T &data();

    bool compare(const IPAddress &rhs) const;
    bool operator ==(const IPAddress &rhs) const;
    bool operator !=(const IPAddress &rhs) const;

    static IPAddress fromString(const char *str);
    static IPAddress fromHostname(const char *hostname);
    static IPAddress fromRaw(const void *data, size_t size);
    template <typename T>
    static IPAddress fromRaw(const T &data);
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
    static_assert(AF_INET != 0 && AF_INET6 != 0, "We use mAddr.ss_family == 0 on invalid");
    static_assert(AF_UNSPEC == 0, "We use mAddr.ss_family == 0 on invalid");

    IPEndpoint();
    IPEndpoint(const char *str);
    IPEndpoint(::sockaddr_in addr4);
    IPEndpoint(::sockaddr_in6 addr6);
    IPEndpoint(::sockaddr_storage storage);
    IPEndpoint(const IPAddress &address, uint16_t port);
    IPEndpoint(const IPEndpoint &) = default;

    std::string toString() const;
    IPAddress4 address4() const;
    IPAddress6 address6() const;
    IPAddress address() const;
    uint16_t  port() const;
    int       family() const;
    int       length() const;
    bool      isValid() const;
    const void *data() const;
    template <typename T>
    const T &data() const;

    void *data();
    template <typename T>
    T &data();

    bool compare(const IPEndpoint &rhs) const;
    bool operator ==(const IPEndpoint &rhs) const;
    bool operator !=(const IPEndpoint &rhs) const;

    static IPEndpoint fromString(const char *str);
    static IPEndpoint fromRaw(const void *data, size_t size);
    template <typename T>
    static IPEndpoint fromRaw(const T &data);
private:
    ::sockaddr_storage mAddr { };
};

class SockInitializer {
public:
    SockInitializer();
    SockInitializer(const SockInitializer &) = delete;
    ~SockInitializer();

    bool isInitalized() const noexcept { return mInited; }
private:
    bool mInited;
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

    Result<size_t> recv(void *buf, size_t len, int flags = 0) const;
    Result<size_t> send(const void *buf, size_t len, int flags = 0) const;
    Result<size_t> sendto(const void *buf, size_t len, int flags, const IPEndpoint *endpoint) const;
    Result<size_t> sendto(const void *buf, size_t len, int flags, const IPEndpoint &endpoint) const;
    Result<size_t> recvfrom(void *buf, size_t len, int flags, IPEndpoint *endpoint) const;

    template <typename T, size_t N>
    Result<size_t> send(const T (&buf)[N], int flags = 0) const;

    Result<void> listen(int backlog = 0) const;
    Result<void> connect(const IPEndpoint &endpoint) const;
    Result<void> bind(const IPEndpoint &endpoint) const;
    Result<void> setBlocking(bool blocking) const;
    Result<void> setReuseAddr(bool reuse) const;
    Result<void> setOption(int level, int optname, const void *optval, socklen_t optlen) const;
    Result<void> getOption(int level, int optname, void *optval, socklen_t *optlen) const;

#ifdef _WIN32
    Result<void> ioctl(long cmd, u_long *args) const;
#endif

    bool isValid() const;

    Result<int> family() const;
    Result<int> type() const;
    
    Result<Error> error() const;

    template <typename T>
    Result<std::pair<T, IPEndpoint> > accept() const;

    Result<IPEndpoint> localEndpoint() const;
    Result<IPEndpoint> remoteEndpoint() const;

    socket_t get() const noexcept {
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

    socket_t release(socket_t newSocket = InvalidSocket);
    bool     reset(socket_t newSocket = InvalidSocket);
    bool     close();

    Socket &operator =(Socket &&s);
    Socket &operator =(const Socket &) = delete;

    Result<std::pair<Socket, IPEndpoint> > accept() const;

    static Result<Socket> create(int family, int type, int protocol);
};

// --- IPAddress4 Impl
inline IPAddress4::IPAddress4() { }
inline IPAddress4::IPAddress4(::in_addr addr) : ::in_addr(addr) { }

inline std::string IPAddress4::toString() const {
    return ::inet_ntoa(*this);
}
inline uint32_t IPAddress4::toUint32() const {
    return ::ntohl(toUint32NetworkOrder());
}
inline uint32_t IPAddress4::toUint32NetworkOrder() const {
    return reinterpret_cast<const uint32_t&>(*this);
}
inline bool IPAddress4::isAny() const {
    return toUint32() == INADDR_ANY;
}
inline bool IPAddress4::isNone() const {
    return toUint32() == INADDR_NONE;
}
inline bool IPAddress4::isLoopback() const {
    return toUint32() == INADDR_LOOPBACK;
}
inline bool IPAddress4::isBroadcast() const {
    return toUint32() == INADDR_BROADCAST;
}
inline bool IPAddress4::operator ==(const IPAddress4 &other) const {
    return toUint32NetworkOrder() == other.toUint32NetworkOrder();
}
inline bool IPAddress4::operator !=(const IPAddress4 &other) const {
    return toUint32NetworkOrder() != other.toUint32NetworkOrder();
}

inline IPAddress4 IPAddress4::any() {
    return IPAddress4::fromUint32(INADDR_ANY);
}
inline IPAddress4 IPAddress4::loopback() {
    return IPAddress4::fromUint32(INADDR_LOOPBACK);   
}
inline IPAddress4 IPAddress4::broadcast() {
    return IPAddress4::fromUint32(INADDR_BROADCAST);
}
inline IPAddress4 IPAddress4::none() {
    return IPAddress4::fromUint32(INADDR_NONE);
}
inline IPAddress4 IPAddress4::fromString(const char *address) {
    IPAddress4 addr;
    if (::inet_pton(AF_INET, address, &addr) != 1) {
        return IPAddress4::none();
    }
    return addr;
}
inline IPAddress4 IPAddress4::fromHostname(const char *hostnamne) {
    auto ent = ::gethostbyname(hostnamne);
    if (!ent || ent->h_addrtype != AF_INET) {
        return IPAddress4::none();
    }
    return *reinterpret_cast<const IPAddress4*>(ent->h_addr_list[0]);
}
inline IPAddress4 IPAddress4::fromUint32(uint32_t uint32) {
    static_assert(sizeof(uint32_t) == sizeof(::in_addr), "sizeof mismatch");
    uint32 = ::htonl(uint32);
    return reinterpret_cast<::in_addr&>(uint32);
}
inline IPAddress4 IPAddress4::fromUint32NetworkOrder(uint32_t uint32) {
    return reinterpret_cast<::in_addr&>(uint32);
}

// --- IPAddress6 Impl
inline IPAddress6::IPAddress6() { }
inline IPAddress6::IPAddress6(::in6_addr addr) : ::in6_addr(addr) { }

inline std::string IPAddress6::toString() const {
    char buf[INET6_ADDRSTRLEN] {0};
    ::inet_ntop(AF_INET6, this, buf, sizeof(buf));
    return buf;
}
inline bool IPAddress6::isAny() const {
    return IN6_IS_ADDR_UNSPECIFIED(this);
}
inline bool IPAddress6::isNone() const {
    return IN6_IS_ADDR_UNSPECIFIED(this);
}
inline bool IPAddress6::isLoopback() const {
    return IN6_IS_ADDR_LOOPBACK(this);
}
inline bool IPAddress6::isMulticast() const {
    return IN6_IS_ADDR_MULTICAST(this);
}
inline bool IPAddress6::operator ==(const IPAddress6 &addr) const {
    return IN6_ARE_ADDR_EQUAL(this, &addr);
}
inline bool IPAddress6::operator !=(const IPAddress6 &addr) const {
    return !IN6_ARE_ADDR_EQUAL(this, &addr);
}

inline IPAddress6 IPAddress6::any() {
    return ::in6_addr IN6ADDR_ANY_INIT;
}
inline IPAddress6 IPAddress6::none() {
    return ::in6_addr IN6ADDR_ANY_INIT;
}
inline IPAddress6 IPAddress6::loopback() {
    return ::in6_addr IN6ADDR_LOOPBACK_INIT;
}
inline IPAddress6 IPAddress6::fromString(const char *str) {
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

inline bool IPAddress::isValid() const {
    return mFamily != None;
}
inline int  IPAddress::family() const {
    return mFamily;
}
inline int  IPAddress::length() const {
    switch (mFamily) {
        case V4:  return sizeof(::in_addr);
        case V6:  return sizeof(::in6_addr);
        default:  return 0;
    }
}
inline std::string IPAddress::toString() const {
    if (!isValid()) {
        return std::string();
    }
    char buf[INET6_ADDRSTRLEN] {0};
    ::inet_ntop(family(), &mStorage, buf, sizeof(buf));
    return buf;
}

template <typename T>
inline const T &IPAddress::data() const {
    return *reinterpret_cast<const T *>(&mStorage);
}
inline const void *IPAddress::data() const {
    return &mStorage;
}
inline void *IPAddress::data() {
    return &mStorage;
}

inline bool IPAddress::compare(const IPAddress &other) const {
    if (family() != other.family()) {
        return false;
    }
    switch (mFamily) {
        case V4:  return data<IPAddress4>() == other.data<IPAddress4>();
        case V6:  return data<IPAddress6>() == other.data<IPAddress6>();
        default:  return true; //< All are invalid address
    }
}
inline bool IPAddress::operator ==(const IPAddress &other) const {
    return compare(other);
}
inline bool IPAddress::operator !=(const IPAddress &other) const {
    return !compare(other);
}

inline IPAddress IPAddress::fromString(const char *str) {
    return IPAddress(str);
}
inline IPAddress IPAddress::fromHostname(const char *hostname) {
    auto ent = ::gethostbyname(hostname);
    if (!ent) {
        return IPAddress();
    }
    return IPAddress::fromRaw(ent->h_addr_list[0], ent->h_length);
}
inline IPAddress IPAddress::fromRaw(const void *raw, size_t len) {
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
inline IPAddress IPAddress::fromRaw(const T &data) {
    static_assert(sizeof(T) == sizeof(::in_addr) || sizeof(T) == sizeof(::in6_addr), "Invalid size");
    return IPAddress::fromRaw(&data, sizeof(T));
}

// --- IPEndpoint Impl
inline IPEndpoint::IPEndpoint() { }
inline IPEndpoint::IPEndpoint(const char *str) : IPEndpoint(fromString(str)) { }
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

inline bool IPEndpoint::isValid() const {
    return mAddr.ss_family != 0;
}
inline int  IPEndpoint::family() const {
    return mAddr.ss_family;
}
inline int  IPEndpoint::length() const {
    switch (mAddr.ss_family) {
        case AF_INET: return sizeof(::sockaddr_in);
        case AF_INET6: return sizeof(::sockaddr_in6);
        default: return 0;
    }
}
inline uint16_t IPEndpoint::port() const {
    switch (mAddr.ss_family) {
        case AF_INET: 
            return ::ntohs(data<::sockaddr_in>().sin_port);
        case AF_INET6: 
            return ::ntohs(data<::sockaddr_in6>().sin6_port);
        default : return 0;
    }
}
inline IPAddress IPEndpoint::address() const {
    switch (mAddr.ss_family) {
        case AF_INET: 
            return IPAddress::fromRaw(data<::sockaddr_in>().sin_addr);
        case AF_INET6: 
            return IPAddress::fromRaw(data<::sockaddr_in6>().sin6_addr);
        default: return IPAddress();
    }
}
inline IPAddress4 IPEndpoint::address4() const {
    ILIAS_ASSERT(mAddr.ss_family == AF_INET);
    return IPAddress4(data<::sockaddr_in>().sin_addr);
}
inline IPAddress6 IPEndpoint::address6() const {
    ILIAS_ASSERT(mAddr.ss_family == AF_INET6);
    return IPAddress6(data<::sockaddr_in6>().sin6_addr);
}
inline std::string IPEndpoint::toString() const {
    if (!isValid()) {
        return std::string();
    }
    if (family() == AF_INET6) {
        return '[' + address().toString() + ']' + ':' + std::to_string(port());
    }
    return address().toString() + ':' + std::to_string(port());
}
inline const void *IPEndpoint::data() const {
    return &mAddr;
}
template <typename T>
inline const T &IPEndpoint::data() const {
    return *reinterpret_cast<const T*>(&mAddr);
}
template <typename T>
inline T &IPEndpoint::data() {
    return *reinterpret_cast<T*>(&mAddr);
}

inline bool IPEndpoint::compare(const IPEndpoint &other) const {
    return family() == other.family() && address() == other.address() && port() == other.port();
}
inline bool IPEndpoint::operator ==(const IPEndpoint &other) const {
    return compare(other);
}
inline bool IPEndpoint::operator !=(const IPEndpoint &other) const {
    return !compare(other);
}

inline IPEndpoint IPEndpoint::fromString(const char *str) {
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
inline IPEndpoint IPEndpoint::fromRaw(const void *raw, size_t len) {
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
inline IPEndpoint IPEndpoint::fromRaw(const T &raw) {
    static_assert(sizeof(T) == sizeof(::sockaddr_in) || 
                  sizeof(T) == sizeof(::sockaddr_in6) ||
                  sizeof(T) == sizeof(::sockaddr_storage), 
                  "Invalid raw type"
    );
    return fromRaw(&raw, sizeof(T));
}

// --- SocketView Impl
inline Result<size_t> SocketView::recv(void *buf, size_t len, int flags) const {
    ssize_t ret = ::recv(mFd, static_cast<byte_t*>(buf), len, flags);
    if (ret < 0) {
        return Unexpected(Error::fromErrno());
    }
    return ret;
}
inline Result<size_t> SocketView::send(const void *buf, size_t len, int flags) const {
    ssize_t ret = ::send(mFd, static_cast<const byte_t*>(buf), len, flags);
    if (ret < 0) {
        return Unexpected(Error::fromErrno());
    }
    return ret;
}
inline Result<size_t> SocketView::recvfrom(void *buf, size_t len, int flags, IPEndpoint *ep) const {
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
inline Result<size_t> SocketView::sendto(const void *buf, size_t len, int flags, const IPEndpoint *ep) const {
    const ::sockaddr *addr = ep ? &ep->data<::sockaddr>() : nullptr;
    const ::socklen_t addrLen = ep ? ep->length() : 0;
    ssize_t ret = ::sendto(mFd, static_cast<const byte_t*>(buf), len, flags, addr, addrLen);
    if (ret < 0) {
        return Unexpected(Error::fromErrno());
    }
    return ret;
}
inline Result<size_t> SocketView::sendto(const void *buf, size_t len, int flags, const IPEndpoint &ep) const {
    return sendto(buf, len, flags, &ep);
}    

// Helper 
template <typename T, size_t N>
inline Result<size_t> SocketView::send(const T (&buf)[N], int flags) const {
    static_assert(std::is_standard_layout<T>::value && std::is_trivial<T>::value, "T must be POD type");
    return send(buf, sizeof(T) * N, flags);
}

inline Result<void> SocketView::listen(int backlog) const {
    if (::listen(mFd, backlog) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline Result<void> SocketView::connect(const IPEndpoint &ep) const {
    if (::connect(mFd, &ep.data<::sockaddr>(), ep.length()) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline Result<void> SocketView::bind(const IPEndpoint &ep) const {
    if (::bind(mFd, &ep.data<::sockaddr>(), ep.length()) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline bool SocketView::isValid() const {
    return mFd != ILIAS_INVALID_SOCKET;
}
inline Result<void> SocketView::getOption(int level, int optname, void *optval, socklen_t *optlen) const {
    if (::getsockopt(mFd, level, optname, static_cast<byte_t*>(optval), optlen) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline Result<void> SocketView::setOption(int level, int optname, const void *optval, socklen_t optlen) const {
    if (::setsockopt(mFd, level, optname, static_cast<const byte_t*>(optval), optlen) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
inline Result<void> SocketView::setReuseAddr(bool reuse) const {
    int data = reuse ? 1 : 0;
    return setOption(SOL_SOCKET, SO_REUSEADDR, &data, sizeof(data));
}
inline Result<void> SocketView::setBlocking(bool blocking) const {
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
inline Result<void> SocketView::ioctl(long cmd, u_long *pargs) const {
    if (::ioctlsocket(mFd, cmd, pargs) == 0) {
        return Result<void>();
    }
    return Unexpected(Error::fromErrno());
}
#endif

inline Result<int> SocketView::family() const {
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
inline Result<int> SocketView::type() const {
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
inline Result<std::pair<T, IPEndpoint> > SocketView::accept() const {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    int fd = ::accept(mFd, reinterpret_cast<::sockaddr*>(&addr), &len);
    if (fd != ILIAS_INVALID_SOCKET) {
        return std::make_pair(T(fd), IPEndpoint::fromRaw(&addr, len));
    }
    return Unexpected(Error::fromErrno());
}

inline Result<IPEndpoint> SocketView::localEndpoint() const {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    if (::getsockname(mFd, reinterpret_cast<::sockaddr*>(&addr), &len) == 0) {
        return IPEndpoint::fromRaw(&addr, len);
    }
    return Unexpected(Error::fromErrno());
}
inline Result<IPEndpoint> SocketView::remoteEndpoint() const {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    if (::getpeername(mFd, reinterpret_cast<::sockaddr*>(&addr), &len) == 0) {
        return IPEndpoint::fromRaw(&addr, len);
    }
    return Unexpected(Error::fromErrno());
}
inline Result<Error> SocketView::error() const {
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

inline socket_t Socket::release(socket_t newSocket) {
    socket_t prev = mFd;
    mFd = newSocket;
    return prev;
}
inline bool Socket::reset(socket_t newSocket) {
    bool ret = true;
    if (isValid()) {
        ret = (ILIAS_CLOSE(mFd) == 0);
    }
    mFd = newSocket;
    return ret;
}
inline bool Socket::close() {
    return reset();
}

inline Socket &Socket::operator =(Socket &&s) {
    if (this == &s) {
        return *this;
    }
    reset(s.release());
    return *this;
}

inline Result<std::pair<Socket, IPEndpoint> > Socket::accept() const {
    return SocketView::accept<Socket>();
}

inline Result<Socket> Socket::create(int family, int type, int proto) {
    auto sock = ::socket(family, type, proto);
    if (sock != ILIAS_INVALID_SOCKET) {
        return Result<Socket>(Socket(sock));
    }
    return Unexpected(Error::fromErrno());
}

// -- Network order / Host
inline uint16_t ToNetworkOrder(uint16_t v) {
    return ::htons(v);
}
inline uint32_t ToNetworkOrder(uint32_t v) {
    return ::htonl(v);
}
inline uint16_t ToHostOrder(uint16_t v) {
    return ::ntohs(v);
}
inline uint32_t ToHostOrder(uint32_t v) {
    return ::ntohl(v);
}

// --- Init spec
inline bool Initialize() {
#ifdef _WIN32
    ::WSADATA data {};
    return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}
inline void Uninitialize() {
#ifdef _WIN32
    ::WSACleanup();
#endif
}

inline SockInitializer::SockInitializer() {
    mInited = Initialize();
}
inline SockInitializer::~SockInitializer() {
    if (mInited) {
        Uninitialize();
    }
}

// --- Error mapping
inline Error Error::fromErrno(uint32_t code) {

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
inline Error Error::fromHErrno(uint32_t code) {
    return Error::fromErrno(code);
}
inline Error Error::fromErrno() {
    return Error::fromErrno(ILIAS_ERRNO);
}
inline Error Error::fromHErrno() {
    return Error::fromHErrno(ILIAS_H_ERRNO);
}

ILIAS_NS_END