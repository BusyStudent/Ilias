#pragma once

#include "backend.hpp"
#include "../inet.hpp"
#include "../task.hpp"

#include <memory>
#include <span>


ILIAS_NS_BEGIN

// --- Concepts

/**
 * @brief Define a stream client can connect, recv and send data
 * 
 * @tparam T 
 */
template <typename T>
concept StreamClient = requires(T t) {
    /**
     * @brief Connect to a remote endpoint
     * 
     * @param endpoint 
     * @return Task<void> 
     */
    t.connect(IPEndpoint{ });

    /**
     * @brief Send data to the remote endpoint
     * 
     * @param buffer 
     * @param size 
     * @return Task<size_t> 
     */
    t.send(nullptr, size_t{ });

    /**
     * @brief Receive data from the remote endpoint
     * 
     * @param buffer 
     * @param size 
     * @return Task<size_t> 
     */
    t.recv(nullptr, size_t{ });

    /**
     * @brief Doing graceful shutdown
     * 
     * @return Task<void>
     */
    t.shutdown();
};

/**
 * @brief Define a listener can bind on a endpoint, accept new client
 * 
 * @tparam T 
 */
template <typename T>
concept StreamListener = requires(T t) {
    t.bind(IPEndpoint{ }, int { });
    t.accept();
};

/**
 * @brief Define a datagram client can bind on a endpoint, send and recv data
 * 
 * @tparam T 
 */
template <typename T>
concept DatagramClient = requires(T t) {
    t.bind(IPEndpoint{ });
    t.sendto(nullptr, size_t{ }, IPEndpoint{ });
    t.recvfrom(nullptr, size_t{ });
};

template <typename T>
concept Connectable = requires(T t) {
    t.connect(IPEndpoint{ }); 
};

/**
 * @brief A CRTP for StreamClient, add useful methods like recvAll, sendAll etc...
 * 
 * @tparam T 
 */
template <typename T>
class AddStreamMethod {
public:
    /**
     * @brief Send all the data to, it will send data as more as possible
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto sendAll(const void *buffer, size_t n) -> Task<size_t> {
        auto cur = static_cast<const uint8_t*>(buffer);
        size_t sended = 0;
        while (n > 0) {
            auto ret = co_await self()->send(buffer, n);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (*ret == 0) {
                break;
            }
            sended += *ret;
            n -= *ret;
            cur += *ret;
        }
        co_return sended;
    }

    /**
     * @brief Send all the data to, it will send data as more as possible, span version
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto sendAll(std::span<const std::byte> buffer) -> Task<size_t> {
        return sendAll(buffer.data(), buffer.size());
    }

    auto writeAll(std::span<const std::byte> buffer) -> Task<size_t> { return sendAll(buffer); }
    auto writeAll(const void *buffer, size_t n) -> Task<size_t> { return sendAll(buffer, n); }

    /**
     * @brief Recv data from, it will try to recv data as more as possible
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto recvAll(void *buffer, size_t n) -> Task<size_t> {
        auto cur = static_cast<uint8_t*>(buffer);
        size_t received = 0;
        while (n > 0) {
            auto ret = co_await self()->recv(cur, n);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            if (*ret == 0) {
                break;
            }
            received += *ret;
            n -= *ret;
            cur += *ret;
        }
        co_return received;
    }

    /**
     * @brief Recv data from, it will try to recv data as more as possible, span version
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto recvAll(std::span<std::byte> buffer) -> Task<size_t> {
        return recvAll(buffer.data(), buffer.size());
    }

    auto readAll(std::span<std::byte> buffer) -> Task<size_t> { return recvAll(buffer); }
    auto readAll(void *buffer, size_t n) -> Task<size_t> { return recvAll(buffer, n); }

    /**
     * @brief Alias of recv, used as compatibility
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> Task<size_t> {
        return self()->recv(buffer.data(), buffer.size());
    }

    /**
     * @brief Alias of recv, used as compatibility
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto read(const void *buffer, size_t n) -> Task<size_t> {
        return self()->recv(buffer, n);
    }

    /**
     * @brief Alias of send, used as compatibility
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> Task<size_t> {
        return self()->send(buffer.data(), buffer.size());
    }

    /**
     * @brief Alias of send, used as compatibility
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto write(const void *buffer, size_t n) -> Task<size_t> { 
        return self()->send(buffer, n); 
    }
private:
    auto self() -> T * {
        return static_cast<T *>(this);
    }
};


/**
 * @brief Helper class to wrap a StreamClient as dynamic type
 * 
 */
class IStreamClient final : public AddStreamMethod<IStreamClient> {    
public:
    IStreamClient() = default;
    IStreamClient(std::nullptr_t) { }
    IStreamClient(const IStreamClient &) = delete;
    IStreamClient(IStreamClient &&client) = default;
    ~IStreamClient() = default;

