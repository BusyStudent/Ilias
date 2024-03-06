#pragma once

#include <cstring>
#include <string>

#ifndef ILIAS_NAMESPACE
    #define ILIAS_NAMESPACE Ilias
#endif

#ifndef ILIAS_ASSERT
    #define ILIAS_ASSERT(x) assert(x)
    #include <cassert>
#endif

#ifndef ILIAS_MALLOC 
    #define ILIAS_REALLOC(x, y) ::realloc(x, y)
    #define ILIAS_MALLOC(x) ::malloc(x)
    #define ILIAS_FREE(x) ::free(x)
    #include <cstdlib>
#endif

#define ILIAS_ASSERT_MSG(x, msg) ILIAS_ASSERT((x) && (msg))
#define ILIAS_NS_BEGIN namespace ILIAS_NAMESPACE {
#define ILIAS_NS_END }

// Platform
#if  defined(_WIN32)
    #define ILIAS_INVALID_SOCKET INVALID_SOCKET
    #define ILIAS_ERRNO     ::WSAGetLastError()
    #define ILIAS_H_ERRNO   ::WSAGetLastError()
    #define ILIAS_ERROR_T   ::DWORD
    #define ILIAS_SOCKET_T  ::SOCKET
    #define ILIAS_SSIZE_T     int
    #define ILIAS_BYTE_T      char
    #define ILIAS_CLOSE(s)  ::closesocket(s)
    #define ILIAS_POLL      ::WSAPoll
    #define ILIAS_ETIMEOUT  (WSAETIMEOUT)
    #include <WinSock2.h>
    #include <WS2tcpip.h>

    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib")
    #endif
#elif defined(__linux)
    #define ILIAS_INVALID_SOCKET -1
    #define ILIAS_ERRNO     (errno)
    #define ILIAS_H_ERRNO   (h_errno)
    #define ILIAS_ERROR_T     int
    #define ILIAS_SOCKET_T    int
    #define ILIAS_SSIZE_T     int
    #define ILIAS_BYTE_T      void
    #define ILIAS_CLOSE(s)  ::close(s)
    #define ILIAS_POLL      ::poll
    #define ILIAS_ETIMEOUT  (ETIMEDOUT)

    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <sys/epoll.h>
    #include <arpa/inet.h>
    #include <poll.h>
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
        V6 = AF_INET6
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

    static IPEndpoint fromRaw(const void *data, size_t size);
    template <typename T>
    static IPEndpoint fromRaw(const T &data);
private:
    ::sockaddr_storage mAddr { };
};

/**
 * @brief A Wrapper to erase errcode difference
 * 
 */
class SockError {
public:
    SockError() = default;
    SockError(error_t error) : mError(error) { }
    SockError(const SockError &) = default;

    /**
     * @brief Get message of this error, in local encoding
     * 
     * @return std::string 
     */
    std::string   message() const;
    /**
     * @brief Get message of this error, in utf8 encoding
     * 
     * @return std::string 
     */
    std::string u8message() const;

    /**
     * @brief Get the raw error code
     * 
     * @return error_t 
     */
    error_t error() const;

    bool isOk() const;
    /**
     * @brief Is Posix like code ETIMEDOUT
     * 
     * @return true 
     * @return false 
     */
    bool isTimedOut() const;
    /**
     * @brief Is Posix like code EWOULDBLOCK or EAGAIN
     * 
     * @return true 
     * @return false 
     */
    bool isWouldBlock() const;
    /**
     * @brief Is Posix like code EINPROGRESS
     * 
     * @return true 
     * @return false 
     */
    bool isInProgress() const;

    /**
     * @brief Get Error code from errno (WSAGetLastError)
     * 
     * @return SockError 
     */
    static SockError fromErrno();
    /**
     * @brief Get Error code from h_errno (WSAGetLastError)
     * 
     * @return SockError 
     */
    static SockError fromHErrno();

    operator error_t() const noexcept {
        return mError;
    }
private:
    error_t mError {};
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

