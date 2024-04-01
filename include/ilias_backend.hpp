#pragma once

#include "ilias.hpp"
#include "ilias_inet.hpp"
#include "ilias_expected.hpp"

// --- Import coroutine if
#if defined(__cpp_lib_coroutine)
#include "ilias_co.hpp"
#endif


ILIAS_NS_BEGIN

// --- Function
#if defined(__cpp_lib_move_only_function)
template <typename ...Args>
using Function = std::move_only_function<Args...>;
#else
template <typename ...Args>
using Function = std::function<Args...>;
#endif

// --- Types
using RecvHandlerArgs = Expected<size_t, Error>;
using RecvHandler = Function<void (RecvHandlerArgs &&)>;

using SendHandlerArgs = Expected<size_t, Error>;
using SendHandler = Function<void (SendHandlerArgs &&)>;

template <typename T>
using AcceptHandlerArgsT = Expected<std::pair<T, IPEndpoint>, Error>;
using AcceptHandlerArgs = Expected<std::pair<Socket, IPEndpoint> , Error>;
using AcceptHandler = Function<void (AcceptHandlerArgs &&)>;

using ConnectHandlerArgs = Expected<void, Error>;
using ConnectHandler = Function<void (ConnectHandlerArgs &&)>;

using RecvfromHandlerArgs = Expected<std::pair<size_t, IPEndpoint>, Error>;
using RecvfromHandler = Function<void (RecvfromHandlerArgs &&)>;

using SendtoHandlerArgs = Expected<size_t, Error>;
using SendtoHandler = Function<void (SendtoHandlerArgs &&)>;

using BindHandlerArgs = Expected<void, Error>;

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

#if defined(__cpp_lib_coroutine)
    virtual auto asyncRecv(SocketView socket, void *buffer, size_t n, int64_t timeout) -> IAwaitable<RecvHandlerArgs>;
    virtual auto asyncSend(SocketView socket, const void *buffer, size_t n, int64_t timeout) -> IAwaitable<SendHandlerArgs>;
    virtual auto asyncAccept(SocketView socket, int64_t timeout) -> IAwaitable<AcceptHandlerArgs>;
    virtual auto asyncConnect(SocketView socket, const IPEndpoint &endpoint, int64_t timeout) -> IAwaitable<ConnectHandlerArgs>;

    virtual auto asyncRecvfrom(SocketView socket, void *buffer, size_t n, int64_t timeout) -> IAwaitable<RecvfromHandlerArgs>;
    virtual auto asyncSendto(SocketView socket, const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout) -> IAwaitable<SendtoHandlerArgs>;
#endif
};

#if defined(__cpp_concepts)
// --- Concepts
template <typename T>
concept StreamClient = requires(T t) {
    t.connect(IPEndpoint{ }, int64_t{ });
    t.send(nullptr, size_t{ }, int64_t{ });
    t.recv(nullptr, size_t{ }, int64_t{ });
};
template <typename T>
concept StreamListener = requires(T t) {
    t.bind(IPEndpoint{ }, int { });
    t.accept(int64_t{ });
};
template <typename T>
concept DatagramClient = requires(T t) {
    t.bind(IPEndpoint{ });
    t.sendto(nullptr, size_t{ }, IPEndpoint{ }, int64_t{ });
    t.recvfrom(nullptr, size_t{ }, int64_t{ });
};

/**
 * @brief Helper class to wrap a StreamClient as dynamic type
 * 
 */
class IStreamClient {
public:
    IStreamClient() = default;
    IStreamClient(const IStreamClient &) = delete;
    IStreamClient(IStreamClient &&client) : mPtr(client.mPtr) { client.mPtr = nullptr; }
    ~IStreamClient() { delete mPtr; }

    template <StreamClient T>
    IStreamClient(T &&value) : mPtr(new Impl<T>(std::move(value))) {}

    auto connect(const IPEndpoint &endpoint, int64_t timeout = -1) -> IAwaitable<ConnectHandlerArgs> {
        return mPtr->connect(endpoint, timeout);
    }
    auto send(const void *buffer, size_t n, int64_t timeout = -1) -> IAwaitable<SendHandlerArgs> {
        return mPtr->send(buffer, n, timeout);
    }
    auto recv(void *buffer, size_t n, int64_t timeout = -1) -> IAwaitable<RecvHandlerArgs> {
        return mPtr->recv(buffer, n, timeout);
    }

    template <StreamClient T>
    T &view() const noexcept {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr) != nullptr);
        return static_cast<Impl<T> *>(mPtr)->value;
    }
    template <StreamClient T>
    T  release() noexcept {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr) != nullptr);
        T v = std::move(static_cast<Impl<T> *>(mPtr)->value);
        delete mPtr;
        mPtr = nullptr;
        return v;
    }

    IStreamClient &operator =(const IStreamClient &client) = delete;
    IStreamClient &operator =(IStreamClient &&client) {
        delete mPtr;
        mPtr = client.mPtr;
        client.mPtr = nullptr;
        return *this;
    }
    IStreamClient &operator =(std::nullptr_t) {
        delete mPtr;
        mPtr = nullptr;
        return *this;
    }
    explicit operator bool() const noexcept {
        return mPtr != nullptr;
    }
