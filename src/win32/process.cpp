#include <ilias/detail/win32defs.hpp>
#include <ilias/io/context.hpp>
#include <ilias/fs/pipe.hpp>
#include <ilias/process.hpp>

ILIAS_NS_BEGIN

auto Process::spawn(std::string_view exec, std::vector<std::string_view> args, uint32_t flags) -> IoResult<Process> {
    // Process commandline
    std::wstring cmdline;
    if (exec.find(' ') != std::string_view::npos) {
        cmdline = L"\"" + win32::toWide(exec) + L"\"";
    }
    else {
        cmdline = win32::toWide(exec);
    }
    
    for (const auto &arg : args) {
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
    ::STARTUPINFOW info {
        .cb = sizeof(info),
    };
    ::PROCESS_INFORMATION pi {};

    // Redirect
    ::SECURITY_ATTRIBUTES sa { // We need to set this to true so that the child process can inherit the handles
        .nLength = sizeof(sa),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    ::BOOL inherit = FALSE;
    struct Pair {
        ~Pair() {
            if (read) {
                ::CloseHandle(read);
            }
            if (write) {
                ::CloseHandle(write);
            }
        }
        HANDLE read = nullptr;
        HANDLE write = nullptr;
    } stdio[3];
    // Create pipe for each redirect, and disable the our pair inheritance
    if (flags & Process::RedirectStdin) {
        if (!win32::pipe(&stdio[0].read, &stdio[0].write, &sa)) {
            return Err(SystemError::fromErrno());
        }
        if (!::SetHandleInformation(stdio[0].write, HANDLE_FLAG_INHERIT, 0)) {
            return Err(SystemError::fromErrno());
        }
        info.hStdInput = stdio[0].read;
        info.dwFlags |= STARTF_USESTDHANDLES;
        inherit = TRUE;
    }
    if (flags & Process::RedirectStdout) {
        if (!win32::pipe(&stdio[1].read, &stdio[1].write, &sa)) {
            return Err(SystemError::fromErrno());
        }
        if (!::SetHandleInformation(stdio[1].read, HANDLE_FLAG_INHERIT, 0)) {
            return Err(SystemError::fromErrno());
        }
        info.hStdOutput = stdio[1].write;
        info.dwFlags |= STARTF_USESTDHANDLES;
        inherit = TRUE;
    }
    if (flags & Process::RedirectStderr) {
        if (!win32::pipe(&stdio[2].read, &stdio[2].write, &sa)) {
            return Err(SystemError::fromErrno());
        }
        if (!::SetHandleInformation(stdio[2].read, HANDLE_FLAG_INHERIT, 0)) {
            return Err(SystemError::fromErrno());
        }
        info.hStdError = stdio[2].write;
        info.dwFlags |= STARTF_USESTDHANDLES;
        inherit = TRUE;
    }
    auto ok = ::CreateProcessW(
        nullptr,
        cmdline.data(),
        nullptr,
        nullptr,
        inherit,
        NORMAL_PRIORITY_CLASS,
        nullptr,
        nullptr,
        &info,
        &pi
    );
    if (!ok) {
        return Err(SystemError::fromErrno());
    }
    ::CloseHandle(pi.hThread); // We don't need this

    // Begin wrap it
    Process proc;
    proc.mHandle.reset(pi.hProcess);

    if (flags & Process::RedirectStdin) {
        auto ptr = std::exchange(stdio[0].write, nullptr);
        auto handle = IoHandle<FileDescriptor>::make(FileDescriptor(ptr));
        if (!handle) {
            return Err(handle.error());
        }
        proc.mStdin = Pipe(std::move(*handle));
    }
    if (flags & Process::RedirectStdout) {
        auto ptr = std::exchange(stdio[1].read, nullptr);
        auto handle = IoHandle<FileDescriptor>::make(FileDescriptor(ptr));
        if (!handle) {
            return Err(handle.error());
        }
        proc.mStdout = Pipe(std::move(*handle));
    }
    if (flags & Process::RedirectStderr) {
        auto ptr = std::exchange(stdio[2].read, nullptr);
        auto handle = IoHandle<FileDescriptor>::make(FileDescriptor(ptr));
        if (!handle) {
            return Err(handle.error());
        }
        proc.mStderr = Pipe(std::move(*handle));
    }
    return proc;
}

auto Process::kill() const -> IoResult<void> {
    auto handle = mHandle.get();
    if (!::TerminateProcess(handle, 0)) {
        return Err(SystemError::fromErrno());
    }
    return {};
}

auto Process::detach() -> void {
    mHandle.reset();
}

auto Process::wait() const -> IoTask<int32_t> {
    if (auto res = co_await win32::waitObject(mHandle.get()); !res) {
        co_return Err(res.error());
    }
    DWORD code = 0;
    if (!::GetExitCodeProcess(mHandle.get(), &code)) {
        co_return Err(SystemError::fromErrno());
    }
    co_return std::bit_cast<int32_t>(code);
}

ILIAS_NS_END
