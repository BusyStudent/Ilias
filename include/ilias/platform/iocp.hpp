/**
 * @file iocp.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the iocp asyncio on the windows platform
 * @version 0.1
 * @date 2024-08-12
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/timer.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>

ILIAS_NS_BEGIN

namespace win32 {

struct NtDll;

/**
 * @brief The iocp implementation of the io context
 * 
 */
class ILIAS_API IocpContext final : public IoContext {
public:
    IocpContext();
    IocpContext(const IocpContext &) = delete;
    ~IocpContext();

    // For Executor
    auto post(void (*fn)(void *), void *args) -> void override;
    auto run(runtime::StopToken token) -> void override;
    auto sleep(uint64_t ms) -> Task<void> override;

    // For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> IoResult<void> override;
    auto cancel(IoDescriptor *fd) -> IoResult<void> override;

    auto read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> override;
    auto write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> override;

    auto accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> override;
    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;

    auto sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> override;
    auto recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;
    auto connectNamedPipe(IoDescriptor *fd) -> IoTask<void> override;
    auto waitObject(HANDLE handle) -> IoTask<void> override;
private:
    auto processCompletion(DWORD timeout) -> void;
    auto processCompletionEx(DWORD timeout) -> void;

    SockInitializer       mInit;
    runtime::TimerService mService;
    HANDLE mIocpFd = nullptr;
    HANDLE mAfdDevice = nullptr; // For poll
    NtDll &mNt;

    // Batching
    ULONG mEntriesIdx  = 0; // The index of the current entry (for dispatch)
    ULONG mEntriesSize = 0; // The number of entries valid in the mBatchEntries array
    ULONG mEntriesCapacity = 64; // The size of the mBatchEntries array
    std::unique_ptr<::OVERLAPPED_ENTRY[]> mEntries;
};

} // namespace win32

// Export for user
using win32::IocpContext;

ILIAS_NS_END