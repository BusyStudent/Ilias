#pragma once

/**
 * @file sys.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For import system headers and make some error handling here
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "../ilias.hpp"

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
    #include <afunix.h>

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

#include "../detail/expected.hpp"
#include "../detail/charcvt.hpp"
#include <cstring>
#include <cstddef>
#include <memory>
#include <span>

ILIAS_NS_BEGIN

// ---Platform
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
 * @brief A Error category from (system os, like win32, linux, etc)
 * 
 */
class SystemCategory final : public ErrorCategory {
public:
    auto name() const -> std::string_view override;
    auto message(int64_t code) const -> std::string override;
    auto equivalent(int64_t self, const Error &other) const -> bool override;

    static auto instance() -> SystemCategory &;
    static auto translate(error_t sysErr) -> Error::Code;
};

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
inline auto SystemCategory::message(int64_t code) const -> std::string {

#ifdef _WIN32
    wchar_t *args = nullptr;
    ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&args), 0, nullptr
    );
    auto ret = WideToUtf8(args);
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
inline auto SystemCategory::equivalent(int64_t value, const Error &other) const -> bool {
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

inline auto Error::fromErrno(int64_t code) -> Error {

#if 0
    return SystemCategory::translate(code);
#else
    return Error(code, SystemCategory::instance());
#endif

}
inline auto Error::fromErrno() -> Error {
    return Error::fromErrno(ILIAS_ERRNO);
}

ILIAS_NS_END