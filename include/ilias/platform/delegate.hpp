#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/io/context.hpp>
#include <ilias/task/utils.hpp>
#include <ilias/task/task.hpp>
#include <ilias/platform.hpp> // PlatformContext
#include <thread>
#include <latch>

ILIAS_NS_BEGIN

namespace detail {

// The background thread entry
template <typename T>
inline auto threadedContextLoop(runtime::StopToken token, T **out, std::latch *initDone) -> void {
    T context {};
    context.install();

    // Init complete
    *out = &context;
    initDone->count_down();
    out = nullptr;
    initDone = nullptr;

    // Wait for stop token request
    context.run(token);
}

template <typename T>
inline auto makeThreadedContextImpl() -> std::shared_ptr<T> {
    struct State {
        runtime::StopSource source;
        std::thread         thread;
    };

    State      state {};
    std::latch latch {1};
    T          *context = nullptr;
    state.thread = std::thread {&threadedContextLoop<T>, state.source.get_token(), &context, &latch};
    latch.wait(); // Wait init done

    return std::shared_ptr<T> {context, [state = std::move(state)](T *ptr) mutable {
        ptr->schedule([&]() { // Do the stop request in the thread, more safer :)
            state.source.request_stop();
        });
        state.thread.join();
        // No-need to delete the context, it is owned by the thread
    }};
}

} // namespace detail

/**
 * @brief Helper class to proxy all io operations to another context
 * 
 */
class ProxyContext : public IoContext {
public:
    explicit ProxyContext(std::shared_ptr<IoContext> context);
    ProxyContext();
    ~ProxyContext();

    // IoContext
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

    auto sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> override;
    auto recvmsg(IoDescriptor *fd, MutableMsgHdr &msg, int flags) -> IoTask<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;
private:
    std::shared_ptr<IoContext> mContext; //< The context to delegate to
};

inline ProxyContext::ProxyContext(std::shared_ptr<IoContext> context) : mContext(std::move(context)) {

}

inline ProxyContext::ProxyContext() : mContext(detail::makeThreadedContextImpl<PlatformContext>()) {
    
}

inline ProxyContext::~ProxyContext() {
    mContext.reset();
}

inline auto ProxyContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> {
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

inline auto ProxyContext::removeDescriptor(IoDescriptor* fd) -> IoResult<void> {
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

inline auto ProxyContext::cancel(IoDescriptor *fd) -> IoResult<void> {
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

inline auto ProxyContext::sleep(uint64_t ms) -> Task<void> {
    co_return co_await scheduleOn(mContext->sleep(ms), *mContext);
}

inline auto ProxyContext::read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->read(fd, buffer, offset), *mContext);
}

inline auto ProxyContext::write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->write(fd, buffer, offset), *mContext);
}

inline auto ProxyContext::accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> {
    co_return co_await scheduleOn(mContext->accept(fd, endpoint), *mContext);
}

inline auto ProxyContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
    co_return co_await scheduleOn(mContext->connect(fd, endpoint), *mContext);
}

inline auto ProxyContext::sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->sendto(fd, buffer, flags, endpoint), *mContext);
}

inline auto ProxyContext::recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->recvfrom(fd, buffer, flags, endpoint), *mContext);
}

inline auto ProxyContext::sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->sendmsg(fd, msg, flags), *mContext);
}

inline auto ProxyContext::recvmsg(IoDescriptor *fd, MutableMsgHdr &msg, int flags) -> IoTask<size_t> {
    co_return co_await scheduleOn(mContext->recvmsg(fd, msg, flags), *mContext);
}

inline auto ProxyContext::poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> {
    co_return co_await scheduleOn(mContext->poll(fd, events), *mContext);
}

/**
 * @brief Create an IoContext that run on another context
 * 
 * @tparam T 
 */
template <typename T> requires(std::is_base_of_v<IoContext, T>)
inline auto makeThreadedContext() -> std::shared_ptr<T> {
    return detail::makeThreadedContextImpl<T>();
}

ILIAS_NS_END