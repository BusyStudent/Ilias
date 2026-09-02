#pragma once

#include <ilias/io/system_error.hpp> // SystemError
#include <ilias/io/error.hpp> // IoResult
#include <ilias/io/fd.hpp> // FileDescriptor
#include <ilias/log.hpp> // ILIAS_ERROR
#include <sys/syscall.h> // pidfd
#include <sys/wait.h> // waitpid
#include <concepts> // std::invocable
#include <unistd.h> // fork, execve
#include <fcntl.h> // O_CLOEXEC
#include <sched.h> // clone
#include <csignal> // signal
#include <cerrno> // errno

#if __has_include(<linux/sched.h>)
    #include <linux/sched.h> // CLONE_PIDFD
#endif // __has_include(<linux/sched.h>)

#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    #define ILIAS_USE_PIDFD
#else
    #include <sys/socket.h> // socketpair
    #include <system_error>
    #include <thread>
    #include <mutex>
    #include <map>
#endif // SYS_pidfd_open && SYS_pidfd_send_signal && SYS_clone3

ILIAS_NS_BEGIN

namespace pidfd {

// MARK: Native Pidfd
// Just very thin wrapper
#if defined(ILIAS_USE_PIDFD)

// Do vfork like syscall, child will run the function passed as argument
template <std::invocable Fn>
inline auto vfork(Fn child) -> IoResult<std::pair<pid_t, FileDescriptor> > {

#if !defined(SYS_clone3) || !defined(CLONE_PIDFD)
    auto ptr = std::addressof(child);
    auto trampline = [](void *ptr) -> int {
        (*static_cast<Fn*>(ptr))();
        _Exit(0); // noreturn
    };

    // System call
    alignas(sizeof(void*) * 2) std::byte stack[4096]; // This stack should be large enough
    ::pid_t pid = ::clone(trampline, stack + sizeof(stack), CLONE_VM | CLONE_VFORK | SIGCHLD, ptr);
    if (pid == -1) { // Failed
        return Err(SystemError::fromErrno());
    }
    return std::pair {
        pid,
        FileDescriptor(::syscall(SYS_pidfd_open, pid, 0)) // Open the pidfd
    };
#else
    // Prepare resource
    int fd = -1;

    // Prepare args
    ::clone_args args {};
    args.pidfd = reinterpret_cast<uintptr_t>(&fd);
    args.flags = CLONE_PIDFD; // Flags
    // args.exit_signal = SIGCHLD; maybe we didn't need this? we use pidfd

    auto pid = static_cast<::pid_t>(::syscall(SYS_clone3, &args, sizeof(args)));
    if (pid == -1) { // Error
        return Err(SystemError::fromErrno());
    }
    if (pid == 0) { // Child
        child();
        _Exit(0); // noreturn
    }
    return std::pair {
        pid,
        FileDescriptor{fd}
    };
#endif // 0
}

inline auto kill(pid_t, int pidfd, int sig, siginfo_t *info, unsigned int flags) -> IoResult<void> {
    if (::syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags) == -1) {
        return Err(SystemError::fromErrno());
    }
    return {};
}

inline auto waitfd(int pidfd, ::siginfo_t *info, int flags) -> IoResult<void> {
    if (::waitid(P_PIDFD, pidfd, info, flags) == -1) {
        return Err(SystemError::fromErrno());
    }
    return {};
}
#else
// MARK: SIGCHILD
// Emm, little dirty, but it works :)
namespace {

// The writer part to send the siginfo
constinit int gSigchildPipe = 0;
constinit struct {
    union {
        void (*handler)(int) = nullptr;
        void (*action)(int, siginfo_t *, void *);
    };
    bool isAction = false;
} gSignalHandler; // The previous signal handler

auto sigchildHandler(int sig, siginfo_t *info, void *ctxt) -> void {
    std::byte byte{};
    while (::write(gSigchildPipe, &byte, sizeof(byte)) == -1 && errno == EINTR) {}

    // Chain it
    if (gSignalHandler.handler == SIG_DFL || gSignalHandler.handler == SIG_IGN) {
        return;
    }
    if (gSignalHandler.isAction) {
        gSignalHandler.action(sig, info, ctxt);
    }
    else {
        gSignalHandler.handler(sig);
    }
}

struct ChildCollector {
    ChildCollector() {
        // Prepare pipes
        int pipes[2];
        if (::pipe2(pipes, O_CLOEXEC) != 0) {
            ILIAS_THROW(std::system_error{errno, std::system_category(), "pipe2"});
        }
        gSigchildPipe = pipes[1]; // Writer
        thread = std::thread{&ChildCollector::run, this, FileDescriptor{pipes[0]}};

        // Prepare handler
        struct sigaction action{};
        struct sigaction prev{};
        action.sa_flags = SA_SIGINFO;
        action.sa_sigaction = sigchildHandler;
        if (::sigaction(SIGCHLD, &action, &prev) == -1) {
            ILIAS_THROW(std::system_error{errno, std::system_category(), "sigaction"});
        }
        if (prev.sa_flags & SA_SIGINFO) {
            gSignalHandler.action = prev.sa_sigaction;
            gSignalHandler.isAction = true;
        }
        else {
            gSignalHandler.handler = prev.sa_handler;
        }
    }