    /**
     * @brief Construct a new IStreamClient object
     * 
     * @tparam T 
     * @param value 
     */
    template <StreamClient T>
    IStreamClient(T &&value) : mPtr(new Impl<T>(std::move(value))) { }

    /**
     * @brief Connect to a remote endpoint
     * 
     * @param endpoint 
     * @return Task<void> 
     */
    auto connect(const IPEndpoint &endpoint) -> Task<void> {
        return mPtr->connect(endpoint);
    }

    /**
     * @brief Send num of datas
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto send(const void *buffer, size_t n) -> Task<size_t> {
        return mPtr->send(buffer, n);
    }

    /**
     * @brief Recv num of datas
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto recv(void *buffer, size_t n) -> Task<size_t> {
        return mPtr->recv(buffer, n);
    }

    /**
     * @brief Close the client, it does not gracefully close the connection
     * 
     */
    auto close() noexcept -> void {
        mPtr.reset();
    }

    auto shutdown() -> Task<void> {
        return mPtr->shutdown();
    }

    /**
     * @brief Observe the client inside the wrapper
     * 
     * @tparam T 
     * @return T& 
     */
    template <StreamClient T>
    auto view() const -> T & {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr.get()) != nullptr);
        return static_cast<Impl<T> *>(mPtr.get())->value;
    }

    auto operator =(const IStreamClient &client) -> IStreamClient & = delete;
    auto operator =(IStreamClient &&client) -> IStreamClient & = default;
    auto operator =(std::nullptr_t) -> IStreamClient &{
        mPtr = nullptr;
        return *this;
    }

    /**
     * @brief Check is empty?
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mPtr != nullptr;
    }
private:
    struct Base {
        virtual auto connect(const IPEndpoint &endpoint) -> Task<void> = 0;
        virtual auto send(const void *buffer, size_t n) -> Task<size_t> = 0;
        virtual auto recv(void *buffer, size_t n) -> Task<size_t> = 0;
        virtual auto shutdown() -> Task<void> = 0;
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
        auto shutdown() -> Task<void> override {
            return value.shutdown();
        }
        T value;
    };
    std::unique_ptr<Base> mPtr;
};
static_assert(StreamClient<IStreamClient>); //< Make sure IStreamClient is a has StreamClient concept

/**
 * @brief Helper class to wrap a StreamListener as dynamic type
 * 
 */
class IStreamListener final {
public:
    using Client = IStreamClient;

    IStreamListener() = default;
    IStreamListener(const IStreamListener &) = delete;
    IStreamListener(IStreamListener &&listener) = default;
    ~IStreamListener() = default;

    template <StreamListener T>
    IStreamListener(T &&value) : mPtr(std::make_unique<Impl<T> >(std::move(value))) { }

    /**
     * @brief Bind the listener to the specified endpoint
     * 
     * @param endpoint The endpoint to bind to
     * @param backlog The maximum length of the queue of pending connections
     * @return Result<> 
     */
    auto bind(const IPEndpoint &endpoint, int backlog = 0) const -> Result<> {
        return mPtr->bind(endpoint, backlog);
    }

