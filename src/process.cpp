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
#else // Posix platform, use posix_spawn
    // Allocate argv array with space for program name + args + nullptr
    auto args = std::vector<char *> {};
    auto envs = std::vector<char *> {};

    // Prepare args
    args.reserve(mArgs.size() + 2);
    args.push_back(mExec.data());
    for (auto &arg : mArgs) {
        args.push_back(arg.data());
    }
    args.push_back(nullptr);

    // Prepare envs
    envs.reserve(mEnvs.size() + 1);
    for (auto &env : mEnvs) {
        envs.push_back(env.data());
    }
    envs.push_back(nullptr);

    // Prepare state
    struct State {
        ~State() {
            if (errpipes[0] != -1) {
                ::close(errpipes[0]);
            }
            if (errpipes[1] != -1) {
                ::close(errpipes[1]);
            }
            if (pid != -1) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, nullptr, 0);
            }
        }

        int     errpipes[2] {-1, -1};
        ::pid_t pid = -1;
    } state;
    if (::pipe2(state.errpipes, O_CLOEXEC) == -1) {
        return Err(SystemError::fromErrno());
    }

    // Begin the spawn
    // Maybe we can try to use pidfd_spawn() in the future ?
    ::pid_t pid = ::fork();
    if (pid == -1) {
        return Err(SystemError::fromErrno());
    }
    if (pid == 0) { // Child, close the read end
        ::close(std::exchange(state.errpipes[0], -1));
        
        do {
            // Redirect if needed
            if (mStdin && ::dup2(mStdin->get(), STDIN_FILENO) == -1) {
                break;
            }
            if (mStdout && ::dup2(mStdout->get(), STDOUT_FILENO) == -1) {
                break;
            }
            if (mStderr && ::dup2(mStderr->get(), STDERR_FILENO) == -1) {
                break;
            }
            ::execvp(mExec.c_str(), args.data());
        }
        while (0);

        // Error happened
        ::error_t err = errno;
        ::write(state.errpipes[1], &err, sizeof(err));
        ::_Exit(127);
    }

    // Parent, close the write end
    state.pid = pid;
    ::close(std::exchange(state.errpipes[1], -1));
    ::error_t err = 0;
    ::ssize_t nbytes = 0;
    while ((nbytes = ::read(state.errpipes[0], &err, sizeof(err))) == -1 && errno == EINTR) {}
    if (nbytes == sizeof(err)) { // Got error from the child
        return Err(SystemError(err));
    }

    // Close it, move the ownership to child
    mStdin.reset();
    mStdout.reset();
    mStderr.reset();

    // Open pidfd
    Process proc {};
    if (auto pidfd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)); pidfd == -1) {
        return Err(SystemError::fromErrno());
    }
    else {
        auto handle = IoHandle<FileDescriptor>::make(FileDescriptor {pidfd}, IoDescriptor::Pollable);
        if (!handle) {
            return Err(handle.error());
        }
        proc.mHandle = std::move(*handle);
        proc.mPid = static_cast<uint32_t>(pid);
    }
    state.pid = -1; // All done, clear the guard
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

#if defined(_WIN32)
    if (!::TerminateProcess(mHandle.get(), 0)) {
        return Err(SystemError::fromErrno());
    }
#else // Pidfd
    if (mPid == 0) {
        return Err(IoError::InvalidArgument);
    }
    if (::syscall(SYS_pidfd_send_signal, mHandle.fd().get(), SIGKILL, nullptr, 0) == -1) {
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
    if (::waitid(P_PIDFD, mHandle.fd().get(), &info, WEXITED) == -1) {
        co_return Err(SystemError::fromErrno());
    }
    co_return info.si_status;
#endif    
}

ILIAS_NS_END
