#pragma once

#include <ilias/detail/intrusive.hpp> // Rc, List
#include <ilias/runtime/functional.hpp> // SmallFunction
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/spawn.hpp>
#include <ilias/task/task.hpp>
#include <vector> // std::vector

ILIAS_NS_BEGIN

namespace task {

using runtime::CoroHandle;
using runtime::CoroContext;
using runtime::StopRegistration;

// The common part of TaskGroup<T>
class TaskGroupBase {
public:
    ILIAS_API
    TaskGroupBase() noexcept;

    ILIAS_API
    TaskGroupBase(TaskGroupBase &&) noexcept;

    ILIAS_API
    ~TaskGroupBase();

    // API for impl TaskGroup<T>
    auto size() const noexcept -> size_t {
        return mNumRunning + mNumCompleted;
    }

    auto completionSize() const noexcept -> size_t {
        return mNumCompleted;
    }

    auto runningSize() const noexcept -> size_t {
        return mNumRunning;
    }

    // Send the stop request to all tasks
    ILIAS_API
    auto stop() -> void;

    // Insert a new task spaned handle to the group
    ILIAS_API
    auto insert(Rc<TaskSpawnContextBase> task) -> StopHandle;

    ILIAS_API
    auto nextCompletion() noexcept -> Rc<TaskSpawnContextBase>;
private:
    auto notifyCompletion() -> void;
    auto onTaskCompleted(TaskSpawnContextBase &ctxt) -> void;

    using List = intrusive::List<TaskSpawnContextBase>; // intrusive list doesn't have O(1) size()

    List   mRunning;
    List   mCompleted;
    bool   mStopRequested = false;
    size_t mNumRunning = 0; // The size of the running list
    size_t mNumCompleted = 0; // The size of the completed list
    SmallFunction<void()> mNotify; // Called when a new completion is added
friend class TaskGroupWaitNextBase;
friend class TaskGroupWaitAllBase;
};

// The common part of waitNext
class TaskGroupWaitNextBase {
public:
    TaskGroupWaitNextBase(TaskGroupBase &group) : mGroup(group) {}
    TaskGroupWaitNextBase(TaskGroupWaitNextBase &&) = default;

    auto await_ready() const -> bool {
        ILIAS_ASSERT(mGroup.size() != 0, "The group is empty");
        return mGroup.completionSize() > 0;
    }

    ILIAS_API
    auto await_suspend(CoroHandle caller) noexcept -> void;
protected:
    bool mGot = false;
    TaskGroupBase &mGroup;
    CoroHandle     mCaller;
    StopRegistration mReg;
};

// The common part of waitAll
class TaskGroupWaitAllBase {
public:
    TaskGroupWaitAllBase(TaskGroupBase &group) : mGroup(group) {}
    TaskGroupWaitAllBase(TaskGroupWaitAllBase &&) = default;

    auto await_ready() const -> bool {
        return mGroup.size() == mGroup.completionSize(); // All tasks are completed or empty
    }

    ILIAS_API
    auto await_suspend(CoroHandle caller) noexcept -> void;
protected:
    auto onCompletion() noexcept -> void;

    bool mShutdown = false; // If true, all completions will be discarded (used by shutdown or stop requested)
    TaskGroupBase &mGroup;
    CoroHandle     mCaller;
    StopRegistration mReg;
};


template <typename T>
class TaskGroupWaitNext final : public TaskGroupWaitNextBase {
public:
    TaskGroupWaitNext(TaskGroupBase &group, uintptr_t *id) : TaskGroupWaitNextBase(group), mId(id) {}

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

// Impl TaskGroup::waitAll
template <typename T>
class TaskGroupWaitAll final : public TaskGroupWaitAllBase {
public:
    TaskGroupWaitAll(TaskGroupBase &group) : TaskGroupWaitAllBase(group) {}

    using Value = typename Option<T>::value_type; // Rplace the void to std::monostate
    using Vector = std::vector<Value>;

    auto await_resume() -> Vector {
        Vector vec;
        while (mGroup.completionSize() > 0) { // Collect all completions
            auto ctxt = mGroup.nextCompletion();
            auto val = static_cast<TaskSpawnContext<T> &>(*ctxt).value();
            if (val) { // Is not stopped
                vec.emplace_back(std::move(*val));
            }
        }
        return vec;
    }
};

// Impl TaskGroup::shutdown
class TaskGroupShutdown final : public TaskGroupWaitAllBase {
public:
    TaskGroupShutdown(TaskGroupBase &group) : TaskGroupWaitAllBase(group) {}

    auto await_ready() -> bool {
        mShutdown = true;
        mGroup.stop(); // Send the stop request
        return TaskGroupWaitAllBase::await_ready();
    }
    auto await_resume() -> void {}
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

    /**
     * @brief Create an task group, spawn a task from the awaitable.
     * 
     * @tparam U 
     * @param awaitable The awaitable to spawn.
     */
    template <Awaitable U> requires (std::is_same_v<AwaitableResult<U>, T>)
    explicit TaskGroup(U awaitable, runtime::CaptureSource source = {}) {
        spawn(std::move(awaitable), source);
    }

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
     * @param awaitable The task to spawn. construct from the awaitable
     * @return StopHandle
     */
    template <Awaitable U> requires (std::is_same_v<AwaitableResult<U>, T>)
    auto spawn(U awaitable, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawn(std::move(awaitable), source));
    }

    /**
     * @brief Spawn a task and to the group,
     * 
     * @tparam Fn 
     * @param fn the function that creates the awaitable.
     * @return StopHandle
     */
    template <std::invocable Fn> requires (std::is_same_v<AwaitableResult<std::invoke_result_t<Fn> >, T>)
    auto spawn(Fn fn, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawn(std::move(fn), source));
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
        return insert(::ilias::spawnBlocking(std::move(fn), source));
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
    [[nodiscard]]
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
     * @note If the stop requested, The function will forward the stop to the group and wait for the tasks to finish.
     * 
     */
    [[nodiscard]]
    auto shutdown() -> task::TaskGroupShutdown {
        return {mGroup};
    }

    /**
     * @brief Get the next task that has completed.
     * @param id The pointer to receive the id of the task. (If nullptr, the id will not be set)
     * @note Don't call this function concurrently and it can't be called when the group is empty
     * @return Option<T> nullopt on the task that has been stopped.
     */
    [[nodiscard]]
    auto next(uintptr_t *id = nullptr) noexcept -> task::TaskGroupWaitNext<T> {
        return {mGroup, id};
    }

    /**
     * @brief Wait All tasks to finish. the return vector doesn't contain the task that has been stopped.
     * @note If the stop requested, The function will forward the stop to the group and wait for the tasks to finish.
     * @return Vector<
     */
    [[nodiscard]]
    auto waitAll() noexcept -> task::TaskGroupWaitAll<T> {
        return {mGroup};
    }

    // Operator
    auto operator =(const TaskGroup &) = delete;
private:
    task::TaskGroupBase mGroup;
};

// Types
template <Awaitable T>
TaskGroup(T awaitable, runtime::CaptureSource source = {}) -> TaskGroup<AwaitableResult<T> >;

ILIAS_NS_END