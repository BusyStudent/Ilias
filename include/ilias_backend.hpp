#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"

// --- Import coroutine if
#if defined(__cpp_lib_coroutine)
#include "ilias_co.hpp"
#endif


ILIAS_NS_BEGIN

// --- AwaitResult
#if defined(__cpp_lib_coroutine)
template <typename T>
using AwaitResult = CallbackAwaitable<T>;
#endif

// --- Function
#if defined(__cpp_lib_move_only_function)
template <typename ...Args>
using Function = std::move_only_function<Args...>;
#else
template <typename ...Args>
using Function = std::function<Args...>;
#endif

// --- Types
using RecvHandlerArgs = Expected<size_t, SockError>;
using RecvHandler = Function<void (RecvHandlerArgs &&)>;

using SendHandlerArgs = Expected<size_t, SockError>;
using SendHandler = Function<void (SendHandlerArgs &&)>;

template <typename T>
using AcceptHandlerArgsT = Expected<std::pair<T, IPEndpoint>, SockError>;
using AcceptHandlerArgs = Expected<std::pair<Socket, IPEndpoint> , SockError>;
using AcceptHandler = Function<void (AcceptHandlerArgs &&)>;

using ConnectHandlerArgs = Expected<void, SockError>;
using ConnectHandler = Function<void (ConnectHandlerArgs &&)>;

using RecvfromHandlerArgs = Expected<std::pair<size_t, IPEndpoint>, SockError>;
using RecvfromHandler = Function<void (RecvfromHandlerArgs &&)>;

using SendtoHandlerArgs = Expected<size_t, SockError>;
using SendtoHandler = Function<void (SendtoHandlerArgs &&)>;

// --- IOContext
class IOContext {
public:
    // Async Interface
    virtual bool asyncInitialize(SocketView socket) = 0;
    virtual bool asyncCleanup(SocketView socket) = 0;
    virtual bool asyncCancel(SocketView, void *operation) = 0;

    virtual void *asyncRecv(SocketView socket, void *buffer, size_t n, int64_t timeout, RecvHandler &&cb) = 0;
    virtual void *asyncSend(SocketView socket, const void *buffer, size_t n, int64_t timeout, SendHandler &&cb) = 0;
    virtual void *asyncAccept(SocketView socket, int64_t timeout, AcceptHandler &&cb) = 0;
    virtual void *asyncConnect(SocketView socket, const IPEndpoint &endpoint, int64_t timeout, ConnectHandler &&cb) = 0;
    
    virtual void *asyncRecvfrom(SocketView socket, void *buffer, size_t n, int64_t timeout, RecvfromHandler &&cb) = 0;
    virtual void *asyncSendto(SocketView socket, const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout, SendtoHandler &&cb) = 0;
};

ILIAS_NS_END