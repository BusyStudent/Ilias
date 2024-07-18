#pragma once

#include "loop.hpp"
#include "task.hpp"
#include <chrono>

ILIAS_NS_BEGIN

namespace chrono = ::std::chrono;

/**
 * @brief Awaiter for Sleep a specified time
 * 
 * @tparam T 
 */
template <typename T>
class SleepAwaiter {
public:
    SleepAwaiter(TaskPromise<T> &caller, int64_t ms) : mCaller(caller), mMs(ms) { }
    SleepAwaiter(const SleepAwaiter &) = delete;
    SleepAwaiter& operator=(const SleepAwaiter &) = delete;
    SleepAwaiter(SleepAwaiter &&) = default;

    auto await_ready() const -> bool {
        if (mMs <= 0) {
            return true;
        }
        return mCaller.isCanceled();
    }
    // Return to EventLoop
    auto await_suspend(std::coroutine_handle<TaskPromise<T> > h) -> bool {
        ILIAS_ASSERT(mCaller.handle() == h);
        // Create a timer
        mTimer = mCaller.eventLoop()->addTimer(mMs, &SleepAwaiter::_onTimer, this, EventLoop::TimerSingleShot);
        return mTimer != 0; //< On 0, addTimer failed
    }
    auto await_resume() const -> Result<void> {
        if (mTimer) {
            mCaller.eventLoop()->delTimer(mTimer);
        }
        if (mCaller.isCanceled()) {
            return Unexpected(Error::Canceled);
        }
        return {};
    }
private:
    static void _onTimer(void *ptr) {
        auto self = static_cast<SleepAwaiter *>(ptr);
        ILIAS_CHECK_EXISTS(self->mCaller.handle());
        self->mTimer = 0;
        self->mCaller.handle().resume();
    }

    uintptr_t mTimer = 0;
    TaskPromise<T> &mCaller;
    int64_t mMs = 0; //< Target sleep time
};

/**
 * @brief Place holder for wait for the time
 * 
 */
struct _SleepTags {
    int64_t time;
};

template <>
class AwaitTransform<_SleepTags> {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _SleepTags &tag) {
        return SleepAwaiter<T>(*caller, tag.time);
    }
};

/**
 * @brief Sleep a specified time
 * 
 * @param ms 
 * @return Task<> 
 */
inline auto Sleep(chrono::milliseconds ms) -> Task<> {
    co_return co_await _SleepTags(ms.count());
}


ILIAS_NS_END