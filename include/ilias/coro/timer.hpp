#pragma once

#include "loop.hpp"
#include "task.hpp"
#include "awaiter.hpp"
#include <chrono>

ILIAS_NS_BEGIN

namespace chrono = ::std::chrono;

/**
 * @brief Awaiter for Sleep a specified time
 * 
 * @tparam T 
 */
class _SleepAwaiter final : public AwaiterImpl<_SleepAwaiter>{
public:
    _SleepAwaiter(int64_t ms) : mMs(ms) { }
    _SleepAwaiter(const _SleepAwaiter &) = delete;
    _SleepAwaiter& operator=(const _SleepAwaiter &) = delete;
    _SleepAwaiter(_SleepAwaiter &&) = default;

    auto ready() const -> bool {
        return mMs <= 0;
    }
    // Return to EventLoop
    auto suspend(CoroHandle handle) -> bool {
        // Create a timer
        mCaller = handle;
        mTimer = mCaller->eventLoop()->addTimer(mMs, &_SleepAwaiter::_onTimer, this, EventLoop::TimerSingleShot);
        return mTimer != 0; //< On 0, addTimer failed
    }
    auto resume() const -> Result<> {
        return mResult;
    }
    auto cancel() {
        if (mTimer) {
            mCaller->eventLoop()->delTimer(mTimer);
        }
        mResult = Unexpected(Error::Canceled);
    }
private:
    static void _onTimer(void *ptr) {
        auto self = static_cast<_SleepAwaiter *>(ptr);
        ILIAS_CHECK_EXISTS(self->mCaller.handle());
        self->mTimer = 0;
        self->mCaller.resume();
    }

    CoroHandle mCaller;
    Result<>  mResult;
    uintptr_t mTimer = 0; //< Timer handle
    int64_t   mMs = 0; //< Target sleep time
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
        return _SleepAwaiter(tag.time);
    }
};

inline auto _Sleep(int64_t ms) -> Task<> {
    co_return co_await _SleepTags(ms);
}

/**
 * @brief Sleep a specified time
 * 
 * @param ms 
 * @return Task<> 
 */
inline auto Sleep(chrono::milliseconds ms) -> Task<> {
    return _Sleep(ms.count());
}


ILIAS_NS_END