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
    #define ILIAS_MAP(x) WSA##x
#elif defined(__unix__)
    #include <cstring>
    #include <cerrno>
    #define ILIAS_MAP(x) x
#endif

ILIAS_NS_BEGIN

/**
 * @brief The system error category, 
 * 
 */
class SystemCategory final : public std::error_category {
public:
    auto name() const noexcept -> const char* override;
    auto message(int value) const -> std::string override;
    auto equivalent(int value, const std::error_condition &other) const noexcept -> bool override;
    auto default_error_condition(int value) const noexcept -> std::error_condition override;
    
    ILIAS_API
    static auto instance() noexcept -> const SystemCategory &;
private:
    constexpr SystemCategory() noexcept {}
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
        AccessDenied                  = ILIAS_MAP(EACCES),
        AddressInUse                  = ILIAS_MAP(EADDRINUSE),
        AddressNotAvailable           = ILIAS_MAP(EADDRNOTAVAIL),
        AddressFamilyNotSupported     = ILIAS_MAP(EAFNOSUPPORT),
        AlreadyInProgress             = ILIAS_MAP(EALREADY),
        BadFileDescriptor             = ILIAS_MAP(EBADF),
        ConnectionAborted             = ILIAS_MAP(ECONNABORTED),
        ConnectionRefused             = ILIAS_MAP(ECONNREFUSED),
        ConnectionReset               = ILIAS_MAP(ECONNRESET),
        DestinationAddressRequired    = ILIAS_MAP(EDESTADDRREQ),
        BadAddress                    = ILIAS_MAP(EFAULT),
        HostDown                      = ILIAS_MAP(EHOSTDOWN),
        HostUnreachable               = ILIAS_MAP(EHOSTUNREACH),
        InProgress                    = ILIAS_MAP(EINPROGRESS),
        Interrupted                   = ILIAS_MAP(EINTR),
        InvalidArgument               = ILIAS_MAP(EINVAL),
        SocketIsConnected             = ILIAS_MAP(EISCONN),
        TooManyOpenFiles              = ILIAS_MAP(EMFILE),
        MessageTooLarge               = ILIAS_MAP(EMSGSIZE),
        NetworkDown                   = ILIAS_MAP(ENETDOWN),
        NetworkReset                  = ILIAS_MAP(ENETRESET),
        NetworkUnreachable            = ILIAS_MAP(ENETUNREACH),
        NoBufferSpaceAvailable        = ILIAS_MAP(ENOBUFS),
        ProtocolOptionNotSupported    = ILIAS_MAP(ENOPROTOOPT),
        ProtocolWrongTypeForSocket    = ILIAS_MAP(EPROTOTYPE),
        SocketIsNotConnected          = ILIAS_MAP(ENOTCONN),
        NotASocket                    = ILIAS_MAP(ENOTSOCK),
        OperationNotSupported         = ILIAS_MAP(EOPNOTSUPP),
        ProtocolFamilyNotSupported    = ILIAS_MAP(EPFNOSUPPORT),
        ProtocolNotSupported          = ILIAS_MAP(EPROTONOSUPPORT),
        SocketShutdown                = ILIAS_MAP(ESHUTDOWN),
        SocketTypeNotSupported        = ILIAS_MAP(ESOCKTNOSUPPORT),
        TimedOut                      = ILIAS_MAP(ETIMEDOUT),
        WouldBlock                    = ILIAS_MAP(EWOULDBLOCK),
#if   defined(ERROR_OPERATION_ABORTED) // For windows cancellation
        Canceled                      = ERROR_OPERATION_ABORTED,
#elif defined(ECANCELED)               // For linux cancellation
        Canceled                      = ECANCELED,
#endif
    };

    constexpr explicit SystemError(error_t err) : mErr(err) {}
    constexpr SystemError(Code err) : mErr(err) {}
    constexpr SystemError() = default;

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
ILIAS_FORMATTABLE(SystemError);

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

// ADL for error code
inline auto make_error_code(SystemError::Code err) -> std::error_code {
    return {static_cast<int>(err), SystemCategory::instance()};
}

inline auto make_error_code(SystemError err) -> std::error_code {
    return {static_cast<int>(err), SystemCategory::instance()};
}

ILIAS_NS_END

// Enable error code
template <>
struct std::is_error_code_enum<ilias::SystemError::Code> : std::true_type {};

template <>
struct std::is_error_code_enum<ilias::SystemError> : std::true_type {};

#undef ILIAS_MAP