private:
    struct Base {
        virtual auto connect(const IPEndpoint &endpoint, int64_t timeout) -> IAwaitable<ConnectHandlerArgs> = 0;
        virtual auto send(const void *buffer, size_t n, int64_t timeout) -> IAwaitable<SendHandlerArgs> = 0;
        virtual auto recv(void *buffer, size_t n, int64_t timeout) -> IAwaitable<RecvHandlerArgs> = 0;
        virtual ~Base() = default;
    };
    template <StreamClient T>
    struct Impl final : Base {
        Impl(T &&value) : value(std::move(value)) { }
        Impl(const Impl &) = delete;
        ~Impl() = default;
        auto connect(const IPEndpoint &endpoint, int64_t timeout) -> IAwaitable<ConnectHandlerArgs> override {
            return value.connect(endpoint, timeout);
        }
        auto send(const void *buffer, size_t n, int64_t timeout) -> IAwaitable<SendHandlerArgs> override {
            return value.send(buffer, n, timeout);
        }
        auto recv(void *buffer, size_t n, int64_t timeout) -> IAwaitable<RecvHandlerArgs> override {
            return value.recv(buffer, n, timeout);
        }
        T value;
    };
    Base *mPtr = nullptr;
};
static_assert(StreamClient<IStreamClient>); //< Make sure IStreamClient is a has StreamClient concept

/**
 * @brief Helper class to wrap a StreamListener as dynamic type
 * 
 */
class IStreamListener {
public:
    using Client = IStreamClient;

    IStreamListener() = default;
    IStreamListener(const IStreamListener &) = delete;
    IStreamListener(IStreamListener &&listener) : mPtr(listener.mPtr) { listener.mPtr = nullptr; }
    ~IStreamListener() { delete mPtr; }

    template <StreamListener T>
    IStreamListener(T &&value) : mPtr(new Impl<T>(std::move(value))) { }

    auto bind(const IPEndpoint &endpoint, int backlog = 0) const -> BindHandlerArgs {
        return mPtr->bind(endpoint, backlog);
    }
    auto accept(int64_t timeout = -1) const -> Task<AcceptHandlerArgsT<IStreamClient> > {
        return mPtr->accept(timeout);
    }
    auto localEndpoint() const -> Result<IPEndpoint> {
        return mPtr->localEndpoint();
    }

    template <StreamListener T>
    T &view() const noexcept {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr) != nullptr);
        return static_cast<Impl<T> *>(mPtr)->value;
    }
    template <StreamListener T>
    T  release() noexcept {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr) != nullptr);
        T v = std::move(static_cast<Impl<T> *>(mPtr)->value);
        delete mPtr;
        mPtr = nullptr;
        return v;
    }

    IStreamListener &operator =(const IStreamListener &listener) = delete;
    IStreamListener &operator =(IStreamListener &&listener) {
        delete mPtr;
        mPtr = listener.mPtr;
        listener.mPtr = nullptr;
        return *this;
    }
    IStreamListener &operator =(std::nullptr_t) {
        delete mPtr;
        mPtr = nullptr;
        return *this;
    }
    explicit operator bool() const noexcept {
        return mPtr != nullptr;
    }
private:
    struct Base {
        virtual auto bind(const IPEndpoint &endpoint, int backlog) -> BindHandlerArgs = 0;
        virtual auto accept(int64_t timeout) -> Task<AcceptHandlerArgsT<IStreamClient> > = 0;
        virtual auto localEndpoint() -> Result<IPEndpoint> = 0;
        virtual ~Base() = default;
    };
    template <StreamListener T>
    struct Impl final : Base {
        Impl(T &&value) : value(std::move(value)) { }
        Impl(const Impl &) = delete;
        ~Impl() = default;
        auto bind(const IPEndpoint &endpoint, int backlog) -> BindHandlerArgs override {
            return value.bind(endpoint, backlog);
        }
        auto accept(int64_t timeout) -> Task<AcceptHandlerArgsT<IStreamClient> > override {
            auto val = co_await value.accept(timeout);
            if (!val) {
                co_return Unexpected(val.error());
            }
            else {
                auto &[client, addr] = *val;
                co_return std::pair {IStreamClient(std::move(client)), addr};
            }
        }
        auto localEndpoint() -> Result<IPEndpoint> override {
            return value.localEndpoint();
        }
        T value;
    };
    Base *mPtr = nullptr;
};
static_assert(StreamListener<IStreamListener>); //< Make sure IStreamListener is a has StreamListener concept

/**
 * @brief Helper class to wrap a DatagramClient as dynamic type
 * 
 */
class IDatagramClient {
public:
    IDatagramClient() = default;
    IDatagramClient(const IDatagramClient &) = delete;
    IDatagramClient(IDatagramClient &&client) : mPtr(client.mPtr) { client.mPtr = nullptr; }
    ~IDatagramClient() { delete mPtr; }

