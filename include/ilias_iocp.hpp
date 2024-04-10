// #ifdef _WIN32
#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include "ilias_backend.hpp"

#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
#include <map>

#pragma comment(lib, "mswsock.lib")

ILIAS_NS_BEGIN

class IOCPOverlapped : public ::OVERLAPPED {
public:
    IOCPOverlapped() {
        ::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
    }

    // Called on Dispatched
    void (*onCompelete)(IOCPOverlapped *self, BOOL ok, DWORD byteTrans) = nullptr;
};

/**
 * @brief A Context for Windows IOCP
 * 
 */
class IOCPContext final : public IoContext {
public:
    IOCPContext();
    IOCPContext(const IOCPContext&) = delete;
    ~IOCPContext();

    // EventLoop
    auto run() -> void override;
    auto quit() -> void override;
    auto post(void (*)(void *), void *) -> void override;
    auto delTimer(uintptr_t timer) -> bool override;
    auto addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) -> uintptr_t override;

    // IoContext
    auto addSocket(SocketView fd) -> Result<void> override;
    auto removeSocket(SocketView fd) -> Result<void> override;
    
    auto send(SocketView fd, const void *buffer, size_t n) -> Task<size_t> override;
    auto recv(SocketView fd, void *buffer, size_t n) -> Task<size_t> override;
    auto connect(SocketView fd, const IPEndpoint &endpoint) -> Task<void> override;
    auto accept(SocketView fd) -> Task<std::pair<Socket, IPEndpoint> > override;
    auto sendto(SocketView fd, const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> override;
    auto recvfrom(SocketView fd, void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > override;
private:
    auto _calcWaiting() const -> DWORD { return INFINITE; }
    auto _loadFunctions() -> void;
    
    SockInitializer mInitalizer;
    HANDLE mIocpFd = INVALID_HANDLE_VALUE; //< iocp fd
    bool mQuit = false;

    // Timers
    std::multimap<uint64_t, std::pair<void*, void*> > mTimers;
};

using PlatformIoContext = IOCPContext;

ILIAS_NS_END
