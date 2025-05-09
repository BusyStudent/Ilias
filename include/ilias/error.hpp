/**
 * @file error.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Generic Error handling system
 * @version 0.1
 * @date 2024-08-07
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/ilias.hpp>
#include <concepts>
#include <utility>
#include <string>
#include <array>

/**
 * @brief Macro to declare a error category
 * 
 * @param errc Error code type
 * @param category Error category type with instance() static method
 * 
 */
#define ILIAS_DECLARE_ERROR(errc, category)             \
    inline auto _ilias_makeError_(errc e) noexcept {    \
        using ::ILIAS_NAMESPACE::Error;                 \
        return Error{int64_t(e), category::instance()}; \
    }

ILIAS_NS_BEGIN

/**
 * @brief The utility namespace for reflection
 * 
 */
namespace reflect {

/**
 * @brief Get the name of the type in compile time
 * 
 * @tparam T 
 * @return std::string_view 
 */
template <auto T>
consteval auto nameof() {
#ifdef _MSC_VER
    constexpr std::string_view name(__FUNCSIG__);
    constexpr size_t nsEnd = name.find_last_of("::");
    constexpr size_t end = name.find('>', nsEnd);
    // size_t dotBegin = name.find_first_of(',');
    // size_t end = name.find_last_of('>');
    // return name.substr(dotBegin + 1, end - dotBegin - 1);
    return name.substr(nsEnd + 1, end - nsEnd - 1);
#else
    std::string_view name(__PRETTY_FUNCTION__);
    size_t eqBegin = name.find_last_of(' ');
    size_t end = name.find_last_of(']');
    return name.substr(eqBegin + 1, end - eqBegin - 1);
#endif
}

/**
 * @brief Get the array of the name of the type in compile time
 * 
 * @tparam T 
 * @return std::array<char,?> (no null terminator)
 */
template <auto T>
consteval auto nameof2() {
    constexpr auto name = nameof<T>();
    std::array<char, name.size()> buffer {0};
    for (size_t i = 0; i < name.size(); ++i) {
        buffer[i] = name[i];
    }
    return buffer;
}

} // namespace refl

// Error forward declaration
class ErrorCategory;
class Error;

/**
 * @brief Check is error code, can be cast into Error class
 * 
 * @tparam T 
 */
template <typename T>
concept IntoError = requires(T t) {
    { _ilias_makeError_(t) } -> std::same_as<Error>;
};


/**
 * @brief Error wrapping generic error codes
 * 
 */
class Error {
public:
    enum Code : int64_t {
        // --- Common
        Ok = 0,                      //< No Error
        Unknown,                     //< Unknown Error

        // --- Operation
        Canceled,                    //< Operation is Canceled
        Pending,                     //< Operation is pending

        // --- Channel
        ChannelBroken,               //< Channel is broken
        ChannelEmpty,                //< Channel is empty
        ChannelFull,                 //< Channel is full

        // --- Socket
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

        // -- Misc
        ZeroReturn,                  //< This operation failed because the unlaying interface returned zero
        OutOfMemory,                 //< Out of memory

        // --- DNS
        HostNotFound,                //< Host not found
        NoDataRecord,                //< No data record of requested type

        // --- SSL
        SSL,                         //< SSL 
        SSLUnknown,                  //< Unkown error from ssl layer

        // --- Http
        HttpBadReply,                //< A reply with bad format or another things
        HttpBadRequest,              //< The given request some field is not valid

        // --- WebSocket
        WebSocketBadHandshake,       //< Bad handshake
        WebSocketBadFrame,           //< Bad frame
        WebSocketClosed,             //< The peer close the connection, so operation cannot continue

        // --- Socks5
        Socks5AuthenticationFailed,  //< Authentication failed
        Socks5Unknown,               //< Unknown error


        // --- User
        User,                        //< User defined error beginning
    };

    /**
     * @brief Construct a new Error object from registered error code enum
     * 
     * @tparam T 
     * @param err 
     */
    template <IntoError T>
    Error(T err);

    /**
     * @brief Construct a new Error object from a error code and category
     * 
     * @param err 
     * @param c 
     */
    Error(int64_t err, const ErrorCategory &c);

    /**
     * @brief Construct a new Error by copy
     * 
     */
    Error(const Error &err);

    /**
     * @brief Construct a new Error object, equal to Error::Ok
     * 
     */
    Error();

    /**
     * @brief Destroy the Error object
     * 
     */
    ~Error();

    /**
     * @brief Does this Error is ok?
     * 
     * @return true 
     * @return false 
     */
    auto isOk() const -> bool;

    /**
     * @brief Get the value of the error
     * 
     * @return int64_t 
     */
    auto value() const -> int64_t;

