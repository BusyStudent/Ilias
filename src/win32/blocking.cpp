#include <ilias/platform/detail/blocking.hpp>
#include <ilias/detail/win32defs.hpp>
#include <ilias/io/system_error.hpp> // SystemError
#include <ilias/io/fd.hpp> // Win32Handle
#include <ilias/log.hpp>
#include <atomic> // std::atomic
#include <latch> // std::latch

ILIAS_NS_BEGIN

// Do io call in thread pool
// MARK: SynchronousIo
template <typename Fn>
inline auto ioCall(const runtime::StopToken &token, Fn fn) -> std::invoke_result_t<Fn> {
    // Get the thread handle, used for CancelSynchronousIo
    HANDLE threadHandle = nullptr;
    auto ok = ::DuplicateHandle(
        ::GetCurrentProcess(),
        ::GetCurrentThread(),
        ::GetCurrentProcess(),
        &threadHandle,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS
    );
    if (!ok) {
        return Err(SystemError::fromErrno());
    }

    Win32Handle raii {threadHandle};

    // Check the stop request
    std::invoke_result_t<Fn> result = Err(SystemError::Canceled); // store the return value
    std::atomic<HANDLE> handle {threadHandle}; // nullptr on (canceled or completed)
    std::latch latch {1};
    runtime::StopCallback callback(token, [&]() {
        if (auto h = handle.exchange(nullptr); h != nullptr) {
            ::CancelSynchronousIo(h);
            latch.count_down();
        }
    });
    if (!token.stop_requested()) {
        result = fn();
    }
    if (handle.exchange(nullptr) == nullptr) { // Check the cancel is start?, if start, wait for it, if not, mark as completed
        ILIAS_TRACE("Win32", "Wait for CancelSynchronousIo at thread {}", ::GetCurrentThreadId());
        latch.wait();
    }
    return result;
}

auto runtime::threadpool::read(fd_t fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto token = co_await this_coro::stopToken();
    auto val = co_await blocking([&]() {
        return ioCall(token, [&]() -> IoResult<size_t> {
            ::DWORD readed = 0;
            if (::ReadFile(fd, buffer.data(), buffer.size(), &readed, nullptr)) {
                return readed;
            }
            return Err(SystemError::fromErrno());
        });
    });
    if (val == Err(SystemError::Canceled)) {
        co_await this_coro::stopped(); // Try set the context to stopped
    }
    co_return val;
}

auto runtime::threadpool::write(fd_t fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto token = co_await this_coro::stopToken();
    auto val = co_await blocking([&]() {
        return ioCall(token, [&]() -> IoResult<size_t> {
            ::DWORD written = 0;
            if (::WriteFile(fd, buffer.data(), buffer.size(), &written, nullptr)) {
                return written;
            }
            return Err(SystemError::fromErrno());
        });
    });
    if (val == Err(SystemError::Canceled)) {
        co_await this_coro::stopped(); // Try set the context to stopped
    }
    co_return val;
}

ILIAS_NS_END