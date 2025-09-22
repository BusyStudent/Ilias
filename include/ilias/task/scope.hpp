#pragma once

#include <ilias/detail/intrusive.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>

ILIAS_NS_BEGIN

/**
 * @brief A Scope guard for task execution, it will await all tasks in the scope to finish
 * 
 */
class ILIAS_API TaskScope final {
public:
    ~TaskScope();

    auto size() const noexcept -> size_t { return mNumRunning; }
    auto empty() const noexcept -> bool { return mNumRunning == 0; }
    auto stop() noexcept -> void;

    /**
     * @brief Insert an handle to the scope, the scope will wait for it to finish
     * 
     * @tparam T 
     * @param handle The wait handle, the return value will be discard
     * @return StopHandle 
     */
    template <typename T>
    auto insert(WaitHandle<T> handle) -> StopHandle {
        return insertImpl(std::move(handle)._leak());
    }

    // Spawn
    template <typename T>
    auto spawn(Task<T> task) -> StopHandle {
        return insert(ILIAS_NAMESPACE::spawn(std::move(task)));
    }

    template <std::invocable Fn>
    auto spawn(Fn fn) -> StopHandle {
        return insert(ILIAS_NAMESPACE::spawn(std::move(fn)));
    }

    template <std::invocable Fn>
    auto spawnBlocking(Fn fn) -> StopHandle {
        return insert(ILIAS_NAMESPACE::spawnBlocking(std::move(fn)));
    }

    /**
     * @brief Create a task scope and enter it
     * 
     * @tparam Fn 
     * @param fn The function that return an Task<T>
     * @return std::invoke_result_t<Fn, TaskScope &> 
     */
    template <typename Fn, typename ...Args> requires (std::invocable<Fn, TaskScope &, Args...>)
    [[nodiscard]]
    static auto enter(Fn fn, Args ...args) -> std::invoke_result_t<Fn, TaskScope &, Args...> {
        TaskScope scope;
        co_return co_await ( // Get current context stop token used to forward the stop to the scope
            fn(scope, args...) | finally(scope.cleanup(co_await runtime::context::stopToken()))
        );
    }
private:
    TaskScope(const TaskScope &) = delete;
    TaskScope(TaskScope &&) = delete;
    TaskScope();

    auto cleanup(runtime::StopToken token) -> Task<void>; // Use the stop token
    auto insertImpl(intrusive::Rc<task::TaskSpawnContext> task) -> StopHandle;
    auto onTaskCompleted(task::TaskSpawnContext &ctxt) -> void;

    using List = intrusive::List<task::TaskSpawnContext>;

    List   mRunning;
    size_t mNumRunning = 0;
    bool   mStopRequested = false;
    bool   mStopping = false;
    runtime::CoroHandle mWaiter = nullptr;
};

ILIAS_NS_END