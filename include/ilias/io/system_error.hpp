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

#include <ilias/io/error.hpp>
#include <ilias/defines.hpp>
#include <ilias/result.hpp>
#include <system_error>
#include <cerrno>
#include <string> 

#if   defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
    #define MAP(x) WSA##x
#elif defined(__unix__)
    #include <errno.h>
    #include <string.h>
    #define MAP(x) x
#endif

ILIAS_NS_BEGIN

/**
 * @brief The system error category, 
 * 
 */
class ILIAS_API SystemCategory final : public std::error_category {
public:
    constexpr SystemCategory() noexcept {}

    auto name() const noexcept -> const char* override;
    auto message(int value) const -> std::string override;
    auto equivalent(int value, const std::error_condition &other) const noexcept -> bool override;
    auto default_error_condition(int value) const noexcept -> std::error_condition override;
    
    static auto instance() noexcept -> const SystemCategory &;
};

/**
 * @brief System Error class, wrapping the system error (Win32 or POSIX)
 * 
 */
class ILIAS_API SystemError {
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
    SystemError(Code err) : mErr(err) { }
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
     * @brief Convert to IoError
     * 
     * @return IoError 
     */
    auto toIoError() const -> IoError;

    /**
     * @brief Compare with another SystemError
     * 
     */
    auto operator <=>(const SystemError &other) const noexcept = default;

    /**
     * @brief cast to int
     * 
     * @return Error 
     */
    explicit operator int() const { return mErr; }

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

ILIAS_DECLARE_ERROR(SystemError, SystemCategory);
ILIAS_DECLARE_ERROR(SystemError::Code, SystemCategory);


// SystemError
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

#undef MAP