#include <ilias/platform/detail/blocking.hpp>
#include <ilias/detail/scope_exit.hpp>
#include <ilias/io/system_error.hpp> // SystemError
#include <ilias/log.hpp>
#include <pthread.h> // pthread_self
#include <unistd.h>
#include <csignal>
#include <atomic> // std::atomic

#if __has_include(<aio.h>)
    #define ILIAS_HAVE_AIO
    #include <aio.h>
#endif // __has_include(<aio.h>)

ILIAS_NS_BEGIN

namespace runtime {
namespace {

#if defined(ILIAS_HAVE_AIO)
// MARK: Aio
/**
 * @brief The common base for aio awaiter
 * 
 */
class AioAwaiterBase : public ::aiocb { // The control block
public:
    AioAwaiterBase(int fd) : ::aiocb { .aio_fildes = fd } {}
    AioAwaiterBase(AioAwaiterBase &&) = default;

    // Always suspend
    auto await_ready() noexcept -> bool { 
        aio_sigevent.sigev_notify = SIGEV_THREAD; //< Use callback
        aio_sigevent.sigev_value.sival_ptr = this;
        aio_sigevent.sigev_notify_function = onNotifyEntry;
        return false; 
    }

    // Do the suspend
    template <typename T>
    auto suspend(runtime::CoroHandle caller) -> bool {
        mCaller = caller;
        if (!static_cast<T *>(this)->onSubmit()) { 
            // Error on submit, accroding to man, it will set errno
            mResult = Err(SystemError::fromErrno());
            return false;
        }
        mReg.register_<&AioAwaiterBase::cancel>(caller.stopToken(), this);
        return true;
    }

    // Try cancel the operation, note it will unlink self in the list
    auto cancel() -> void {
        [[maybe_unused]] auto ret = ::aio_cancel(this->aio_fildes, this);
        ILIAS_TRACE("AIO", "Cancel op on fd {}, res {}", this->aio_fildes, ret);
    }
protected:
    auto onNotify() -> void {
        // Get the result
        if (auto res = ::aio_return(this); res >= 0) {
            mResult = res;
        }
        else {
            mResult = Err(SystemError(::aio_error(this)));
        }
        ILIAS_TRACE("AIO", "Operation complete on fd {}, result {}", this->aio_fildes, mResult ? std::to_string(*mResult) : mResult.error().message());
        
        // Check the stop request
        if (mResult == Err(SystemError::Canceled) && mCaller.isStopRequested()) {
            // If the operation was canceled and the caller requested stop, mark the caller as stopped
            mCaller.executor().schedule([this]() { // Back to the executor thread
                mCaller.setStopped();
            });
            return;
        }

        // Check if stop request
        mCaller.schedule();
    }

    static auto onNotifyEntry(::sigval val) -> void {
        static_cast<AioAwaiterBase *>(val.sival_ptr)->onNotify();
    }

    // States
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;

    // Result of the io operation
    IoResult<size_t> mResult {0};
};

/**
 * @brief The CRTP for aio awaiter
 * 
 * @tparam T 
 */
template <typename T>
class AioAwaiter : public AioAwaiterBase {
public:
    using AioAwaiterBase::AioAwaiterBase;

    auto await_suspend(runtime::CoroHandle caller) -> bool {
        return suspend<T>(caller);
    }

    auto await_resume() {
        return static_cast<T *>(this)->onComplete(mResult);
    }

    // Default implementations
    auto onComplete(IoResult<size_t> res) -> IoResult<size_t> {
        return res;
    }
};

/**
 * @brief Wrapping aio_read
 * 
 */
class AioReadAwaiter final : public AioAwaiter<AioReadAwaiter> {
public:
    AioReadAwaiter(int fd, MutableBuffer buffer, std::optional<size_t> offset) : AioAwaiter(fd) {
        aio_offset = offset.value_or(0);
        aio_nbytes = buffer.size_bytes();
        aio_buf = buffer.data();
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("Aio", "Submit read {} bytes offset {} on fd {}", aio_nbytes, aio_offset, aio_fildes);
        return ::aio_read(this) == 0;
    }
};

/**
 * @brief Wrapping aio_write
 * 
 */
class AioWriteAwaiter final : public AioAwaiter<AioWriteAwaiter> {
public:
    AioWriteAwaiter(int fd, Buffer buffer, std::optional<size_t> offset) : AioAwaiter(fd) {
        aio_offset = offset.value_or(0);
        aio_nbytes = buffer.size_bytes();
        aio_buf = const_cast<std::byte*>(buffer.data());
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("Aio", "Submit write {} bytes offset {} on fd {}", aio_nbytes, aio_offset, aio_fildes);
        return ::aio_write(this) == 0;
    }
};

class AioSyncAwaiter final : public AioAwaiter<AioSyncAwaiter> {
public:
    AioSyncAwaiter(int fd, int op) : AioAwaiter(fd), mOp(op) {}

