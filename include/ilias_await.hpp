#pragma once

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
    TaskAwaiter(TaskAwaiter &&) = default;
    TaskAwaiter& operator=(const TaskAwaiter &) = delete;

    auto await_ready() const -> bool {
        ILIAS_CTRACE("[Ilias] TaskAwaiter<{}>::await_ready caller {}", typeid(T).name(), static_cast<void*>(&mCaller));
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
        ILIAS_CTRACE(
            "[Ilias] TaskAwaiter<{}>::await_resume caller {} canceled {}, resumeCaller {}", 
            typeid(T).name(), static_cast<void*>(&mCaller), 
            mCaller.isCanceled(), static_cast<void*>(mCaller.resumeCaller())
        );
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
 * @brief Select a one ready task from it 
 * 
 * @tparam T 
 * @tparam Args 
 */
template <typename T, typename ...Args>
class WhenAnyAwaiter {
public:
    using Tuple = std::tuple<TaskPromise<Args>* ...>;
    using Variant = std::variant<Result<Args> ...>;

    WhenAnyAwaiter(TaskPromise<T> &caller, const Tuple &tasks) :
        mCaller(caller), mTasks{tasks} { }
    WhenAnyAwaiter(const WhenAnyAwaiter &) = delete;
    WhenAnyAwaiter& operator=(const WhenAnyAwaiter &) = delete;
    WhenAnyAwaiter(WhenAnyAwaiter &&) = default; 
    ~WhenAnyAwaiter() {
        if (mHasValue) {
            mValue.~Variant();
        }
    }

    auto await_ready() const -> bool {
        if (mCaller.isCanceled()) {
            return true;
        }
        bool got = false; //< Does we got any value by resume it?
        auto resume = [&](auto task) {
            if (got) {
                return;
            }
            task->handle().resume();
            if (task->handle().done()) {
                mCaller.setResumeCaller(task);
                got = true;
            }
        };
        // Dispatch all to resume
        std::apply([&](auto ...tasks) {
            (resume(tasks), ...);
        }, mTasks);
        return got;
    }
    // Return to Event Loop
    auto await_suspend(std::coroutine_handle<TaskPromise<T> > h) const -> void {
        auto setAwaiting = [this](auto task) {
            if (!task->handle().done()) {
                task->setPrevAwaiting(&mCaller);
            }
        };
        // Dispatch all to set Awating
        std::apply([&](auto ...tasks) {
            (setAwaiting(tasks), ...);
        }, mTasks);
    }
    auto await_resume() -> Variant {
        if (mCaller.isCanceled()) {
            return Variant(std::in_place_index_t<0>{}, Unexpected(Error::Canceled));
        }
        ILIAS_ASSERT(mCaller.resumeCaller());
        _makeResult(std::make_index_sequence<sizeof ...(Args)>());
        return std::move(mValue);
    }
private:
    template <size_t I>
    auto _makeResult() -> void {
        if (mHasValue) {
            return;
        }
        auto task = std::get<I>(mTasks);
        if (mCaller.resumeCaller() != task) {
            return;
        }
        mHasValue = true;
        new(&mValue) Variant(std::in_place_index_t<I>{}, task->value());
    }
    template <size_t ...N>
    auto _makeResult(std::index_sequence<N...>) -> void {
        (_makeResult<N>(), ...);
    }

    TaskPromise<T> &mCaller;
    Tuple mTasks;

    // Result value
    union {
        Variant mValue;
        int pad = 0;
    };
    bool mHasValue = false;
};

/**
 * @brief When all tasks done
 * 
 * @tparam T 
 * @tparam Args 
 */
template <typename T, typename ...Args>
class WhenAllAwaiter {
public:
    using InTuple = std::tuple<TaskPromise<Args>* ...>;
    using OutTuple = std::tuple<Result<Args> ...>;

    WhenAllAwaiter(TaskPromise<T> &caller, const InTuple &tasks) : mCaller(caller), mTasks(tasks) { }
    WhenAllAwaiter(const WhenAllAwaiter &) = delete;
    WhenAllAwaiter(WhenAllAwaiter &&) = default;

    auto await_ready() -> bool {
        if (mCaller.isCanceled()) {
            return true;
        }
        auto resume = [&, this](auto task) {
            task->handle().resume();
            if (task->handle().done()) {
                mWaitCount --;
            }
        };
        // Dispatch all to resume
        std::apply([&, this](auto ...tasks) {
            (resume(tasks), ...);
        }, mTasks);
        return mWaitCount == 0;
    }
    // Return to Event Loop
    auto await_suspend(std::coroutine_handle<TaskPromise<T> > h) -> std::coroutine_handle<> {
        // Let the wating task resume the helper task
        auto setAwaiting = [this](auto task) {
            if (!task->handle().done()) {
                task->setPrevAwaiting(&mHelperTask.promise());
            }
        };
        std::apply([&](auto ...tasks) {
            (setAwaiting(tasks), ...);
        }, mTasks);
        // Let the helper task resume us
        mHelperTask.promise().setPrevAwaiting(&mCaller);

        // Switch to the helper task
        return mHelperTask.handle();
    }
    auto await_resume() const -> OutTuple {
        if (mCaller.isCanceled()) {
            return _makeCanceledResult(std::make_index_sequence<sizeof ...(Args)>());
        }
        return _makeResult(std::make_index_sequence<sizeof ...(Args)>());
    }
private:
    template <size_t ...N>
    auto _makeResult(std::index_sequence<N...>) const -> OutTuple {
        return OutTuple {
            (std::get<N>(mTasks)->value())...
        };
    }
    template <size_t ...N>
    auto _makeCanceledResult(std::index_sequence<N...>) const -> OutTuple {
        return OutTuple {
            (std::get<N>(mTasks), Unexpected(Error::Canceled))...
        };
    }
    // Make a helper task, let awating task resume it and let it resume us
    auto _helperTask() -> Task<void> {
        while (mWaitCount && !mCaller.isCanceled()) {
            co_await std::suspend_always();
            mWaitCount --;
        }
        co_return Result<>();
    }

    TaskPromise<T> &mCaller;
    InTuple mTasks;
    size_t mWaitCount = sizeof ...(Args);
    Task<void> mHelperTask {_helperTask()};
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
    SleepAwaiter(SleepAwaiter &&) = default;

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
 * @brief Awaiter for Get current task's promise
 * 
 * @tparam T 
 */
template <typename T>
class PromiseAwaiter {
public:
    PromiseAwaiter(TaskPromise<T> &caller) : mCaller(caller) { }
    
    auto await_ready() const -> bool { return true; }
    auto await_suspend(std::coroutine_handle<> h) const -> void { ILIAS_ASSERT(false); }
    auto await_resume() const -> TaskPromise<T> * { return &mCaller; }
private:
    TaskPromise<T> &mCaller;
};

/**
 * @brief Place holder for wait until the time
 * 
 */
struct _SleepTags {
    chrono::steady_clock::time_point time;
};
struct _PromiseTags {

};
/**
 * @brief Place holder for transform to WhenAnyAwaiter
 * 
 * @tparam Args 
 */
template <typename T>
struct _WhenAnyTags {
    T tuple;
};
template <typename T>
struct _WhenAllTags {
    T tuple;
};


template <typename U>
class AwaitTransform<Task<U> > {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const Task<U> &task) {
        return TaskAwaiter<T, U>(*caller, task.promise());
    }
};

template <>
class AwaitTransform<_SleepTags> {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _SleepTags &tag) {
        return SleepAwaiter<T>(*caller, tag.time);
    }
};

