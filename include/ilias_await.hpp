#pragma once

#include <concepts>
#include <coroutine>
#include <variant>
#include <chrono>
#include <tuple>
#include "ilias_task.hpp"

ILIAS_NS_BEGIN

namespace chrono = ::std::chrono;

/**
 * @brief Awaiter for Task to await another task
 * 
 * @tparam T 
 */
template <typename T, typename U>
class TaskAwaiter {
public:
    TaskAwaiter(TaskPromise<T> &caller, TaskPromise<U> &task) : mCaller(caller), mTask(task) { }
    TaskAwaiter(const TaskAwaiter &) = delete;
    TaskAwaiter& operator=(const TaskAwaiter &) = delete;

    auto await_ready() const -> bool {
        ILIAS_CTRACE("[Ilias] TaskAwaiter<{}>::await_ready", typeid(T).name());
        if (mCaller.isCanceled()) {
            return true;
        }
        mTask.handle().resume();
        return mTask.handle().done();
    }
    // Return to EventLoop
    auto await_suspend(std::coroutine_handle<TaskPromise<T> > h) const -> void {
        ILIAS_ASSERT(mCaller.handle() == h);
        // When the task done, let it resume the caller
        mTask.setPrevAwaiting(&mCaller);
    }
    [[nodiscard("Don't discard await result")]]
    auto await_resume() const -> Result<U> {
        ILIAS_CTRACE("[Ilias] TaskAwaiter<{}>::await_resume {}", typeid(T).name(), mCaller.isCanceled());
        if (mCaller.isCanceled()) {
            //< Avoid mTask is still no done, when it was cancel, it will resume the caller
            mTask.setPrevAwaiting(nullptr);
            return Unexpected(Error::Canceled);
        }
        return mTask.value();
    }
private:
    TaskPromise<T> &mCaller;
    TaskPromise<U> &mTask;
};

/**
 * @brief Awaiter for Sleep a specified time
 * 
 * @tparam T 
 */
template <typename T>
class SleepAwaiter {
public:
    SleepAwaiter(TaskPromise<T> &caller, chrono::steady_clock::time_point t) : mCaller(caller), mTime(t) { }
    SleepAwaiter(const SleepAwaiter &) = delete;
    SleepAwaiter& operator=(const SleepAwaiter &) = delete;

    auto await_ready() const -> bool {
        if (mTime <= chrono::steady_clock::now()) {
            return true;
        }
        return mCaller.isCanceled();
    }
    // Return to EventLoop
    auto await_suspend(std::coroutine_handle<TaskPromise<T> > h) -> bool {
        ILIAS_ASSERT(mCaller.handle() == h);
        auto ms = chrono::duration_cast<chrono::milliseconds>(
            mTime - chrono::steady_clock::now()
        );
        // Create a timer
        mTimer = mCaller.eventLoop()->addTimer(ms.count(), &SleepAwaiter::_onTimer, this, EventLoop::TimerSingleShot);
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
        self->mTimer = 0;
        self->mCaller.handle().resume();
    }

    uintptr_t mTimer = 0;
    TaskPromise<T> &mCaller;
    chrono::steady_clock::time_point mTime; //< Target sleep time
};
/**
 * @brief Place holder for wait until the time
 * 
 */
struct _SleepTags {
    chrono::steady_clock::time_point time;
};

template <typename U>
class AwaitTransform<Task<U> > {
public:
    template <typename T>
    auto transform(TaskPromise<T> *caller, const Task<U> &task) {
        return TaskAwaiter<T, U>(*caller, task.promise());
    }
};

template <>
class AwaitTransform<_SleepTags> {
public:
    template <typename T>
    auto transform(TaskPromise<T> *caller, const _SleepTags &tag) {
        return SleepAwaiter<T>(*caller, tag.time);
    }
};

/**
 * @brief Sleep until the specified time
 * 
 * @param t 
 * @return Task<> 
 */
inline auto SleepUntil(chrono::steady_clock::time_point t) -> Task<> {
    co_return co_await _SleepTags{t};
}
/**
 * @brief Sleep a specified time
 * 
 * @param ms 
 * @return Task<> 
 */
inline auto Sleep(chrono::milliseconds ms) -> Task<> {
    return SleepUntil(chrono::steady_clock::now() + ms);
}


ILIAS_NS_END