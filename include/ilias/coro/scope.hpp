#pragma once

#include "task.hpp"
#include <set>

ILIAS_NS_BEGIN

/**
 * @brief Task scope for structured concurrency, not moveable
 * 
 */
class TaskScope {
public:
    TaskScope();
    TaskScope(const TaskScope &) = delete;
    ~TaskScope();

    /**
     * @brief Wait all task in scope done
     * 
     * @return num of task waited
     */
    auto syncWait() -> size_t;
    /**
     * @brief Cancel all task in scope
     * 
     * @return num of task canceled 
     */
    auto syncCancel() -> size_t;

    /**
     * @brief Spawn a task into scope
     * 
     * @tparam Callable 
     * @tparam Args 
     * @param callable 
     * @param args 
     */
    template <typename Callable, typename ...Args>
    auto spawn(Callable &&callable, Args &&...args) -> void;
    /**
     * @brief Post a task into scope
     * 
     * @tparam T 
     * @param task 
     */
    template <typename T>
    auto postTask(Task<T> &&task) -> void;
private:
    auto _onTaskDone() -> Task<>;

    std::set<CoroPromise *> mSet;
    StopToken *mToken = nullptr;
    EventLoop *mLoop = EventLoop::instance();
    Task<> mHelper; //< Used to get notified when one task done
};

inline TaskScope::TaskScope() {
    ILIAS_ASSERT_MSG(mLoop, "EventLoop not initialized");
    mHelper = _onTaskDone();
}
inline TaskScope::~TaskScope() {
    syncCancel();
    ILIAS_ASSERT(mSet.empty());
}

inline auto TaskScope::syncWait() -> size_t {
    size_t size = mSet.size();
    if (size == 0) {
        return 0;
    }
    StopToken token;
    mToken = &token;
    mLoop->run(token);
    mToken = nullptr;
    return size - mSet.size();
}
inline auto TaskScope::syncCancel() -> size_t {
    size_t n = 0;
    for (auto iter = mSet.begin(); iter != mSet.end();) {
        auto promise = *iter;
        // We are iter this set, so we can not let the helper do remove
        promise->setPrevAwaiting(nullptr);
        if (promise->cancel() == CancelStatus::Pending) {
            // Just skip
            promise->setPrevAwaiting(&mHelper.promise());
            ++iter;
            continue;
        }
        // Remove from set
        iter = mSet.erase(iter);
        ++n;
    }
    return n;
}

template <typename Callable, typename ...Args>
inline auto TaskScope::spawn(Callable &&callable, Args &&...args) -> void {
    return postTask(
        Task<>::fromCallable(
            std::forward<Callable>(callable), 
            std::forward<Args>(args)...
        )
    );
}
template <typename T>
inline auto TaskScope::postTask(Task<T> &&task) -> void {
    ILIAS_ASSERT(!mSet.contains(&task.promise()));

    mSet.insert(&task.promise()); //< Insert it to the set
    task.promise().setPrevAwaiting(&mHelper.promise());
    mLoop->postTask(std::move(task)); //< Let it resume in the event loop
}

inline auto TaskScope::_onTaskDone() -> Task<> {
    // Awaiter for get resumeCaller by suspend and clear it when got value
    struct Awaiter {
        auto await_ready() -> bool { return false; }
        auto await_suspend(Task<>::handle_type h) -> bool {
            handle = h;
            auto &self = handle.promise();
            if (self.isCanceled()) {
                return false;
            }
            return self.resumeCaller() == nullptr; //< If not nullptr, resume and got value
        }
        auto await_resume() -> PromiseBase * {
            auto &self = handle.promise();
            auto caller = self.resumeCaller();
            self.setResumeCaller(nullptr);
            return caller;
        }
        Task<>::handle_type handle;
    };
    while (true) {
        // Get next done task
        auto promise = co_await Awaiter();
        if (!promise) { // nullptr on cancel
            co_return {};
        }
        auto iter = mSet.find(promise);
        ILIAS_ASSERT(iter != mSet.end());
        mSet.erase(iter);

        if (mSet.empty() && mToken) {
            mToken->stop();
            mToken = nullptr;
        }
    }
}

ILIAS_NS_END