    auto onSubmit() -> bool {
        return ::aio_fsync(mOp, this) == 0;
    }

    auto onComplete(IoResult<size_t> res) -> IoResult<void> {
        if (!res) {
            return Err(res.error());
        }
        return {};
    }
private:
    int mOp;
};

#else

// MARK: Blocking
// We select an signal to used as interrupt the handler
constinit int gSignalCancel = 0; // 0 on unsupport cancel

[[using gnu: used, visibility("hidden"), constructor(101)]] // Run as earily as possible
auto initSignalCancel() -> void { 
    struct sigaction action{};
    struct sigaction prev{};

    action.sa_sigaction = [](int, ::siginfo_t *, void *) {}; // No-op handler
    action.sa_flags = SA_SIGINFO;
    ::sigemptyset(&action.sa_mask);

    for (int sig = SIGRTMIN; sig <= SIGRTMAX; sig++) {
        if (::sigaction(sig, &action, &prev) == -1) {
            break;
        }
        if (prev.sa_handler != SIG_DFL) { // Used, try to find a another one
            ::sigaction(sig, &prev, nullptr);
            continue;
        }
        // Maybe block the signal ?
        ::sigset_t set{};
        ::sigemptyset(&set);
        ::sigaddset(&set, sig);
        ::pthread_sigmask(SIG_BLOCK, &set, nullptr); // In all thread begin, it may safe?
        gSignalCancel = sig; // Got
        return;
    }
}

// Perform the blocking io call (handle the cancel inside)
template <std::invocable Fn>
auto ioCall(const StopToken &token, Fn fn) -> std::invoke_result_t<Fn> {
    if (gSignalCancel == 0) [[unlikely]] {
        return fn();
    }

    std::atomic<pthread_t> threadId{::pthread_self()};

    // Register the stop callback
    StopCallback cb(token, [&]() {
        auto id = threadId.exchange({});
        if (id == pthread_t{}) { // Already done
            return;
        }
        ::pthread_kill(id, gSignalCancel);
    });


    // Prepare unblock the signal
    ::sigset_t set{};
    ::sigemptyset(&set);
    ::sigaddset(&set, gSignalCancel);
    ::pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
    ScopeExit guard([&]() {
        threadId.exchange({}); // Clear the thread id
        ::pthread_sigmask(SIG_BLOCK, &set, nullptr);
    });

    do {
        if (token.stop_requested()) {
            return Err(SystemError::Canceled); // Canceled
        }
        auto res = fn();
        if (res == Err(SystemError(EINTR)) && token.stop_requested()) {
            return Err(SystemError::Canceled); // Canceled
        }
        if (res == Err(SystemError(EINTR))) { // Interrupted by anther signal, try again
            continue;
        }
        return res;
    }
    while (true);
}

#endif // ILIAS_HAVE_AIO

} // namespace

// MARK: Threadpool
auto threadpool::read(fd_t fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
#if defined(ILIAS_HAVE_AIO)
    co_return co_await AioReadAwaiter{fd, buffer, offset};
#else
    co_await this_coro::stopPoint();
    auto token = co_await this_coro::stopToken();
    auto res = co_await blocking([&]() {
        return ioCall(token, [&]() -> IoResult<size_t> {
            ::ssize_t bytes = 0;
            if (offset) {
                bytes = ::pread(fd, buffer.data(), buffer.size(), *offset);
            }
            else {
                bytes = ::read(fd, buffer.data(), buffer.size());
            }
            if (bytes < 0) {
                return Err(SystemError::fromErrno());
            }
            return bytes;
        });
    });
    // Maybe canceled?
    if (res == Err(SystemError::Canceled)) {
        co_await this_coro::stopPoint();
    }
    co_return res;
#endif // ILIAS_HAVE_AIO
}

auto threadpool::write(fd_t fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
#if defined(ILIAS_HAVE_AIO)
    co_return co_await AioWriteAwaiter{fd, buffer, offset};
#else
    co_await this_coro::stopPoint();
    auto token = co_await this_coro::stopToken();
    auto res = co_await blocking([&]() {
        return ioCall(token, [&]() -> IoResult<size_t> {
            ::ssize_t bytes = 0;
            if (offset) {
                bytes = ::pwrite(fd, buffer.data(), buffer.size(), *offset);
            }
            else {
                bytes = ::write(fd, buffer.data(), buffer.size());
            }
            if (bytes < 0) {
                return Err(SystemError::fromErrno());
            }
            return bytes;
        });
    });
        
    // Maybe canceled?
    if (res == Err(SystemError::Canceled)) {
        co_await this_coro::stopPoint();
    }
    co_return res;
#endif // ILIAS_HAVE_AIO
}

} // namespace runtime

ILIAS_NS_END