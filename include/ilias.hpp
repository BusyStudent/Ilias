#pragma once

#include <concepts>
#include <string>
#include <array>

#ifndef ILIAS_NAMESPACE
    #define ILIAS_NAMESPACE Ilias
#endif

#ifndef ILIAS_ASSERT
    #define ILIAS_ASSERT(x) assert(x)
    #include <cassert>
#endif

#ifndef ILIAS_MALLOC 
    #define ILIAS_REALLOC(x, y) ::realloc(x, y)
    #define ILIAS_MALLOC(x) ::malloc(x)
    #define ILIAS_FREE(x) ::free(x)
    #include <cstdlib>
#endif

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

/**
 * @brief Error wrapping socket error codes
 * 
 */
class Error {
public:
    enum class Code : uint32_t {
        // --- Common
        Ok = 0,                      //< No Error
        Unknown,                     //< Unknown Error

        // --- Coroutine
        Canceled,                     //< Task is Canceled
        Pending,                     //< Task is pending

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

        // --- SSL
        SSL,                         //< SSL 

        // --- User
        User,                        //< User defined error beginning
    };
    using enum Code;

    constexpr Error() = default;
    constexpr Error(Code err) : mErr(err) { }
    constexpr Error(const Error &) = default;
    constexpr ~Error() = default;

    /**
     * @brief Does this Error is ok?
     * 
     * @return true 
     * @return false 
     */
    bool isOk() const;
    /**
     * @brief Get the message of the error
     * 
     * @return std::string 
     */
    std::string message() const;
    /**
     * @brief Get Error code from errno (WSAGetLastError)
     * 
     * @return SockError 
     */
    static Error fromErrno();
    /**
     * @brief Get Error code from h_errno (WSAGetLastError)
     * 
     * @return SockError 
     */
    static Error fromHErrno();
    /**
     * @brief Convert Errono code to Error
     * 
     * @param err 
     * @return Error 
     */
    static Error fromErrno(uint32_t err);
    /**
     * @brief Convert h_errno code to Error
     * 
     * @param err 
     * @return Error 
     */
    static Error fromHErrno(uint32_t err);

    operator Code() const noexcept {
        return mErr;
    }
private:
    template <Code>
    static consteval auto _errMessage();
    template <size_t ...N>
    static consteval auto _errTable(std::index_sequence<N...>);

    Code mErr = Ok;
};

// --- Error Impl
template <Error::Code c>
inline consteval auto Error::_errMessage() {
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
inline consteval auto Error::_errTable(std::index_sequence<N...>) {
    constexpr std::array<std::string_view, sizeof ...(N)> table = {
        _errMessage<Code(N)>()...
    };
    return table;
}
inline std::string Error::message() const {
    constexpr auto table = _errTable(std::make_index_sequence<size_t(User)>());
    if (size_t(mErr) > table.size()) {
        return "Unknown error";
    }
    return std::string(table[size_t(mErr)]);
}
inline bool Error::isOk() const {
    return mErr == Ok;
}

ILIAS_NS_END