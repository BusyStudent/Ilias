#pragma once

/**
 * @file system_error.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapping the system error from io
 * @version 0.1
 * @date 2024-08-07
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include <ilias/ilias.hpp>
#include <ilias/error.hpp>
#include <cerrno>

#if defined(_WIN32)
    #define _WINSOCKAPI_ // Avoid windows.h to include winsock.h
    #define NOMINMAX
    #include <Windows.h>
#endif

ILIAS_NS_BEGIN

/**
 * @brief System Error class
 * 
 */
class SystemError {
public:
    explicit SystemError(error_t err) : mErr(err) { }
    SystemError() = default;

    /**
     * @brief Check the error is ok
     * 
     * @return true 
     * @return false 
     */
    auto isOk() const noexcept -> bool;

    /**
     * @brief cast to int64_t
     * 
     * @return Error 
     */
    explicit operator int64_t() const { return mErr; }

    /**
     * @brief Get the system error from the errno
     * 
     * @param err 
     * @return SystemError 
     */
    static SystemError fromErrno();
private:
    error_t mErr = 0;
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

ILIAS_DECLARE_ERROR(SystemError, SystemCategory);

// --- SystemCategory
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
    auto len = ::WideCharToMultiByte(CP_UTF8, 0, args, -1, nullptr, 0, nullptr, nullptr);
    std::string buf(len - 1, '\0'); //< This len includes the null terminator
    len = ::WideCharToMultiByte(CP_UTF8, 0, args, -1, &buf[0], len, nullptr, nullptr);
    ::LocalFree(args);
    return buf;
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

#if defined(ERROR_OPERATION_ABORTED) //< For IOCP Cancelation
        case ERROR_OPERATION_ABORTED: return Error::Canceled;
#endif

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


// --- SystemError
inline auto SystemError::fromErrno() -> SystemError {

#if defined(_WIN32)
    auto err = ::GetLastError();
#else
    auto err = errno;
#endif
    return SystemError(err);
}
inline auto SystemError::isOk() const noexcept -> bool {
    return mErr == 0;
}

ILIAS_NS_END