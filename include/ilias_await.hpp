#pragma once

#include "ilias_task.hpp"

#include <coroutine>
#include <optional>
#include <chrono>
#include <tuple>

ILIAS_NS_BEGIN

namespace chrono = ::std::chrono;

// --- Concepts
template <typename T>
struct PromiseTupleImpl : std::false_type { };

template <typename... Ts>
struct PromiseTupleImpl<std::tuple<TaskPromise<Ts> *...> > : std::true_type { };

/**
 * @brief Make sure the args is like std::tuple<TaskPromise<Ts> *...>
 * 
 * @tparam T 
 */
template <typename T>
concept PromiseTuple = PromiseTupleImpl<std::remove_reference_t<T> >::value;

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
        auto resume = [](auto task) {
            task->handle().resume();
        };
        auto check = [this](auto task) {
            if (task->handle().done()) {
                mWaitCount --;
            }
        };
        // Dispatch all to resume
        std::apply([&](auto ...tasks) {
            (resume(tasks), ...);
        }, mTasks);
        std::apply([&](auto ...tasks) {
            (check(tasks), ...);
        }, mTasks);
        return mWaitCount == 0;
    }
    // Return to Event Loop
    auto await_suspend(std::coroutine_handle<TaskPromise<T> > h) -> std::coroutine_handle<> {
        // Let the wating task resume the helper task
        mHelperTask = _helperTask();
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
        if (mHelperTask) {
            mHelperTask.promise().setPrevAwaiting(nullptr);
            mHelperTask.cancel();
        }
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
            (   std::get<N>(mTasks)->setPrevAwaiting(nullptr),
                std::get<N>(mTasks)->cancel(),
                Unexpected(Error::Canceled)
            )...
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
    Task<void> mHelperTask;
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
    using InTuple = std::tuple<TaskPromise<Args>* ...>;
    using OutTuple = std::tuple<std::optional<Result<Args> > ...>;

    WhenAnyAwaiter(TaskPromise<T> &caller, const InTuple &tasks) :
        mCaller(caller), mTasks{tasks} { }
    WhenAnyAwaiter(const WhenAnyAwaiter &) = delete;
    WhenAnyAwaiter(WhenAnyAwaiter &&) = default; 
    ~WhenAnyAwaiter() = default;

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
    auto await_resume() -> OutTuple {
        // Clear all task's prev awatting, avoid it resume the Caller
        std::apply([](auto ...tasks) {
            (tasks->setPrevAwaiting(nullptr), ...);
        }, mTasks);
        // Check Canceled
        if (mCaller.isCanceled()) {
            mCaller.setResumeCaller(nullptr); //< Set none-resume it, it will generator all std::nullopt tuple
        }
        else {
            ILIAS_ASSERT(mCaller.resumeCaller());
        }
        return _makeAllResult(std::make_index_sequence<sizeof ...(Args)>());
    }
private:
    template <size_t I>
    auto _makeResult() -> std::tuple_element_t<I, OutTuple> {
        auto task = std::get<I>(mTasks);
        if (mCaller.resumeCaller() != task) {
            return std::nullopt;
        }
        return task->value();
    }
    template <size_t ...N>
    auto _makeAllResult(std::index_sequence<N...>) -> OutTuple {
        return OutTuple(_makeResult<N>()...);
    }

    TaskPromise<T> &mCaller;
    InTuple mTasks;
};

// Vec version
template <typename T>
class WhenAllVecAwaiter {
public:
    WhenAllVecAwaiter(PromiseBase &caller, std::vector<Task<T> > &vec) :
        mCaller(caller), mVec(vec) { }
    WhenAllVecAwaiter(const WhenAllVecAwaiter &) = delete;
    WhenAllVecAwaiter(WhenAllVecAwaiter &&) = default;
    ~WhenAllVecAwaiter() = default;

