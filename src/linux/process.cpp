#include <ilias/process/process.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/error.hpp>
#include <ilias/io/context.hpp>
#include <sys/syscall.h> // pidfd
#include <sys/poll.h> // POLLIN
#include <sys/wait.h> // waitpid
#include <csignal> // SIGCHLD
#include <unistd.h> // pipe, vfork, execve

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
    auto program = strndupa(exec.data(), exec.size());
    // Allocate argv array with space for program name + args + nullptr
    auto vec = (char**) alloca((args.size() + 2) * sizeof(char*));
    vec[0] = program;
    for (size_t i = 0; i < args.size(); i++) {
        vec[i + 1] = strndupa(args[i].data(), args[i].size());
    }
    vec[args.size() + 1] = nullptr;

    // Prepare the env, pipes...
    struct State {
        ~State() {
            closepair(errpipe);
            closepair(in);
            closepair(out);
            closepair(err);
        }

        int errpipe[2] = {-1, -1}; // for report error for execve
        int in[2]  = {-1, -1}; // read -> write
        int out[2] = {-1, -1};
        int err[2] = {-1, -1};
    } state;

    if (::pipe2(state.errpipe, O_CLOEXEC) == -1) {
        return Err(SystemError::fromErrno());
    }

    // For redirecting...
    if (flags & Process::RedirectStdin) {
        if (::pipe2(state.in, O_CLOEXEC) == -1) {
            return Err(SystemError::fromErrno());
        }
    }
    if (flags & Process::RedirectStdout) {
        if (::pipe2(state.out, O_CLOEXEC) == -1) {
            return Err(SystemError::fromErrno());
        }
    }
    if (flags & Process::RedirectStderr) {
        if (::pipe2(state.err, O_CLOEXEC) == -1) {
            return Err(SystemError::fromErrno());
        }
    }

    // Begin fork, I think 16k is enough for simple exec
    auto pid = ::fork();
    if (pid == -1) {
        return Err(SystemError::fromErrno());
    }
    if (pid == 0) {
        // Child
        auto redirect = [](int &from, int to) -> bool {
            int flags = ::fcntl(from, F_GETFL, 0);
            if (flags == -1) {
                return false;
            }
            // Remove CLOEXEC
            flags &= ~O_CLOEXEC;
            if (::fcntl(from, F_SETFL, flags) == -1) {
                return false;
            }
            if (::dup2(from, to) == -1) {
                return false;
            }
            ::close(from);
            from = -1;
            return true;
        };
        auto spawn = [&]() -> int {
            if (flags & Process::RedirectStdin) {
                if (!redirect(state.in[0], STDIN_FILENO)) {
                    return errno;
                }
            }
            if (flags & Process::RedirectStdout) {
                if (!redirect(state.out[1], STDOUT_FILENO)) {
                    return errno;
                }
            }
            if (flags & Process::RedirectStderr) {
                if (!redirect(state.err[1], STDERR_FILENO)) {
                    return errno;
                }
            }
            ::execvp(program, vec);
            return errno;
        };
        auto err = spawn();
        ::write(state.errpipe[1], &err, sizeof(err));
        ::_Exit(EXIT_FAILURE);
    }
    // Close the write end of the error pipes
    ::close(std::exchange(state.errpipe[1], -1));

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

    int err = 0;
    if (::read(state.errpipe[0], &err, sizeof(err)) == sizeof(err)) { // We Got an execve error
        return Err(SystemError(err));
    }

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