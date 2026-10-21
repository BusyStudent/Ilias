#pragma once

#include <ilias/detail/intrusive.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/spawn.hpp> // TaskSpawnContextBase
#include <ilias/task/utils.hpp> // finally
#include <ilias/task/task.hpp> // Task
#include <optional> // std::optional

ILIAS_NS_BEGIN

namespace task {

// Import types
using runtime::StopRegistration;
using runtime::StopToken;
using runtime::CoroHandle;
using intrusive::Rc;

// The hidden implementation of the TaskScope
class ILIAS_API TaskScopeBase final {
public:
    TaskScopeBase() noexcept;
    TaskScopeBase(const TaskScopeBase &) = delete;
    ~TaskScopeBase();

    auto size() const noexcept -> size_t { return mNumRunning; }
    auto stop() noexcept -> bool;
    auto insert(Rc<TaskSpawnContextBase> handle) -> StopHandle;
private:
    auto onTaskCompleted(TaskSpawnContextBase &ctxt) noexcept -> void;
    using List = intrusive::List<TaskSpawnContextBase>;

    List   mRunning;
    size_t mNumRunning = 0;
    bool   mStopRequested = false;
    bool   mStopping = false;
    CoroHandle mWaiter = nullptr;
friend class TaskScopeWaitAll;
};

// Impl waitAll
class TaskScopeWaitAll {
public:
    TaskScopeWaitAll(TaskScopeBase &scope) : mScope(scope) {}
    TaskScopeWaitAll(TaskScopeWaitAll &&) = default;

    // All tasks are done?
    auto await_ready() const noexcept -> bool { return mScope.size() == 0; }
    ILIAS_API
    auto await_suspend(CoroHandle caller) -> void;
    auto await_resume() -> void {}
protected:
    TaskScopeBase &mScope;
    StopRegistration mReg;
    std::optional<StopToken> mToken;
};

// Impl shutdown
class TaskScopeShutdown final : public TaskScopeWaitAll {
public:
    TaskScopeShutdown(TaskScopeBase &scope) : TaskScopeWaitAll(scope) {}
    TaskScopeShutdown(TaskScopeShutdown &&) = default;

    auto await_ready() const noexcept -> bool { // NOLINT
        mScope.stop();
        return TaskScopeWaitAll::await_ready();
    }
};

} // namespace task

/**
 * @brief Scope guard for managing task lifetimes
 *
 * @note Ensure all tasks have completed before destroying the scope;
 *       otherwise the program will `ABORT`. Use waitAll() to wait for
 *       completion, or use enter() to run tasks within the scope.
 */
class ILIAS_API TaskScope final {
public:
    TaskScope(const TaskScope &) = delete;
    TaskScope() = default;
    ~TaskScope() {
        ILIAS_ASSERT(empty(), "TaskScope must be empty when destroyed, call waitAll() or shutdown() before destruction");
    }

    /**
     * @brief Get the number of running tasks in the scope
     * 
     * @return size_t 
     */
    [[nodiscard]]
    auto size() const noexcept -> size_t { 
        return mScope.size();
    }

    /**
     * @brief Check the scope is empty?
     * 
     * @return true 
     * @return false 
     */
    [[nodiscard]]
    auto empty() const noexcept -> bool { 
        return mScope.size() == 0;
    }

    /**
     * @brief Try to stop all tasks in the scope
     * 
     */
    auto stop() noexcept -> bool { 
        return mScope.stop(); 
    }

    /**
     * @brief Wait all tasks in the scope to finish, (safe on an empty scope)
     * 
     * @return Task<void> 
     */
    [[nodiscard]]
    auto waitAll() -> task::TaskScopeWaitAll { 
        return {mScope}; 
    }

    /**
     * @brief Stop all tasks and wait for them to finish.
     * 
     * @return Task<void> 
     */
    [[nodiscard]]
    auto shutdown() -> task::TaskScopeShutdown {
        return {mScope};
    }

    /**
     * @brief Insert an handle to the scope, the scope will wait for it to finish
     * 
     * @tparam T 
     * @param handle The wait handle, the return value will be discard
     * @return StopHandle 
     */
    template <typename T>
    auto insert(WaitHandle<T> handle) -> StopHandle {
        return mScope.insert(std::move(handle)._leak());
    }

    // Spawn
    template <Awaitable T>
    auto spawn(T awaitable, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawn(std::move(awaitable), source));
    }

    template <std::invocable Fn>
    auto spawn(Fn fn, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawn(std::move(fn), source));
    }
    
    template <std::invocable Fn>
    auto spawnBlocking(Fn fn, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawnBlocking(std::move(fn), source));
    }

    /**
     * @brief Create a task scope and enter it
     * 
     * @tparam Fn 
     * @param fn The function that return an awaitable
     * @return Task<AwaitableResult<fn()>>
     */
    template <typename Fn, typename ...Args> requires (std::invocable<Fn, TaskScope &, Args...>)
    [[nodiscard]]
    static auto enter(Fn fn, Args ...args) -> Task<AwaitableResult<std::invoke_result_t<Fn, TaskScope &, Args...>>> {
        TaskScope scope;
        runtime::StopCallback callback { // Forward the current stop request to the scope
            co_await this_coro::stopToken(),
            ForwardHelper{scope},
        };
        co_return co_await ( // Get current context stop token used to forward the stop to the scope
            fn(scope, args...) | finally(scope.waitAll())
        );
    }
private:
    // Helper used for stop callback to forward the stop to the scope
    struct ForwardHelper {
        auto operator()() noexcept -> void {
            scope.stop();
        }
        TaskScope &scope;
    };

    task::TaskScopeBase mScope;
};

ILIAS_NS_END