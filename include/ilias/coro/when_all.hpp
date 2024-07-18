#pragma once

#include "task.hpp"


ILIAS_NS_BEGIN

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

template <typename T>
struct _WhenAllTags {
    T tuple;
};

template <IsTask T>
struct _WhenAllVecTags {
    std::vector<T> &vec;
};

template <typename T>
_WhenAllTags(T) -> _WhenAllTags<T>;


template <typename Tuple>
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

template <typename T, IsTask T1>
inline auto operator &&(_WhenAllTags<T> a, const T1 &b) noexcept {
    return _WhenAllTags {
        std::tuple_cat(a.tuple, std::tuple{ &b.promise() })
    };
}

ILIAS_NS_END