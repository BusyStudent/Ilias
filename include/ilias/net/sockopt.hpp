/**
 * @file sockopt.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The helper class for socket options
 * @version 0.1
 * @date 2024-09-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/net/system.hpp>
#include <concepts>

#if defined(_WIN32)
    #include <MSWSock.h>

    #if !defined(SIO_UDP_CONNRESET) // Polyfill for SIO_UDP_CONNRESET in minGW
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
    #endif // defined(SIO_UDP_CONNRESET)

    #if !defined(SIO_UDP_NETRESET) // Polyfill for SIO_UDP_NETRESET in minGW
        #define SIO_UDP_NETRESET _WSAIOW(IOC_VENDOR,15)
    #endif // defined(SIO_UDP_NETRESET)
#else
    #include <netinet/tcp.h>
#endif // defined(_WIN32)

ILIAS_NS_BEGIN

/**
 * @brief Check option can be used to set on a socket
 * 
 * @tparam T 
 */
template <typename T>
concept SetSockOption = requires(const T t) {
    { t.setopt(socket_t{}) } -> std::convertible_to<IoResult<void> >;
};

/**
 * @brief Check option can be used to get from a socket
 * 
 * @tparam T 
 */
template <typename T>
concept GetSockOption = requires(T t) {
    { T::getopt(socket_t{}) } -> std::convertible_to<IoResult<T> >;
};

/**
 * @brief Check a type is a socket option
 * 
 * @tparam T 
 */
template <typename T>
concept SockOption = SetSockOption<T> || GetSockOption<T>;


/**
 * @brief Namespace for a lot of socket options
 * 
 */
namespace sockopt {

/**
 * @brief Helper enum to limit the access of socket options
 * 
 */
enum OptionAccess {
    Read = 1 << 0,
    Write = 1 << 1,
    ReadWrite = Read | Write
};

/**
 * @brief Socket option base helper class
 * 
 * @tparam Level 
 * @tparam Optname 
 * @tparam T
 * @tparam Access The access of the option
 */
template <int Level, int Optname, typename T, OptionAccess Access = OptionAccess::ReadWrite>
class OptionT {
public:
    constexpr OptionT() = default;
    constexpr OptionT(T value) : mValue(value) { }