    ssize_t recv(void *buf, size_t len, int flags = 0) const;
    ssize_t send(const void *buf, size_t len, int flags = 0) const;
    ssize_t sendto(const void *buf, size_t len, int flags, const IPEndpoint *endpoint) const;
    ssize_t recvfrom(void *buf, size_t len, int flags, IPEndpoint *endpoint) const;

    template <typename T, size_t N>
    ssize_t send(const T (&buf)[N], int flags = 0) const;

    bool    listen(int backlog = 0) const;
    bool    connect(const IPEndpoint &endpoint) const;
    bool    bind(const IPEndpoint &endpoint) const;
    bool    setBlocking(bool blocking) const;
    bool    setReuseAddr(bool reuse) const;
    bool    setOption(int level, int optname, const void *optval, socklen_t optlen) const;

#ifdef _WIN32
    bool    ioctl(long cmd, u_long *args) const;
#endif

    bool    isValid() const;
    bool    isBlocking() const;

    SockError error() const;

    template <typename T>
    std::pair<T, IPEndpoint> accept() const;

    IPEndpoint localEndpoint() const;
    IPEndpoint remoteEndpoint() const;

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
    Socket   clone();

    Socket &operator =(Socket &&s);
    Socket &operator =(const Socket &) = delete;

    std::pair<Socket, IPEndpoint> accept() const;

    static Socket create(int family, int type, int protocol);
    static std::pair<Socket, Socket> makePair(int family, int type, int protocol);
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
    addr.s_addr = ::inet_addr(address);
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

inline bool IPAddress::compare(const IPAddress &other) const {
    if (family() != other.family()) {
        return false;
    }
    return ::memcmp(&mStorage, &other.mStorage, length()) == 0;
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

// --- SockError Impl
inline error_t SockError::error() const {
    return mError;
}
inline std::string SockError::message() const {
#ifdef _WIN32
    char *msg = nullptr;
    ::FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        nullptr,
        mError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        LPSTR(&msg),
        0,
        nullptr
    );
    std::string result(msg);
    ::LocalFree(msg);
    return result;
#else
    return ::strerror(mError);
#endif
}
inline std::string SockError::u8message() const {
#ifdef _WIN32
    wchar_t *msg = nullptr;
    ::FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        nullptr,
        mError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        LPWSTR(&msg),
        0,
        nullptr
    );
    int len = ::WideCharToMultiByte(CP_UTF8, 0, msg, -1, nullptr, 0, nullptr, nullptr);
    std::string result;
    result.resize(len);
    len = ::WideCharToMultiByte(CP_UTF8, 0, msg, -1, &result[0], len, nullptr, nullptr);
    ::LocalFree(msg);
    return result;
#else
    return message();
#endif
}

inline bool SockError::isOk() const {
    return mError == 0;
}

#ifdef _WIN32
inline bool SockError::isTimedOut() const {
    return mError == WSAETIMEDOUT;
}
inline bool SockError::isWouldBlock() const {
    return mError == WSAEWOULDBLOCK;
}
inline bool SockError::isInProgress() const {
    return mError == WSAEINPROGRESS;
}
#else
inline bool SockError::isWouldBlock() const {
    return mError == EWOULDBLOCK || mError == EAGAIN;
}
inline bool SockError::isInProgress() const {
    return mError == EINPROGRESS;
}
#endif

inline SockError SockError::fromErrno() {
    return ILIAS_ERRNO;
}
inline SockError SockError::fromHErrno() {
    return ILIAS_H_ERRNO;
}

