#pragma once

#include "promise.hpp"
#include "task.hpp"

ILIAS_NS_BEGIN

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
    TaskAwaiter(TaskAwaiter &&) = default;
    TaskAwaiter& operator=(const TaskAwaiter &) = delete;

    auto await_ready() const -> bool {
        if (mCaller.isCanceled()) {
            return true;
        }
        auto handle = mTask.handle();
        if (!handle.done()) {
            handle.resume();
        }
        return handle.done();
    }
    // Return to EventLoop
    auto await_suspend(std::coroutine_handle<TaskPromise<T> > h) const -> void {
        ILIAS_ASSERT(mCaller.handle() == h);
        // When the task done, let it resume the caller
        mTask.setPrevAwaiting(&mCaller);
    }
    [[nodiscard("Don't discard await result")]]
    auto await_resume() const -> Result<U> {
        if (mCaller.isCanceled() && !mTask.handle().done()) {
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

template <typename U>
class AwaitTransform<Task<U> > {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const Task<U> &task) {
        return TaskAwaiter<T, U>(*caller, task.promise());
    }
};

ILIAS_NS_END