template <>
class AwaitTransform<_PromiseTags> {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _PromiseTags &tag) {
        return PromiseAwaiter<T>(*caller);
    }
};

template <typename Tuple>
class AwaitTransform<_WhenAnyTags<Tuple> > {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _WhenAnyTags<Tuple> &tag) {
        return WhenAnyAwaiter(*caller, tag.tuple);
    }
};

template <typename Tuple>
class AwaitTransform<_WhenAllTags<Tuple> > {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _WhenAllTags<Tuple> &tag) {
        return WhenAllAwaiter(*caller, tag.tuple);
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
/**
 * @brief Wait any task was ready
 * 
 * @tparam Args 
 * @param tasks 
 * @return auto 
 */
template <IsTask ...Args>
inline auto WhenAny(Args &&...tasks) {
    return _WhenAnyTags { std::tuple((&tasks.promise())...) };
}
/**
 * @brief Wait all task was ready
 * 
 * @tparam Args 
 * @param tasks 
 * @return auto 
 */
template <IsTask ...Args>
inline auto WhenAll(Args &&...tasks) {
    return _WhenAllTags { std::tuple((&tasks.promise())...) };
}
/**
 * @brief Get the Current coroutine Promise object
 * 
 * @return _PromiseTags 
 */
inline auto GetPromise() -> _PromiseTags {
    return _PromiseTags {};
}

ILIAS_NS_END