    template <DatagramClient T>
    IDatagramClient(T &&value) : mPtr(new Impl<T>(std::move(value))) { }

    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout = -1) -> IAwaitable<SendtoHandlerArgs> {
        return mPtr->sendto(buffer, n, endpoint, timeout);
    }
    auto recvfrom(void *buffer, size_t n, int64_t timeout = -1) -> IAwaitable<RecvfromHandlerArgs> {
        return mPtr->recvfrom(buffer, n, timeout);
    }
    auto bind(const IPEndpoint &endpoint) -> BindHandlerArgs {
        return mPtr->bind(endpoint);
    }

    template <DatagramClient T>
    T &view() const noexcept {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr) != nullptr);
        return static_cast<Impl<T> *>(mPtr)->value;
    }
    template <DatagramClient T>
    T  release() noexcept {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr) != nullptr);
        T v = std::move(static_cast<Impl<T> *>(mPtr)->value);
        delete mPtr;
        mPtr = nullptr;
        return v;
    }
    
    IDatagramClient &operator =(const IDatagramClient &client) = delete;
    IDatagramClient &operator =(IDatagramClient &&client) {
        delete mPtr;
        mPtr = client.mPtr;
        client.mPtr = nullptr;
        return *this;
    }
    IDatagramClient &operator =(std::nullptr_t) {
        delete mPtr;
        mPtr = nullptr;
        return *this;
    }
private:
    struct Base {
        virtual auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout = -1) -> IAwaitable<SendtoHandlerArgs> = 0;
        virtual auto recvfrom(void *buffer, size_t n, int64_t timeout = -1) -> IAwaitable<RecvfromHandlerArgs> = 0;
        virtual auto bind(const IPEndpoint &endpoint) -> BindHandlerArgs= 0;
        virtual ~Base() = default;
    };
    template <DatagramClient T>
    struct Impl final : Base {
        Impl(T &&value) : value(std::move(value)) { }
        Impl(const Impl &) = delete;
        ~Impl() = default;
        auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout) -> IAwaitable<SendtoHandlerArgs> override {
            co_return co_await value.sendto(buffer, n, endpoint, timeout);
        }
        auto recvfrom(void *buffer, size_t n, int64_t timeout) -> IAwaitable<RecvfromHandlerArgs> override {
            co_return co_await value.recvfrom(buffer, n, timeout);
        }
        auto bind(const IPEndpoint &endpoint) -> BindHandlerArgs override {
            return value.bind(endpoint);
        }
        T value;
    };

    Base *mPtr = nullptr;
};
static_assert(DatagramClient<IDatagramClient>); //< Make sure IDatagramClient is a has DatagramClient concept

#endif

// --- Coroutinue for IOContext's fallback
#if defined(__cpp_lib_coroutine)
inline auto IOContext::asyncRecv(SocketView socket, void *buffer, size_t n, int64_t timeout) -> IAwaitable<RecvHandlerArgs> {
    using Awaitable = CallbackAwaitable<RecvHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) mutable {
        asyncRecv(socket, buffer, n, timeout, [func](RecvHandlerArgs &&args) mutable {
            func(std::move(args));
        });
    });
}
inline auto IOContext::asyncSend(SocketView socket, const void *buffer, size_t n, int64_t timeout) -> IAwaitable<SendHandlerArgs> {
    using Awaitable = CallbackAwaitable<SendHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) mutable {
        asyncSend(socket, buffer, n, timeout, [func](SendHandlerArgs &&args) mutable {
            func(std::move(args));
        });
    });
}
inline auto IOContext::asyncAccept(SocketView socket, int64_t timeout) -> IAwaitable<AcceptHandlerArgs> {
    using Awaitable = CallbackAwaitable<AcceptHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) mutable {
        asyncAccept(socket, timeout, [func](AcceptHandlerArgs &&args) mutable {
            func(std::move(args));
        });
    });
}
inline auto IOContext::asyncConnect(SocketView socket, const IPEndpoint &endpoint, int64_t timeout) -> IAwaitable<ConnectHandlerArgs> {
    using Awaitable = CallbackAwaitable<ConnectHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) mutable {
        asyncConnect(socket, endpoint, timeout, [func](ConnectHandlerArgs &&args) mutable {
            func(std::move(args));
        });
    });
}

inline auto IOContext::asyncRecvfrom(SocketView socket, void *buffer, size_t n, int64_t timeout) -> IAwaitable<RecvfromHandlerArgs> {
    using Awaitable = CallbackAwaitable<RecvfromHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) mutable {
        asyncRecvfrom(socket, buffer, n, timeout, [func](RecvfromHandlerArgs &&args) mutable {
            func(std::move(args));
        });
    });
}
inline auto IOContext::asyncSendto(SocketView socket, const void *buffer, size_t n, const IPEndpoint &endpoint, int64_t timeout) -> IAwaitable<SendtoHandlerArgs> {
    using Awaitable = CallbackAwaitable<SendtoHandlerArgs>;
    return Awaitable([=, this](Awaitable::ResumeFunc &&func) mutable {
        asyncSendto(socket, buffer, n, endpoint, timeout, [func](RecvHandlerArgs &&args) mutable {
            func(std::move(args));
        });
    });
}
#endif

ILIAS_NS_END