    auto setopt(socket_t sock) const -> IoResult<void> requires (bool(Access & OptionAccess::Write)) {
        auto ret = ::setsockopt(sock, Level, Optname, reinterpret_cast<const char *>(&mValue), sizeof(T));
        if (ret != 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    static auto getopt(socket_t sock) -> IoResult<OptionT> requires (bool(Access & OptionAccess::Read)) {
        ::socklen_t optlen = sizeof(T);
        T value;
        auto ret = ::getsockopt(sock, Level, Optname, reinterpret_cast<char *>(&value), &optlen);
        if (ret != 0) {
            return Err(SystemError::fromErrno());
        }
        return OptionT {value};
    }

    /**
     * @brief Get the value of the option
     * 
     */
    constexpr auto value() const noexcept { return mValue; }

    /**
     * @brief Directly get the value of the option
     * 
     * @return T 
     */
    constexpr operator T() const noexcept { return mValue; }
private:
    T mValue { };
};

// For Some options, the value type is not int
// Add platform specific options here
#if defined(_WIN32)

/**
 * @brief The option using WSAIoctl
 * 
 * @tparam Opcode 
 * @tparam T 
 * @tparam Access 
 */
template <DWORD Opcode, typename T, OptionAccess Access = OptionAccess::ReadWrite>
class WSAOptionT {
public:
    constexpr WSAOptionT() = default;
    constexpr WSAOptionT(T value) : mValue(value) { }

    auto setopt(socket_t sock) const -> IoResult<void> requires (bool(Access & OptionAccess::Write)) {
        DWORD bytes = 0;
        auto in = mValue;
        auto out = mValue;
        auto ret = ::WSAIoctl(sock, Opcode, &in, sizeof(in), &out, sizeof(out), &bytes, nullptr, nullptr);
        if (ret != 0) {
            return Err(SystemError::fromErrno());
        }
        return {};
    }

    static auto getopt(socket_t sock) -> IoResult<WSAOptionT> requires (bool(Access & OptionAccess::Read)) {
        DWORD bytes = 0;
        auto out = T { };
        auto ret = ::WSAIoctl(sock, Opcode, nullptr, 0, &out, sizeof(out), &bytes, nullptr, nullptr);
        if (ret != 0) {
            return Err(SystemError::fromErrno());
        }
        return out;
    }

    /**
     * @brief Get the value of the option
     * 
     */
    constexpr auto value() const noexcept { return mValue; }

    /**
     * @brief Directly get the value of the option
     * 
     * @return T 
     */
    constexpr operator T() const noexcept { return mValue; }
private:
    T mValue { };
};

using dword_t = ::DWORD;
#else
using dword_t = int;
#endif // defined(_WIN32)


// SOL_SOCKET
/**
 * @brief Set socket option SO_REUSEADDR (true or false)
 * 
 */
using ReuseAddress = OptionT<SOL_SOCKET, SO_REUSEADDR, int>;

/**
 * @brief Set the socket option SO_BROADCAST (true or false)
 * 
 */
using Broadcast = OptionT<SOL_SOCKET, SO_BROADCAST, int>;

/**
 * @brief Set the socket option SO_KEEPALIVE (true or false)
 * 
 */
using KeepAlive = OptionT<SOL_SOCKET, SO_KEEPALIVE, int>;

/**
 * @brief Set the socket option SO_LINGER (struct linger)
 * 
 */
using Linger = OptionT<SOL_SOCKET, SO_LINGER, ::linger>;

/**
 * @brief Set the socket option SO_OOBINLINE (true or false)
 * 
 */
using OOBInline = OptionT<SOL_SOCKET, SO_OOBINLINE, int>;

/**
 * @brief Set the socket option SO_SNDBUF (int)
 * 
 */
using SendBufSize = OptionT<SOL_SOCKET, SO_SNDBUF, int>;

/**
 * @brief Set the socket option SO_RCVBUF (int)
 * 
 */
using RecvBufSize = OptionT<SOL_SOCKET, SO_RCVBUF, int>;

#if defined(SO_REUSEPORT)
/**
 * @brief Set socket option SO_REUSEPORT (true or false)
 * 
 */
using ReusePort = OptionT<SOL_SOCKET, SO_REUSEPORT, int>;
#endif // defined(SO_REUSEPORT)


// IPPROTO_TCP
/**
 * @brief Set the tcp socket option TCP_NODELAY (true or false)
 * 
 */
using TcpNoDelay = OptionT<IPPROTO_TCP, TCP_NODELAY, dword_t>;

/**
 * @brief Set the tcp socket option TCP_KEEPIDLE (int)
 * 
 */
using TcpKeepIdle = OptionT<IPPROTO_TCP, TCP_KEEPIDLE, dword_t>;

/**
 * @brief Set the tcp socket option TCP_KEEPINTVL (int)
 * 
 */
using TcpKeepIntvl = OptionT<IPPROTO_TCP, TCP_KEEPINTVL, dword_t>;

/**
 * @brief Set the tcp socket option TCP_KEEPCNT (int)
 * 
 */
using TcpKeepCnt = OptionT<IPPROTO_TCP, TCP_KEEPCNT, dword_t>;

#if defined(TCP_USER_TIMEOUT)
/**
 * @brief Set the tcp socket option TCP_USER_TIMEOUT (int)
 * 
 */
using TcpUserTimeout = OptionT<IPPROTO_TCP, TCP_USER_TIMEOUT, int>;
#endif // defined(TCP_USER_TIMEOUT)



// Platform specific
#if defined(_WIN32)
/**
 * @brief Set the udp socket option SIO_UDP_CONNRESET (true or false)
 * 
 */
using UdpConnReset = WSAOptionT<SIO_UDP_CONNRESET, ::DWORD, OptionAccess::Write>;

/**
 * @brief Set the udp socket option SIO_UDP_NETRESET (true or false)
 *
 */
using UdpNetReset = WSAOptionT<SIO_UDP_NETRESET, ::DWORD, OptionAccess::Write>;

/**
 * @brief Get the base handle of the socket
 * 
 */
using BaseHandle = WSAOptionT<SIO_BASE_HANDLE, ::DWORD, OptionAccess::Read>;

/**
 * @brief Get the protocol information of the socket
 * 
 */
using ProtocolInfo = OptionT<SOL_SOCKET, SO_PROTOCOL_INFO, ::WSAPROTOCOL_INFO, OptionAccess::Read>;
#endif // defined(WIN32)


} // namespace sockopt

ILIAS_NS_END


#if !defined(ILIAS_NO_FORMAT)

// Formatter for SOL_SOCKET options
ILIAS_FORMATTER(sockopt::ReuseAddress) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "ReuseAddress({})", bool(opt));
    }
};

ILIAS_FORMATTER(sockopt::Broadcast) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "Broadcast({})", bool(opt));
    }
};

ILIAS_FORMATTER(sockopt::KeepAlive) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "KeepAlive({})", bool(opt));
    }
};

ILIAS_FORMATTER(sockopt::Linger) {
    auto format(const auto &opt, auto &ctxt) const {
        auto li = ::linger(opt);
        return format_to(ctxt.out(), "Linger(.l_onoff = {}, .l_linger = {})", li.l_onoff, li.l_linger);
    }
};

ILIAS_FORMATTER(sockopt::OOBInline) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "OOBInline({})", bool(opt));
    }
};

ILIAS_FORMATTER(sockopt::SendBufSize) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "SendBufSize({})", int(opt));
    }
};

ILIAS_FORMATTER(sockopt::RecvBufSize) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "RecvBufSize({})", int(opt));
    }
};

// IPPROTO_TCP
ILIAS_FORMATTER(sockopt::TcpNoDelay) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "TcpNoDelay({})", bool(opt));
    }
};

ILIAS_FORMATTER(sockopt::TcpKeepIdle) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "TcpKeepIdle({})", int(opt));
    }
};

ILIAS_FORMATTER(sockopt::TcpKeepIntvl) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "TcpKeepIntvl({})", int(opt));
    }
};

ILIAS_FORMATTER(sockopt::TcpKeepCnt) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "TcpKeepCnt({})", int(opt));
    }
};

#if defined(TCP_USER_TIMEOUT)
ILIAS_FORMATTER(sockopt::TcpUserTimeout) {
    auto format(const auto &opt, auto &ctxt) const {
        return format_to(ctxt.out(), "TcpUserTimeout({})", int(opt));
    }
};
#endif // defined(TCP_USER_TIMEOUT)

#endif // !defined(ILIAS_NO_FORMAT)