#pragma once

#include "task.hpp"

ILIAS_NS_BEGIN

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

/**
 * @brief Place holder for transform to WhenAnyAwaiter
 * 
 * @tparam Args 
 */
template <typename T>
struct _WhenAnyTags {
    T tuple;
};


/**
 * @brief Place holder for WhenAny(Vec<Task<T> >)
 * 
 * @tparam T 
 */
template <_IsTask T>
struct _WhenAnyVecTags {
    std::vector<T> &vec;
};


template <typename T>
_WhenAnyTags(T) -> _WhenAnyTags<T>;

template <typename Tuple>
class AwaitTransform<_WhenAnyTags<Tuple> > {
public:
    template <typename T>
    static auto transform(TaskPromise<T> *caller, const _WhenAnyTags<Tuple> &tag) {
        return WhenAnyAwaiter(*caller, tag.tuple);
    }
};

/**
 * @brief Wait any task was ready
 * 
 * @tparam Args 
 * @param tasks 
 * @return auto 
 */
template <_IsTask ...Args>
inline auto WhenAny(Args &&...tasks) noexcept {
    return _WhenAnyTags { std::tuple((&tasks.promise())...) };
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
template <_IsTask T1, _IsTask T2>
inline auto operator ||(const T1 &t, const T2 &t2) noexcept {
    return _WhenAnyTags { std::tuple{ &t.promise(), &t2.promise() } };
}

template <typename T, _IsTask T1>
inline auto operator ||(_WhenAnyTags<T> a, const T1 &b) noexcept {
    return _WhenAnyTags {
        std::tuple_cat(a.tuple, std::tuple{ &b.promise() })
    };
}

ILIAS_NS_END