    /**
     * @brief Get the message of the error
     * 
     * @return std::string 
     */
    auto message() const -> std::string;

    /**
     * @brief Get the category of the error
     * 
     * @return const ErrorCategory &
     */
    auto category() const -> const ErrorCategory &;

    /**
     * @brief Get the error code value and message
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;

    /**
     * @brief Assign a value
     * 
     * @return Error& 
     */
    auto operator =(const Error &) -> Error & = default;

    /**
     * @brief Check if the error cod
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const {
        return !isOk();
    }
private:
    int64_t mErr = Ok;
    const ErrorCategory *mCategory = nullptr;
};


/**
 * @brief A Class for user to explain the error code
 * 
 */
class ErrorCategory {
public:

    /**
     * @brief Get the message string of this value
     * 
     * @return std::string 
     */
    virtual auto message(int64_t value) const -> std::string = 0;

    /**
     * @brief Get the name of the category
     * 
     * @return std::string_view 
     */
    virtual auto name() const -> std::string_view = 0;
    
    /**
     * @brief Check another error code is equ
     * 
     * @param self Current error belong self category
     * @param other Another Error wait to be compared
     * @return true 
     * @return false 
     */
    virtual auto equivalent(int64_t self, const Error &other) const -> bool;
};

/**
 * @brief Default category for translate bultin error code
 * 
 */
class IliasCategory final : public ErrorCategory {
public:
    auto message(int64_t err) const -> std::string override;
    auto name() const -> std::string_view override;

    static auto instance() -> const IliasCategory &;
private:
    template <size_t ...N>
    static auto messageImpl(std::index_sequence<N...>, int64_t i) -> std::string_view;
};

ILIAS_DECLARE_ERROR(Error::Code, IliasCategory);

// --- Error Impl
template <size_t ...N>
inline auto IliasCategory::messageImpl(std::index_sequence<N...>, int64_t i) -> std::string_view {
    constinit static auto data = std::tuple {
        reflect::nameof2<Error::Code(N)>()...
    };
    std::array<std::string_view, sizeof...(N)> table {
        std::string_view(
            std::get<N>(data).data(),
            std::get<N>(data).size()
        )...
    };
    if (i < 0 || i >= int64_t(table.size())) {
        return "Unknown error";
    }
    return table[i];
}
inline auto IliasCategory::instance() -> const IliasCategory & {
    static IliasCategory c;
    return c;
}
inline auto IliasCategory::message(int64_t err) const -> std::string {
    return std::string(
        messageImpl(std::make_index_sequence<size_t(Error::User)>(), err)
    );
}
inline auto IliasCategory::name() const -> std::string_view {
    return "ilias";
}
inline auto ErrorCategory::equivalent(int64_t value, const Error &other) const -> bool {
    return this == &other.category() && value == other.value();
}

// --- Error
template <IntoError T>
inline Error::Error(T err) : Error(_ilias_makeError_(err)) { }
inline Error::Error(int64_t err, const ErrorCategory &c) : mErr(err), mCategory(&c) { }
inline Error::Error() : mErr(Ok), mCategory(&IliasCategory::instance()) { }
inline Error::Error(const Error &) = default;
inline Error::~Error() = default;

inline auto Error::message() const -> std::string {
    return mCategory->message(mErr);
}
inline auto Error::category() const -> const ErrorCategory & {
    return *mCategory;   
}
inline auto Error::value() const -> int64_t {
    return mErr;
}
inline auto Error::isOk() const -> bool {
    return mErr == Ok;
}
inline auto Error::toString() const -> std::string {
    std::string ret;
    ret += '[';
    ret += mCategory->name();
    ret += ": ";
    ret += std::to_string(mErr);
    ret += "] ";
    ret += message();
    return ret;
}

// --- Compare
inline auto operator ==(const ErrorCategory &cat1, const ErrorCategory &cat2) noexcept -> bool {
    return &cat1 == &cat2;
}
inline auto operator !=(const ErrorCategory &cat1, const ErrorCategory &cat2) noexcept -> bool {
    return &cat1 != &cat2;
}
inline auto operator ==(const Error &left, const Error &right) noexcept -> bool {
    // Only one category think that we are the same
    return left.category().equivalent(left.value(), right) || right.category().equivalent(right.value(), left);
}
inline auto operator !=(const Error &left, const Error &right) noexcept -> bool {
    return (!left.category().equivalent(left.value(), right)) && (!right.category().equivalent(right.value(), left));
}

ILIAS_NS_END

// --- Formatter for Error
#if !defined(ILIAS_NO_FORMAT)
ILIAS_FORMATTER(Error) {
    auto format(const auto &err, auto &ctxt) const {
        return format_to(ctxt.out(), "[{}: {}] {}", err.category().name(), err.value(), err.message());
    }
};
#endif