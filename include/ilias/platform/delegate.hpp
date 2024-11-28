#pragma once

#include <ilias/io/context.hpp>
#include <ilias/task/spawn.hpp>
#include <ilias/task/task.hpp>
#include <thread>
#include <latch>

ILIAS_NS_BEGIN

/**
 * @brief Helper class to delegate an IoContext to a separate thread
 * 
 * @tparam T The IoContext to delegate
 */
template <typename T>
class DelegateContext : public IoContext {
public:
    DelegateContext();
    ~DelegateContext();

    // < For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> Result<void> override;

    auto sleep(uint64_t ms) -> Task<void> override;

    auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> override;
    auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> override;

    auto accept(IoDescriptor *fd, IPEndpoint *endpoint) -> Task<socket_t> override;
    auto connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> override;

    auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, const IPEndpoint *endpoint) -> Task<size_t> override;
    auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, IPEndpoint *endpoint) -> Task<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> override;

#if defined(_WIN32)
    auto connectNamedPipe(IoDescriptor *fd) -> Task<void> override;
#endif // defined(_WIN32)

private:
    auto run() -> void; //< The main loop of the delegate
    
    CancellationToken mToken; //< The token used to stop the thread
    std::thread mThread; //< The worker thread
    std::latch  mLatch {1}; //< The latch used to wait the context creation
    T *mContext = nullptr; //< The context to delegate to
};

template <typename T>
inline DelegateContext<T>::DelegateContext() : mThread(&DelegateContext::run, this) {
    mLatch.wait();
}

template <typename T>
inline DelegateContext<T>::~DelegateContext() {
    mContext->post([](void *token) {
        static_cast<CancellationToken*>(token)->cancel();
    }, &mToken);
    mThread.join();
}

template <typename T>
inline auto DelegateContext<T>::addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> {
    std::latch latch {1};
    Result<IoDescriptor*> ret {nullptr};

    auto callback = [&]() {
        ret = mContext->addDescriptor(fd, type);
        latch.count_down();
    };
    mContext->post([](void *callbackPtr) {
        auto cb = reinterpret_cast<decltype(callback) *>(callbackPtr);
        (*cb)();
    }, &callback);
    latch.wait();
    return ret;
}

template <typename T>
inline auto DelegateContext<T>::removeDescriptor(IoDescriptor* fd) -> Result<void> {
    std::latch latch {1};
    Result<void> ret;

    auto callback = [&]() {
        ret = mContext->removeDescriptor(fd);
        latch.count_down();
    };
    mContext->post([](void *callbackPtr) {
        auto cb = reinterpret_cast<decltype(callback) *>(callbackPtr);
        (*cb)();
    }, &callback);
    latch.wait();
    return ret;
}

template <typename T>
inline auto DelegateContext<T>::run() -> void {
    auto ctxt = std::make_unique<T>();
    mContext = ctxt.get();
    mLatch.count_down();
    mContext->run(mToken);
    mContext = nullptr;
}

template <typename T>
inline auto DelegateContext<T>::sleep(uint64_t ms) -> Task<void> {
    co_return co_await scheduleOn(*mContext, mContext->sleep(ms));
}

template <typename T>
inline auto DelegateContext<T>::read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> {
    co_return co_await scheduleOn(*mContext, mContext->read(fd, buffer, offset));
}

template <typename T>
inline auto DelegateContext<T>::write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> {
    co_return co_await scheduleOn(*mContext, mContext->write(fd, buffer, offset));
}

template <typename T>
inline auto DelegateContext<T>::accept(IoDescriptor *fd, IPEndpoint *endpoint) -> Task<socket_t> {
    co_return co_await scheduleOn(*mContext, mContext->accept(fd, endpoint));
}

template <typename T>
inline auto DelegateContext<T>::connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> {
    co_return co_await scheduleOn(*mContext, mContext->connect(fd, endpoint));
}

template <typename T>
inline auto DelegateContext<T>::sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, const IPEndpoint *endpoint) -> Task<size_t> {
    co_return co_await scheduleOn(*mContext, mContext->sendto(fd, buffer, flags, endpoint));
}

template <typename T>
inline auto DelegateContext<T>::recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, IPEndpoint *endpoint) -> Task<size_t> {
    co_return co_await scheduleOn(*mContext, mContext->recvfrom(fd, buffer, flags, endpoint));
}

template <typename T>
inline auto DelegateContext<T>::poll(IoDescriptor *fd, uint32_t events) -> Task<uint32_t> {
    co_return co_await scheduleOn(*mContext, mContext->poll(fd, events));
}

#if defined(_WIN32)
template <typename T>
inline auto DelegateContext<T>::connectNamedPipe(IoDescriptor *fd) -> Task<void> {
    co_return co_await scheduleOn(*mContext, mContext->connectNamedPipe(fd));
}
#endif // defined(_WIN32)


ILIAS_NS_END