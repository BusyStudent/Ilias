// #ifdef _WIN32
#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"
#include "ilias_backend.hpp"

#include <WinSock2.h>
#include <Windows.h>
#include <MSWSock.h>
#include <map>

#pragma comment(lib, "mswsock.lib")

ILIAS_NS_BEGIN

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

    // File EXT
    auto addFd(fd_t fd) -> Result<void> override;
    auto removeFd(fd_t fd) -> Result<void> override;

    auto write(fd_t fd, const void *buffer, size_t n) -> Task<size_t> override;
    auto read(fd_t fd, void *buffer, size_t n) -> Task<size_t> override;
private:
    auto _calcWaiting() const -> DWORD;
    auto _runTimers() -> void;
    auto _loadFunctions() -> void;
    auto _runIo(DWORD timeout) -> void;
    
    SockInitializer mInitalizer;
    HANDLE mIocpFd = INVALID_HANDLE_VALUE; //< iocp fd
    bool mQuit = false;

    // Timers
    struct Timer {
        uintptr_t id; //< TimerId
        int64_t ms;  //< Interval in milliseconds
        int flags;    //< Timer flags
        void (*fn)(void *);
        void *arg;
    };
    std::map<uintptr_t, std::multimap<uint64_t, Timer>::iterator> mTimers;
    std::multimap<uint64_t, Timer> mTimerQueue;
    uint64_t mTimerIdBase = 0; //< A self-increasing timer id base
template <typename T, typename RetT>
friend class IOCPAwaiter;
};

ILIAS_NS_END
