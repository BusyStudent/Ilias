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
#include <ilias/io/context.hpp>
#include <ilias/io/error.hpp>
#include <ilias/io/fd.hpp>
#include <ilias/fs/pipe.hpp>
#include <initializer_list>
#include <string>
#include <ranges>
#include <vector>

ILIAS_NS_BEGIN

namespace detail {

#if defined(_WIN32)
struct HandleDeleter {
    void operator()(void *handle) const {
        ::CloseHandle(handle);
    }
};

using ProcessHandle = std::unique_ptr<void, HandleDeleter>;
#else // linux
using ProcessHandle = IoHandle<FileDescriptor>; // pidfd
#endif // _WIN32

} // namespace detail

/**
 * @brief The process class
 * 
 */
class ILIAS_API Process {
public:
    class Builder;
    class Output;

    Process(Process &&other) = default;
    Process() = default;

    /**
     * @brief Detch the process, just like thread detach, we will lose the control of the process
     * 
     */
    auto detach() -> void;

    /**
     * @brief Kill the process
     * 
     * @return IoResult<void> 
     */
    auto kill() const -> IoResult<void>;

    /**
     * @brief Wait for the process to be done, if canceled, we will kill the process
     * 
     * @return int32_t The exit status of the process
     */
    auto wait() const -> IoTask<int32_t>;

    /**
     * @brief Get the pid of the process
     * 
     * @return uint32_t 
     */
    auto pid() const -> uint32_t {
        return mPid;
    }

    /**
     * @brief Get the native handle for the process (Process HANDLE on Windows, pidfd on Linux)
     * 
     * @return fd_t 
     */
    auto nativeHandle() const -> fd_t;

    // Operators
    auto operator <=>(const Process &) const noexcept = default; 
    auto operator =(Process &&) -> Process & = default;

    /**
     * @brief Check the process is not empty
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mPid != 0; // The linux platform may fall back to use pid directly, so check it
    }
private:
    detail::ProcessHandle mHandle;
    uint32_t              mPid = 0;
};

/**
 * @brief The Output content of the process
 * 
 */
class Process::Output {
public:
    int32_t exitStatus = 0;
    std::string cout;
    std::string cerr;
};

/**
 * @brief The Builder::Builder class, used to create a process
 * 
 */
class Process::Builder {
public:
    explicit Builder(std::string_view exec) : mExec(exec) {}
    Builder(Builder &&) = default;
    ~Builder() = default;

    /**
     * @brief Set the arguments passed to the process
     * 
     * @tparam R 
     * @param arguments The arguments (like std::vector<std::string> etc.)
     * @return Builder & 
     */
    template <std::ranges::input_range R>
    auto args(R &&arguments) -> Builder & {
        mArgs.insert(mArgs.end(), std::ranges::begin(arguments), std::ranges::end(arguments));
        return *this;
    }

    /**
     * @brief Set the arguments passed to the process, allow args({"arg1", "arg2", "arg3})
     * 
     * @param args 
     * @return Builder & 
     */
    auto args(std::initializer_list<std::string_view> args) -> Builder & {
        mArgs.insert(mArgs.end(), args.begin(), args.end());
        return *this;
    }

    /**
     * @brief Redirect the stdin of the process
     * 
     * @param fd The fd must be readable
     * @return Builder & 
     */
    auto cin(FileDescriptor fd) -> Builder & {
        mStdin.emplace(std::move(fd));
        return *this;
    }

    /**
     * @brief Redirect the stdin to the pipe reader part
     * 
     * @param reader 
     * @return Builder & 
     */
    auto cin(PipeReader reader) -> Builder & {
        mStdin.emplace(reader.detach());
        return *this;
    }

    /**
     * @brief Redirect the stdout of the process
     * 
     * @param fd The fd must be writable
     * @return Builder & 
     */
    auto cout(FileDescriptor fd) -> Builder & {
        mStdout.emplace(std::move(fd));
        return *this;
    }

    /**
     * @brief Redirect the stdout to the pipe writer part
     * 
     * @param writer 
     * @return Builder & 
     */
    auto cout(PipeWriter writer) -> Builder & {
        mStdout.emplace(writer.detach());
        return *this;
    }

    /**
     * @brief Redirect the stderr of the process
     * 
     * @param fd The fd must be writable
     * @return Builder & 
     */
    auto cerr(FileDescriptor fd) -> Builder & {
        mStderr.emplace(std::move(fd));
        return *this;
    }

    /**
     * @brief Redirect the stderr to the pipe writer part
     * 
     * @param writer 
     * @return Builder & 
     */
    auto cerr(PipeWriter writer) -> Builder & {
        mStderr.emplace(writer.detach());
        return *this;
    }

    // Win32 specific
#if defined(_WIN32)
    auto creationFlags(::DWORD flags) -> Builder & {
        mCreationFlags = flags;
        return *this;
    }
#endif // _WIN32


    /**
     * @brief Spawn the process
     * 
     * @return IoResult<Process> 
     */
    ILIAS_API
    auto spawn() -> IoResult<Process>;

    /**
     * @brief Spawn the process and wait until it's done, get the output
     * 
     * @return IoTask<Output> 
     */
    ILIAS_API
    auto output() -> IoTask<Output>;

    // Operators
    auto operator <=>(const Builder &) const noexcept = default;
    auto operator =(Builder &&) -> Builder & = default;
private:
    std::string              mExec; // The executable path
    std::vector<std::string> mArgs; // The arguments
    std::vector<std::string> mEnvs; // The environment variables
    std::optional<FileDescriptor> mStdin;
    std::optional<FileDescriptor> mStdout;
    std::optional<FileDescriptor> mStderr;

#if defined(_WIN32)
    ::DWORD mCreationFlags = 0;
#endif // _WIN32

};

inline auto Process::detach() -> void {
    mHandle = {};
    mPid = 0;
}

ILIAS_NS_END