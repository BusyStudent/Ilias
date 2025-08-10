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
#include <ilias/fs/pipe.hpp>
#include <string>

ILIAS_NS_BEGIN

/**
 * @brief The process class
 * 
 */
class ILIAS_API Process {
public:
    enum Flags : uint32_t {
        None           = 0 << 0,
        RedirectStdin  = 1 << 0,
        RedirectStdout = 1 << 1,
        RedirectStderr = 1 << 2,
        RedirectAll = RedirectStdin | RedirectStdout | RedirectStderr,
    };
    enum Behavior : uint32_t {
        Detach, // Detach the process when the process is destroyed
        Kill,   // Kill the process when the process is destroyed
    };

    Process() = default;
    Process(const Process &) = delete;
    Process(Process &&other) = default;
    ~Process() {
        if (!bool(mHandle)) {
            return;
        }
        if (mBehavior == Kill) {
            auto _ = kill();
        }
        else {
            detach();
        }
    }

    // We can't use stdin, stdout, stderr as member variable name, they are macros :(
    auto in() -> Pipe &;
    auto out() -> Pipe &;
    auto err() -> Pipe &;

    /**
     * @brief Detch the process, just like thread detach, we will lose the control of the process
     * 
     */
    auto detach() -> void;

    /**
     * @brief Set the Behavior when destroy the process
     * 
     * @param behavior 
     */
    auto setBehavior(Behavior behavior) -> void;

    /**
     * @brief Send a signal to the process
     * 
     * @return IoResult<void> 
     */
    auto kill() const -> IoResult<void>;

    /**
     * @brief Wait for the process to be done, if canceled, we will kill the process
     * 
     * @return int32_t The exit code of the process
     */
    auto wait() const -> IoTask<int32_t>;

    /**
     * @brief Get the native handle for the process (Process HANDLE on Windows, pidfd on Linux)
     * 
     * @return fd_t 
     */
    auto nativeHandle() const -> fd_t;

    /**
     * @brief Spawn a process with the specified command line arguments
     * 
     * @param exec The executable path
     * @param args The arguments passed to the executable
     * @param flags The flags for the process, see Flags
     * @return IoResult<Process>
     * 
     */
    static auto spawn(std::string_view exec, std::vector<std::string_view> args = {}, uint32_t flags = None) -> IoResult<Process>;

    /**
     * @brief Check the process is not empty
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:

#if defined(_WIN32)
    struct Deleter {
        void operator()(void *handle) const {
            ::CloseHandle(handle);
        }
    };
    std::unique_ptr<void, Deleter> mHandle;
#else
    IoHandle<FileDescriptor> mHandle;
#endif // _WIN32

    // For redirect
    Pipe mStdin;
    Pipe mStdout;
    Pipe mStderr;
    Behavior mBehavior = Kill;
};

inline auto Process::in() -> Pipe & {
    return mStdin;
}

inline auto Process::out() -> Pipe & {
    return mStdout;
}

inline auto Process::err() -> Pipe & {
    return mStderr;
}

inline auto Process::setBehavior(Behavior behavior) -> void {
    mBehavior = behavior;
}

inline auto Process::nativeHandle() const -> fd_t {
    
#if defined(_WIN32)
    return mHandle.get();
#else
    return fd_t(mHandle.fd());
#endif // _WIN32

}

ILIAS_NS_END