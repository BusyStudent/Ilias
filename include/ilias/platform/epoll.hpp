/**
 * @file epoll.hpp
 * @author llhsdmd (llhsdmd@gmail.com)
 * @brief Impl the epoll asyncio on the linux platform
 * @version 0.1
 * @date 2024-09-03
 *
 * @copyright Copyright (c) 2024
 *
 */

#pragma once

#include <ilias/runtime/timer.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/io/context.hpp>
#include <ilias/task/task.hpp>
#include <ilias/buffer.hpp>
#include <thread> // std::thread_id
#include <mutex> // std::mutex
#include <deque> // std::deque
#include <span> // std::span

#include <sys/epoll.h> // epoll_event

ILIAS_NS_BEGIN

namespace linux {

class ILIAS_API EpollContext final : public IoContext {
public:
    EpollContext();
    EpollContext(const EpollContext &) = delete;
    ~EpollContext();

    ///> @brief Add a new system descriptor to the context
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor *> override;
    ///> @brief Remove a descriptor from the context
    auto removeDescriptor(IoDescriptor *fd) -> IoResult<void> override;
    ///> @brief Cancel all pending Io operations on a descriptor
    auto cancel(IoDescriptor *fd) -> IoResult<void> override;

    ///> @brief Read from a descriptor
    auto read(IoDescriptor *fd, MutableBuffer buffer, ::std::optional<size_t> offset)
        -> IoTask<size_t> override;
    ///> @brief Write to a descriptor
    auto write(IoDescriptor *fd, Buffer buffer, ::std::optional<size_t> offset)
        -> IoTask<size_t> override;

    ///> @brief Connect to a remote endpoint
    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;
    ///> @brief Accept a connection
    auto accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> override;

    ///> @brief Send data to a remote endpoint
    auto sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint)
        -> IoTask<size_t> override;
    ///> @brief Receive data from a remote endpoint
    auto recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint)
        -> IoTask<size_t> override;

    ///> @brief Poll a descriptor for events
    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;

    ///> @brief Post a callable to the executor
    auto post(void (*fn)(void *), void *args) -> void override;

    ///> @brief Enter and run the task in the executor, it will infinitely loop until the token is canceled
    auto run(runtime::StopToken token) -> void override;

    ///> @brief Sleep for a specified amount of time
    auto sleep(uint64_t ms) -> Task<void> override;
private:
    struct Callback {
        void (*fn)(void *);
        void *args;
    };
    auto processCompletion(std::stop_token &token) -> void;
    auto processEvents(std::span<const epoll_event> events) -> void;
    auto pollCallbacks() -> void;

    ///> @brief The epoll file descriptor
    int                    mEpollFd = -1;
    int                    mEventFd = -1; // For wakeup the epoll, there is some new callback in the queue
    runtime::TimerService  mService;
    std::deque<Callback>   mCallbacks; // The callbacks in current thread, non mutex
    std::deque<Callback>   mPendingCallbacks; // The callbacks from another thread, protected by mMutex
    std::mutex             mMutex;
    std::thread::id        mThreadId { std::this_thread::get_id() };
};

} // namespace linux


// Export for user
using linux::EpollContext;

ILIAS_NS_END