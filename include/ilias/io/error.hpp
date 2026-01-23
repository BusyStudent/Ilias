#pragma once
#include <ilias/defines.hpp>
#include <ilias/result.hpp>
#include <system_error>
#include <string>

#define ILIAS_DECLARE_ERROR(errc, category)                                     \
    inline auto _ilias_error_category_of(errc) -> const std::error_category & { \
        return category::instance();                                            \
    }                                                                           \

ILIAS_NS_BEGIN

// Result for doing io operations
template <typename T>
using IoResult = Result<T, std::error_code>;

// Async Result for doing io operations
template <typename T>
using IoTask = Task<IoResult<T> >;

// Async Generator for doing io operations
template <typename T>
using IoGenerator = Generator<IoResult<T> >;

// Interop with std::error_code
template <typename T>
concept IntoError = requires(T t) {
    _ilias_error_category_of(t);  // Get the error category of T
};

/**
 * @brief The error category for io operations
 * 
 */
class ILIAS_API IoCategory final : public std::error_category {
public:
    constexpr IoCategory() {}

    auto name() const noexcept -> const char* override;
    auto message(int value) const -> std::string override;
    auto equivalent(int value, const std::error_condition &other) const noexcept -> bool override;

    static auto instance() noexcept -> const IoCategory &;
};

/**
 * @brief The platform independent error code for io operations, if user want to compare, use toKind(IoError::XXX)
 * 
 */
class ILIAS_API IoError {
public:
    enum Code : int {
        Ok = 0,
        
        // System
        AccessDenied,                //< Access is denied
        AddressFamilyNotSupported,   //< Address family is not supported
        AddressInUse,                //< Address is already in use
        AddressNotAvailable,         //< Address is not available
        AlreadyInProgress,           //< Operation is already in progress
        BadAddress,                  //< Bad address
        BadFileDescriptor,           //< Bad file descriptor
        ConnectionAborted,           //< Connection aborted by peer
        ConnectionRefused,           //< Connection refused by peer
        ConnectionReset,             //< Connection reset by peer
        DestinationAddressRequired,  //< Destination address is required
        HostDown,                    //< Host is down
        HostUnreachable,             //< Host is unreachable
        InProgress,                  //< Operation is in progress
        InvalidArgument,             //< Invalid argument
        MessageTooLarge,             //< Message is too large
        NetworkDown,                 //< Network is down
        NetworkReset,                //< Network reset by peer
        NetworkUnreachable,          //< Network is unreachable
        NoBufferSpaceAvailable,      //< No buffer space available
        NotASocket,                  //< fd is not a socket
        OperationNotSupported,       //< Operation is not supported
        ProtocolFamilyNotSupported,  //< Protocol family is not supported
        ProtocolNotSupported,        //< Protocol is not supported
        ProtocolOptionNotSupported,  //< Protocol option is not supported
        SocketIsConnected,           //< Socket is connected
        SocketIsNotConnected,        //< Socket is not connected
        SocketShutdown,              //< Socket is shutdown
        SocketTypeNotSupported,      //< Socket type is not supported
        TimedOut,                    //< Operation timed out
        TooManyOpenFiles,            //< Too many open files
        WouldBlock,                  //< Socket is non-blocking, operation would block
        Canceled,                    //< Operation was canceled

        // System, getaddrinfo
        HostNotFound,                //< Host not found

        // TLS
        Tls,                         //< Generic TLS Error

        // Utils
        UnexpectedEOF,               //< The operation can't be completed because the lower level read io call returned zero, we need more data
        WriteZero,                   //< The operation can't be completed because the lower level write io call returned zero

        Other,                       //< Other error
        Unknown = Other,             //< For compatibility with old code
    };

    explicit IoError(int err) : mErr(static_cast<Code>(err)) { }
    IoError(Code err) : mErr(err) { }
    IoError() = default;

    /**
     * @brief Get the error string for the given error code
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;

    /**
     * @brief Convert the code to the std::errc
     * 
     * @return std::errc 
     */
    auto toStd() const -> std::errc;

    /**
     * @brief Compare 
     * 
     */
    auto operator <=>(const IoError &other) const noexcept = default;

    explicit operator int() const noexcept { 
        return int(mErr);
    }
private:
    Code mErr = Ok;
};

/**
 * @brief Convert the code to std::error_condition, useful when compare with std::error_code
 * 
 * @param err 
 * @return std::error_condition 
 */
inline auto toKind(IoError err) noexcept -> std::error_condition {
    return {static_cast<int>(err), IoCategory::instance()};
}

inline auto toKind(IoError::Code err) noexcept -> std::error_condition {
    return {static_cast<int>(err), IoCategory::instance()};
}

ILIAS_DECLARE_ERROR(IoError::Code, IoCategory);
ILIAS_DECLARE_ERROR(IoError, IoCategory);

template <IntoError T>
inline auto make_error_code(T t) noexcept -> std::error_code {
    return {static_cast<int>(t), _ilias_error_category_of(t)};
}

ILIAS_NS_END

template <ilias::IntoError T> 
struct std::is_error_code_enum<T> : std::true_type {};