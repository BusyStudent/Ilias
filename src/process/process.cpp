#include <ilias/detail/scope_exit.hpp>
#include <ilias/task/when_all.hpp>
#include <ilias/task/spawn.hpp>
#include <ilias/io/context.hpp>
#include <ilias/process.hpp>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#else
    #include <sys/syscall.h> // pidfd
    #include <sys/poll.h> // POLLIN
    #include <unistd.h> // pipe, vfork, execve
    #include <csignal> // SIGCHLD
    #include <cerrno>
    #include "pidfd.hpp" // pidfd and fallback
#endif // _WIN32

ILIAS_NS_BEGIN

// MARK: Builder
auto Process::Builder::output() -> IoTask<Output> {
    ILIAS_CO_TRY(auto out, PipePair::make());
    ILIAS_CO_TRY(auto err, PipePair::make());

    // Bind the pipes
    this->cout(std::move(out.writer));
    this->cerr(std::move(err.writer));

    // Start it
    ILIAS_CO_TRY(auto proc, this->spawn());

    Output output {};
    auto [outDone, errDone, done] = co_await whenAll(
        out.reader.readToEnd(output.cout),
        err.reader.readToEnd(output.cerr),
        proc.wait()
    );
    if (!done) {
        co_return Err(done.error());
    }
    output.exitStatus = *done;
    co_return output;
}

// MARK: Process
auto Process::kill() const -> IoResult<void> {

#if defined(_WIN32)
    if (!::TerminateProcess(mHandle.get(), 0)) {
        return Err(SystemError::fromErrno());
    }
    return {};
#else // Pidfd
    if (mPid == 0) {
        return Err(IoError::InvalidArgument);
    }
    return pidfd::kill(static_cast<::pid_t>(mPid), nativeHandle(), SIGKILL, nullptr, 0);
#endif // _WIN32
}

auto Process::wait() -> IoTask<int32_t> {
    if (!mHandle) {
        co_return Err(IoError::InvalidArgument);
    }
#if defined(_WIN32)
    ::DWORD code {};
    ILIAS_CO_TRYV(co_await win32::waitObject(mHandle.get()));
    if (!::GetExitCodeProcess(mHandle.get(), &code)) {
        co_return Err(SystemError::fromErrno());
    }
    mHandle = {}; // Used
    co_return static_cast<int32_t>(code);
#else // Pidfd
    ScopeExit guard([&]() {
        auto _ = kill();
    });
    while (true) {
        ILIAS_CO_TRY(auto events, co_await mHandle.poll(POLLIN));
        if (events & POLLIN) {
            guard.release();
            break;
        }
    }
    ::siginfo_t info {};
    ILIAS_CO_TRYV(pidfd::waitfd(nativeHandle(), &info, WEXITED));
    mHandle = {}; // Used
    co_return info.si_status;
#endif // _WIN32
}

auto Process::detach() -> void {
    if (!mHandle) {
        return;
    }
#if defined(__linux__)
    ilias::spawn([](Process proc) -> Task<void> {
        auto _ = co_await proc.wait();
    }(std::move(*this)));
#endif // __linux__
    mHandle = {};
    mPid = 0;
    mKillOnDestroy = false;
}

ILIAS_NS_END
