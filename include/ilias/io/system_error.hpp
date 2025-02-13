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
    #define MAP(x) WSA##x
    #elif defined(__unix__)
    #include <errno.h>
    #include <string.h>
    #define MAP(x) x
#endif

ILIAS_NS_BEGIN

/**
 * @brief System Error class, wrapping the system error (Win32 or POSIX)
 * 
 */
class SystemError {
public:
    /**
     * @brief The known system dependent error codes
     * 
     */
    enum Code : error_t {
        Ok                            = 0,
        AccessDenied                  = MAP(EACCES),
        AddressInUse                  = MAP(EADDRINUSE),
        AddressNotAvailable           = MAP(EADDRNOTAVAIL),
        AddressFamilyNotSupported     = MAP(EAFNOSUPPORT),
        AlreadyInProgress             = MAP(EALREADY),
        BadFileDescriptor             = MAP(EBADF),
        ConnectionAborted             = MAP(ECONNABORTED),
        ConnectionRefused             = MAP(ECONNREFUSED),
        ConnectionReset               = MAP(ECONNRESET),
        DestinationAddressRequired    = MAP(EDESTADDRREQ),
        BadAddress                    = MAP(EFAULT),
        HostDown                      = MAP(EHOSTDOWN),
        HostUnreachable               = MAP(EHOSTUNREACH),
        InProgress                    = MAP(EINPROGRESS),
        InvalidArgument               = MAP(EINVAL),
        SocketIsConnected             = MAP(EISCONN),
        TooManyOpenFiles              = MAP(EMFILE),
        MessageTooLarge               = MAP(EMSGSIZE),
        NetworkDown                   = MAP(ENETDOWN),
        NetworkReset                  = MAP(ENETRESET),
        NetworkUnreachable            = MAP(ENETUNREACH),
        NoBufferSpaceAvailable        = MAP(ENOBUFS),
        ProtocolOptionNotSupported    = MAP(ENOPROTOOPT),
        SocketIsNotConnected          = MAP(ENOTCONN),
        NotASocket                    = MAP(ENOTSOCK),
        OperationNotSupported         = MAP(EOPNOTSUPP),
        ProtocolFamilyNotSupported    = MAP(EPFNOSUPPORT),
        ProtocolNotSupported          = MAP(EPROTONOSUPPORT),
        SocketShutdown                = MAP(ESHUTDOWN),
        SocketTypeNotSupported        = MAP(ESOCKTNOSUPPORT),
        TimedOut                      = MAP(ETIMEDOUT),
        WouldBlock                    = MAP(EWOULDBLOCK),
#if   defined(ERROR_OPERATION_ABORTED) // For windows cancellation
        Canceled                      = ERROR_OPERATION_ABORTED,
#elif defined(ECANCELED) // For linux cancellation
        Canceled                      = ECANCELED,
#endif
    };

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
     * @brief Convert to string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;

    /**
     * @brief Compare with another SystemError
     * 
     */
    auto operator <=>(const SystemError &other) const noexcept = default;

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
    static auto fromErrno() -> SystemError;
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
ILIAS_DECLARE_ERROR(SystemError::Code, SystemCategory);

// --- SystemCategory
inline auto SystemCategory::name() const -> std::string_view {
    return "os";
}

inline auto SystemCategory::message(int64_t code) const -> std::string {
    return SystemError(code).toString();
}

inline auto SystemCategory::translate(error_t code) -> Error::Code {
    switch (code) {
        case SystemError::Ok                        : return Error::Ok;
        case SystemError::AccessDenied              : return Error::AccessDenied;
        case SystemError::AddressInUse              : return Error::AddressInUse;
        case SystemError::AddressNotAvailable       : return Error::AddressNotAvailable;
        case SystemError::AddressFamilyNotSupported : return Error::AddressFamilyNotSupported;
        case SystemError::AlreadyInProgress         : return Error::AlreadyInProgress;
        case SystemError::BadFileDescriptor         : return Error::BadFileDescriptor;
        case SystemError::ConnectionAborted         : return Error::ConnectionAborted;
        case SystemError::ConnectionRefused         : return Error::ConnectionRefused;
        case SystemError::ConnectionReset           : return Error::ConnectionReset;
        case SystemError::DestinationAddressRequired: return Error::DestinationAddressRequired;
        case SystemError::BadAddress                : return Error::BadAddress;
        case SystemError::HostDown                  : return Error::HostDown;
        case SystemError::HostUnreachable           : return Error::HostUnreachable;
        case SystemError::InProgress                : return Error::InProgress;
        case SystemError::InvalidArgument           : return Error::InvalidArgument;
        case SystemError::SocketIsConnected         : return Error::SocketIsConnected;
        case SystemError::TooManyOpenFiles          : return Error::TooManyOpenFiles;
        case SystemError::MessageTooLarge           : return Error::MessageTooLarge;
        case SystemError::NetworkDown               : return Error::NetworkDown;
        case SystemError::NetworkReset              : return Error::NetworkReset;
        case SystemError::NetworkUnreachable        : return Error::NetworkUnreachable;
        case SystemError::NoBufferSpaceAvailable    : return Error::NoBufferSpaceAvailable;
        case SystemError::ProtocolOptionNotSupported: return Error::ProtocolOptionNotSupported;
        case SystemError::SocketIsNotConnected      : return Error::SocketIsNotConnected;
        case SystemError::NotASocket                : return Error::NotASocket;
        case SystemError::OperationNotSupported     : return Error::OperationNotSupported;
        case SystemError::ProtocolFamilyNotSupported: return Error::ProtocolFamilyNotSupported;
        case SystemError::ProtocolNotSupported      : return Error::ProtocolNotSupported;
        case SystemError::SocketShutdown            : return Error::SocketShutdown;
        case SystemError::SocketTypeNotSupported    : return Error::SocketTypeNotSupported;
        case SystemError::TimedOut                  : return Error::TimedOut;
        case SystemError::WouldBlock                : return Error::WouldBlock;
        case SystemError::Canceled                  : return Error::Canceled;
        default                                     : return Error::Unknown;
    }
}

inline auto SystemCategory::equivalent(int64_t value, const Error &other) const -> bool {
    if (this == &other.category() && value == other.value()) {
        // Category is same, value is same
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

inline auto SystemError::toString() const -> std::string {

#ifdef _WIN32
    wchar_t *args = nullptr;
    ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, mErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&args), 0, nullptr
    );
    auto len = ::WideCharToMultiByte(CP_UTF8, 0, args, -1, nullptr, 0, nullptr, nullptr);
    std::string buf(len - 1, '\0'); //< This len includes the null terminator
    len = ::WideCharToMultiByte(CP_UTF8, 0, args, -1, &buf[0], len, nullptr, nullptr);
    ::LocalFree(args);
    return buf;
#else
    return ::strerror(mErr);
#endif

}


ILIAS_NS_END

// --- Formatter for SystemError
#if !defined(ILIAS_NO_FORMAT)
ILIAS_FORMATTER(SystemError) {
    auto format(const auto &err, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", err.toString());
    }
};
#endif

#undef MAP