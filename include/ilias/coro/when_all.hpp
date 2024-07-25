#pragma once

#include "task.hpp"
#include "awaiter.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Helper task for count N of resume
 * 
 * @param waitCount 
 * @return Task<> 
 */
inline auto _CounterTask(size_t waitCount) -> Task<> {
    while (waitCount) {
        if (auto ret = co_await SuspendAlways{}; !ret) {
            //< Canceled
            co_return ret;
        }
        waitCount --;
    }
    co_return {};
}

/**
 * @brief When all tasks done
 * 
 * @tparam Args 
 */
template <typename ...Args>
class WhenAllAwaiter final : public AwaiterImpl<WhenAllAwaiter<Args...> >{
public:
    using InTuple = std::tuple<TaskPromise<Args>* ...>;
    using OutTuple = std::tuple<Result<Args> ...>;

    WhenAllAwaiter(const InTuple &tasks) : mTasks(tasks) { }

    auto ready() -> bool {
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
    auto suspend(CoroHandle caller) -> std::coroutine_handle<> {
        // Let the wating task resume the helper task
        mHelperTask = _CounterTask(mWaitCount);
        auto setAwaiting = [this](auto task) {
            if (!task->handle().done()) {
                task->setPrevAwaiting(&mHelperTask.promise());
            }
        };
        std::apply([&](auto ...tasks) {
            (setAwaiting(tasks), ...);
        }, mTasks);
        // Let the helper task resume us
        mHelperTask.promise().setPrevAwaiting(&caller.promise());

        // Switch to the helper task
        return mHelperTask.handle();
    }
    auto resume() const -> OutTuple {
        if (mHelperTask) {
            // MUST done
            ILIAS_ASSERT(mHelperTask.handle().done());
        }
        return _makeResult(std::make_index_sequence<sizeof ...(Args)>());
    }
    auto cancel() -> void {
        // Unlink all tasks
        if (mHelperTask) {
            mHelperTask.promise().setPrevAwaiting(nullptr);
            mHelperTask.cancel();
            mHelperTask.clear();
        }
        _cancelAll(std::make_index_sequence<sizeof ...(Args)>());
    }
private:
    template <size_t ...N>
    auto _makeResult(std::index_sequence<N...>) const -> OutTuple {
        return OutTuple {
            (std::get<N>(mTasks)->value())...
        };
    }
    template <size_t ...N>
    auto _cancelAll(std::index_sequence<N...>) const -> void {
        std::tuple {
            (   std::get<N>(mTasks)->setPrevAwaiting(nullptr),
                std::get<N>(mTasks)->cancel()
            )...
        };
    }

    InTuple mTasks;
    size_t mWaitCount = sizeof ...(Args);
    Task<void> mHelperTask;
};

// Vec version
template <typename T>
class WhenAllVecAwaiter final : public AwaiterImpl<WhenAllVecAwaiter<T> > {
public:
    WhenAllVecAwaiter(std::vector<Task<T> > &vec) : mVec(vec) { }

    auto ready() -> bool {
        // Try resume
        for (auto &task : mVec) {
            task.handle().resume();
        }
        _collectResult();
        return mVec.empty();
    }
    auto suspend(CoroHandle handle) -> void {
        mHelper = _helperTask();
        mHelper->setPrevAwaiting(&handle.promise());
        for (auto &task : mVec) {
            task->setPrevAwaiting(&mHelper.promise());
        }
    }
    auto resume() -> std::vector<Result<T> > {
        if (mHelper) {
            mHelper.cancel();
        }
        _collectResult();
        return std::move(mResults);
    }
    auto cancel() { 
        // No-op 
    }
private:
    auto _helperTask() -> Task<void> {
        size_t num = mVec.size();
        while (num) {
            --num;
            if (num) {
                if (!co_await SuspendAlways{}) {
                    co_return {};
                }
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

    std::vector<Task<T> > &mVec;
    std::vector<Result<T> > mResults;
    Task<void> mHelper;
};

template <typename T>
struct _WhenAllTags {
    T tuple;
};

template <_IsTask T>
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
        return WhenAllAwaiter(tag.tuple);
    }
};

template <_IsTask T>
class AwaitTransform<_WhenAllVecTags<T> > {
public:
    template <typename U>
    static auto transform(TaskPromise<U> *caller, const _WhenAllVecTags<T> &tag) {
        return WhenAllVecAwaiter(tag.vec);
    }
};

/**
 * @brief Wait all task was ready
 * 
 * @tparam Args 
 * @param tasks 
 * @return auto 
 */
template <_IsTask ...Args>
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
template <_IsTask T>
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
template <_IsTask T1, _IsTask T2>
inline auto operator &&(const T1 &a, const T2 &b) noexcept {
    return _WhenAllTags { std::tuple{ &a.promise(), &b.promise() } };
}

template <typename T, _IsTask T1>
inline auto operator &&(_WhenAllTags<T> a, const T1 &b) noexcept {
    return _WhenAllTags {
        std::tuple_cat(a.tuple, std::tuple{ &b.promise() })
    };
}

ILIAS_NS_END