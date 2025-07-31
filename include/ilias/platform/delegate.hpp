#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/io/context.hpp>
#include <ilias/task/utils.hpp>
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
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> IoResult<void> override;
    auto cancel(IoDescriptor *fd) -> IoResult<void> override;

    auto sleep(uint64_t ms) -> Task<void> override;

    auto read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> override;
    auto write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> override;

    auto accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> override;
    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;

    auto sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> override;
    auto recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;

#if defined(_WIN32)
    auto connectNamedPipe(IoDescriptor *fd) -> IoTask<void> override;
#endif // defined(_WIN32)

private:
    auto mainloop() -> void;

    runtime::StopSource mSource;
    std::thread mThread; //< The worker thread
    std::latch  mLatch {1}; //< The latch used to wait the context creation
    T *mContext = nullptr; //< The context to delegate to
};

template <typename T>
inline DelegateContext<T>::DelegateContext() : mThread(&DelegateContext::mainloop, this) {
    mLatch.wait();
}

template <typename T>
inline DelegateContext<T>::~DelegateContext() {
    mContext->post([](void *source) {
        static_cast<runtime::StopSource*>(source)->request_stop();
    }, &mSource);
    mThread.join();
}

template <typename T>
inline auto DelegateContext<T>::addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> {
    std::latch latch {1};
    IoResult<IoDescriptor*> ret {nullptr};

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
inline auto DelegateContext<T>::removeDescriptor(IoDescriptor* fd) -> IoResult<void> {
    std::latch latch {1};
    IoResult<void> ret;

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
inline auto DelegateContext<T>::cancel(IoDescriptor *fd) -> IoResult<void> {
    std::latch latch {1};
    IoResult<void> ret;

    auto callback = [&]() {
        ret = mContext->cancel(fd);
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
inline auto DelegateContext<T>::mainloop() -> void {
    T ctxt;
    mContext = &ctxt;
    mLatch.count_down();
    mContext->run(mSource.get_token());
    mContext = nullptr;
}

template <typename T>
inline auto DelegateContext<T>::sleep(uint64_t ms) -> Task<void> {
    co_return co_await scheduleOn(mContext->sleep(ms), *mContext);
}

template <typename T>
inline auto DelegateContext<T>::read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->read(fd, buffer, offset), *mContext);
}

template <typename T>
inline auto DelegateContext<T>::write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->write(fd, buffer, offset), *mContext);
}

template <typename T>
inline auto DelegateContext<T>::accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> {
    co_return co_await scheduleOn(mContext->accept(fd, endpoint), *mContext);
}

template <typename T>
inline auto DelegateContext<T>::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    co_return co_await scheduleOn(mContext->connect(fd, endpoint), *mContext);
}

template <typename T>
inline auto DelegateContext<T>::sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->sendto(fd, buffer, flags, endpoint), *mContext);
}

template <typename T>
inline auto DelegateContext<T>::recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->recvfrom(fd, buffer, flags, endpoint), *mContext);
}

template <typename T>
inline auto DelegateContext<T>::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    co_return co_await scheduleOn(mContext->poll(fd, events), *mContext);
}

#if defined(_WIN32)
template <typename T>
inline auto DelegateContext<T>::connectNamedPipe(IoDescriptor *fd) -> IoTask<void> {
    co_return co_await scheduleOn(mContext->connectNamedPipe(fd), *mContext);
}
#endif // defined(_WIN32)


ILIAS_NS_END