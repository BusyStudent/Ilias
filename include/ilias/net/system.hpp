/**
 * @file system.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For import system headers and initialize windows socket
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/ilias.hpp>
#include <ilias/error.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/detail/expected.hpp>

// --- Import system header files ---
#if defined(_WIN32)
    #define ILIAS_INVALID_SOCKET   INVALID_SOCKET
    #define ILIAS_CLOSE_SOCKET(fd) ::closesocket(fd)
    #define ILIAS_CLOSE(fd)        ::CloseHandle(fd)
    #define ILIAS_POLL             ::WSAPoll
    #define ILIAS_SHUT_RD          SD_RECEIVE
    #define ILIAS_SHUT_WR          SD_SEND
    #define ILIAS_SHUT_RDWR        SD_BOTH
    #define ILIAS_IOVEC_T          ::WSABUF
    #define ILIAS_MSGHDR_T         ::WSAMSG

    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <afunix.h>

    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib")
    #endif
#else
    #define ILIAS_INVALID_SOCKET   -1
    #define ILIAS_CLOSE_SOCKET(fd) ::close(fd)
    #define ILIAS_CLOSE(fd)        ::close(fd)
    #define ILIAS_POLL             ::poll
    #define ILIAS_SHUT_RD          SHUT_RD
    #define ILIAS_SHUT_WR          SHUT_WR
    #define ILIAS_SHUT_RDWR        SHUT_RDWR
    #define ILIAS_IOVEC_T          ::iovec
    #define ILIAS_MSGHDR_T         ::msghdr

    #include <sys/socket.h>
    #include <sys/poll.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
#endif


ILIAS_NS_BEGIN

using iovec_t  = ILIAS_IOVEC_T;
using msghdr_t = ILIAS_MSGHDR_T;

/**
 * @brief The poll event enum. It is the same as in poll.h and can be combined with bitwise operations.
 * 
 */
enum PollEvent : uint32_t {
    In  = POLLIN,  //< When fd can be read (similar to readfds in select)
    Out = POLLOUT, //< When fd can be written (similar to writefds in select)
    Pri = POLLPRI, //< When fd has urgent data (similar to exceptfds in select)

    Err = POLLERR, //< When fd has an error condition (not passed to poll, only in revents)
    Hup = POLLHUP, //< When fd is hung up (not passed to poll, only in revents)
};

/**
 * @brief The socket shutdown mode enum
 * 
 */
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
        return Unexpected(SystemError::fromErrno());
    }
#endif
    return {};
}

inline auto SockInitializer::uninitialize() -> Result<void> {

#if defined(_WIN32)
    if (::WSACleanup() != 0) {
        return Unexpected(SystemError::fromErrno());
    }
#endif
    return {};
}

// --- Endianess
inline auto hostToNetwork(uint16_t value) -> uint16_t {
    return ::htons(value);
}

inline auto hostToNetwork(uint32_t value) -> uint32_t {
    return ::htonl(value);
}

inline auto networkToHost(uint16_t value) -> uint16_t {
    return ::ntohs(value);
}

inline auto networkToHost(uint32_t value) -> uint32_t {
    return ::ntohl(value);
}

ILIAS_NS_END