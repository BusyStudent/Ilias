/**
 * @file uring.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the io uring Context on the linux
 * @version 0.1
 * @date 2024-09-15
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <liburing.h>
#include <thread>
#include <deque>

ILIAS_NS_BEGIN

namespace os_linux {

/**
 * @brief The Configuration for io_uring
 * 
 */
struct UringConfig {
    unsigned int entries = 64;
    unsigned int flags = 0;
};

/**
 * @brief The io context by using io_uring
 * 
 */
class ILIAS_API UringContext final : public IoContext {
public:
    UringContext(UringConfig conf = {});
    UringContext(const UringContext &) = delete;
    ~UringContext();

    // For Executor
    auto post(void (*fn)(void *), void *args) -> void override;
    auto run(runtime::StopToken token) -> void override;
    auto sleep(uint64_t ms) -> Task<void> override;

    // For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor *> override;
    auto removeDescriptor(IoDescriptor *fd) -> IoResult<void> override;
    auto cancel(IoDescriptor *fd) -> IoResult<void> override;

    auto read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> override;
    auto write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> override;

    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;
    auto accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> override;

    auto sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> override;
    auto recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> override;

    auto sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> override;
    auto recvmsg(IoDescriptor *fd, MutableMsgHdr &msg, int flags) -> IoTask<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;

    // Uring specific
    auto submit() -> IoResult<void>;
private:
    auto processCompletion() -> void;
    auto allocSqe() -> ::io_uring_sqe *;

    using Callback = std::pair<void (*)(void *), void *>;

    ::io_uring           mRing {};
    int                  mEventFd = -1;
    std::deque<Callback> mCallbacks; // The callbacks in current thread, non mutex
    std::deque<Callback> mPendingCallbacks; // The callbacks from another thread, protected by mMutex
    std::mutex           mMutex;
    std::thread::id      mThreadId { std::this_thread::get_id() };

    // Features
    struct {
        bool cancelFd = false;
    } mFeatures;
};

} // namespace os_linux

using os_linux::UringContext;
using os_linux::UringConfig;

ILIAS_NS_END