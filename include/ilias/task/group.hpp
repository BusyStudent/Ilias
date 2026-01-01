#pragma once

#include <ilias/detail/intrusive.hpp> // Rc, List
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <vector> // std::vector

ILIAS_NS_BEGIN

namespace task {

using runtime::CoroContext;
using runtime::CoroHandle;
using runtime::StopRegistration;

// Forward declaration
class TaskGroupAwaiterBase;

// The common part of TaskGroup<T>
class ILIAS_API TaskGroupBase {
public:
    TaskGroupBase();
    TaskGroupBase(TaskGroupBase &&) noexcept;
    ~TaskGroupBase();

    // API for TaskGroup<T>
    auto size() const noexcept -> size_t;
    auto stop() -> void;
    auto insert(Rc<TaskSpawnContextBase> task) -> StopHandle;
    auto hasCompletion() const noexcept -> bool;
    auto nextCompletion() noexcept -> Rc<TaskSpawnContextBase>;
private:
    auto notifyCompletion() -> void;
    auto onTaskCompleted(TaskSpawnContextBase &ctxt) -> void;

    using List = intrusive::List<TaskSpawnContextBase>; // intrusive list doesn't have O(1) size()
    using Awaiter = TaskGroupAwaiterBase;

    List     mRunning;
    List     mCompleted;
    bool     mStopRequested = false;
    size_t   mNumRunning = 0; // The size of the running list
    size_t   mNumCompleted = 0; // The size of the completed list
    Awaiter *mAwaiter = nullptr;
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
        return static_cast<TaskSpawnContext<T> &>(*ctxt).value();
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

    using value_type = typename Option<T>::value_type; // Use Option<T> to replace void to std::monostate :(
    using Vector = std::vector<value_type>;

    /**
     * @brief Insert a handle to the group, the group take the ownership of the handle.
     * 
     * @param handle The handle to insert. (can't be empty)
     * @return StopHandle
     */
    auto insert(WaitHandle<T> handle) -> StopHandle {
        return mGroup.insert(std::move(handle)._leak());
    }

    /**
     * @brief Spawn a task and to the group, 
     * 
     * @param task The task to spawn. (can't be empty)
     * @return StopHandle
     */
    auto spawn(Task<T> task, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ILIAS_NAMESPACE::spawn(std::move(task), source));
    }

    /**
     * @brief Spawn a task and to the group,
     * 
     * @tparam Fn 
     * @param fn the function that creates the task.
     * @return StopHandle
     */
    template <std::invocable Fn> requires (std::is_same_v<std::invoke_result_t<Fn>, Task<T> >)
    auto spawn(Fn fn, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ILIAS_NAMESPACE::spawn(std::move(fn), source));
    }

    /**
     * @brief Spawn an blocking callable as a task to the group
     * 
     * @tparam Fn
     * @param fn The blcoking function
     * @return StopHandle (the stop won't work if the function doesn't handle the stop request)
     */
    template <std::invocable Fn> requires (std::is_same_v<std::invoke_result_t<Fn>, T>)
    auto spawnBlocking(Fn fn, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ILIAS_NAMESPACE::spawnBlocking(std::move(fn), source));
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
     * @return Task<Vector> 
     */
    auto waitAll() -> Task<Vector>;
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
auto TaskGroup<T>::waitAll() -> Task<Vector> {
    Vector vec;
    while (!empty()) {
        if (auto ret = co_await next(); ret) {
            vec.emplace_back(std::move(*ret));
        }
    }
    co_return vec;
}

// Sequence version of the whenAny & whenll
/**
 * @brief When any of the tasks in the sequence is completed, return the result of the task.
 * 
 * @tparam T 
 * @param seq The sequence of tasks. (can't be empty)
 * @return The value of the task that has been completed first.
 */
template <AwaitableSequence T>
[[nodiscard]]
inline auto whenAny(T seq) -> Task<typename TaskGroup<AwaitableSequenceValue<T> >::value_type> {
    auto group = TaskGroup<AwaitableSequenceValue<T> > {};
    for (auto &&task : seq) {
        group.spawn(std::move(task));
    }
    ILIAS_ASSERT(!group.empty());

    // Get one
    auto value = co_await group.next();    
    co_await group.shutdown();

    // Because we didn't call stop on the group, it can't be nullopt
    co_return std::move(*value);
}

/**
 * @brief When all of the tasks in the sequence is completed, return the result of all tasks.
 * 
 * @tparam T 
 * @param seq The sequence of tasks. (can be empty)
 * @return The vector of the result of all tasks.
 */
template <AwaitableSequence T>
[[nodiscard]]
inline auto whenAll(T seq) -> Task<typename TaskGroup<AwaitableSequenceValue<T> >::Vector> {
    auto group = TaskGroup<AwaitableSequenceValue<T> > {};
    for (auto &&task : seq) {
        group.spawn(std::move(task));
    }
    co_return co_await group.waitAll();
}

ILIAS_NS_END