    /**
     * @brief Accept a new connection
     * 
     * @return Task<std::pair<IStreamClient, IPEndpoint> > 
     */
    auto accept() const -> Task<std::pair<IStreamClient, IPEndpoint> > {
        return mPtr->accept();
    }

    /**
     * @brief Get the local endpoint
     * 
     * @return Result<IPEndpoint> 
     */
    auto localEndpoint() const -> Result<IPEndpoint> {
        return mPtr->localEndpoint();
    }

    template <StreamListener T>
    auto view() const noexcept -> T & {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr.get()) != nullptr);
        return static_cast<Impl<T> *>(mPtr.get())->value;
    }

    template <StreamListener T>
    auto release() noexcept -> T {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr.get()) != nullptr);
        T v = std::move(static_cast<Impl<T> *>(mPtr.get())->value);
        mPtr.reset();
        return v;
    }

    auto operator =(const IStreamListener &listener) -> IStreamListener & = delete;
    auto operator =(IStreamListener &&listener) -> IStreamListener & = default;
    auto operator =(std::nullptr_t) -> IStreamListener & {
        mPtr.reset();
        return *this;
    }

    /**
     * @brief Check if the listener is empty
     * 
     * @return true 
     * @return false 
     */
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
    std::unique_ptr<Base> mPtr;
};
static_assert(StreamListener<IStreamListener>); //< Make sure IStreamListener is a has StreamListener concept

/**
 * @brief Helper class to wrap a DatagramClient as dynamic type
 * 
 */
class IDatagramClient final {
public:
    IDatagramClient() = default;
    IDatagramClient(const IDatagramClient &) = delete;
    IDatagramClient(IDatagramClient &&client) : mPtr(std::move(client.mPtr)) { }
    ~IDatagramClient() = default;

    template <DatagramClient T>
    IDatagramClient(T &&value) : mPtr(std::make_unique<Impl<T>>(std::move(value))) { }

    /**
     * @brief Send data to the specified endpoint
     * 
     * @param buffer The data to send
     * @param n The size of the data
     * @param endpoint The endpoint to send to
     * @return Task<size_t> 
     */
    auto sendto(const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
        return mPtr->sendto(buffer, n, endpoint);
    }

    /**
     * @brief Receive data from the specified endpoint
     * 
     * @param buffer The buffer to store the received data
     * @param n The size of the buffer
     * @return Task<std::pair<size_t, IPEndpoint> > 
     */
    auto recvfrom(void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
        return mPtr->recvfrom(buffer, n);
    }

    /**
     * @brief Bind the client to the specified endpoint
     * 
     * @param endpoint The endpoint to bind to
     * @return Result<void> 
     */
    auto bind(const IPEndpoint &endpoint) -> Result<void> {
        return mPtr->bind(endpoint);
    }

    template <DatagramClient T>
    auto view() const noexcept -> T & {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr.get()) != nullptr);
        return static_cast<Impl<T> *>(mPtr.get())->value;
    }

    template <DatagramClient T>
    auto release() noexcept -> T {
        ILIAS_ASSERT(dynamic_cast<Impl<T> *>(mPtr.get()) != nullptr);
        T v = std::move(static_cast<Impl<T> *>(mPtr.get())->value);
        mPtr.reset();
        return v;
    }
    
    auto operator =(const IDatagramClient &client) -> IDatagramClient & = delete;
    auto operator =(IDatagramClient &&client) -> IDatagramClient & {
        mPtr = std::move(client.mPtr);
        return *this;
    }
    auto operator =(std::nullptr_t) -> IDatagramClient & {
        mPtr.reset();
        return *this;
    }

    /**
     * @brief Check if the client is empty
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mPtr != nullptr;
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
    std::unique_ptr<Base> mPtr;
};
static_assert(DatagramClient<IDatagramClient>); //< Make sure IDatagramClient is a has DatagramClient concept

ILIAS_NS_END