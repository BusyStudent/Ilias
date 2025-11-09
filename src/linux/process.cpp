#include <ilias/io/system_error.hpp>
#include <ilias/io/error.hpp>
#include <ilias/io/context.hpp>
#include <ilias/process.hpp>
#include <sys/syscall.h> // pidfd
#include <sys/poll.h> // POLLIN
#include <sys/wait.h> // waitpid
#include <unistd.h> // pipe, vfork, execve
#include <csignal> // SIGCHLD
#include <spawn.h>

ILIAS_NS_BEGIN

namespace {
    auto closepair(int *fds) -> void {
        if (fds[0] != -1) {
            ::close(fds[0]);
        }
        if (fds[1] != -1) {
            ::close(fds[1]);
        }
    }
}

auto Process::spawn(std::string_view exec, std::vector<std::string_view> args, uint32_t flags) -> IoResult<Process> {
    // Dup it on the stack
    auto program = std::string(exec);
    auto arguments = std::vector<std::string>(args.begin(), args.end());
    // Allocate argv array with space for program name + args + nullptr
    auto vec = std::make_unique<char *[]>(args.size() + 2);
    vec[0] = program.data();
    for (size_t i = 0; i < args.size(); i++) {
        vec[i + 1] = arguments[i].data();
    }
    vec[args.size() + 1] = nullptr;

    // Prepare the env, pipes...
    struct State {
        State() {
            ::posix_spawn_file_actions_init(&action);
        }
        ~State() {
            closepair(in);
            closepair(out);
            closepair(err);
            ::posix_spawn_file_actions_destroy(&action);
        }

        int in[2]  = {-1, -1}; // read -> write
        int out[2] = {-1, -1};
        int err[2] = {-1, -1};
        ::posix_spawn_file_actions_t action;
    } state;

    // For redirecting...
    if (flags & Process::RedirectStdin) {
        if (::pipe2(state.in, O_CLOEXEC) == -1) {
            return Err(SystemError::fromErrno());
        }
        ::posix_spawn_file_actions_adddup2(&state.action, state.in[0], STDIN_FILENO);
        ::posix_spawn_file_actions_addclose(&state.action, state.in[1]);
    }
    if (flags & Process::RedirectStdout) {
        if (::pipe2(state.out, O_CLOEXEC) == -1) {
            return Err(SystemError::fromErrno());
        }
        ::posix_spawn_file_actions_adddup2(&state.action, state.out[1], STDOUT_FILENO);
        ::posix_spawn_file_actions_addclose(&state.action, state.out[0]);
        
    }
    if (flags & Process::RedirectStderr) {
        if (::pipe2(state.err, O_CLOEXEC) == -1) {
            return Err(SystemError::fromErrno());
        }
        ::posix_spawn_file_actions_adddup2(&state.action, state.err[1], STDERR_FILENO);
        ::posix_spawn_file_actions_addclose(&state.action, state.err[0]);
    }

    // Begin the spawn
    ::pid_t pid = 0;
    if (auto err = ::posix_spawnp(&pid, program.c_str(), &state.action, nullptr, vec.get(), nullptr); err != 0) {
        return Err(SystemError(err));
    }

    // Parent
    struct Guard {
        ~Guard() {
            if (pid != -1) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, nullptr, 0);
            }
        }
        pid_t pid;
    } guard {pid};

    // Open pidfd
    Process proc;
    if (auto pidfd = FileDescriptor(::syscall(SYS_pidfd_open, pid, 0)); pidfd.get() == -1) {
        return Err(SystemError::fromErrno());
    }
    else {
        auto handle = IoHandle<FileDescriptor>::make(std::move(pidfd), IoDescriptor::Pollable);
        if (!handle) {
            return Err(handle.error());
        }
        proc.mHandle = std::move(*handle);
    }
    // Set the pipes
    if (flags & Process::RedirectStdin) {
        auto fd = std::exchange(state.in[1], -1);
        auto handle = IoHandle<FileDescriptor>::make(FileDescriptor(fd), IoDescriptor::Pipe);
        if (!handle) {
            return Err(handle.error());
        }
        proc.mStdin = std::move(*handle);
    }
    if (flags & Process::RedirectStdout) {
        auto fd = std::exchange(state.out[0], -1);
        auto handle = IoHandle<FileDescriptor>::make(FileDescriptor(fd), IoDescriptor::Pipe);
        if (!handle) {
            return Err(handle.error());
        }
        proc.mStdout = std::move(*handle);
    }
    if (flags & Process::RedirectStderr) {
        auto fd = std::exchange(state.err[0], -1);
        auto handle = IoHandle<FileDescriptor>::make(FileDescriptor(fd), IoDescriptor::Pipe);
        if (!handle) {
            return Err(handle.error());
        }
        proc.mStderr = std::move(*handle);
    }
    guard.pid = -1; // All done, clear the guard
    return proc;
}

auto Process::wait() const -> IoTask<int32_t> {
    while (true) {
        auto events = co_await mHandle.poll(POLLIN);
        if (!events) {
            co_return Err(events.error());
        }
        if (*events & POLLIN) {
            break;
        }
    }
    ::siginfo_t info;
    if (::waitid(P_PIDFD, fd_t(mHandle.fd()), &info, WEXITED) == -1) {
        co_return Err(SystemError::fromErrno());
    }
    co_return info.si_status;
}

auto Process::kill() const -> IoResult<void> {
    if (::syscall(SYS_pidfd_send_signal, fd_t(mHandle.fd()), SIGKILL, nullptr, 0) == -1) {
        return Err(SystemError::fromErrno());
    }
    return {};
}

auto Process::detach() -> void {
    mHandle.close();
    mStdin.close();
    mStdout.close();
    mStderr.close();
}


ILIAS_NS_END