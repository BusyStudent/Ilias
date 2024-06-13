#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <array>

#ifndef ILIAS_NAMESPACE
    #define ILIAS_NAMESPACE Ilias
#endif

#ifndef ILIAS_ASSERT
    #define ILIAS_ASSERT(x) assert(x)
    #include <cassert>
#endif

#ifndef ILIAS_CHECK
    #define ILIAS_CHECK(x) if (!(x)) { ILIAS_ASSERT(x); ::abort(); }
#endif

#ifndef ILIAS_MALLOC 
    #define ILIAS_REALLOC(x, y) ::realloc(x, y)
    #define ILIAS_MALLOC(x) ::malloc(x)
    #define ILIAS_FREE(x) ::free(x)
    #include <cstdlib>
#endif

#define ILIAS_DECLARE_ERROR(errc, category_)   \
    template <>                                \
    class ErrorTraits<errc> {                  \
    public:                                    \
        static const category_ & category() {  \
            return category_::instance();      \
        }                                      \
    }

#define ILIAS_ASSERT_MSG(x, msg) ILIAS_ASSERT((x) && (msg))
#define ILIAS_NS_BEGIN namespace ILIAS_NAMESPACE {
#define ILIAS_NS_END }


ILIAS_NS_BEGIN

// --- Network
class IPEndpoint;
class IPAddress4;
class IPAddress6;
class IPAddress;
class SocketView;
class Socket;

// --- Coroutine 
template <typename T = void>
class Task;
template <typename T>
class AwaitTransform;

// --- Error
template <typename T>
class ErrorTraits;
class ErrorCategory;
class Error;

/**
 * @brief Check is error code, can be put into Error class
 * 
 * @tparam T 
 */
template <typename T>
concept ErrorCode = requires(T t) {
    ErrorTraits<T>::category();
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
    virtual auto message(uint32_t value) const -> std::string = 0;
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
    virtual auto equivalent(uint32_t self, const Error &other) const -> bool;
};

/**
 * @brief Error wrapping generic error codes
 * 
 */
class Error {
public:
    enum Code : uint32_t {
        // --- Common
        Ok = 0,                      //< No Error
        Unknown,                     //< Unknown Error

        // --- Coroutine
        Canceled,                    //< Task is Canceled
        Pending,                     //< Task is pending
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

        // --- DNS
        HostNotFound,                //< Host not found
        NoDataRecord,                //< No data record of requested type

        // --- SSL
        SSL,                         //< SSL 
        SSLUnknown,                  //< Unkown error from ssl layer

        // --- Http
        HttpBadReply,                //< A reply with bad format or another things
        HttpBadRequest,              //< The given request some field is not valid

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
    template <ErrorCode T>
    Error(T err);
    /**
     * @brief Construct a new Error object from a error code and category
     * 
     * @param err 
     * @param c 
     */
    Error(uint32_t err, const ErrorCategory &c);
    Error(const Error &);
    Error();
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
     * @return uint32_t 
     */
    auto value() const -> uint32_t;
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
     * @brief Get Error code from errno (WSAGetLastError)
     * 
     * @return 
     */
    static auto fromErrno() -> Error;
    /**
     * @brief Get Error code from h_errno (WSAGetLastError)
     * 
     * @return 
     */
    static auto fromHErrno() -> Error;
    /**
     * @brief Convert Errono code to Error
     * 
     * @param err 
     * @return Error 
     */
    static auto fromErrno(uint32_t err) -> Error;
    /**
     * @brief Convert h_errno code to Error
     * 
     * @param err 
     * @return Error 
     */
    static auto fromHErrno(uint32_t err) -> Error;
private:
    uint32_t mErr = Ok;
    const ErrorCategory *mCategory = nullptr;
};

/**
 * @brief Default category for translate bultin error code
 * 
 */
class IliasCategory final : public ErrorCategory {
public:
    auto message(uint32_t err) const -> std::string override;
    auto name() const -> std::string_view override;

    static auto instance() -> const IliasCategory &;
private:
    template <Error::Code>
    static consteval auto _errMessage();
    template <size_t ...N>
    static consteval auto _errTable(std::index_sequence<N...>);
};

ILIAS_DECLARE_ERROR(Error::Code, IliasCategory);

// --- Error Impl
template <Error::Code c>
inline consteval auto IliasCategory::_errMessage() {
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
template <size_t ...N>
inline consteval auto IliasCategory::_errTable(std::index_sequence<N...>) {
    constexpr std::array<std::string_view, sizeof ...(N)> table = {
        _errMessage<Error::Code(N)>()...
    };
    return table;
}
inline auto IliasCategory::instance() -> const IliasCategory & {
    static IliasCategory c;
    return c;
}
inline auto IliasCategory::message(uint32_t err) const -> std::string {
    constexpr auto table = _errTable(std::make_index_sequence<size_t(Error::User)>());
    if (err > table.size()) {
        return "Unknown error";
    }
    return std::string(table[err]);
}
inline auto IliasCategory::name() const -> std::string_view {
    return "ilias";
}
inline auto ErrorCategory::equivalent(uint32_t value, const Error &other) const -> bool {
    return this == &other.category() && value == other.value();
}

inline Error::Error(uint32_t err, const ErrorCategory &c) : mErr(err), mCategory(&c) { }
template <ErrorCode T>
inline Error::Error(T err) : mErr(uint32_t(err)), mCategory(&ErrorTraits<T>::category()) { }
inline Error::Error() : mErr(Ok), mCategory(&IliasCategory::instance()) { }
inline Error::Error(const Error &) = default;
inline Error::~Error() = default;

inline auto Error::message() const -> std::string {
    return mCategory->message(mErr);
}
inline auto Error::category() const -> const ErrorCategory & {
    return *mCategory;   
}
inline auto Error::value() const -> uint32_t {
    return mErr;
}
inline auto Error::isOk() const -> bool {
    return mErr == Ok;
}
inline auto Error::toString() const -> std::string {
    std::string ret;
    ret += '[';
    ret += mCategory->name();
    ret += ':';
    ret += std::to_string(mErr);
    ret += "] ";
    ret += message();
    return ret;
}

// --- Compare
inline bool operator ==(const ErrorCategory &cat1, const ErrorCategory &cat2) noexcept {
    return &cat1 == &cat2;
}
inline bool operator !=(const ErrorCategory &cat1, const ErrorCategory &cat2) noexcept {
    return &cat1 != &cat2;
}
inline bool operator ==(const Error &left, const Error &right) noexcept {
    // Only one category think that we are the same
    return left.category().equivalent(left.value(), right) || right.category().equivalent(right.value(), left);
}
inline bool operator !=(const Error &left, const Error &right) noexcept {
    return (!left.category().equivalent(left.value(), right)) && (!right.category().equivalent(right.value(), left));
}

ILIAS_NS_END