    auto await_ready() -> bool {
        if (mCaller.isCanceled() || mVec.empty()) {
            return true;
        }
        // Try resume
        for (auto &task : mVec) {
            task.handle().resume();
        }
        _collectResult();
        return mVec.empty();
    }
    auto await_suspend(auto handle) -> void {
        mHelper = _helperTask();
        mHelper->setPrevAwaiting(&mCaller);
        for (auto &task : mVec) {
            task->setPrevAwaiting(&mHelper.promise());
        }
    }
    auto await_resume() -> std::vector<Result<T> > {
        if (mHelper) {
            mHelper.cancel();
        }
        _collectResult();
        return std::move(mResults);
    }
private:
    auto _helperTask() -> Task<void> {
        size_t num = mVec.size();
        while (num && !mHelper->isCanceled()) {
            --num;
            if (num) {
                co_await std::suspend_always();
            }
        }
        co_return {};
    }
    auto _collectResult() -> void {
        // Try collect result from begin to end
        for (auto it = mVec.begin(); it != mVec.end(); ) {
            if (it->handle().done()) {
                mResults.emplace_back(it->promise().value());
                it = mVec.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    PromiseBase &mCaller;
    std::vector<Task<T> > &mVec;
    std::vector<Result<T> > mResults;
    Task<void> mHelper;
};

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

/**
 * @brief Place holder for transform to WhenAnyAwaiter
 * 
 * @tparam Args 
 */
template <PromiseTuple T>
struct _WhenAnyTags {
    T tuple;
};

template <PromiseTuple T>
struct _WhenAllTags {
    T tuple;
};

/**
 * @brief Place holder for WhenAny(Vec<Task<T> >)
 * 
 * @tparam T 
 */
template <IsTask T>
struct _WhenAnyVecTags {
    std::vector<T> &vec;
};


template <IsTask T>
struct _WhenAllVecTags {
    std::vector<T> &vec;
};


template <typename T>
_WhenAnyTags(T) -> _WhenAnyTags<T>;
template <typename T>
_WhenAllTags(T) -> _WhenAllTags<T>;

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

template <PromiseTuple Tuple>
class AwaitTransform<_WhenAnyTags<Tuple> > {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _WhenAnyTags<Tuple> &tag) {
        return WhenAnyAwaiter(*caller, tag.tuple);
    }
};

template <PromiseTuple Tuple>
class AwaitTransform<_WhenAllTags<Tuple> > {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _WhenAllTags<Tuple> &tag) {
        return WhenAllAwaiter(*caller, tag.tuple);
    }
};

template <IsTask T>
class AwaitTransform<_WhenAllVecTags<T> > {
public:
    template <typename U>
    static auto transform(TaskPromise<U> *caller, const _WhenAllVecTags<T> &tag) {
        return WhenAllVecAwaiter(*caller, tag.vec);
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

/**
 * @brief Wait any task was ready
 * 
 * @tparam Args 
 * @param tasks 
 * @return auto 
 */
template <IsTask ...Args>
inline auto WhenAny(Args &&...tasks) noexcept {
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
inline auto WhenAll(Args &&...tasks) noexcept {
    return _WhenAllTags { std::tuple((&tasks.promise())...) };
}

/**
 * @brief When all task in vector ready and get value
 * 
 * @tparam T 
 * @param task 
 * @return auto 
 */
template <IsTask T>
inline auto WhenAll(std::vector<T> &task) noexcept {
    return _WhenAllVecTags { task };
}

/**
 * @brief When All a and b
 * 
 * @tparam T1 
 * @tparam T2 
 * @param t 
 * @param t2 
 * @return auto 
 */
template <IsTask T1, IsTask T2>
inline auto operator &&(const T1 &a, const T2 &b) noexcept {
    return _WhenAllTags { std::tuple{ &a.promise(), &b.promise() } };
}

template <PromiseTuple T, IsTask T1>
inline auto operator &&(_WhenAllTags<T> a, const T1 &b) noexcept {
    return _WhenAllTags {
        std::tuple_cat(a.tuple, std::tuple{ &b.promise() })
    };
}

/**
 * @brief When Any a or b
 * 
 * @tparam T1 
 * @tparam T2 
 * @param t 
 * @param t2 
 * @return auto 
 */
template <IsTask T1, IsTask T2>
inline auto operator ||(const T1 &t, const T2 &t2) noexcept {
    return _WhenAnyTags { std::tuple{ &t.promise(), &t2.promise() } };
}

template <PromiseTuple T, IsTask T1>
inline auto operator ||(_WhenAnyTags<T> a, const T1 &b) noexcept {
    return _WhenAnyTags {
        std::tuple_cat(a.tuple, std::tuple{ &b.promise() })
    };
}

// This namespace for Get the task self' infomation or doing some yield or control... etc...
namespace ThisTask {
    struct _Yield {
        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(auto handle) const noexcept -> void { handle.promise().eventLoop()->resumeHandle(handle); }
        auto await_resume() const noexcept -> void { }
    };

    /**
     * @brief Yield
     * 
     * @return _Yield 
     */
    [[nodiscard("Non't forget to use co_await")]]
    inline auto yield() -> _Yield { return { }; }

    /**
     * @brief Sleep for ms
     * 
     * @param ms 
     * @return auto 
     */
    [[nodiscard("Non't forget to use co_await")]]
    inline auto msleep(int64_t ms) { return Sleep(chrono::milliseconds(ms)); }
}
namespace this_task = ThisTask; //< For STL Style

ILIAS_NS_END