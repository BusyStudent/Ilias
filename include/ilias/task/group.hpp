#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <memory> // std::shared_ptr
#include <vector> // std::vector
#include <deque>  // std::deque
#include <set> // std::set

ILIAS_NS_BEGIN

namespace task {

using runtime::CoroContext;
using runtime::CoroHandle;
using runtime::StopRegistration;

// The common part of TaskGroup<T>
class ILIAS_API TaskGroupBase {
public:
    TaskGroupBase();
    TaskGroupBase(TaskGroupBase &&) noexcept;
    ~TaskGroupBase();

    // API for TaskGroup<T>
    auto size() const noexcept -> size_t;
    auto stop() -> void;
    auto insert(std::shared_ptr<TaskSpawnContext> task) -> StopHandle;
    auto hasCompletion() const noexcept -> bool;
    auto nextCompletion() noexcept -> std::shared_ptr<TaskSpawnContext>;
private:
    auto notifyCompletion() -> void;
    static auto onTaskCompleted(TaskSpawnContext &ctxt, void *_self) -> void;

    struct Less { // We only compare the address
        using is_transparent = void;

        auto operator ()(const auto &a, const auto &b) const noexcept -> bool {
            return std::to_address(a) < std::to_address(b);
        }
    };

    using RawSet = std::set<std::shared_ptr<TaskSpawnContext>, Less>;
    using Queue = std::deque<std::shared_ptr<TaskSpawnContext> >;

    RawSet mPending;
    Queue  mCompleted;
    bool   mStopRequested = false;
    void  *mAwaiter = nullptr;
friend class TaskGroupAwaiterBase;
};

// The common part of waitNext
class TaskGroupAwaiterBase {
public:
    TaskGroupAwaiterBase(TaskGroupBase &group) : mGroup(group) {}

    auto await_ready() const noexcept -> bool {
        return mGroup.hasCompletion();
    }

    ILIAS_API
    auto await_suspend(CoroHandle caller) -> void;
protected:
    TaskGroupBase &mGroup;
private:
    auto onStopRequested() -> void;
    auto onCompletion() -> void; // Called by TaskGroupBase

    bool mStopRequested = false;
    bool mGot = false;
    CoroHandle mCaller;
    StopRegistration mReg;
friend class TaskGroupBase;
};

template <typename T>
class TaskGroupAwaiter final : public TaskGroupAwaiterBase {
public:
    TaskGroupAwaiter(TaskGroupBase &group, uintptr_t *id) : TaskGroupAwaiterBase(group), mId(id) {}

    auto await_resume() -> Option<T> {
        auto ctxt = mGroup.nextCompletion();
        if (mId) {
            *mId = ctxt->id();
        }
        return ctxt->value<T>();
    }
private:
    uintptr_t *mId;
};

} // namespace task

/**
 * @brief The TaskGroup of tasks, spawn tasks in here and wait for them to finish.
 * @note If the group is destroyed, all tasks will receive the stop request. The TaskGroup will not wait for the tasks to finish.
 * 
 * @tparam T The result type of the tasks.
 */
template <typename T>
class TaskGroup final {
public:
    TaskGroup() = default;
    TaskGroup(TaskGroup &&) = default;
    ~TaskGroup() = default;

    using value_type = typename task::Option<T>::value_type; // Use Option<T> to replace void to std::monostate :(

    /**
     * @brief Insert a handle to the group, the group take the ownership of the handle.
     * 
     * @param handle The handle to insert. (can't be empty)
     */
    auto insert(WaitHandle<T> handle) -> StopHandle {
        return mGroup.insert(std::move(handle)._leak());
    }

    auto spawn(Task<T> task) -> StopHandle {
        return insert(ILIAS_NAMESPACE::spawn(std::move(task)));
    }

    template <std::invocable Fn> requires (std::is_same_v<std::invoke_result_t<Fn>, Task<T> >)
    auto spawn(Fn fn) -> StopHandle {
        return insert(ILIAS_NAMESPACE::spawn(std::move(fn)));
    }

    template <std::invocable Fn> requires (std::is_same_v<std::invoke_result_t<Fn>, T>)
    auto spawnBlocking(Fn fn) -> StopHandle {
        return insert(ILIAS_NAMESPACE::spawnBlocking(std::move(fn)));
    }

    /**
     * @brief Get the num of the task existing (running + done) in the group.
     * 
     * @return size_t 
     */
    [[nodiscard]]
    auto size() const noexcept -> size_t {
        return mGroup.size();
    }

    /**
     * @brief  Check if the group is empty.
     * 
     * @return true 
     * @return false 
     */
    auto empty() const noexcept -> bool {
        return mGroup.size() == 0;
    }

    /**
     * @brief Send the stop request to all tasks in the group.
     * 
     */
    auto stop() -> void {
        return mGroup.stop();
    }

    // Wait Function, all of them shouldn't be called concurrently
    /**
     * @brief Stop all tasks and wait for them to finish.
     * 
     */
    [[nodiscard]]
    auto shutdown() -> Task<void>;

    /**
     * @brief Get the next task that has completed.
     * @param id The pointer to receive the id of the task. (If nullptr, the id will not be set)
     * @note If the group is empty, it will wait forever until a new task is inserted and completed.
     * @return Option<T> nullopt on the task that has been stopped.
     */
    [[nodiscard]]
    auto next(uintptr_t *id = nullptr) noexcept -> task::TaskGroupAwaiter<T> {
        return {mGroup, id};
    }

    /**
     * @brief Wait All tasks to finish. the return vector doesn't contain the task that has been stopped.
     * 
     * @return Task<std::vector<value_type> > 
     */
    auto waitAll() -> Task<std::vector<value_type> >;
private:
    task::TaskGroupBase mGroup;
};

template <typename T>
auto TaskGroup<T>::shutdown() -> Task<void> {
    stop();
    while (!empty()) {
        auto _ = co_await next();
    }
}

template <typename T>
auto TaskGroup<T>::waitAll() -> Task<std::vector<value_type> > {
    std::vector<value_type> vec;
    while (!empty()) {
        if (auto ret = co_await next(); ret) {
            vec.emplace_back(std::move(*ret));
        }
    }
    co_return vec;
}

// Make compile faster?
extern template ILIAS_API class TaskGroup<void>;

ILIAS_NS_END