// --- SocketView Impl
inline ssize_t SocketView::recv(void *buf, size_t len, int flags) const {
    return ::recv(mFd, static_cast<byte_t*>(buf), len, flags);
}
inline ssize_t SocketView::send(const void *buf, size_t len, int flags) const {
    return ::send(mFd, static_cast<const byte_t*>(buf), len, flags);
}
inline ssize_t SocketView::recvfrom(void *buf, size_t len, int flags, IPEndpoint *ep) const {
    ::sockaddr_storage addr {};
    ::socklen_t size = sizeof(addr);
    ssize_t ret = ::recvfrom(mFd, static_cast<byte_t*>(buf), len, flags, reinterpret_cast<::sockaddr*>(&addr), &size);
    if (ret >= 0 && ep) {
        *ep = IPEndpoint::fromRaw(&addr, size);
    }
    return ret;
}
inline ssize_t SocketView::sendto(const void *buf, size_t len, int flags, const IPEndpoint *ep) const {
    const ::sockaddr *addr = ep ? &ep->data<::sockaddr>() : nullptr;
    const ::socklen_t addrLen = ep ? ep->length() : 0;
    return ::sendto(mFd, static_cast<const byte_t*>(buf), len, flags, addr, addrLen);
}

// Helper 
template <typename T, size_t N>
ssize_t SocketView::send(const T (&buf)[N], int flags) const {
    static_assert(std::is_pod<T>::value, "T must be POD type");
    return send(buf, sizeof(T) * N, flags);
}

inline bool SocketView::listen(int backlog) const {
    return ::listen(mFd, backlog) == 0;
}
inline bool SocketView::connect(const IPEndpoint &ep) const {
    return ::connect(mFd, &ep.data<::sockaddr>(), ep.length()) == 0;
}
inline bool SocketView::bind(const IPEndpoint &ep) const {
    return ::bind(mFd, &ep.data<::sockaddr>(), ep.length()) == 0;
}
inline bool SocketView::isValid() const {
    return mFd != ILIAS_INVALID_SOCKET;
}
inline bool SocketView::setOption(int level, int optname, const void *optval, socklen_t optlen) const {
    return ::setsockopt(mFd, level, optname, static_cast<const byte_t*>(optval), optlen) == 0;
}
inline bool SocketView::setReuseAddr(bool reuse) const {
    int data = reuse ? 1 : 0;
    return setOption(SOL_SOCKET, SO_REUSEADDR, &data, sizeof(data));
}
inline bool SocketView::setBlocking(bool blocking) const {
#ifdef _WIN32
    u_long block = blocking ? 0 : 1;
    return ioctl(FIONBIO, &block);
#else
    int flags = ::fcntl(mFd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    if (blocking) {
        flags &= ~O_NONBLOCK;
    }
    else {
        flags |= O_NONBLOCK;
    }
    if (::fcntl(mFd, F_SETFL, flags) < 0) {
        return false;
    }
    return true;
#endif
}

#ifdef _WIN32
inline bool SocketView::ioctl(long cmd, u_long *pargs) const {
    return ::ioctlsocket(mFd, cmd, pargs) == 0;
}
#endif

template <typename T>
inline std::pair<T, IPEndpoint> SocketView::accept() const {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    int fd = ::accept(mFd, reinterpret_cast<::sockaddr*>(&addr), &len);
    if (fd != ILIAS_INVALID_SOCKET) {
        return std::make_pair(T(fd), IPEndpoint::fromRaw(&addr, len));
    }
    return std::make_pair(T(), IPEndpoint());
}

inline IPEndpoint SocketView::localEndpoint() const {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    if (::getsockname(mFd, reinterpret_cast<::sockaddr*>(&addr), &len) == 0) {
        return IPEndpoint::fromRaw(&addr, len);
    }
    return IPEndpoint();
}
inline IPEndpoint SocketView::remoteEndpoint() const {
    ::sockaddr_storage addr {};
    ::socklen_t len = sizeof(addr);
    if (::getpeername(mFd, reinterpret_cast<::sockaddr*>(&addr), &len) == 0) {
        return IPEndpoint::fromRaw(&addr, len);
    }
    return IPEndpoint();
}
inline SockError SocketView::error() const {
    error_t err;
    ::socklen_t len = sizeof(error_t);
    if (::getsockopt(mFd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == 0) {
        return SockError(err);
    }
    return SockError();
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

inline std::pair<Socket, IPEndpoint> Socket::accept() const {
    return SocketView::accept<Socket>();
}

inline Socket Socket::create(int family, int type, int proto) {
    return Socket(family, type, proto);
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

ILIAS_NS_END