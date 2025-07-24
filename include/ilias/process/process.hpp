/**
 * @file process.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The process class, used to manage the process
 * @version 0.1
 * @date 2025-05-04
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include <ilias/io/system_error.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/task/task.hpp>
#include <utility>
#include <csignal>
#include <string>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
    #include <ilias/detail/win32.hpp>
#else
    #include <sys/syscall.h> // We need to directly use syscall for pidfd
    #include <sys/poll.h>
    #include <unistd.h>
    #include <spawn.h>
#endif

ILIAS_NS_BEGIN

/**
 * @brief The process class
 * 
 */
class Process {
public:
    Process() = default;
    Process(IoContext &ctxt, fd_t handle);
    Process(const Process &) = delete;
    Process(Process &&other) noexcept;
    ~Process() { detach(); }

    /**
     * @brief Detch the process, just like thread detach, we will lose the control of the process
     * 
     */
    auto detach() -> void;

    /**
     * @brief Send a signal to the process
     * 
     * @return IoResult<void> 
     */
    auto kill() const -> IoResult<void>;

    /**
     * @brief Wait for the process to be done, if canceled, we will kill the process
     * 
     * @return auto 
     */
    auto join() const;

    /**
     * @brief Get the descriptor for the process
     * 
     * @return fd_t 
     */
    auto fd() const -> fd_t;

    auto operator =(Process &&other) -> Process &;

    /**
     * @brief Spawn a process with the specified command line arguments.s
     * 
     * @param exec The executable path
     * @param args The arguments passed to the executable
     * 
     */
    static auto spawn(std::string_view exec, std::vector<std::string_view> args = {});
    static auto spawn(IoContext &ctxt, std::string_view exec, std::vector<std::string_view> args = {}) -> IoResult<Process>;
    static auto spawnLinux(IoContext &ctxt, std::string_view exec, std::vector<std::string_view> args) -> IoResult<Process>;
    static auto spawnWin32(IoContext &ctxt, std::wstring args) -> IoResult<Process>;

    /**
     * @brief Check if the process is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mCtxt != nullptr;
    }
private:
    IoContext    *mCtxt = nullptr;
    IoDescriptor *mDesc = nullptr; // The added descriptor in the context (only used in linux (pidfd))
    fd_t          mHandle = fd_t(-1); // The process handle (HANDLE in Windows, pidfd in linux)
};

inline Process::Process(IoContext &ctxt, fd_t handle) : mCtxt(&ctxt), mHandle(handle) {

#if !defined(_WIN32)
    mDesc = ctxt.addDescriptor(handle, IoDescriptor::Pipe).value_or(nullptr); // make a dirty hack to add the pidfd to the context, TODO: use a better way
#endif

}

inline Process::Process(Process &&other) noexcept : 
    mCtxt(std::exchange(other.mCtxt, nullptr)),
    mDesc(std::exchange(other.mDesc, nullptr)),
    mHandle(std::exchange(other.mHandle, fd_t(-1))) 
{

}

inline auto Process::detach() -> void {
    if (!mCtxt) {
        return;        
    }
    if (mDesc) {
        mCtxt->removeDescriptor(mDesc);
    }
    fd_utils::close(mHandle);
    mCtxt = nullptr;
    mDesc = nullptr;
    mHandle = fd_t(-1);
}

inline auto Process::kill() const -> IoResult<void> {
    if (!mCtxt) {
        return Err(IoError::InvalidArgument);
    }

#if defined(_WIN32)
    if (::TerminateProcess(mHandle, 0)) {
        return {};
    }
#elif defined(SYS_pidfd_send_signal)
    auto send_signal = [](int pidfd, int sig, siginfo_t *info, unsigned int flags) {
        return ::syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
    };
    if (send_signal(mHandle, SIGKILL, nullptr, 0) == 0) {
        return {};
    }
#else
    return Err(SystemError::OperationNotSupported);
#endif

    return Err(SystemError::fromErrno());
}

inline auto Process::join() const {

#if defined(_WIN32)
    return win32::WaitObject(mHandle, INFINITE, [this]() {
        kill();
    });
#else
    return mCtxt->poll(mDesc, POLLIN); // I don't want to include network module here, so just use the platform defines
#endif

}

inline auto Process::fd() const -> fd_t {
    return mHandle;
}

inline auto Process::operator=(Process &&other) -> Process & {
    if (this == &other) {
        return *this;
    }
    detach();
    mCtxt = std::exchange(other.mCtxt, nullptr);
    mDesc = std::exchange(other.mDesc, nullptr);
    mHandle = std::exchange(other.mHandle, fd_t(-1));
    return *this;
}

inline auto Process::spawn(std::string_view exec, std::vector<std::string_view> args) {
    struct Awaiter : detail::GetContextAwaiter {
        auto await_resume() {
            return spawn(context(), exec, std::move(args));
        }
        std::string_view exec;
        std::vector<std::string_view> args;
    };
    Awaiter awaiter;
    awaiter.exec = exec;
    awaiter.args = std::move(args);
    return awaiter;
}

inline auto Process::spawn(IoContext &ctxt, std::string_view exec, std::vector<std::string_view> args) -> IoResult<Process> {

#if defined(_WIN32)
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
    return spawnWin32(ctxt, std::move(cmdline));
#else
    return spawnLinux(ctxt, exec, std::move(args));
#endif
}

#if defined(SYS_pidfd_open) // We need this system call
inline auto Process::spawnLinux(IoContext &ctxt, std::string_view exec, std::vector<std::string_view> args) -> IoResult<Process> {
    auto open = [](pid_t pid, unsigned int flags) {
        return ::syscall(SYS_pidfd_open, pid, flags);
    };
    // Dup it on the stack
    auto program = strndupa(exec.data(), exec.size());
    // Allocate argv array with space for program name + args + nullptr
    auto vec = (char**) alloca((args.size() + 2) * sizeof(char*));
    vec[0] = program;
    for (size_t i = 0; i < args.size(); i++) {
        vec[i + 1] = strndupa(args[i].data(), args[i].size());
    }
    vec[args.size() + 1] = nullptr;
    // Spawn it
    pid_t pid = 0;
    if (::posix_spawnp(&pid, program, nullptr, nullptr, vec, nullptr) != 0) {
        return Err(SystemError::fromErrno());
    }
    
    if (int fd = open(pid, 0); fd >= 0) {
        return Process(ctxt, fd);
    }
    return Err(SystemError::fromErrno());
}
#endif // defined(SYS_pidfd_open)

#if defined(_WIN32)
inline auto Process::spawnWin32(IoContext &ctxt, std::wstring args) -> IoResult<Process> {
    ::STARTUPINFOW si {
        .cb = sizeof(::STARTUPINFOW),
    };
    ::PROCESS_INFORMATION pi { };
    auto ok = ::CreateProcessW(
        nullptr,
        args.data(),
        nullptr,
        nullptr,
        FALSE,
        NORMAL_PRIORITY_CLASS,
        nullptr,
        nullptr,
        &si,
        &pi
    );
    if (!ok) {
        return Err(SystemError::fromErrno());
    }
    ::CloseHandle(pi.hThread); // We don't need the thread handle
    return Process(ctxt, pi.hProcess);
}
#endif // defined(_WIN32)

ILIAS_NS_END