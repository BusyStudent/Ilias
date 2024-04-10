#pragma once

#include "ilias.hpp"
#include "ilias_inet.hpp"
#include "ilias_expected.hpp"

// --- Import coroutine
#include "ilias_co.hpp"
#include "ilias_task.hpp"
#include "ilias_await.hpp"


ILIAS_NS_BEGIN

// --- Concepts
template <typename T>
concept StreamClient = requires(T t) {
    t.connect(IPEndpoint{ });
    t.send(nullptr, size_t{ });
    t.recv(nullptr, size_t{ });
};
template <typename T>
concept StreamListener = requires(T t) {
    t.bind(IPEndpoint{ }, int { });
    t.accept();
};
template <typename T>
concept DatagramClient = requires(T t) {
    t.bind(IPEndpoint{ });
    t.sendto(nullptr, size_t{ }, IPEndpoint{ });
    t.recvfrom(nullptr, size_t{ });
};

/**
 * @brief Interface for provide async network services
 * 
 */
class IoContext : public EventLoop {
public:
    virtual auto addSocket(SocketView fd) -> Result<void> = 0;
    virtual auto removeSocket(SocketView fd) -> Result<void> = 0;
    
    virtual auto send(SocketView fd, const void *buffer, size_t n) -> Task<size_t> = 0;
    virtual auto recv(SocketView fd, void *buffer, size_t n) -> Task<size_t> = 0;
    virtual auto connect(SocketView fd, const IPEndpoint &endpoint) -> Task<void> = 0;
    virtual auto accept(SocketView fd) -> Task<std::pair<Socket, IPEndpoint> > = 0;
    virtual auto sendto(SocketView fd, const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> = 0;
    virtual auto recvfrom(SocketView fd, void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > = 0;

    static auto instance() -> IoContext * {
        return dynamic_cast<IoContext*>(EventLoop::instance());
    }
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

    auto connect(const IPEndpoint &endpoint) -> Task<void> {
        return mPtr->connect(endpoint);
    }
    auto send(const void *buffer, size_t n) -> Task<size_t> {
        return mPtr->send(buffer, n);
    }
    auto recv(void *buffer, size_t n) -> Task<size_t> {
        return mPtr->recv(buffer, n);
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
        virtual auto connect(const IPEndpoint &endpoint) -> Task<void> = 0;
        virtual auto send(const void *buffer, size_t n) -> Task<size_t> = 0;
        virtual auto recv(void *buffer, size_t n) -> Task<size_t> = 0;
        virtual ~Base() = default;
    };
    template <StreamClient T>
    struct Impl final : Base {
        Impl(T &&value) : value(std::move(value)) { }
        Impl(const Impl &) = delete;
        ~Impl() = default;
        auto connect(const IPEndpoint &endpoint) -> Task<void> override {
            return value.connect(endpoint);
        }
        auto send(const void *buffer, size_t n) -> Task<size_t> override {
            return value.send(buffer, n);
        }
        auto recv(void *buffer, size_t n) -> Task<size_t> override {
            return value.recv(buffer, n);
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

    auto bind(const IPEndpoint &endpoint, int backlog = 0) const -> Result<> {
        return mPtr->bind(endpoint, backlog);
    }
    auto accept() const -> Task<std::pair<IStreamClient, IPEndpoint> > {
        return mPtr->accept();
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
        virtual auto bind(const IPEndpoint &endpoint, int backlog) -> Result<void> = 0;
        virtual auto accept() -> Task<std::pair<IStreamClient, IPEndpoint> > = 0;
        virtual auto localEndpoint() -> Result<IPEndpoint> = 0;
        virtual ~Base() = default;
    };
    template <StreamListener T>
    struct Impl final : Base {
        Impl(T &&value) : value(std::move(value)) { }
        Impl(const Impl &) = delete;
        ~Impl() = default;
        auto bind(const IPEndpoint &endpoint, int backlog) -> Result<void> override {
            return value.bind(endpoint, backlog);
        }
        auto accept() -> Task<std::pair<IStreamClient, IPEndpoint> > override {
            auto val = co_await value.accept();
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

    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
        return mPtr->sendto(buffer, n, endpoint);
    }
    auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
        return mPtr->recvfrom(buffer, n);
    }
    auto bind(const IPEndpoint &endpoint) -> Result<void> {
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
        virtual auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> = 0;
        virtual auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > = 0;
        virtual auto bind(const IPEndpoint &endpoint) -> Result<void> = 0;
        virtual ~Base() = default;
    };
    template <DatagramClient T>
    struct Impl final : Base {
        Impl(T &&value) : value(std::move(value)) { }
        Impl(const Impl &) = delete;
        ~Impl() = default;
        auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> override {
            return value.sendto(buffer, n, endpoint);
        }
        auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > override {
            return value.recvfrom(buffer, n);
        }
        auto bind(const IPEndpoint &endpoint) -> Result<void>  override {
            return value.bind(endpoint);
        }
        T value;
    };

    Base *mPtr = nullptr;
};
static_assert(DatagramClient<IDatagramClient>); //< Make sure IDatagramClient is a has DatagramClient concept


ILIAS_NS_END