    ~ChildCollector() {
        if (gSigchildPipe != 0) {
            ::close(gSigchildPipe); // It will interupt the reader thread
        }
        thread.join();
    }

    auto run(FileDescriptor reader) -> void {
        std::byte byte{};
        while (true) {
            auto res = ::read(reader.get(), &byte, sizeof(byte));
            if (res == sizeof(byte)) {
                peekChildren();
                continue;
            }
            if (res == 0) { // Request to stop
                break;
            }
            if (errno == EINTR) { // Try again
                continue;
            }
            ILIAS_ERROR("Process", "Child collector thread read error: {}", SystemError::fromErrno());
            break;
        }
        ILIAS_DEBUG("Process", "Child collector thread exit");
    }

    auto peekChildren() -> void {
        ILIAS_DEBUG("Process", "Peek children, {} pending", waiters.size());
        // We try to join all children in our list, avoid collect the childs doesn't belong to us
        std::lock_guard locker{mutex};
        ::siginfo_t info{};
        for (auto it = waiters.begin(); it != waiters.end(); ) {
            info.si_pid = 0;
            auto &[pid, fd] = *it;

            if (::waitid(P_PID, pid, &info, WEXITED | WNOHANG) != 0) {
                if (errno == ECHILD) { // Some one already joined the child
                    ILIAS_DEBUG("Process", "pid {} already joined", pid);
                    it = waiters.erase(it);
                    continue;
                }
                ILIAS_ERROR("Process", "waitid failed on pid {}, error: {}", pid, SystemError::fromErrno());
                ++it;
            }
            else if (info.si_pid == 0) { // Still running
                ILIAS_DEBUG("Process", "pid {} is still running", pid);
                ++it;
            }
            else {
                ILIAS_DEBUG("Process", "pid {} exited with code {}", pid, info.si_status);
                // Got it, use UDS to avoid SIGPIPE
                while (::send(fd.get(), &info, sizeof(info), MSG_NOSIGNAL) == -1 && errno == EINTR) {}
                it = waiters.erase(it);
            }
        }
    }

    static auto instance() -> ChildCollector & {
        static ChildCollector c;
        return c;
    }

    std::map<pid_t, FileDescriptor> waiters; // Mapping pid to the file descriptor... to write the siginfo_t
    std::thread thread;
    std::mutex  mutex;
};

} // namespace

template <std::invocable Fn>
inline auto vfork(Fn child) -> IoResult<std::pair<pid_t, FileDescriptor> > {
    auto &instance = ChildCollector::instance();

    // The pipe used to sync the parent and the child
    FileDescriptor syncReader;
    FileDescriptor syncWriter;
    {
        int pipes[2];
        if (::pipe2(pipes, O_CLOEXEC) != 0) {
            return Err(SystemError::fromErrno());
        }
        syncReader = FileDescriptor{pipes[0]};
        syncWriter = FileDescriptor{pipes[1]};
    }

    // The uds used to wait for the child to exit
    FileDescriptor reader;
    FileDescriptor writer;
    {
        int socks[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socks) != 0) {
            return Err(SystemError::fromErrno());
        }
        reader = FileDescriptor{socks[0]};
        writer = FileDescriptor{socks[1]};
    }

    ::pid_t pid = ::fork();
    if (pid == -1) {
        return Err(SystemError::fromErrno());
    }
    if (pid == 0) {
        // Child
        reader.close();
        writer.close();
        syncWriter.close();

        // Wait the process to be ready
        ILIAS_DEBUG("Process", "Child {} wait for parent to be ready", ::getpid());
        std::byte c{};
        while (::read(syncReader.get(), &c, 1) == -1 && errno == EINTR) {}
        ILIAS_DEBUG("Process", "Child {} begin run", ::getpid());

        // Call child code
        child();
        ::_Exit(0);
    }

    // Parent
    syncReader.close();
    {
        std::lock_guard locker{instance.mutex};
        instance.waiters.emplace(pid, std::move(writer));
    }

    // Register done, let the child work
    ILIAS_DEBUG("Process", "Parent registered child pid {}, run it", pid);
    std::byte c{};
    while (::write(syncWriter.get(), &c, 1) == -1 && errno == EINTR) {}
    
    return std::pair {
        pid,
        std::move(reader)
    };
}

inline auto kill(pid_t pid, int, int sig, siginfo_t *, unsigned int) -> IoResult<void> {
    if (::kill(pid, sig) != 0) {
        return Err(SystemError::fromErrno());
    }
    return {};
}

inline auto waitfd(int fd, ::siginfo_t *info, int) -> IoResult<void> {
    if (::read(fd, info, sizeof(*info)) != sizeof(*info)) {
        // In case of error, almost impossible 
        ILIAS_ERROR("Process", "Error reading from waitfd: {}", fd);
        return Err(SystemError::fromErrno());
    }
    return {};
}

#endif // ILIAS_USE_PIDFD

} // namespace pidfd

ILIAS_NS_END