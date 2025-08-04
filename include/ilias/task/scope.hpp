#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/group.hpp> // WaitGroup
#include <ilias/task/task.hpp>

ILIAS_NS_BEGIN

/**
 * @brief A Scope guard for task execution, it will await all tasks in the scope to finish
 * 
 */
class ILIAS_API TaskScope final {
public:
    ~TaskScope();

    auto size() const noexcept -> size_t { return mGroup.size(); }
    auto empty() const noexcept -> bool { return mGroup.empty(); }
    auto stop() -> void { return mGroup.stop(); }

    /**
     * @brief Insert an handle to the scope, the scope will wait for it to finish
     * 
     * @tparam T 
     * @param handle The wait handle, the return value will be discard
     * @return StopHandle 
     */
    template <typename T>
    auto insert(WaitHandle<T> handle) -> StopHandle {
        auto wrapper = [](auto handle) -> Task<void> {
            auto _ = co_await std::move(handle);
        };
        return mGroup.spawn(wrapper(std::move(handle)));
    }

    auto insert(WaitHandle<void> handle) -> StopHandle {
        return mGroup.insert(std::move(handle));
    }

    // Spawn
    template <typename T>
    auto spawn(Task<T> task) -> StopHandle {
        auto wrapper = [](auto task) -> Task<void> {
            auto _ = co_await std::move(task);
        };
        return mGroup.spawn(wrapper(std::move(task)));
    }

    auto spawn(Task<void> task) -> StopHandle { 
        return mGroup.spawn(std::move(task));
    }

    template <std::invocable Fn> requires (std::is_void_v<std::invoke_result_t<Fn> >)
    auto spawnBlocking(Fn fn) -> StopHandle {
        return mGroup.spawnBlocking(std::move(fn));
    }

    template <std::invocable Fn> requires (!std::is_void_v<std::invoke_result_t<Fn> >)
    auto spawnBlocking(Fn fn) -> StopHandle {
        return mGroup.spawnBlocking([f = std::move(fn)]() mutable {
            auto _ = f();
        });
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
            fn(scope, args...) | finally(scope.waitAll(co_await runtime::context::stopToken()))
        );
    }
private:
    TaskScope(const TaskScope &) = delete;
    TaskScope(TaskScope &&) = delete;
    TaskScope();

    auto waitAll(runtime::StopToken token) -> Task<void>; // Use the stop token

    TaskGroup<void> mGroup; // Temporary use TaskGroup to implement TaskScope, for covnvenience, TODO: impl it directly
};

ILIAS_NS_END