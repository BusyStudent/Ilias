#include <ilias/task/when_all.hpp>
#include <ilias/io/context.hpp>
#include <ilias/fs/pipe.hpp>
#include <ilias/process.hpp>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#else
    #include <sys/syscall.h> // pidfd
    #include <sys/poll.h> // POLLIN
    #include <sys/wait.h> // waitpid
    #include <unistd.h> // pipe, vfork, execve
    #include <csignal> // SIGCHLD
    #include <spawn.h>
#endif // _WIN32

ILIAS_NS_BEGIN

// MARK: Builder
auto Process::Builder::spawn() -> IoResult<Process> {

#if defined(_WIN32)
    // Process commandline
    std::wstring cmdline;
    if (mExec.find(' ') != std::string_view::npos) {
        cmdline = L"\"" + win32::toWide(mExec) + L"\"";
    }
    else {
        cmdline = win32::toWide(mExec);
    }
    
    for (const auto &arg : mArgs) {
        cmdline += L" ";
        std::wstring escaped = win32::toWide(arg);
        size_t pos = 0;
        while ((pos = escaped.find(L"\"", pos)) != std::wstring::npos) {
            escaped.insert(pos, L"\\");
            pos += 2;
        }
        cmdline += L"\"" + escaped + L"\"";
    }

    // Process flags
    struct Info : public ::STARTUPINFOEXW {
        Info() : STARTUPINFOEXW {} {
            StartupInfo.cb = sizeof(STARTUPINFOEXW);
        }

        ~Info() {
            if (lpAttributeList) {
                DeleteProcThreadAttributeList(lpAttributeList);
            }
        }

        std::vector<BYTE> attributeList;
    } info {};

    // Redirect
    ::std::vector<HANDLE> handles;
    ::BOOL inherit = FALSE;

    // Create pipe for each redirect, and disable the our pair inheritance
    if (mStdin) {
        auto fd = mStdin->get();
        if (!::SetHandleInformation(fd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            return Err(SystemError::fromErrno());
        }
        info.StartupInfo.hStdInput = fd;
        handles.push_back(fd);
    }
    if (mStdout) {
        auto fd = mStdout->get();
        if (!::SetHandleInformation(fd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            return Err(SystemError::fromErrno());
        }
        info.StartupInfo.hStdOutput = fd;
        handles.push_back(fd);
    }
    if (mStderr) {
        auto fd = mStderr->get();
        if (!::SetHandleInformation(fd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            return Err(SystemError::fromErrno());
        }
        info.StartupInfo.hStdError = fd;
        handles.push_back(fd);
    }
    // Handle inheritance
    if (!handles.empty()) {
        info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        inherit = TRUE;

        // MSDN: If STARTF_USESTDHANDLES is specified, the hStdInput, hStdOutput, and hStdError will pass directly to the child process.
        // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
        if (!info.StartupInfo.hStdInput) {
            info.StartupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
        }
        if (!info.StartupInfo.hStdOutput) {
            info.StartupInfo.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
        }
        if (!info.StartupInfo.hStdError) {
            info.StartupInfo.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
        }

        // Initialize attribute list
        ::SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        info.attributeList.resize(size);
        info.lpAttributeList = reinterpret_cast<::PPROC_THREAD_ATTRIBUTE_LIST>(info.attributeList.data());
        if (!::InitializeProcThreadAttributeList(info.lpAttributeList, 1, 0, &size)) {
            return Err(SystemError::fromErrno());
        }

        // Add handles
        if (!::UpdateProcThreadAttribute(info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(), handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
            return Err(SystemError::fromErrno());
        }
    }

    // Begin create
    ::PROCESS_INFORMATION pi {};
    auto ok = ::CreateProcessW(
        nullptr,
        cmdline.data(),
        nullptr,
        nullptr,
        inherit,
        EXTENDED_STARTUPINFO_PRESENT | mCreationFlags,
        nullptr,
        nullptr,
        &info.StartupInfo,
        &pi
    );
    if (!ok) {
        return Err(SystemError::fromErrno());
    }

    // Release the handles
    ::CloseHandle(pi.hThread); // We don't need this
    mStdin.reset();
    mStdout.reset();
    mStderr.reset();

    // Return it
    Process proc {};
    proc.mHandle.reset(pi.hProcess);
    proc.mPid = pi.dwProcessId;
    return proc;
#else // Using posix spawn
    // Allocate argv array with space for program name + args + nullptr
    auto vec = std::vector<char *> {};
    vec.reserve(mArgs.size() + 2);

    vec.push_back(mExec.data());
    for (auto &arg : mArgs) {
        vec.push_back(arg.data());
    }
    vec.push_back(nullptr);

    // Prepare the env, pipes...
    struct State {
        State() {
            ::posix_spawn_file_actions_init(&action);
        }
        ~State() {
            ::posix_spawn_file_actions_destroy(&action);
        }

        ::posix_spawn_file_actions_t action;
    } state;

    // For redirecting...
    if (mStdin) {
        ::posix_spawn_file_actions_adddup2(&state.action, mStdin->get(), STDIN_FILENO);
    }
    if (mStdout) {
        ::posix_spawn_file_actions_adddup2(&state.action, mStdout->get(), STDOUT_FILENO);
    }
    if (mStderr) {
        ::posix_spawn_file_actions_adddup2(&state.action, mStderr->get(), STDERR_FILENO);
    }

    // Begin the spawn
    ::pid_t pid = 0;
    if (auto err = ::posix_spawnp(&pid, program.c_str(), &state.action, nullptr, vec.data(), nullptr); err != 0) {
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
        ::pid_t pid;
    } guard {pid};

    // Open pidfd
    Process proc;
    if (auto pidfd = FileDescriptor {::syscall(SYS_pidfd_open, pid, 0)}; pidfd.get() == -1) {
        return Err(SystemError::fromErrno());
    }
    else {
        auto handle = IoHandle<FileDescriptor>::make(std::move(pidfd), IoDescriptor::Pollable);
        if (!handle) {
            return Err(handle.error());
        }
        proc.mHandle = std::move(*handle);
        proc.mPid = static_cast<uint32_t>(pid);
    }
    guard.pid = -1; // All done, clear the guard
    return proc;
#endif // _WIN32

}

auto Process::Builder::output() -> IoTask<Output> {
    auto out = PipePair::make();
    auto err = PipePair::make();
    if (!out) {
        co_return Err(out.error());
    }
    if (!err) {
        co_return Err(err.error());
    }

    // Bind the pipes
    this->cout(std::move(out->writer));
    this->cerr(std::move(err->writer));

    // Start it
    auto proc = this->spawn();
    if (!proc) {
        co_return Err(proc.error());
    }

    Output output {};
    auto [outDone, errDone, done] = co_await whenAll(
        out->reader.readToEnd(output.cout),
        err->reader.readToEnd(output.cerr),
        proc->wait()
    );
    if (!done) {
        co_return Err(done.error());
    }
    output.exitStatus = *done;
    co_return output;
}

// MARK: Process
auto Process::kill() const -> IoResult<void> {
    if (!mHandle) {
        return Err(IoError::InvalidArgument);
    }
#if defined(_WIN32)
    if (!::TerminateProcess(mHandle.get(), 0)) {
        return Err(SystemError::fromErrno());
    }
#else // Pidfd
    if (::kill(mPid, SIGKILL) != 0) {
        return Err(SystemError::fromErrno());
    }
#endif
    return {};
}

auto Process::wait() const -> IoTask<int32_t> {
    if (!mHandle) {
        co_return Err(IoError::InvalidArgument);
    }
#if defined(_WIN32)
    ::DWORD code {};
    if (auto res = co_await win32::waitObject(mHandle.get()); !res) {
        co_return Err(res.error());
    }
    if (!::GetExitCodeProcess(mHandle.get(), &code)) {
        co_return Err(SystemError::fromErrno());
    }
    co_return static_cast<int32_t>(code);
#else // Pidfd
    while (true) {
        auto events = co_await mHandle.poll(POLLIN);
        if (!events) {
            co_return Err(events.error());
        }
        if (*events & POLLIN) {
            break;
        }
    }
    ::siginfo_t info {};
    if (::waitid(P_PIDFD, fd_t(mHandle.fd()), &info, WEXITED) == -1) {
        co_return Err(SystemError::fromErrno());
    }
    co_return info.si_status;
#endif    
}

ILIAS_NS_END
