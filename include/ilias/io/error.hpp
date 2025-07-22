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
    auto name() const noexcept -> const char* override;
    auto message(int value) const -> std::string override;
    
    static auto instance() -> IoCategory &;
};

/**
 * @brief The platform independent error code for io operations
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

        // Utils
        UnexpectedEOF,               //< Unexpected end of file
        ZeroReturn,                  //< The operation can't be completed because the lower level io call returned zero

        Other,                       //< Other error
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

ILIAS_DECLARE_ERROR(IoError::Code, IoCategory);
ILIAS_DECLARE_ERROR(IoError, IoCategory);

template <typename T>
inline auto make_error_code(T t) -> std::error_code {
    return IoError(static_cast<int>(t), _ilias_error_category_of(t));
}

ILIAS_NS_END

template <ILIAS_NAMESPACE::IntoError T> 
struct std::is_error_code_enum<T> : std::true_type {};